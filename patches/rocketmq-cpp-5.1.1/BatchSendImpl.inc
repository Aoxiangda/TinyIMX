// Bounded normal-message batches only. FIFO, scheduled, priority and transaction
// messages retain their original transport. Retries remain an application concern.
void ProducerImpl::sendBatch(const std::vector<MessageConstSharedPtr>& messages,
                             std::vector<std::error_code>& errors,
                             std::vector<std::string>& ids) {
  using Policy=tinyimx::eventing::rocketmq_transport::BatchSendReceiptPolicy;
  auto& metrics=tinyimx_mq_send_metrics::Get();metrics.calls.fetch_add(1,std::memory_order_relaxed);
  const auto count=messages.size();
  errors.assign(count,std::make_error_code(std::errc::io_error));ids.assign(count,std::string{});
  if(count==0)return;
  if(count>Policy::max_entries){std::fill(errors.begin(),errors.end(),std::make_error_code(std::errc::invalid_argument));return;}
  std::error_code running;ensureRunning(running);
  if(running){std::fill(errors.begin(),errors.end(),running);return;}
  const auto duration=absl::ToChronoNanoseconds(requestTimeout());
  const auto steady_deadline=std::chrono::steady_clock::now()+duration;
  const auto rpc_deadline=std::chrono::system_clock::now()+duration;
  std::vector<std::string> expected;expected.reserve(count);
  std::string topic,tag;std::size_t body_bytes=0,estimated=count*1024;
  const auto add=[&estimated](std::size_t n){
    if(n>512*1024-estimated)return false;estimated+=n;return true;
  };
  for(const auto& message:messages){
    if(!message){std::fill(errors.begin(),errors.end(),std::make_error_code(std::errc::invalid_argument));return;}
    std::error_code error;validate(*message,error);
    if(error){std::fill(errors.begin(),errors.end(),error);return;}
    if(expected.empty()){topic=message->topic();tag=message->tag();}
    bool valid=message->topic()==topic&&message->tag()==tag&&message->group().empty()&&
      !message->deliveryTimestamp().time_since_epoch().count()&&message->priority()<0&&
      !message->extension().transactional&&!message->id().empty()&&
      message->id().size()<=Policy::max_message_id_bytes&&message->id().find(',')==std::string::npos;
    if(message->body().size()>256*1024-body_bytes)valid=false;
    if(!valid){std::fill(errors.begin(),errors.end(),std::make_error_code(std::errc::invalid_argument));return;}
    body_bytes+=message->body().size();
    valid=add(message->body().size())&&add(message->topic().size())&&add(message->tag().size())&&add(message->traceContext().size());
    for(const auto& key:message->keys())valid=valid&&add(key.size());
    for(const auto& property:message->properties())valid=valid&&add(property.first.size())&&add(property.second.size());
    if(!valid){std::fill(errors.begin(),errors.end(),std::make_error_code(std::errc::message_size));return;}
    expected.push_back(message->id());
  }
  if(!Policy::Map(expected,true,Policy::Aggregate::kOk,{}).request_valid){
    std::fill(errors.begin(),errors.end(),std::make_error_code(std::errc::invalid_argument));return;
  }
  auto publish_info=getPublishInfo(topic);std::vector<rmq::MessageQueue> queues;
  absl::optional<std::string> group{};
  if(!publish_info||!publish_info->selectMessageQueues(group,queues)||queues.empty()){
    std::fill(errors.begin(),errors.end(),ErrorCode::NotFound);return;
  }
  const std::string target=urlOf(queues.front());
  if(target.empty()){std::fill(errors.begin(),errors.end(),ErrorCode::BadGateway);return;}
  auto client=std::dynamic_pointer_cast<RpcClientImpl>(manager()->getRpcClient(target,true));
  if(!client){std::fill(errors.begin(),errors.end(),ErrorCode::BadGateway);return;}
  struct Pending {
    std::mutex mutex;std::condition_variable ready;
    bool done{false},abandoned{false};
    SendMessageRequest request;
    std::vector<MessageConstSharedPtr> messages;
    std::vector<std::string> expected,confirmed_ids;
    std::vector<std::error_code> errors;
  };
  auto pending=std::make_shared<Pending>();pending->messages=messages;pending->expected=expected;
  pending->errors=errors;pending->confirmed_ids=ids;
  for(const auto& message:messages)wrapSendMessageRequest(*message,pending->request,queues.front());
  if(pending->request.ByteSizeLong()>512*1024){
    std::fill(errors.begin(),errors.end(),std::make_error_code(std::errc::message_size));return;
  }
  if(std::chrono::steady_clock::now()>=steady_deadline){
    std::fill(errors.begin(),errors.end(),ErrorCode::RequestTimeout);return;
  }
  Metadata metadata;Signature::sign(client_config_,metadata);
  auto context=std::make_shared<InvocationContext<SendMessageResponse>>();
  context->task_name="TinyIMX bounded normal batch send";context->remote_address=target;
  context->context.set_deadline(rpc_deadline);
  for(const auto& item:metadata)context->context.AddMetadata(item.first,item.second);
  context->callback=[pending,client](const InvocationContext<SendMessageResponse>* completed){
    tinyimx_mq_send_metrics::Get().terminals.fetch_add(1,std::memory_order_relaxed);
    std::vector<std::error_code> values(pending->expected.size(),std::make_error_code(std::errc::io_error));
    std::vector<std::string> confirmed(pending->expected.size());
    try{
      std::vector<Policy::Entry> receipts;
      if(completed->response.entries_size()<=static_cast<int>(Policy::max_entries)){
        receipts.reserve(completed->response.entries_size());
        for(const auto& entry:completed->response.entries())receipts.push_back({entry.message_id(),entry.status().code()==rmq::Code::OK});
        auto aggregate=Policy::Aggregate::kOther;
        if(completed->response.status().code()==rmq::Code::OK)aggregate=Policy::Aggregate::kOk;
        else if(completed->response.status().code()==rmq::Code::MULTIPLE_RESULTS)aggregate=Policy::Aggregate::kMultipleResults;
        const auto mapped=Policy::MapPinnedNormalBatch(pending->expected,completed->status.ok(),aggregate,receipts);
        if(mapped.reason==Policy::Reason::kMalformedReceipts)
          tinyimx_mq_send_metrics::Get().malformed.fetch_add(1,std::memory_order_relaxed);
        for(std::size_t i=0;i<values.size();++i){
          if(mapped.entries[i]==Policy::Decision::kConfirmed){values[i].clear();confirmed[i]=pending->expected[i];}
          else if(completed->status.error_code()==grpc::StatusCode::DEADLINE_EXCEEDED)values[i]=ErrorCode::RequestTimeout;
        }
      }else tinyimx_mq_send_metrics::Get().malformed.fetch_add(1,std::memory_order_relaxed);
    }catch(...){/* All values remain retryable; no publication marker is advanced. */}
    {
      std::lock_guard<std::mutex> lock(pending->mutex);
      if(pending->done||pending->abandoned)return;
      pending->errors=std::move(values);pending->confirmed_ids=std::move(confirmed);pending->done=true;
    }
    pending->ready.notify_one();
  };
  metrics.submissions.fetch_add(1,std::memory_order_relaxed);metrics.entries.fetch_add(count,std::memory_order_relaxed);
  client->asyncSendBatch(pending->request,context);
  std::unique_lock<std::mutex> lock(pending->mutex);
  if(!pending->ready.wait_until(lock,steady_deadline,[pending]{return pending->done;})){
    pending->abandoned=true;metrics.timeouts.fetch_add(1,std::memory_order_relaxed);
    std::fill(errors.begin(),errors.end(),ErrorCode::RequestTimeout);return;
  }
  errors=std::move(pending->errors);ids=std::move(pending->confirmed_ids);
}
