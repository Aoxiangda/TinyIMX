  // A completion may be dispatched after the synchronous deadline. Own every
  // callback-visible value independently of this stack frame and the caller's
  // error_code. Completion and timeout choose one result under the same lock.
  struct PendingSend {
    std::mutex mutex;
    std::condition_variable ready;
    bool completed{false};
    bool abandoned{false};
    std::error_code error;
    SendReceipt receipt;
  };
  auto pending = std::make_shared<PendingSend>();
  const std::string topic = message->topic();
  auto callback = [pending](const std::error_code& code, SendReceipt&& receipt) mutable {
    std::lock_guard<std::mutex> lock(pending->mutex);
    if (pending->completed || pending->abandoned) {
      return;
    }
    pending->error = code;
    pending->receipt = std::move(receipt);
    pending->completed = true;
    pending->ready.notify_one();
  };
  send0(std::move(message), callback, message_queue_list);
  std::unique_lock<std::mutex> lock(pending->mutex);
  const auto deadline = std::chrono::steady_clock::now() +
      std::chrono::nanoseconds(absl::ToInt64Nanoseconds(requestTimeout()));
  if (!pending->ready.wait_until(lock, deadline, [pending] { return pending->completed; })) {
    pending->abandoned = true;
    ec = ErrorCode::RequestTimeout;
    lock.unlock();
    SPDLOG_WARN("Timeout waiting for send result of topic[{}]", topic);
    return {};
  }
  ec = pending->error;
  return std::move(pending->receipt);
}
