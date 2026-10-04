#include "gateway/PrivateReceiverAck.h"
#include <iostream>
#include <stdexcept>

using namespace tinyimx;
#define REQUIRE(x) do { if(!(x)) throw std::runtime_error(#x); } while(false)
int main() {
    int passed=0,failed=0;
    auto test=[&](const char* name,auto fn){try{fn();++passed;std::cout<<"[PASS] "<<name<<'\n';}catch(const std::exception& e){++failed;std::cerr<<"[FAIL] "<<name<<": "<<e.what()<<'\n';}};
    test("valid_private_attempt_calls_one_confirmation",[]{
        ReceiverDeliveryTracker t;REQUIRE(t.RegisterAttempt(100,42,9)==ReceiverDeliveryRegisterStatus::kRegistered);
        int calls=0;auto r=ProcessPrivateReceiverAck(t,100,42,9,[&]{++calls;return true;});
        REQUIRE(r.status==ReceiverDeliveryAckStatus::kConfirmed && r.durable_confirmed && calls==1);
        ReceiverDeliverySnapshot s;REQUIRE(t.GetSnapshot(100,&s)&&s.confirmed);
    });
    test("foreign_receiver_cannot_call_storage_or_advance",[]{
        ReceiverDeliveryTracker t;t.RegisterAttempt(100,42,9);int calls=0;
        auto r=ProcessPrivateReceiverAck(t,100,43,9,[&]{++calls;return true;});
        REQUIRE(r.status==ReceiverDeliveryAckStatus::kReceiverMismatch && calls==0 && !r.durable_confirmed);
        ReceiverDeliverySnapshot s;REQUIRE(t.GetSnapshot(100,&s)&&!s.confirmed);
    });
    test("unregistered_sequence_cannot_advance",[]{
        ReceiverDeliveryTracker t;t.RegisterAttempt(100,42,9);int calls=0;
        auto r=ProcessPrivateReceiverAck(t,100,42,10,[&]{++calls;return true;});
        REQUIRE(r.status==ReceiverDeliveryAckStatus::kUnknownAttempt && calls==0);
        ReceiverDeliverySnapshot s;REQUIRE(t.GetSnapshot(100,&s)&&!s.confirmed);
    });
    test("unknown_message_no_storage_call",[]{
        ReceiverDeliveryTracker t;int calls=0;auto r=ProcessPrivateReceiverAck(t,100,42,9,[&]{++calls;return true;});
        REQUIRE(r.status==ReceiverDeliveryAckStatus::kUnknownMessage && calls==0);
    });
    test("zero_fields_rejected_without_callback",[]{
        ReceiverDeliveryTracker t;int calls=0;auto confirm=[&]{++calls;return true;};
        REQUIRE(ProcessPrivateReceiverAck(t,0,42,9,confirm).status==ReceiverDeliveryAckStatus::kInvalidArgument);
        REQUIRE(ProcessPrivateReceiverAck(t,100,0,9,confirm).status==ReceiverDeliveryAckStatus::kInvalidArgument);
        REQUIRE(ProcessPrivateReceiverAck(t,100,42,0,confirm).status==ReceiverDeliveryAckStatus::kInvalidArgument);
        REQUIRE(calls==0);
    });
    test("duplicate_valid_ack_repairs_uncertain_confirmation",[]{
        ReceiverDeliveryTracker t;t.RegisterAttempt(100,42,9);int calls=0;
        auto first=ProcessPrivateReceiverAck(t,100,42,9,[&]{++calls;return false;});
        REQUIRE(first.status==ReceiverDeliveryAckStatus::kConfirmed&&!first.durable_confirmed);
        ReceiverDeliverySnapshot s;REQUIRE(t.GetSnapshot(100,&s)&&s.confirmed);
        auto second=ProcessPrivateReceiverAck(t,100,42,9,[&]{++calls;return true;});
        REQUIRE(second.status==ReceiverDeliveryAckStatus::kDuplicate&&second.durable_confirmed&&calls==2);
    });
    test("duplicate_with_unknown_sequence_does_not_repair",[]{
        ReceiverDeliveryTracker t;t.RegisterAttempt(100,42,9);
        ProcessPrivateReceiverAck(t,100,42,9,[]{return true;});int calls=0;
        auto r=ProcessPrivateReceiverAck(t,100,42,10,[&]{++calls;return true;});
        REQUIRE(r.status==ReceiverDeliveryAckStatus::kUnknownAttempt&&calls==0);
    });
    test("older_registered_retry_sequence_is_valid_evidence",[]{
        ReceiverDeliveryTracker t;t.RegisterAttempt(100,42,9);
        REQUIRE(t.RegisterRetryAttempt(100,42,9,10,4)==ReceiverDeliveryRetryRegisterStatus::kRetryRegistered);
        int calls=0;auto r=ProcessPrivateReceiverAck(t,100,42,9,[&]{++calls;return true;});
        REQUIRE(r.status==ReceiverDeliveryAckStatus::kConfirmed&&r.durable_confirmed&&calls==1);
    });
    test("group_entry_cannot_confirm_private_message",[]{
        ReceiverDeliveryTracker t;t.RegisterAttempt(GroupDeliveryIdentity(100,42),9);int calls=0;
        auto r=ProcessPrivateReceiverAck(t,100,42,9,[&]{++calls;return true;});
        REQUIRE(r.status==ReceiverDeliveryAckStatus::kUnknownMessage&&calls==0);
        ReceiverDeliverySnapshot s;REQUIRE(t.GetSnapshot(GroupDeliveryIdentity(100,42),&s)&&!s.confirmed);
    });
    test("trimmed_confirmation_requires_known_attempt",[]{
        ReceiverDeliveryTracker t(1);t.RegisterAttempt(100,42,9);t.RegisterAttempt(101,43,10);
        ProcessPrivateReceiverAck(t,100,42,9,[]{return true;});ProcessPrivateReceiverAck(t,101,43,10,[]{return true;});
        int calls=0;auto r=ProcessPrivateReceiverAck(t,100,42,9,[&]{++calls;return true;});
        REQUIRE(r.status==ReceiverDeliveryAckStatus::kUnknownMessage&&calls==0);
    });
    std::cout<<"passed="<<passed<<" failed="<<failed<<'\n';return failed?1:0;
}
