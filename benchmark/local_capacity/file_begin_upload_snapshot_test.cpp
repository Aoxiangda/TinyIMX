// All SQL mutations and fault redirects require a new own schema. No deletion.
#include "common/db/MySqlConnectionPool.h"
#include "services/repository/FileRepository.h"
#include "services/file/repository/FileRepositoryAdapter.h"
#include "services/file/application/FileApplicationService.h"
#include <mysql/mysql.h>
#include <nlohmann/json.hpp>
#include <barrier>
#include <chrono>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string_view>
#include <thread>
namespace {
thread_local int reads=0,pings=0,commit_fault=0;
thread_local bool read_fault=false,session_insert_fault=false;
int checks=0;
void Check(bool yes,const char* name) {
    if(!yes)throw std::runtime_error(name);
    ++checks;std::cout<<"[PASS] "<<name<<'\n';
}
}
extern "C" int __real_mysql_query(MYSQL*,const char*);
extern "C" int __real_mysql_ping(MYSQL*);
extern "C" int __real_mysql_commit(MYSQL*);
extern "C" int __wrap_mysql_query(MYSQL* db,const char* text) {
    std::string_view sql(text);
    if(sql.find("FROM im_file_upload_sessions u JOIN im_files f")!=sql.npos&&
       sql.find("u.client_upload_id = '")!=sql.npos) {
        ++reads;if(read_fault){read_fault=false;return __real_mysql_query(db,"SELECT * FROM own_missing_file_snapshot_table");}
    }
    if(sql.starts_with("INSERT INTO im_file_upload_sessions")&&session_insert_fault) {
        session_insert_fault=false;return __real_mysql_query(db,"SELECT * FROM own_missing_file_snapshot_table");
    }
    return __real_mysql_query(db,text);
}
extern "C" int __wrap_mysql_ping(MYSQL* db){++pings;return __real_mysql_ping(db);}
extern "C" int __wrap_mysql_commit(MYSQL* db) {
    int mode=commit_fault;commit_fault=0;if(mode==1)return 1;
    int code=__real_mysql_commit(db);return mode==2&&code==0?1:code;
}
int main(int argc,char** argv) {
using namespace tinyimx;
using namespace tinyimx::file;
try {
    if(argc!=3)return 2;bool enabled=std::string(argv[2])=="1";
    std::ifstream stream(argv[1]);nlohmann::json cfg;stream>>cfg;const auto& j=cfg.at("mysql");
    MySqlConfig db;db.enable=true;db.host=j.at("host");db.port=j.at("port");
    db.database=j.at("database");db.user=j.at("user");db.password=j.at("password");db.pool_size=j.at("pool_size");
    if(!db.database.starts_with("codex_file_snapshot_20261006_"))throw std::runtime_error("REFUSE_NONOWNED_SCHEMA");
    MySqlConnectionPool pool;Check(pool.Initialize(db),"owned_pool_ready");
    auto exec=[&](const std::string& sql){auto c=pool.Acquire();if(!c||!c->Execute(sql))throw std::runtime_error("OWN_FIXTURE_WRITE_FAILED");};
    auto count=[&](const std::string& table,const std::string& filter="1=1"){
        auto c=pool.Acquire();MySqlQueryResult q;if(!c||!c->Query("SELECT COUNT(*) FROM "+table+" WHERE "+filter,&q))
            throw std::runtime_error("OWN_FIXTURE_READ_FAILED");
        return std::stoull(q.rows.at(0).at(0));
    };
    exec("INSERT INTO im_users(user_id,username,nickname,status) VALUES "
         "(10001,'own-file-a','Owned A',1),(10002,'own-file-b','Owned B',1)");
    FileRepository repository(&pool);FileRepositoryAdapter adapter(&repository,&pool);FileApplicationService service(&adapter);
    BeginUploadCommand command;command.actor_user_id=10001;command.client_upload_id="snapshot'quoted\\id";
    command.file_name="snapshot'bytes.bin";command.content_type="application/octet-stream";command.total_size=4096;
    command.checksum_algorithm="sha256";command.expected_checksum=std::string(64,'a');
    auto made=service.BeginUpload(command);
    Check(made.Accepted()&&made.outcome==BeginUploadOutcome::kCreated&&made.bundle,"original_new_upload_created");
    auto fid=made.bundle->file.file_id,uid=made.bundle->session.upload_id;
    Check(fid>0&&uid>0&&made.bundle->file.storage_key=="files/"+std::to_string(fid)&&
          made.bundle->file.owner_user_id==10001,"durable_identity_owner_storagekey");
    reads=0;pings=0;auto repeat=service.BeginUpload(command);int repeat_reads=reads,repeat_pings=pings;
    Check(repeat.Accepted()&&repeat.outcome==BeginUploadOutcome::kReused&&repeat.bundle&&
          repeat.bundle->file.file_id==fid&&repeat.bundle->session.upload_id==uid&&
          repeat.bundle->file.file_name==command.file_name,"quoted_bytes_fullsnapshot_idempotent");
    Check(repeat_reads==(enabled?1:2)&&repeat_pings==(enabled?1:2),"actual_idempotent_read_and_health_ping_count");
    auto altered=command;altered.file_name="conflicting.bin";reads=0;
    auto conflict=service.BeginUpload(altered);int conflict_reads=reads;
    Check(conflict.Completed()&&conflict.outcome==BeginUploadOutcome::kIdempotencyConflict&&!conflict.bundle&&
          conflict_reads==(enabled?1:2),"fingerprint_conflict_preserved");
    auto other=command;other.actor_user_id=10002;
    auto other_made=service.BeginUpload(other);
    Check(other_made.Accepted()&&other_made.outcome==BeginUploadOutcome::kCreated&&other_made.bundle&&
          other_made.bundle->file.file_id!=fid,"owner_scoped_same_client_id");
    Check(service.GetUploadSession({10002,uid}).status==FileApplicationStatus::kNotFound,"other_owner_session_denied");
    auto absent=command;absent.actor_user_id=999999;absent.client_upload_id="missing-owner";
    Check(service.BeginUpload(absent).status==FileApplicationStatus::kNotFound,"missing_owner_rejected");
    read_fault=true;reads=0;auto failed_read=service.BeginUpload(command);
    Check(!read_fault&&reads==1&&failed_read.status==FileApplicationStatus::kStorageError,"precheck_storage_error_not_reused");
    auto failed_insert=command;failed_insert.client_upload_id="failed-insert";
    auto before_files=count("im_files"),before_sessions=count("im_file_upload_sessions");session_insert_fault=true;
    auto failed_write=service.BeginUpload(failed_insert);
    Check(!session_insert_fault&&failed_write.status==FileApplicationStatus::kStorageError&&
          count("im_files")==before_files&&count("im_file_upload_sessions")==before_sessions,"session_insert_error_rolls_back_speculative_file");
    auto pre=command;pre.client_upload_id="precommit-error";commit_fault=1;
    auto before_commit=service.BeginUpload(pre);
    Check(commit_fault==0&&before_commit.status==FileApplicationStatus::kStorageError&&
          count("im_files")==before_files&&count("im_file_upload_sessions")==before_sessions,"precommit_error_original_rollback");
    auto lost=command;lost.client_upload_id="postcommit-error";commit_fault=2;reads=0;
    auto recovered=service.BeginUpload(lost);int recovery_reads=reads;
    Check(commit_fault==0&&recovered.Accepted()&&recovered.outcome==BeginUploadOutcome::kReused&&recovered.bundle&&
          count("im_files")==before_files+1&&count("im_file_upload_sessions")==before_sessions+1&&
          recovery_reads==2,"simulated_postcommit_error_fresh_durable_recovery");
    auto again=service.BeginUpload(lost);
    Check(again.Accepted()&&again.bundle&&again.bundle->file.file_id==recovered.bundle->file.file_id,
          "recovered_identity_repeat_stable");
    auto cancel=service.CancelUpload({10001,uid});
    Check(cancel.Completed()&&cancel.bundle&&cancel.bundle->file.status==FileStatus::kCanceled&&
          cancel.bundle->session.status==UploadSessionStatus::kCanceled,"cancel_original_atomic_states");
    reads=0;auto canceled_repeat=service.BeginUpload(command);int canceled_reads=reads;
    Check(canceled_repeat.Accepted()&&canceled_repeat.bundle&&canceled_repeat.bundle->file.status==FileStatus::kCanceled&&
          canceled_repeat.bundle->session.status==UploadSessionStatus::kCanceled&&canceled_reads==(enabled?1:2),
          "canceled_snapshot_not_active");
    auto invalid=command;invalid.client_upload_id="invalid-own-bundle";
    auto invalid_made=service.BeginUpload(invalid);
    Check(invalid_made.Accepted()&&invalid_made.bundle,"own_invalid_fixture_initially_valid");
    exec("UPDATE im_file_upload_sessions SET total_size=total_size+1 WHERE upload_id="+
         std::to_string(invalid_made.bundle->session.upload_id));
    Check(service.BeginUpload(invalid).status==FileApplicationStatus::kInvalidRecord,"original_complete_bundle_validation");
    auto concurrent=command;concurrent.client_upload_id="eight-concurrent";
    before_files=count("im_files");before_sessions=count("im_file_upload_sessions");
    std::barrier start(8);std::vector<BeginUploadResult> results(8);std::vector<std::thread> threads;
    for(int i=0;i<8;++i)threads.emplace_back([&,i]{start.arrive_and_wait();results[i]=service.BeginUpload(concurrent);});
    for(auto& t:threads)t.join();
    int creators=0,reused=0;std::set<std::uint64_t> identities;bool all_valid=true;
    for(auto& x:results){all_valid=all_valid&&x.Accepted()&&x.bundle.has_value();
        if(x.bundle)identities.insert(x.bundle->file.file_id);
        creators+=x.outcome==BeginUploadOutcome::kCreated;reused+=x.outcome==BeginUploadOutcome::kReused;}
    Check(all_valid&&creators==1&&reused==7&&identities.size()==1,"eight_actual_concurrent_begin_one_identity");
    Check(count("im_files")==before_files+1&&count("im_file_upload_sessions")==before_sessions+1,
          "unique_race_no_speculative_orphans");
    auto current=service.BeginUpload(concurrent);
    Check(current.Accepted()&&current.bundle,"concurrent_fixture_readable");
    auto race_uid=current.bundle->session.upload_id;
    std::barrier race_start(2);BeginUploadResult raced;CancelUploadResult canceled;
    std::thread retry([&]{race_start.arrive_and_wait();raced=service.BeginUpload(concurrent);});
    std::thread canceler([&]{race_start.arrive_and_wait();canceled=service.CancelUpload({10001,race_uid});});
    retry.join();canceler.join();
    Check(raced.Accepted()&&raced.bundle&&canceled.Completed()&&canceled.bundle&&
          (raced.bundle->session.status==UploadSessionStatus::kActive||raced.bundle->session.status==UploadSessionStatus::kCanceled),
          "concurrent_begin_cancel_returns_valid_snapshot");
    auto durable=service.GetUploadSession({10001,race_uid});
    Check(durable.Found()&&durable.bundle&&durable.bundle->file.status==FileStatus::kCanceled&&
          durable.bundle->session.status==UploadSessionStatus::kCanceled,"subsequent_state_read_sees_durable_cancel");
    auto measure=command;measure.client_upload_id="measured-existing";
    auto initial=service.BeginUpload(measure);Check(initial.Accepted()&&initial.bundle,"measured_fixture_ready");
    nlohmann::json timing=nlohmann::json::array();bool exact=true;
    for(int i=0;i<80;++i) {
        reads=0;pings=0;auto before=std::chrono::steady_clock::now();auto reply=service.BeginUpload(measure);
        auto us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-before).count();
        exact=exact&&reply.Accepted()&&reply.outcome==BeginUploadOutcome::kReused&&reply.bundle&&
              reply.bundle->file.file_id==initial.bundle->file.file_id&&reads==(enabled?1:2)&&pings==(enabled?1:2);
        timing.push_back({{"sample",i},{"total_us",us},{"client_key_reads",reads},{"health_pings",pings}});
    }
    Check(exact,"80_actual_idempotent_calls_exact_state_and_counts");
    std::cout<<nlohmann::json({{"status","FILE_SNAPSHOT_REAL_MYSQL_PASS"},{"enabled",enabled},
        {"pool",db.pool_size},{"checks",checks},{"timing",timing},{"performance_acceptance",false},
        {"postcommit_fault","testwrapper_real_commit_then_fake_error_not_network_fault"},
        {"snapshot_contract","valid_owner_scoped_state_at_precheck_read_later_operations_reread"}}).dump()<<'\n';
    pool.Shutdown();return 0;
}catch(const std::exception& error){std::cerr<<"[FAIL] "<<error.what()<<'\n';return 1;}
}
