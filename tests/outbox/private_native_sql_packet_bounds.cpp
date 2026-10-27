#include "common/config/Config.h"
#include "common/db/MySqlConnection.h"
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
void Require(bool ok,const char* what){if(!ok)throw std::runtime_error(what);std::cout<<"PASS "<<what<<'\n';}
void Begin(tinyimx::MySqlConnection& c,const tinyimx::MySqlConfig& cfg){Require(c.Connect(cfg)&&c.BeginTransaction(),"fresh transaction");}
}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    try{
        tinyimx::Config config;Require(config.LoadFromFile(argv[1])&&config.MySql().database=="codex_perf041_20261018_r90","exact isolated r90");
        std::string rows="SELECT 0 AS n";for(int i=1;i<64;++i)rows+=" UNION ALL SELECT "+std::to_string(i);
        {
            tinyimx::MySqlConnection c;Begin(c,config.MySql());
            std::vector<tinyimx::MySqlTransactionStatementResult> out;
            const auto before=c.GroupedStatementCommandAttempts();
            Require(c.ExecuteTransactionStatementsBoundedRows({{rows,true},{"SELECT 65",true}},&out,64)&&out.size()==2,"64 rows plus following typed result drained");
            Require(out[0].rows.rows.size()==64&&out[1].rows.rows.size()==1&&out[1].rows.rows[0][0]=="65","result boundaries preserved");
            std::set<int> values;for(const auto& row:out[0].rows.rows)values.insert(std::stoi(row[0]));
            Require(values.size()==64&&*values.begin()==0&&*values.rbegin()==63,"all64 values preserved");
            Require(c.GroupedStatementCommandAttempts()==before+1,"two statements use one bounded command");
            tinyimx::MySqlQueryResult after;Require(c.Query("SELECT 42",&after)&&after.rows.size()==1&&after.rows[0][0]=="42","next query sees no residual results");
            Require(c.Rollback(),"read-only test rolled back");
        }
        for(const std::size_t limit:{0,65}){
            tinyimx::MySqlConnection c;Begin(c,config.MySql());std::vector<tinyimx::MySqlTransactionStatementResult> out;
            const auto before=c.GroupedStatementCommandAttempts();
            Require(!c.ExecuteTransactionStatementsBoundedRows({{"SELECT 1",true}},&out,limit)&&c.IsConnected()&&c.InTransaction()&&c.GroupedStatementCommandAttempts()==before,"invalid row limits rejected before send");Require(c.Rollback(),"invalid-limit transaction returned");
        }
        {
            tinyimx::MySqlConnection c;Begin(c,config.MySql());std::vector<tinyimx::MySqlTransactionStatementResult> out;
            Require(!c.ExecuteTransactionStatements({{"SELECT 1 UNION ALL SELECT 2",true}},&out)&&!c.IsConnected()&&!c.InTransaction(),"legacy one-row API still rejects and closes multirow result");
        }
        {
            tinyimx::MySqlConnection c;Begin(c,config.MySql());std::vector<tinyimx::MySqlTransactionStatementResult> out;
            Require(!c.ExecuteTransactionStatementsBoundedRows({{rows,true}},&out,63)&&!c.IsConnected(),"configured row overflow closes uncertain session");
        }
        {
            tinyimx::MySqlConnection c;Begin(c,config.MySql());std::vector<tinyimx::MySqlTransactionStatementResult> out;
            Require(!c.ExecuteTransactionStatementsBoundedRows({{"SELECT 1",true},{"SELECT codex_owned_missing_field",true}},&out,64)&&!c.IsConnected(),"later server error closes session after earlier result");
        }
        {
            tinyimx::MySqlConnection c;Begin(c,config.MySql());std::vector<tinyimx::MySqlTransactionStatementResult> out;
            const std::string large="SELECT REPEAT('x',4194305)";
            Require(!c.ExecuteTransactionStatementsBoundedRows({{large,true},{large,true}},&out,64)&&!c.IsConnected(),"aggregate8MiB overflow remains enforced across results");
        }
        {
            tinyimx::MySqlConnection c;Begin(c,config.MySql());std::vector<tinyimx::MySqlTransactionStatementResult> out;
            const auto before=c.GroupedStatementCommandAttempts();
            const std::string sql="SELECT '"+std::string(1024*1024,'x')+"'";
            Require(!c.ExecuteTransactionStatementsBoundedRows({{sql,true}},&out,64)&&c.IsConnected()&&c.GroupedStatementCommandAttempts()==before,"oversize statement rejected before send");Require(c.Rollback(),"oversize transaction returned");
        }
        std::cout<<"BOUNDED_MULTIROW_REAL_PROTOCOL_ALL_PASS\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
