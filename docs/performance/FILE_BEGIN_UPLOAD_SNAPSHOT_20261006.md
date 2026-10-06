# 文件初始化幂等重读优化（2026-10-06）

状态：仅本地准备SOURCE_ONLY，尚未应用guest或构建/部署/性能验收。正在进行的群部分批真实对照不受影响。

实际源码BeginUpload先FindByClientUploadId获取完整owner-scoped JOIN bundle；存在时ResolveExistingUpload再次同key查询。新上传路径则保持原事务、UserExists、file INSERT、server-generated storagekey UPDATE、session INSERT/readback、COMMIT；唯一键败者与不确定提交必须重新读durable结果。本迭代只去除正常已存在请求的第二次读取。

strict defaultOFF开关TINYIMX_FILE_BEGIN_UPLOAD_SNAPSHOT_ENABLE=1。在found路径将本请求刚读出的完整结果送入原状态/身份/ToView/fingerprint校验，不缓存跨请求结果。原OFF重新读以及新创建/竞争败者/不确定提交的fresh resolver保持。完整ToView必须校验file/session IDs、owner、total_size及状态一致，冲突仍不返回bundle；Get/chunk/finalize/download权限与状态读取不变。

并发状态语义：结果取自本请求precheck SQL快照；原第二次无锁SELECT也不保证回复时最新状态。取消/完成恰好并发时Begin可能返回先前有效状态，后续有状态要求的操作仍按原durable读取验证，不因优化获得权限或写已取消文件。需真实并发Begin+Cancel验证，不能伪造强一致latest保证。

原生待验证六个absent自有schema OFF/ON/非法开关 pool1/4，原始FindSQL及健康PING每幂等调用2→1计数；新创建/引用反斜杠字节/owner隔离/fingerprint冲突/无效bundle/SQL错误/唯一键8并发无孤儿/原取消与状态；提交前回滚和真实提交后模拟错误必须fresh读取恢复。80次每模式原样记录，不表示生产P99或容量。wrapper只测试链接，模拟返回错误不等同实际断网。

不删除任何业务文件或SQL记录；所有own schema、失败、前像与Git保留；实际文件分块/终结/恢复下载/字节正确性和公开链后续仍必须在候选对照验证，全部功能20k–50k极致性能OPEN。
