// Owned callback-API RPC. Use the existing manager queue, not a detached thread.
void RpcClientImpl::asyncSendBatch(const SendMessageRequest& request,
    std::shared_ptr<InvocationContext<SendMessageResponse>> owned) {
  auto keep=shared_from_this();
  auto terminal=std::make_shared<std::atomic<bool>>(false);
  auto finish=[owned,keep,terminal](grpc::Status status){
    if(terminal->exchange(true,std::memory_order_acq_rel))return;
    owned->status=std::move(status);
    try{owned->callback(owned.get());}
    catch(const std::exception& error){SPDLOG_WARN("Batch send callback failed: {}",error.what());}
    catch(...){SPDLOG_WARN("Batch send callback failed");}
  };
  auto dispatch=[keep,finish](grpc::Status status){
    auto manager=keep->clientManager().lock();
    if(!manager||manager->state()!=State::STARTED){
      finish(grpc::Status(grpc::StatusCode::CANCELLED,"Send manager stopped"));return;
    }
    try{manager->submit([finish,status]{finish(status);});}
    catch(...){finish(grpc::Status(grpc::StatusCode::INTERNAL,"Send dispatch failed"));}
  };
  try{stub_->async()->SendMessage(&owned->context,&request,&owned->response,dispatch);}
  catch(...){owned->context.TryCancel();dispatch(grpc::Status(grpc::StatusCode::INTERNAL,"Send submit failed"));}
}
