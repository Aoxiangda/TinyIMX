// Inserted within ROCKETMQ_NAMESPACE in RpcClientImpl.cpp. Keep the normal
// manager callback queue, but use owned context and one terminal delivery.
void RpcClientImpl::asyncAckBatch(const AckMessageRequest& request,
                                 std::shared_ptr<InvocationContext<AckMessageResponse>> owned) {
  auto keep=shared_from_this();
  auto terminal=std::make_shared<std::atomic<bool>>(false);
  auto finish=[owned,keep,terminal](grpc::Status status) {
    if(terminal->exchange(true,std::memory_order_acq_rel))return;
    owned->status=std::move(status);
    const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now()-owned->start_time).count();
    SPDLOG_DEBUG("RPC[{}] costs {}ms",owned->task_name,elapsed);
    if(!owned->status.ok())SPDLOG_WARN("One async call is already dead");
    // Do not use onCompletion(): the legacy implementation deletes this,
    // whereas this path is deliberately shared-owned through the gRPC tag.
    try{owned->callback(owned.get());}
    catch(const std::exception& error){SPDLOG_WARN("Unexpected error while invoking batch ACK callback: {}",error.what());}
    catch(...){SPDLOG_WARN("Unexpected error while invoking batch ACK callback");}
  };
  auto dispatch=[keep,finish](grpc::Status status) {
    auto manager=keep->clientManager().lock();
    if(!manager||manager->state()!=State::STARTED){finish(grpc::Status(grpc::StatusCode::CANCELLED,"ACK manager gone"));return;}
    try{manager->submit([finish,status]{finish(status);});}
    catch(...){finish(grpc::Status(grpc::StatusCode::INTERNAL,"ACK dispatch failed"));}
  };
  try{stub_->async()->AckMessage(&owned->context,&request,&owned->response,dispatch);}
  catch(...){owned->context.TryCancel();dispatch(grpc::Status(grpc::StatusCode::INTERNAL,"ACK submit failed"));}
}
