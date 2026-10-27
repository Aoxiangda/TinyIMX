// Inserted inside ROCKETMQ_NAMESPACE in Producer.cpp; class layout is unchanged.
void Producer::sendBatch(const std::vector<MessageConstSharedPtr>& messages,
                         std::vector<std::error_code>& errors,
                         std::vector<std::string>& ids) noexcept {
  try {
    if(messages.empty()){errors.clear();ids.clear();return;}
    if(messages.size()>32||!impl_){
      errors.assign(messages.size(),std::make_error_code(std::errc::invalid_argument));
      ids.assign(messages.size(),std::string{});return;
    }
    impl_->sendBatch(messages,errors,ids);
  }catch(...){
    // Allocation or submission failure must never become publication progress.
    try{
      errors.assign(messages.size(),std::make_error_code(std::errc::io_error));
      ids.assign(messages.size(),std::string{});
    }catch(...){errors.clear();ids.clear();}
  }
}
namespace {
int TinyIMXInvokeSendBatch(Producer* producer,
    const std::vector<MessageConstSharedPtr>* messages,
    std::vector<std::error_code>* errors,std::vector<std::string>* ids) noexcept {
  if(!producer||!messages||!errors||!ids)return 1;
  producer->sendBatch(*messages,*errors,*ids);
  return errors->size()==messages->size()&&ids->size()==messages->size()?0:2;
}
}
extern "C" const TinyIMXBatchSendApiV1* tinyimx_rocketmq_batch_send_api_v1() noexcept {
#ifdef _GLIBCXX_USE_CXX11_ABI
  const std::uint32_t abi=_GLIBCXX_USE_CXX11_ABI;
#else
  const std::uint32_t abi=0xffffffffu;
#endif
  static const TinyIMXBatchSendApiV1 api={kTinyIMXBatchSendMagic,1,
    sizeof(TinyIMXBatchSendApiV1),abi,32,256*1024,sizeof(Producer),
    sizeof(MessageConstSharedPtr),TinyIMXInvokeSendBatch,tinyimx_mq_send_metrics::Read};
  return &api;
}
