#include "common/config/Config.h"
#include "common/db/MySqlConnectionPool.h"
#include "common/logging/Logger.h"
#include "services/projection/unread/UnreadProjectionReader.h"

#include <iostream>
#include <stdexcept>

using namespace tinyimx::projection::unread;
namespace {
void Require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
std::uint64_t Selects(tinyimx::MySqlConnectionPool& pool) {
    auto lease=pool.Acquire();Require(bool(lease),"session status lease failed");tinyimx::MySqlQueryResult r;
    Require(lease->Query("SHOW SESSION STATUS LIKE 'Com_select'",&r)&&r.rows.size()==1&&r.rows[0].size()==2,"session status read failed");
    return std::stoull(r.rows[0][1]);
}
}
int main(int argc,char**argv) {
    if(argc!=3||std::string(argv[2])!="--readonly-r57")return 2;
    tinyimx::Config config;
    if(!config.LoadFromFile(argv[1])||config.MySql().database!="codex_perf041_20261018_r57")return 2;
    if(!tinyimx::Logger::Instance().Init(config.Logger()))return 2;
    auto mysql=config.MySql();mysql.pool_size=1;tinyimx::MySqlConnectionPool pool;if(!pool.Initialize(mysql))return 2;
    try {
        UnreadProjectionReader reader(&pool);
        std::vector<std::pair<std::uint64_t,std::uint64_t>> dialogs={{500002,500001},{500002,500003},{500001,500002},{500004,500003},{500002,500001}};
        std::vector<UnreadProjectionReadResult<DialogUnreadSnapshot>> singles;
        const auto before=Selects(pool);
        for(auto [u,p]:dialogs)singles.push_back(reader.LoadDialogSnapshot(u,p));
        const auto after_single=Selects(pool);const auto batch=reader.LoadDialogSnapshots(dialogs);const auto after_batch=Selects(pool);
        Require(after_single-before==dialogs.size()&&after_batch-after_single==1,"SQL statement count was not N to 1 on sole session");
        Require(batch.size()==dialogs.size(),"batch result cardinality differs");
        for(std::size_t i=0;i<dialogs.size();++i){
            const auto& a=singles[i];const auto& b=batch[i];Require(a.success&&b.success,"snapshot read failed");
            Require(b.value.receiver_user_id==dialogs[i].first&&b.value.peer_user_id==dialogs[i].second&&a.value.private_unread==b.value.private_unread&&a.value.total_unread==b.value.total_unread,"batch differs from individual authoritative query");
        }
        Require(batch[0].value.private_unread==12&&batch[0].value.total_unread==12&&batch[1].value.private_unread==0&&batch[1].value.total_unread==12&&batch[2].value.total_unread==0&&batch[3].value.total_unread==0,"retained fixture truth differs");
        Require(reader.LoadDialogSnapshots({}).empty()&&reader.LoadDialogSnapshots(std::vector<std::pair<std::uint64_t,std::uint64_t>>(257,{500002,500001})).empty(),"batch bound not enforced");
        const auto invalid_before=Selects(pool);
        for(auto pair:{std::pair<std::uint64_t,std::uint64_t>{0,1},{1,0},{1,1}}){auto r=reader.LoadDialogSnapshots({pair});Require(r.size()==1&&!r[0].success,"invalid identity accepted");}
        Require(Selects(pool)==invalid_before,"invalid requests issued SQL");
        auto limit=reader.LoadDialogSnapshots(std::vector<std::pair<std::uint64_t,std::uint64_t>>(256,{500002,500001}));
        Require(limit.size()==256,"maximum batch rejected");for(const auto& r:limit)Require(r.success&&r.value.private_unread==12&&r.value.total_unread==12,"duplicate maximum batch differs");
        std::cout<<"[PASS] real MySQL readonly r57: 5 individual SELECTs versus 1 batch SELECT, duplicate/multi-peer/empty truth equal; 256 bound; invalid IDs issue zero SQL\n";
        pool.Shutdown();tinyimx::Logger::Instance().Shutdown();return 0;
    }catch(const std::exception& e){std::cerr<<"[FAIL] "<<e.what()<<'\n';pool.Shutdown();tinyimx::Logger::Instance().Shutdown();return 1;}
}
