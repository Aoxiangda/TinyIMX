#include "common/config/Config.h"
#include "common/db/MySqlConnectionPool.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
using Json=nlohmann::json;
void Require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
std::string Scalar(tinyimx::MySqlConnection& c,const std::string& q){tinyimx::MySqlQueryResult r;Require(c.Query(q,&r)&&r.rows.size()==1&&r.rows[0].size()==1,"scalar query failed");return r.rows[0][0];}
int main(int argc,char**argv){Json result={{"issue_id","PERF-051"},{"status","FAIL"},{"checks",Json::array()}};
try{
    Require(argc==4,"config schema enabled required");std::string schema=argv[2];bool enabled=std::string(argv[3])=="1";
    Require(schema.starts_with("codex_perf051_")&&schema.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_")==std::string::npos,"unowned fixture");
    tinyimx::Config config;Require(config.LoadFromFile(argv[1]),"config failed");auto settings=config.MySql();settings.database=schema;settings.pool_size=1;
    tinyimx::MySqlConnection control;Require(control.Connect(settings),"control failed");tinyimx::MySqlConnectionPool pool;Require(pool.Initialize(settings),"pool failed");
    auto check=[&](bool ok,const char* message){Require(ok,message);result["checks"].push_back(message);};
    auto c=pool.Acquire();check(c&&Scalar(*c,"SELECT DATABASE()")==schema,"fixture_scope_exact");
    result["transport"]=c->ConnectionDescription();
    check(c->ConnectionDescription().find(settings.unix_socket.empty()?"TCP/IP":"UNIX socket")!=std::string::npos,"actual_transport_matches_explicit_configuration");
    check(Scalar(*c,"SELECT CURRENT_USER()")==settings.user+"@%","same_authenticated_fixture_account_no_transport_bypass");
    auto bad=settings;bad.unix_socket="/run/codex-perf051-does-not-exist.sock";tinyimx::MySqlConnection unavailable;
    check(!unavailable.Connect(bad),"missing_explicit_unix_socket_never_falls_back_to_tcp");
    check(c->Execute("CREATE TABLE pool_probe(id INT PRIMARY KEY,value INT NOT NULL)ENGINE=InnoDB"),"owned_probe_table_created");
    unsigned skipped=0,probed=0;
    for(unsigned i=0;i<10;++i){auto before=c->PingAttempts();c.Reset();c=pool.Acquire();Require(c&&Scalar(*c,"SELECT 1")=="1","warm acquire failed");auto diff=c->PingAttempts()-before;Require(diff<=1,"unexpected multiple probes");skipped+=diff==0;probed+=diff==1;}
    result["warm_acquisitions"]={{"skipped",skipped},{"probed",probed}};
    check(enabled?skipped>=8:probed==10,"warm_probe_policy_and_original_off_path");
    auto before=c->PingAttempts();c.Reset();std::this_thread::sleep_for(std::chrono::milliseconds(550));c=pool.Acquire();check(c&&c->PingAttempts()==before+1&&Scalar(*c,"SELECT 1")=="1","expired_validation_always_wire_pings");
    tinyimx::MySqlQueryResult pending;Require(c->Query("SELECT 1;SELECT 2",&pending),"pending result fixture failed");
    check(!c->MayReuseWithoutPing(),"pending_multi_result_never_fast_reused");
    c->Close();c.Reset();c=pool.Acquire();check(c&&Scalar(*c,"SELECT 1")=="1","explicit_protocol_close_reconnects_cleanly");
    // Kill only this account's fixture connection. Poll must detect FIN/error;
    // the next acquisition reconnects before any business statement executes.
    auto id=Scalar(*c,"SELECT CONNECTION_ID()");auto* object=&*c;c.Reset();Require(control.Execute("KILL "+id),"owned connection kill failed");
    auto until=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(object->MayReuseWithoutPing()&&std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    check(!object->MayReuseWithoutPing(),"readable_or_closed_transport_never_skipped");
    c=pool.Acquire();check(c&&Scalar(*c,"SELECT CONNECTION_ID()")!=id&&Scalar(*c,"SELECT 1")=="1","dead_idle_connection_reconnected_before_query");
    // A disconnect after acquisition can race even with the original PING.
    // Its failed write is reported once and must never be replayed implicitly.
    id=Scalar(*c,"SELECT CONNECTION_ID()");Require(control.Execute("KILL "+id),"kill before business failed");std::this_thread::sleep_for(std::chrono::milliseconds(20));
    check(!c->Execute("INSERT INTO pool_probe VALUES(1,1)")&&!c->IsConnected(),"failed_business_write_poisoned_and_not_replayed");
    c.Reset();c=pool.Acquire();check(c&&Scalar(*c,"SELECT COUNT(*)FROM pool_probe")=="0","next_acquire_recovers_without_replaying_failed_write");
    check(c->BeginTransaction()&&c->Execute("INSERT INTO pool_probe VALUES(2,2)"),"transaction_started_without_early_visibility");
    check(!c->MayReuseWithoutPing(),"active_transaction_never_fast_reused");
    check(Scalar(control,"SELECT COUNT(*)FROM pool_probe")=="0","uncommitted_write_invisible");
    id=Scalar(*c,"SELECT CONNECTION_ID()");Require(control.Execute("KILL "+id),"transaction kill failed");std::this_thread::sleep_for(std::chrono::milliseconds(20));
    check(!c->Commit(),"disconnect_during_transaction_never_successfully_commits");c.Reset();c=pool.Acquire();
    check(c&&!c->InTransaction()&&Scalar(*c,"SELECT COUNT(*)FROM pool_probe")=="0","uncertain_session_discarded_rollback_and_clean_reconnect");
    check(c->BeginTransaction()&&c->Execute("INSERT INTO pool_probe VALUES(3,3)")&&c->Commit(),"valid_transaction_still_commits");
    check(c->BeginTransaction()&&!c->Execute("INSERT INTO pool_probe VALUES(3,9)")&&c->IsConnected()&&c->Rollback(),"duplicate_constraint_error_keeps_healthy_session_and_rollback");
    check(Scalar(*c,"SELECT value FROM pool_probe WHERE id=3")=="3","duplicate_error_does_not_change_original_row");
    c.Reset();pool.Shutdown();check(!pool.Acquire(std::chrono::milliseconds(1)),"shutdown_rejects_new_lease");
    result["status"]="PASS";
}catch(const std::exception&e){result["error"]=e.what();}
std::cout<<result.dump()<<std::endl;return result["status"]=="PASS"?0:2;}
