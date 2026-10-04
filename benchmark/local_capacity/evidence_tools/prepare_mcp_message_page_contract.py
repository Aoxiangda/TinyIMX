from pathlib import Path
r=Path(__file__).resolve().parent.parent;s=r/'source'
p=s/'services/intelligence/mcp/McpDomainTools.cpp';t=p.read_text(encoding='utf-8')
assert t.count('constexpr std::uint32_t kMaxPageSize = 100;')==1
t=t.replace('constexpr std::uint32_t kMaxPageSize = 100;','constexpr std::uint32_t kMaxPageSize = 100;\n// MessageService read RPCs support at most 50 records per page.\nconstexpr std::uint32_t kMaxMessagePageSize = 50;')
t=t.replace('std::uint32_t PageSize(const Json& args) {','std::uint32_t PageSize(const Json& args, std::uint32_t maximum = kMaxPageSize) {')
t=t.replace('if (value == 0 || value > kMaxPageSize) {\n        Invalid("limit must be in range 1-100");','if (value == 0 || value > maximum) {\n        Invalid("limit must be in range 1-" + std::to_string(maximum));')
t=t.replace('Json LimitSchema() { return Json{{"type", "integer"}, {"minimum", 1}, {"maximum", kMaxPageSize}}; }','Json LimitSchema(std::uint32_t maximum = kMaxPageSize) {\n    return Json{{"type", "integer"}, {"minimum", 1}, {"maximum", maximum}};\n}')
for name in ['tinyimx.message.list_conversations','tinyimx.message.list_history']:
 a=t.index('{"'+name+'"');z=t.index('}, error)) return false;',a)+len('}, error)) return false;')
 part=t[a:z];assert part.count('LimitSchema()')==1 and part.count('PageSize(args)')==1
 t=t[:a]+part.replace('LimitSchema()','LimitSchema(kMaxMessagePageSize)').replace('PageSize(args)','PageSize(args, kMaxMessagePageSize)')+t[z:]
p.write_text(t,encoding='utf-8',newline='\n')

p=s/'tests/mcp/mcp_domain_tools_test.cpp';t=p.read_text(encoding='utf-8')
t=t.replace('mutable bool fail_conversations{false};','mutable bool fail_conversations{false};\n    mutable std::uint32_t last_limit{0};\n    mutable std::size_t page_backend_calls{0};')
for name in ['ListFriends','ListConversations','ListHistory','ListMyGroups','ListGroupMembers']:
 a=t.index('> '+name+'(');z=t.index('last_actor = r.actor_user_id;',a)+len('last_actor = r.actor_user_id;')
 t=t[:z]+'\n        last_limit = r.limit;\n        ++page_backend_calls;'+t[z:]
