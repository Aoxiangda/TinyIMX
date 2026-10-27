// Inserted within ROCKETMQ_NAMESPACE in SimpleConsumerImpl.cpp.
namespace {
std::error_code TinyIMXGrpcAckCode(grpc::StatusCode code) {
  switch(code){
    case grpc::StatusCode::DEADLINE_EXCEEDED:return ErrorCode::RequestTimeout;
    case grpc::StatusCode::UNAUTHENTICATED:return ErrorCode::Unauthorized;
    case grpc::StatusCode::PERMISSION_DENIED:return ErrorCode::Forbidden;
    case grpc::StatusCode::RESOURCE_EXHAUSTED:return ErrorCode::TooManyRequests;
    case grpc::StatusCode::CANCELLED:return std::make_error_code(std::errc::operation_canceled);
    default:return std::make_error_code(std::errc::io_error);
  }
}
std::error_code TinyIMXAckCode(rmq::Code code) {
  switch(code){
    case rmq::Code::OK:return {};
    case rmq::Code::ILLEGAL_TOPIC:return ErrorCode::IllegalTopic;
    case rmq::Code::ILLEGAL_CONSUMER_GROUP:return ErrorCode::IllegalConsumerGroup;
    case rmq::Code::INVALID_RECEIPT_HANDLE:return ErrorCode::InvalidReceiptHandle;
    case rmq::Code::CLIENT_ID_REQUIRED:return ErrorCode::InternalClientError;
    case rmq::Code::TOPIC_NOT_FOUND:return ErrorCode::TopicNotFound;
    case rmq::Code::UNAUTHORIZED:return ErrorCode::Unauthorized;
    case rmq::Code::FORBIDDEN:return ErrorCode::Forbidden;
    case rmq::Code::TOO_MANY_REQUESTS:return ErrorCode::TooManyRequests;
    case rmq::Code::INTERNAL_SERVER_ERROR:return ErrorCode::InternalServerError;
    case rmq::Code::PROXY_TIMEOUT:return ErrorCode::GatewayTimeout;
    default:return ErrorCode::NotSupported;
  }
}
struct TinyIMXAckGroup {
  std::string target;
  AckMessageRequest request;
  std::vector<std::size_t> positions;
  std::set<std::string> ids;
};
struct TinyIMXAckState {
  std::mutex mutex;
  std::vector<std::error_code> values;
  std::vector<TinyIMXAckGroup> groups;
  std::vector<unsigned char> done;
  std::vector<MessageConstSharedPtr> handles;
  std::size_t remaining{0};
  std::function<void(const std::vector<std::error_code>&)> callback;
  void finish(std::size_t index,const std::vector<std::error_code>* codes=nullptr) {
    bool last=false;
    {
      std::lock_guard<std::mutex> lock(mutex);
      if(done[index])return;
      const auto& positions=groups[index].positions;
      if(codes&&codes->size()==positions.size())
        for(std::size_t i=0;i<positions.size();++i)values[positions[i]]=(*codes)[i];
      done[index]=1;last=--remaining==0;
    }
    if(last)callback(values);
  }
};
}
void SimpleConsumerImpl::ackBatchAsync(const std::vector<MessageConstSharedPtr>& messages,
    std::function<void(const std::vector<std::error_code>&)> callback) {
  tinyimx_mq_ack_metrics::Get().calls.fetch_add(1,std::memory_order_relaxed);
  if(messages.empty()){callback({});return;}
  if(messages.size()>256){callback(std::vector<std::error_code>(messages.size(),std::make_error_code(std::errc::invalid_argument)));return;}
  const auto deadline=std::chrono::system_clock::now()+absl::ToChronoMilliseconds(client_config_.request_timeout);
  auto state=std::make_shared<TinyIMXAckState>();
  state->values.assign(messages.size(),std::make_error_code(std::errc::invalid_argument));
  state->handles=messages;state->callback=std::move(callback);
  std::map<std::pair<std::string,std::string>,std::vector<std::size_t>> partitions;
  // Complete all allocation/grouping/validation before submitting any RPC.
  for(std::size_t i=0;i<messages.size();++i){
    const auto& message=messages[i];
    if(!message||!tinyimx_rocketmq_batch::BoundedText(message->topic(),128)||
       !tinyimx_rocketmq_batch::BoundedText(message->id(),256)||
       !tinyimx_rocketmq_batch::BoundedText(message->extension().receipt_handle,8192)||
       !tinyimx_rocketmq_batch::BoundedText(message->extension().target_endpoint,1024))continue;
    const auto key=std::make_pair(message->extension().target_endpoint,message->topic());
    auto& choices=partitions[key];std::size_t selected=state->groups.size();
    for(auto index:choices)if(!state->groups[index].ids.count(message->id())){selected=index;break;}
    if(selected==state->groups.size()){
      TinyIMXAckGroup group;group.target=key.first;
      group.request.mutable_group()->CopyFrom(client_config_.subscriber.group);
      group.request.mutable_topic()->set_resource_namespace(resourceNamespace());
      group.request.mutable_topic()->set_name(key.second);
      state->groups.push_back(std::move(group));choices.push_back(selected);
    }
    auto& group=state->groups[selected];group.ids.insert(message->id());group.positions.push_back(i);
    auto* entry=group.request.add_entries();entry->set_message_id(message->id());
    entry->set_receipt_handle(message->extension().receipt_handle);
    state->values[i]=std::make_error_code(std::errc::io_error);
  }
  state->remaining=state->groups.size();state->done.assign(state->remaining,0);
  if(!state->remaining){state->callback(state->values);return;}
  for(std::size_t index=0;index<state->groups.size();++index){
    try{
      const auto& group=state->groups[index];
      if(!tinyimx_rocketmq_batch::ValidateRequest(group.request)){state->finish(index);continue;}
      SPDLOG_DEBUG("Prepare to ack message against {} asynchronously. AckMessageRequest: {}",group.target,
        logging::MakeLazyLogValue([&group]{return group.request.DebugString();}));
      Metadata metadata;Signature::sign(client_config_,metadata);
      auto client=std::dynamic_pointer_cast<RpcClientImpl>(manager()->getRpcClient(group.target,true));
      if(!client){state->finish(index);continue;}
      auto context=std::make_shared<InvocationContext<AckMessageResponse>>();
      context->task_name="TinyIMX bounded batch ACK";context->remote_address=group.target;
      context->context.set_deadline(deadline);
      for(const auto& item:metadata)context->context.AddMetadata(item.first,item.second);
      context->callback=[state,index,client](const InvocationContext<AckMessageResponse>* completed){
        tinyimx_mq_ack_metrics::Get().terminals.fetch_add(1,std::memory_order_relaxed);
        try{
          std::vector<std::error_code> values(state->groups[index].positions.size(),std::make_error_code(std::errc::io_error));
          if(!completed->status.ok()){
            const auto error=TinyIMXGrpcAckCode(completed->status.error_code());
            std::fill(values.begin(),values.end(),error);
          }else{
            const auto decoded=tinyimx_rocketmq_batch::Decode(state->groups[index].request,completed->response);
            if(!decoded.valid){
              SPDLOG_WARN("Malformed batch ACK response: {}",decoded.reason);
              std::fill(values.begin(),values.end(),std::make_error_code(std::errc::protocol_error));
            }else{
              for(std::size_t i=0;i<values.size();++i)values[i]=TinyIMXAckCode(decoded.codes[i]);
              if(completed->response.status().code()==rmq::Code::OK)SPDLOG_DEBUG("Ack OK. host={}",completed->remote_address);
              else if(completed->response.status().code()==rmq::Code::MULTIPLE_RESULTS)SPDLOG_DEBUG("Server returns multiple results. host={}",completed->remote_address);
              else SPDLOG_WARN("Batch ACK failure status={}. host={}",static_cast<int>(completed->response.status().code()),completed->remote_address);
            }
          }
          state->finish(index,&values);
        }catch(...){state->finish(index);}
      };
      tinyimx_mq_ack_metrics::Get().submissions.fetch_add(1,std::memory_order_relaxed);
      tinyimx_mq_ack_metrics::Get().entries.fetch_add(group.positions.size(),std::memory_order_relaxed);
      client->asyncAckBatch(group.request,context);
    }catch(...){state->finish(index);}
  }
}
