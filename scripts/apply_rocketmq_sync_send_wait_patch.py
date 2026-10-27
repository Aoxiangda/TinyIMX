"""Repair callback ownership in one pinned, isolated SDK source file.

Apache RocketMQ Clients commit da8271b572b41b627bcbbdfb5b3310c0c5e4736b.
The vendor file retains its Apache-2.0 header. No installed SDK or public ABI
is changed by this source renderer. The patch is idempotent and fails closed
if the fixed original implementation differs.
"""
from pathlib import Path
import argparse,hashlib,json,os
ORIGINAL_SHA='7f9edd38bb461a66401be07ecd5254ad7dc088e4b2104f64deb00fbede3829a7'
SIGNATURE='SendReceipt ProducerImpl::send(MessageConstPtr message, std::error_code& ec) noexcept {'
END='\nvoid ProducerImpl::send(MessageConstPtr message, SendCallback cb)'
def sha(raw):return hashlib.sha256(raw).hexdigest()
def render(original,fragment):
    if sha(original)!=ORIGINAL_SHA:raise ValueError('Pinned ProducerImpl original differs')
    text=original.decode('utf-8');start=text.index(SIGNATURE);end=text.index(END,start)
    old=text[start:end];marker='  auto mtx = std::make_shared<absl::Mutex>();'
    if old.count(marker)!=1:raise ValueError('Pinned synchronous wait insertion differs')
    replacement=old[:old.index(marker)]+fragment.decode('utf-8').rstrip()+'\n'
    text=text[:start]+replacement+text[end:]
    if text.count('#include <memory>')!=1:raise ValueError('Pinned include insertion differs')
    return text.replace('#include <memory>','#include <memory>\n#include <condition_variable>\n#include <mutex>',1).encode('utf-8')
def apply(source,authority,fragment,report):
    source=Path(source);authority=Path(authority);fragment=Path(fragment);report=Path(report)
    if source.is_symlink()or source.resolve()!=source.absolute():raise ValueError('Physical isolated SDK source required')
    original=authority.read_bytes();desired=render(original,fragment.read_bytes());target=source/'source/rocketmq/ProducerImpl.cpp'
    if target.is_symlink()or any(p.is_symlink()for p in target.parents if p.is_relative_to(source)):raise ValueError('SDK overlay symlink refused')
    raw=target.read_bytes()
    if raw not in (original,desired):raise ValueError('Foreign SDK source edit refused')
    temporary=target.with_name(target.name+'.safe-send.tmp')
    if temporary.exists()or temporary.is_symlink():raise ValueError('Unexpected partial SDK patch')
    if raw!=desired:
        with temporary.open('xb')as stream:stream.write(desired);stream.flush();os.fsync(stream.fileno())
        os.replace(temporary,target)
    result=dict(status='PINNED_SYNC_SEND_STACK_CAPTURE_REPLACED_WITH_OWNED_RESULT_CELL_NOT_BUILT_OR_DEPLOYED',before=sha(original),after=sha(desired),already_applied=raw==desired,
                new_or_changed_public_API=False,new_background_thread=False,changed_file='source/rocketmq/ProducerImpl.cpp')
    report.write_text(json.dumps(result,indent=2)+'\n');return result
if __name__=='__main__':
    p=argparse.ArgumentParser()
    for name in ('source','authority','fragment','report'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();print(json.dumps(apply(a.source,a.authority,a.fragment,a.report)))
