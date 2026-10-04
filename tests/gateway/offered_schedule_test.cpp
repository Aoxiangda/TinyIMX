#include "benchmark/local_capacity/OfferedSchedule.h"
#include <chrono>
#include <iostream>
#include <limits>
#include <stdexcept>
using tinyimx::capacity::OfferedRequestCount;
#define REQUIRE(x) do {if(!(x)) throw std::runtime_error(#x);} while(false)
int main(){
    int pass=0,fail=0;auto test=[&](const char* name,auto fn){try{fn();++pass;std::cout<<"[PASS] "<<name<<'\n';}catch(const std::exception& e){++fail;std::cerr<<"[FAIL] "<<name<<": "<<e.what()<<'\n';}};
    test("three_worker_exact_offered_budget",[]{REQUIRE(OfferedRequestCount(100.0/3,60)*3==6000);});
    test("truncated_terminal_tick_cannot_add_a_slot",[]{
        const double rate=100.0/3;const auto budget=OfferedRequestCount(rate,60);
        const auto old_terminal=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::duration<double>(2000/rate));
        REQUIRE(old_terminal<std::chrono::seconds(60));REQUIRE(budget==2000);
        const auto last=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::duration<double>((budget-1)/rate));
        REQUIRE(last<std::chrono::seconds(60));REQUIRE(2000>=budget);
    });
    test("one_two_five_worker_baselines_preserved",[]{for(unsigned n:{1u,2u,5u})REQUIRE(OfferedRequestCount(100.0/n,60)*n==6000);});
    test("real_fractional_rate_ceil_kept",[]{REQUIRE(OfferedRequestCount(2.25,3)==7);REQUIRE(OfferedRequestCount(100.01,60)==6001);});
    test("tiny_positive_rate_has_first_slot",[]{REQUIRE(OfferedRequestCount(1e-12,1)==1);});
    test("positive_nearinteger_product_snapped",[]{REQUIRE(OfferedRequestCount((2000.0+1e-10)/60,60)==2000);REQUIRE(OfferedRequestCount((2000.0+1e-6)/60,60)==2001);});
    test("bounded_large_schedule",[]{REQUIRE(OfferedRequestCount(10000,7200)==72000000);});
    test("invalid_nonfinite_and_zero_rejected",[]{
        for(double rate:{0.0,-1.0,10001.0,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}){
            bool rejected=false;try{(void)OfferedRequestCount(rate,60);}catch(const std::invalid_argument&){rejected=true;}REQUIRE(rejected);
        }
        for(unsigned seconds:{0u,7201u}){bool rejected=false;try{(void)OfferedRequestCount(100,seconds);}catch(const std::invalid_argument&){rejected=true;}REQUIRE(rejected);}
    });
    std::cout<<"passed="<<pass<<" failed="<<fail<<'\n';return fail?1:0;
}
