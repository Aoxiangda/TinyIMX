"""Apply a bounded optional ACK API to a fresh pinned SDK source overlay only."""
import argparse,hashlib,json,os
from pathlib import Path

TARGETS=(
 'include/rocketmq/SimpleConsumer.h','source/rocketmq/include/SimpleConsumerImpl.h',
 'source/client/include/RpcClientImpl.h','source/rocketmq/SimpleConsumer.cpp',
 'source/rocketmq/SimpleConsumerImpl.cpp','source/client/RpcClientImpl.cpp')
def digest(raw):return hashlib.sha256(raw).hexdigest()
def once(text,old,new):
 if text.count(old)!=1:raise ValueError('Pinned insertion point differs: '+old[:60])
 return text.replace(old,new,1)
def render(name,raw,patches):
 text=raw.decode('utf-8')
 if name=='include/rocketmq/SimpleConsumer.h':
  old='  void asyncAck(const Message& message, AckCallback callback) noexcept;'
  return once(text,old,old+'\n\n  // TinyIMX optional ABI extension: non-virtual, externally sequenced lifetime.\n  void ackBatch(const std::vector<MessageConstSharedPtr>& messages,\n                std::vector<std::error_code>& errors) noexcept;').encode()
 if name=='source/rocketmq/include/SimpleConsumerImpl.h':
  old='  void ackAsync(const Message& message, AckCallback callback);'
  return once(text,old,old+'\n\n  void ackBatchAsync(const std::vector<MessageConstSharedPtr>& messages,\n      std::function<void(const std::vector<std::error_code>&)> callback);').encode()
 if name=='source/client/include/RpcClientImpl.h':
  old='  void asyncAck(const AckMessageRequest& request, InvocationContext<AckMessageResponse>* invocation_context) override;'
  return once(text,old,old+'\n\n  // Owned context, normal shared callback queue, no virtual-table change.\n  void asyncAckBatch(const AckMessageRequest& request,\n      std::shared_ptr<InvocationContext<AckMessageResponse>> owned);').encode()
 if name=='source/rocketmq/SimpleConsumer.cpp':
  includes='#include <algorithm>\n#include <condition_variable>\n#include <mutex>\n#include "rocketmq/TinyIMXBatchAckApi.h"\n#include "BatchAckMetrics.h"\n'
  fragment='BatchAckPublic.inc'
 elif name=='source/rocketmq/SimpleConsumerImpl.cpp':
  includes='#include <algorithm>\n#include <map>\n#include <mutex>\n#include <set>\n#include "RpcClientImpl.h"\n#include "BatchAckResult.h"\n#include "BatchAckMetrics.h"\n#include "LazyLogValue.h"\n'
  fragment='BatchAckImpl.inc'
 elif name=='source/client/RpcClientImpl.cpp':
  includes='';fragment='BatchAckRpc.inc'
 else:raise ValueError(name)
 text=once(text,'ROCKETMQ_NAMESPACE_BEGIN',includes+'\nROCKETMQ_NAMESPACE_BEGIN')
 return once(text,'ROCKETMQ_NAMESPACE_END',(patches/fragment).read_text(encoding='utf-8')+'\nROCKETMQ_NAMESPACE_END').encode()

def apply(source,authority,patches,report):
 source=Path(source);patches=Path(patches);authority=json.loads(Path(authority).read_bytes())
 if source.is_symlink()or source.resolve()!=source.absolute():raise ValueError('Physical SDK overlay required')
 desired={};before={}
 for name in TARGETS:
  # Authority is the fixed original Git-blob manifest; changed sources are
  # accepted only if they exactly equal our deterministic rendered result.
  entry=authority['file_proofs']['cpp/'+name]
  original=(Path(authority['source'])/'cpp'/name).read_bytes()
  if digest(original)!=entry['sha256']:raise ValueError('Authority original differs')
  desired[name]=render(name,original,patches);before[name]=entry['sha256']
 for name,file in(
  ('include/rocketmq/TinyIMXBatchAckApi.h','TinyIMXBatchAckApi.h'),
  ('source/rocketmq/include/BatchAckResult.h','BatchAckResult.h'),
  ('source/rocketmq/include/BatchAckMetrics.h','BatchAckMetrics.h')):desired[name]=(patches/file).read_bytes()
 # Validate the entire write set before making any changes.
 for name,data in desired.items():
  path=source/name
  if path.is_symlink()or any(p.is_symlink()for p in path.parents if p.is_relative_to(source)):raise ValueError('Overlay link refused')
  if path.exists():
   h=digest(path.read_bytes())
   if h!=digest(data)and h!=before.get(name):raise ValueError('Foreign SDK edit refused: '+name)
  elif name in before:raise ValueError('Original overlay target absent: '+name)
  tmp=path.with_name(path.name+'.wire-ack.tmp')
  if tmp.exists()or tmp.is_symlink():raise ValueError('Unexpected partial temporary file')
 result={}
 for name,data in desired.items():
  path=source/name;already=path.exists()and path.read_bytes()==data
  if not already:
   path.parent.mkdir(parents=True,exist_ok=True);tmp=path.with_name(path.name+'.wire-ack.tmp')
   with tmp.open('xb')as f:f.write(data);f.flush();os.fsync(f.fileno())
   os.replace(tmp,path)
  result[name]=dict(before=before.get(name),after=digest(data),already=already)
 j=dict(status='PINNED_OPTIONAL_WIRE_ACK_BATCH_SOURCE_OVERLAY_APPLIED_NOT_BUILT_OR_DEPLOYED',files=result,virtual_tables_changed=False,installed_SDK_changed=False)
 Path(report).write_text(json.dumps(j,indent=2)+'\n');return j
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--authority',type=Path,required=True);p.add_argument('--patches',type=Path,required=True);p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 print(json.dumps(apply(a.source,a.authority,a.patches,a.report)))
