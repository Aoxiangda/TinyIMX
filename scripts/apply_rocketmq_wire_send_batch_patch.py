"""Add optional bounded normal-send batching to a fresh exact safe-SDK overlay.

Upstream Apache RocketMQ clients cpp-5.1.1, da8271b572b41b627bcbbdfb5b3310c0c5e4736b.
The baseline includes the already qualified owned synchronous wait and batch ACK.
No installed SDK, virtual table, class field or unrelated source is modified.
"""
import argparse,hashlib,json,os
from pathlib import Path

BASELINE={
 'include/rocketmq/Producer.h':'cb143d8818a67084fce524650bc0583cac4a5aa71b3bca0861485894315a784c',
 'source/rocketmq/include/ProducerImpl.h':'0fd7f67a5af3171b9762949fd7bf80000ca07569c3fc8173a028bd3a7fd44ee1',
 'source/rocketmq/Producer.cpp':'109543cb2d97f0583136ca1ed21dc63a78da0e1fcfe830e1a2892b3ea4cfc639',
 'source/client/include/RpcClientImpl.h':'bc20fc91f4eb892d4660906297dec072fd4c419ffe32b18c5ecd292ae31d9d28',
 'source/client/RpcClientImpl.cpp':'69f38e5eecc499298dc1493c7e9feecfe9400768404e4463108f2b315d7e4b2d',
 'source/rocketmq/ProducerImpl.cpp':'592487083bc144fe3fce14904038aafd86856947b99bc74f94d9783c8154b6f9',
}
POLICY_SHA256='cdaa6bb785314c297817b33db99e231a1638c8529ada85a19f47cf48aa3d7b66'
def sha(raw):return hashlib.sha256(raw).hexdigest()
def once(text,old,new):
    if text.count(old)!=1:raise ValueError('Pinned insertion differs: '+old[:90])
    return text.replace(old,new,1)
def render(name,raw,patches):
    text=raw.decode('utf-8')
    if name=='include/rocketmq/Producer.h':
        old='  SendReceipt send(MessageConstPtr message, std::error_code& ec) noexcept;'
        return once(text,old,old+'\n\n  // Optional normal-message batching; caller sequences producer lifetime.\n  void sendBatch(const std::vector<MessageConstSharedPtr>& messages,\n                 std::vector<std::error_code>& errors,\n                 std::vector<std::string>& ids) noexcept;').encode()
    if name=='source/rocketmq/include/ProducerImpl.h':
        old='  SendReceipt send(MessageConstPtr message, std::error_code& ec) noexcept;'
        return once(text,old,old+'\n\n  void sendBatch(const std::vector<MessageConstSharedPtr>& messages,\n                 std::vector<std::error_code>& errors,\n                 std::vector<std::string>& ids);').encode()
    if name=='source/client/include/RpcClientImpl.h':
        old='  void asyncSend(const SendMessageRequest& request,\n                 InvocationContext<SendMessageResponse>* invocation_context) override;'
        return once(text,old,old+'\n\n  void asyncSendBatch(const SendMessageRequest& request,\n      std::shared_ptr<InvocationContext<SendMessageResponse>> owned);').encode()
    if name=='source/rocketmq/Producer.cpp':
        includes='#include "rocketmq/TinyIMXBatchSendApi.h"\n#include "BatchSendMetrics.h"\n'
        fragment='BatchSendPublic.inc'
    elif name=='source/rocketmq/ProducerImpl.cpp':
        includes='#include <condition_variable>\n#include <mutex>\n#include <vector>\n#include "RpcClientImpl.h"\n#include "BatchSendMetrics.h"\n#include "TinyIMXBatchSendReceiptPolicy.h"\n'
        fragment='BatchSendImpl.inc'
    elif name=='source/client/RpcClientImpl.cpp':
        includes='#include <atomic>\n';fragment='BatchSendRpc.inc'
    else:raise ValueError(name)
    text=once(text,'ROCKETMQ_NAMESPACE_BEGIN',includes+'\nROCKETMQ_NAMESPACE_BEGIN')
    return once(text,'ROCKETMQ_NAMESPACE_END',(patches/fragment).read_text()+'\nROCKETMQ_NAMESPACE_END').encode()

def apply(source,baseline_directory,patches,policy,report):
    source=Path(source);baseline_directory=Path(baseline_directory);patches=Path(patches);policy=Path(policy)
    if source.is_symlink()or source.resolve()!=source.absolute():raise ValueError('Physical isolated source overlay required')
    if source.resolve()==baseline_directory.resolve():raise ValueError('Baseline authority cannot be the write target')
    policy_raw=policy.read_bytes()
    if sha(policy_raw)!=POLICY_SHA256:raise ValueError('Exact C++11/14 qualified policy required')
    desired={}
    for name,digest in BASELINE.items():
        raw=(baseline_directory/name).read_bytes()
        if sha(raw)!=digest:raise ValueError('Safe baseline source changed: '+name)
        desired[name]=render(name,raw,patches)
    desired['include/rocketmq/TinyIMXBatchSendApi.h']=(patches/'TinyIMXBatchSendApi.h').read_bytes()
    desired['source/rocketmq/include/BatchSendMetrics.h']=(patches/'BatchSendMetrics.h').read_bytes()
    desired['source/rocketmq/include/TinyIMXBatchSendReceiptPolicy.h']=policy_raw
    for name,raw in desired.items():
        path=source/name
        if path.is_symlink()or any(p.is_symlink()for p in path.parents if p.is_relative_to(source)):
            raise ValueError('Overlay symlink refused')
        if path.exists()and sha(path.read_bytes())not in (BASELINE.get(name),sha(raw)):
            raise ValueError('Foreign or partial overlay edit refused: '+name)
        if name in BASELINE and not path.is_file():raise ValueError('Baseline target missing: '+name)
        if path.with_name(path.name+'.wire-send.tmp').exists():raise ValueError('Unresolved partial write')
    result={}
    for name,raw in desired.items():
        path=source/name;already=path.exists()and path.read_bytes()==raw
        if not already:
            path.parent.mkdir(parents=True,exist_ok=True);temporary=path.with_name(path.name+'.wire-send.tmp')
            with temporary.open('xb')as sink:sink.write(raw);sink.flush();os.fsync(sink.fileno())
            os.replace(temporary,path)
        result[name]=dict(before=BASELINE.get(name),after=sha(raw),already=already)
    value=dict(status='EXACT_SAFE_SDK_OPTIONAL_NORMAL_BATCH_SEND_SOURCE_APPLIED_NOT_BUILT_OR_DEPLOYED',files=result,
               original_safe_wait_and_batch_ACK_retained=True,virtual_tables_and_class_fields_changed=False,
               pinned_normal_Proxy_joined_receipt_only=True,maximum_entries=32,maximum_body_bytes=256*1024,
               maximum_serialized_request_bytes=512*1024,automatic_retry_or_legacy_fallback=False)
    Path(report).write_text(json.dumps(value,indent=2)+'\n');return value

if __name__=='__main__':
    p=argparse.ArgumentParser()
    for name in ('source','baseline-directory','patches','policy','report'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();print(json.dumps(apply(a.source,a.baseline_directory,a.patches,a.policy,a.report)))