t=t.replace('if (!condition) std::cerr << "CHECK failed: " << message << \'\\n\';','if (!condition) std::cerr << "CHECK failed: " << message << \'\\n\';\n    else std::cout << "[PASS] " << message << \'\\n\';')
needle='    if (!Check(std::is_sorted(names.begin(), names.end()), "deterministic tool order")) return 1;'
t=t.replace(needle,needle+'''
    for (const auto& item : tools) {
        const auto name = item.at("name").get<std::string>();
        if (!item.at("inputSchema").at("properties").contains("limit")) continue;
        const bool message_page = name == "tinyimx.message.list_conversations" ||
                                  name == "tinyimx.message.list_history";
        if (!Check(item.at("inputSchema").at("properties").at("limit").at("maximum") ==
                   (message_page ? 50 : 100), "schema matches each domain page contract")) return 1;
    }
''')
needle='    backend->fail_conversations = true;'
new='''    // A permissive fake must still prove which requests reach the backend.
    int boundary_id = 100;
    for (const std::string tool : {"tinyimx.message.list_conversations", "tinyimx.message.list_history"}) {
        const auto base = tool == "tinyimx.message.list_history" ? Json{{"peer_user_id", 77}} : Json::object();
        for (const Json limit : {Json(1), Json(50), Json()}) {
            auto args = base;
            if (!limit.is_null()) args["limit"] = limit;
            const auto before = backend->page_backend_calls;
            const auto result = Call(dispatcher, ctx, tool, args, boundary_id++);
            if (!Check(result["result"]["isError"] == false, "message valid page or default succeeds")) return 1;
            if (!Check(backend->page_backend_calls == before + 1 && backend->last_actor == 42 &&
                       backend->last_limit == (limit.is_null() ? 50 : limit.get<int>()),
                       "message valid limit forwarded without clamping")) return 1;
        }
        for (const Json limit : {Json(0), Json(-1), Json(51), Json(100), Json(101), Json(1.25),
                                 Json("50"), Json(true), Json(), Json(UINT64_MAX)}) {
            auto args = base;
            args["limit"] = limit;
            const auto before = backend->page_backend_calls;
            const auto result = Call(dispatcher, ctx, tool, args, boundary_id++);
            if (!Check(result["result"]["isError"] == true &&
                       result["result"]["structuredContent"]["error"]["code"] == "invalid_arguments",
                       "invalid message limit produces argument error")) return 1;
            if (!Check(backend->page_backend_calls == before,
                       "invalid message limit rejected before backend RPC")) return 1;
        }
    }
    for (const std::string tool : {"tinyimx.social.list_friends", "tinyimx.group.list_my_groups", "tinyimx.group.list_members"}) {
        Json args{{"limit", 100}};
        if (tool == "tinyimx.group.list_members") args["group_id"] = 123;
        const auto before = backend->page_backend_calls;
        const auto result = Call(dispatcher, ctx, tool, args, boundary_id++);
        if (!Check(result["result"]["isError"] == false && backend->page_backend_calls == before + 1 &&
                   backend->last_limit == 100, "friends and group tools retain page100")) return 1;
    }

'''
assert t.count(needle)==1;t=t.replace(needle,new+needle);p.write_text(t,encoding='utf-8',newline='\n')

addition='''
### 2026-10-05：修正 MCP 消息分页工具契约（验证待完成）

先前真实隔离 MCP 的 page100 会话查询失败有确定代码原因：MCP schema/校验接受 1–100，而 MessageRpcClient 与 MessageApplicationService 的会话/历史读分页最多 50。仅这两项工具改为 schema 和本地校验 1–50；缺省 50、合法 1/50 原样传递，不悄悄截断；51/100 等已经不能完成的请求现在在发起 RPC 前返回 invalid_arguments。好友、我的群、成员列表保留 100，鉴权/身份/查询/期限与 SQL 完全不变。

新增回归检查两类工具边界、缺省、无效类型与大整数，并验证无效参数不调用后端、其他三类工具仍允许 100。构建先在独立目录将原 domain 源码与新测试链接，保存应有失败；再只构建 MCP domain/core/server，使用缓存依赖并单线程，保存原二进制 SHA 与 red/green 日志。构建不会部署或改变 19 个服务，尤其不构建当前仍保存的已拒绝 Gateway 公平调度源码。之后真实隔离 MCP 验证需独立审计，当前没有性能接受结论。

v16 脱敏归档本机 70 文件逐 SHA 通过，archive SHA 488f8ceb9a79614ac0848aa370ddf9abcae8b9c0c19e855572794bfa2b6bbcbc，保留提交合并失败、精确回滚及 CPU 对照。MCP 契约修正不等于私聊尾延迟改善；10k–50k 全功能极致性能、AI 服务/正式 principal 和每功能混合负载验证仍未完成。
'''
for name in ['ISSUES.md','CAPACITY_RUNBOOK.md','ROOT_CAUSE_REVIEW_20261005.md']:
 p=s/'docs/performance'/name;p.write_text(p.read_text(encoding='utf-8')+'\n'+addition,encoding='utf-8',newline='\n')
e=s/'benchmark/local_capacity/evidence_tools'
for name in ['build_mcp_message_page_contract.sh','apply_groupcommit_rejection_and_cost_review.sh']:(e/name).write_bytes((r/'tools'/name).read_bytes())
print('PREPARED_MCP_PAGE_CONTRACT_FILES=7')
