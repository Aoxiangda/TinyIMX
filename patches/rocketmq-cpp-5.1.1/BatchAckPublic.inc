// Inserted within ROCKETMQ_NAMESPACE in SimpleConsumer.cpp.
void SimpleConsumer::ackBatch(const std::vector<MessageConstSharedPtr>& messages,
                              std::vector<std::error_code>& errors) noexcept {
  struct Sync {
    explicit Sync(std::size_t n):values(n,std::make_error_code(std::errc::io_error)){}
    std::mutex mutex;
    std::condition_variable ready;
    std::vector<std::error_code> values;
    bool done{false};
  };
  try {
    if(messages.size()>256){errors.assign(messages.size(),std::make_error_code(std::errc::invalid_argument));return;}
    if(messages.empty()){errors.clear();return;}
    if(!impl_){errors.assign(messages.size(),std::make_error_code(std::errc::not_connected));return;}
    auto state=std::make_shared<Sync>(messages.size());
    auto complete=[state](const std::vector<std::error_code>& values) {
      {
        std::lock_guard<std::mutex> lock(state->mutex);
        if(state->done)return;
        if(values.size()==state->values.size())std::copy(values.begin(),values.end(),state->values.begin());
        state->done=true;
      }
      state->ready.notify_one();
    };
    impl_->ackBatchAsync(messages,complete);
    std::unique_lock<std::mutex> lock(state->mutex);
    state->ready.wait(lock,[&]{return state->done;});
    errors=state->values;
  }catch(...){
    // Never turn allocation/configuration/submission failure into ACK success.
    // All callback state is heap-owned, not references into this stack frame.
    try{errors.assign(messages.size(),std::make_error_code(std::errc::io_error));}catch(...){errors.clear();}
  }
}
namespace {
int TinyIMXInvokeBatch(SimpleConsumer* consumer,const std::vector<MessageConstSharedPtr>* messages,
                       std::vector<std::error_code>* errors) noexcept {
  if(!consumer||!messages||!errors)return 1;
  consumer->ackBatch(*messages,*errors);
  return errors->size()==messages->size()?0:2;
}
}
extern "C" const TinyIMXBatchAckApiV1* tinyimx_rocketmq_batch_ack_api_v1() noexcept {
#ifdef _GLIBCXX_USE_CXX11_ABI
  const std::uint32_t abi=_GLIBCXX_USE_CXX11_ABI;
#else
  const std::uint32_t abi=0xffffffffu;
#endif
  static const TinyIMXBatchAckApiV1 api={kTinyIMXBatchAckMagic,1,sizeof(TinyIMXBatchAckApiV1),
    abi,256,sizeof(SimpleConsumer),sizeof(MessageConstSharedPtr),TinyIMXInvokeBatch,tinyimx_mq_ack_metrics::Read};
  return &api;
}
