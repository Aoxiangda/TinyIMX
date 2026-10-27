// Included inside the fresh-schema probe after the original receiver checks.
if (enabled) {
    insert(20,11,92,1,0);
    insert(300,11,92,1,0);
    insert(400,11,92,1,0);
    for (unsigned id=200;id<250;++id) insert(id,11,92,1,0);
    std::atomic<unsigned> commits_seen{0}, replies_returned{0};
    std::atomic<bool> commit_held{false}, release_commit{false};
    adapter.SetReceiverConfirmationBeforeCommitHookForTest([&](tinyimx::MySqlConnection*){
        if (++commits_seen != 2) return;
        commit_held=true;
        auto limit=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(!release_commit&&std::chrono::steady_clock::now()<limit)std::this_thread::yield();
        Require(release_commit,"test commit gate timeout");
    });
    auto blocker=pool.Acquire();
    Require(blocker&&blocker->BeginTransaction()&&blocker->Execute(
        "UPDATE im_private_messages SET content='held' WHERE message_id=20"),"blocker setup");
    MessageMutationApplicationResult leader_result;
    std::jthread leader([&]{leader_result=app.ConfirmReceiver(20,92);});
    auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(pool.AvailableCount()!=6&&std::chrono::steady_clock::now()<until)std::this_thread::yield();
    Require(pool.AvailableCount()==6,"leader entered locked SQL");
    std::vector<std::pair<unsigned,unsigned>> requests;
    for(unsigned id=200;id<250;++id)requests.emplace_back(id,92);
    for(unsigned i=0;i<7;++i)requests.emplace_back(300,92);
    for(auto pair:std::vector<std::pair<unsigned,unsigned>>{{1,93},{3,92},{4,92},{9999,92},{7,92},{3,93},{4,93}})requests.push_back(pair);
    Require(requests.size()==64,"batch input size");
    std::vector<MessageMutationApplicationResult> replies(64);
    std::vector<std::jthread> waiters;
    for(std::size_t i=0;i<64;++i)waiters.emplace_back([&,i]{replies[i]=app.ConfirmReceiver(requests[i].first,requests[i].second);++replies_returned;});
    until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(adapter.ReceiverConfirmationQueuedForTest()!=64&&std::chrono::steady_clock::now()<until)std::this_thread::yield();
    // Always release blockers before asserting, so a failed check cannot hang join.
    bool queued=adapter.ReceiverConfirmationQueuedForTest()==64;
    MessageMutationApplicationResult overflow;
    if (queued) overflow=app.ConfirmReceiver(400,92);
    Require(blocker->Rollback(),"release blocker");blocker.Reset();leader.join();
    until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!commit_held&&std::chrono::steady_clock::now()<until)std::this_thread::yield();
    bool no_early_reply=commit_held&&replies_returned==0&&Scalar(pool,
        "SELECT COUNT(*) FROM im_private_messages WHERE (message_id BETWEEN 200 AND 249 OR message_id=300) AND delivery_status=0")=="51";
    release_commit=true;
    for(auto& t:waiters)t.join();
    adapter.SetReceiverConfirmationBeforeCommitHookForTest({});
    check(no_early_reply,"all_batch_results_wait_for_commit_and_pending_rows_not_visible_as_confirmed");
    check(queued,"already_arrived_64_requests_coalesced");
    check(overflow.status==MessageApplicationStatus::kStorageError&&Scalar(pool,
        "SELECT delivery_status FROM im_private_messages WHERE message_id=400")=="0","bounded_overflow_rejected_without_mutation");
    unsigned transitions=0;bool valid=leader_result.Succeeded();
    for(unsigned i=0;i<57;++i){valid=valid&&replies[i].Succeeded();transitions+=replies[i].affected_rows;}
    check(valid&&transitions==51&&Scalar(pool,
        "SELECT COUNT(*) FROM im_private_messages WHERE (message_id BETWEEN 200 AND 249 OR message_id=300) AND delivery_status=1 AND delivered_at IS NOT NULL")=="51","batch_51_unique_rows_commit_once_duplicates_idempotent");
    check(replies[57].status==MessageApplicationStatus::kPermissionDenied&&
          replies[58].Succeeded()&&replies[58].affected_rows==0&&
          replies[59].status==MessageApplicationStatus::kFailedPrecondition&&
          replies[60].status==MessageApplicationStatus::kNotFound&&
          replies[61].status==MessageApplicationStatus::kInvalidRecord&&
          replies[62].status==MessageApplicationStatus::kPermissionDenied&&
          replies[63].status==MessageApplicationStatus::kPermissionDenied,
          "mixed_batch_retains_per_request_owner_state_and_record_decisions");
    check(Scalar(pool,"SELECT delivery_status FROM im_private_messages WHERE message_id=3")=="2"&&
          Scalar(pool,"SELECT delivery_status FROM im_private_messages WHERE message_id=4")=="3",
          "mixed_batch_never_regresses_read_or_failed_state");
    insert(500,11,92,1,0);
    unsigned hooks=0;
    adapter.SetReceiverConfirmationBeforeCommitHookForTest([&](tinyimx::MySqlConnection* c){
        ++hooks;tinyimx::MySqlQueryResult cid;
        Require(c->Query("SELECT CONNECTION_ID()",&cid)&&cid.rows.size()==1,"owned connection id");
        Execute(pool,"KILL "+cid.rows[0][0]);
    });
    auto killed=app.ConfirmReceiver(500,92);
    adapter.SetReceiverConfirmationBeforeCommitHookForTest({});
    check(hooks==1&&killed.status==MessageApplicationStatus::kStorageError&&killed.affected_rows==0&&
          Scalar(pool,"SELECT delivery_status FROM im_private_messages WHERE message_id=500")=="0",
          "real_connection_loss_before_commit_fails_without_replay_or_early_ack");
    check(app.ConfirmReceiver(500,92).Succeeded(),"caller_retry_after_failed_transaction_recovers");
    result["fixture_rows"]=66;
}
