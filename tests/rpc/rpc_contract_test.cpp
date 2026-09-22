#include <cstdint>
#include <iostream>
#include <string>
#include <type_traits>

#include "tinyimx/common/v1/common.pb.h"
#include "tinyimx/social/v1/social_service.grpc.pb.h"
#include "tinyimx/social/v1/social_service.pb.h"
#include "tinyimx/user/v1/user_service.grpc.pb.h"
#include "tinyimx/user/v1/user_service.pb.h"
#include "tinyimx/message/v1/message_service.grpc.pb.h"
#include "tinyimx/message/v1/message_service.pb.h"
#include "tinyimx/group/v1/group_service.grpc.pb.h"
#include "tinyimx/group/v1/group_service.pb.h"
#include "tinyimx/file/v1/file_service.grpc.pb.h"
#include "tinyimx/file/v1/file_service.pb.h"

namespace {

int g_failed = 0;

void Expect(bool condition, const char* name) {
  if (condition) {
    std::cout << "[PASS] " << name << '\n';
    return;
  }

  ++g_failed;
  std::cerr << "[FAIL] " << name << '\n';
}

const google::protobuf::FieldDescriptor* FindField(
    const google::protobuf::Descriptor* descriptor,
    const char* name) {
  return descriptor == nullptr ? nullptr : descriptor->FindFieldByName(name);
}

}  // namespace

int main() {
  using tinyimx::common::v1::RequestMeta;
  using tinyimx::social::v1::FriendInfo;
  using tinyimx::social::v1::ListFriendsRequest;
  using tinyimx::social::v1::ListFriendsResponse;
  using tinyimx::social::v1::SocialService;

  static_assert(std::is_class_v<SocialService>);
  static_assert(std::is_class_v<SocialService::StubInterface>);

  RequestMeta meta;
  meta.set_request_id("rpc-request-1");
  meta.set_trace_id("trace-1");
  meta.set_caller_service("gateway");
  meta.set_caller_instance("gateway-a");

  ListFriendsRequest request;
  *request.mutable_meta() = meta;
  request.set_actor_user_id(10001);
  request.set_limit(100);

  Expect(request.meta().request_id() == "rpc-request-1",
         "RequestMeta.request_id round-trip in request");
  Expect(request.meta().trace_id() == "trace-1",
         "RequestMeta.trace_id round-trip in request");
  Expect(request.actor_user_id() == 10001,
         "ListFriendsRequest.actor_user_id typed field");
  Expect(request.limit() == 100,
         "ListFriendsRequest.limit typed field");

  std::string wire;
  Expect(request.SerializeToString(&wire),
         "ListFriendsRequest serializes");

  ListFriendsRequest parsed;
  Expect(parsed.ParseFromString(wire),
         "ListFriendsRequest parses");
  Expect(parsed.actor_user_id() == request.actor_user_id(),
         "ListFriendsRequest actor survives protobuf round-trip");
  Expect(parsed.meta().trace_id() == request.meta().trace_id(),
         "RequestMeta trace survives protobuf round-trip");

  ListFriendsResponse response;
  FriendInfo* friend_info = response.add_friends();
  friend_info->set_friend_user_id(10002);
  friend_info->set_username("user10002");
  friend_info->set_nickname("friend-10002");
  friend_info->set_avatar_url("https://example.invalid/avatar/10002");
  friend_info->set_user_status(1);
  friend_info->set_relation_status(1);
  friend_info->set_relation_created_at("2026-09-03 10:00:00");
  friend_info->set_relation_updated_at("2026-09-03 10:00:00");
  response.set_has_more(false);

  Expect(response.friends_size() == 1,
         "ListFriendsResponse repeated FriendInfo works");
  Expect(response.friends(0).friend_user_id() == 10002,
         "FriendInfo.friend_user_id typed field");
  Expect(!response.has_more(),
         "ListFriendsResponse.has_more typed field");

  const auto* request_descriptor = ListFriendsRequest::descriptor();
  const auto* meta_field = FindField(request_descriptor, "meta");
  const auto* actor_field = FindField(request_descriptor, "actor_user_id");
  const auto* limit_field = FindField(request_descriptor, "limit");

  Expect(meta_field != nullptr && meta_field->number() == 1,
         "ListFriendsRequest.meta field number frozen at 1");
  Expect(actor_field != nullptr && actor_field->number() == 2,
         "ListFriendsRequest.actor_user_id field number frozen at 2");
  Expect(limit_field != nullptr && limit_field->number() == 3,
         "ListFriendsRequest.limit field number frozen at 3");

  const auto* response_descriptor = ListFriendsResponse::descriptor();
  const auto* friends_field = FindField(response_descriptor, "friends");
  const auto* has_more_field = FindField(response_descriptor, "has_more");

  Expect(friends_field != nullptr && friends_field->number() == 1,
         "ListFriendsResponse.friends field number frozen at 1");
  Expect(has_more_field != nullptr && has_more_field->number() == 2,
         "ListFriendsResponse.has_more field number frozen at 2");


  // M14-B1 UserService contract gate.
  using tinyimx::user::v1::AuthenticateRequest;
  using tinyimx::user::v1::AuthenticateResponse;
  using tinyimx::user::v1::GetUserProfileRequest;
  using tinyimx::user::v1::GetUserProfileResponse;
  using tinyimx::user::v1::UserService;

  static_assert(std::is_class_v<UserService>);
  static_assert(std::is_class_v<UserService::StubInterface>);

  AuthenticateRequest auth_request;
  *auth_request.mutable_meta() = meta;
  auth_request.set_username("user10001");
  auth_request.set_password("secret");

  Expect(auth_request.meta().trace_id() == "trace-1",
         "AuthenticateRequest RequestMeta round-trip");
  Expect(auth_request.username() == "user10001",
         "AuthenticateRequest.username typed field");

  const auto* auth_request_descriptor = AuthenticateRequest::descriptor();
  const auto* auth_meta_field = FindField(auth_request_descriptor, "meta");
  const auto* auth_username_field = FindField(auth_request_descriptor, "username");
  const auto* auth_password_field = FindField(auth_request_descriptor, "password");

  Expect(auth_meta_field != nullptr && auth_meta_field->number() == 1,
         "AuthenticateRequest.meta field number frozen at 1");
  Expect(auth_username_field != nullptr && auth_username_field->number() == 2,
         "AuthenticateRequest.username field number frozen at 2");
  Expect(auth_password_field != nullptr && auth_password_field->number() == 3,
         "AuthenticateRequest.password field number frozen at 3");

  AuthenticateResponse auth_response;
  auth_response.set_result(
      tinyimx::user::v1::AUTHENTICATE_RESULT_AUTHENTICATED);
  auto* auth_profile = auth_response.mutable_profile();
  auth_profile->set_user_id(10001);
  auth_profile->set_username("user10001");

  Expect(auth_response.has_profile() &&
             auth_response.profile().user_id() == 10001,
         "AuthenticateResponse authenticated profile works");

  GetUserProfileRequest profile_request;
  *profile_request.mutable_meta() = meta;
  profile_request.set_user_id(10002);

  GetUserProfileResponse profile_response;
  profile_response.mutable_profile()->set_user_id(10002);
  profile_response.mutable_profile()->set_username("user10002");

  const auto* profile_request_descriptor = GetUserProfileRequest::descriptor();
  const auto* profile_meta_field = FindField(profile_request_descriptor, "meta");
  const auto* profile_user_id_field = FindField(profile_request_descriptor, "user_id");

  Expect(profile_meta_field != nullptr && profile_meta_field->number() == 1,
         "GetUserProfileRequest.meta field number frozen at 1");
  Expect(profile_user_id_field != nullptr && profile_user_id_field->number() == 2,
         "GetUserProfileRequest.user_id field number frozen at 2");
  Expect(profile_response.has_profile() &&
             profile_response.profile().username() == "user10002",
         "GetUserProfileResponse.profile typed field");

  // M14-C1 MessageService durable-domain contract gate.  Freeze the
  // complete service surface in C1 even though C2/C3 land mutation paths later.
  using tinyimx::message::v1::ListConversationsRequest;
  using tinyimx::message::v1::ListConversationsResponse;
  using tinyimx::message::v1::ListHistoryRequest;
  using tinyimx::message::v1::ListHistoryResponse;
  using tinyimx::message::v1::MessageRecord;
  using tinyimx::message::v1::GroupMessageRecord;
  using tinyimx::message::v1::PersistGroupMessageRequest;
  using tinyimx::message::v1::PersistGroupMessageResponse;
  using tinyimx::message::v1::GetGroupMessageDeliveryRequest;
  using tinyimx::message::v1::ClaimGroupMessageDeliveriesRequest;
  using tinyimx::message::v1::ClaimGroupMessageDeliveriesForRecipientRequest;
  using tinyimx::message::v1::CompleteGroupMessageDeliveryAttemptRequest;
  using tinyimx::message::v1::ConfirmGroupMessageDeliveryRequest;
  using tinyimx::message::v1::MessageService;

  static_assert(std::is_class_v<MessageService>);
  static_assert(std::is_class_v<MessageService::StubInterface>);

  ListHistoryRequest message_history_request;
  *message_history_request.mutable_meta() = meta;
  message_history_request.set_actor_user_id(10001);
  message_history_request.set_peer_user_id(10002);
  message_history_request.set_before_message_id(900);
  message_history_request.set_limit(20);

  const auto* message_history_descriptor = ListHistoryRequest::descriptor();
  Expect(FindField(message_history_descriptor, "meta") != nullptr &&
             FindField(message_history_descriptor, "meta")->number() == 1,
         "ListHistoryRequest.meta field number frozen at 1");
  Expect(FindField(message_history_descriptor, "actor_user_id") != nullptr &&
             FindField(message_history_descriptor, "actor_user_id")->number() == 2,
         "ListHistoryRequest.actor_user_id field number frozen at 2");
  Expect(FindField(message_history_descriptor, "peer_user_id") != nullptr &&
             FindField(message_history_descriptor, "peer_user_id")->number() == 3,
         "ListHistoryRequest.peer_user_id field number frozen at 3");
  Expect(FindField(message_history_descriptor, "before_message_id") != nullptr &&
             FindField(message_history_descriptor, "before_message_id")->number() == 4,
         "ListHistoryRequest.before_message_id field number frozen at 4");
  Expect(FindField(message_history_descriptor, "limit") != nullptr &&
             FindField(message_history_descriptor, "limit")->number() == 5,
         "ListHistoryRequest.limit field number frozen at 5");

  ListHistoryResponse message_history_response;
  MessageRecord* message_record = message_history_response.add_messages();
  message_record->set_message_id(1001);
  message_record->set_client_message_id("client-message-1001");
  message_record->set_from_user_id(10001);
  message_record->set_to_user_id(10002);
  message_record->set_message_type(1);
  message_record->set_content("{\"text\":\"hello\"}");
  message_record->set_delivery_state(
      tinyimx::message::v1::MESSAGE_DELIVERY_STATE_RECEIVER_CONFIRMED);
  message_record->set_created_at("2026-09-04 10:00:00");
  message_record->set_receiver_confirmed_at("2026-09-04 10:00:01");
  message_history_response.set_has_more(true);

  Expect(message_history_response.messages_size() == 1 &&
             message_history_response.messages(0).message_id() == 1001,
         "ListHistoryResponse MessageRecord typed fields work");
  Expect(message_history_response.messages(0).delivery_state() ==
             tinyimx::message::v1::MESSAGE_DELIVERY_STATE_RECEIVER_CONFIRMED,
         "MessageRecord receiver-confirmed semantic state works");

  ListConversationsRequest message_conversation_request;
  *message_conversation_request.mutable_meta() = meta;
  message_conversation_request.set_actor_user_id(10001);
  message_conversation_request.set_limit(20);

  const auto* message_conversation_descriptor =
      ListConversationsRequest::descriptor();
  Expect(FindField(message_conversation_descriptor, "meta") != nullptr &&
             FindField(message_conversation_descriptor, "meta")->number() == 1,
         "ListConversationsRequest.meta field number frozen at 1");
  Expect(FindField(message_conversation_descriptor, "actor_user_id") != nullptr &&
             FindField(message_conversation_descriptor, "actor_user_id")->number() == 2,
         "ListConversationsRequest.actor_user_id field number frozen at 2");
  Expect(FindField(message_conversation_descriptor, "limit") != nullptr &&
             FindField(message_conversation_descriptor, "limit")->number() == 3,
         "ListConversationsRequest.limit field number frozen at 3");

  ListConversationsResponse message_conversation_response;
  auto* conversation = message_conversation_response.add_conversations();
  conversation->set_peer_user_id(10002);
  conversation->set_last_message_id(1001);
  conversation->set_last_from_user_id(10001);
  conversation->set_last_to_user_id(10002);
  conversation->set_last_message_type(1);
  conversation->set_last_content("{\"text\":\"hello\"}");
  conversation->set_last_delivery_state(
      tinyimx::message::v1::MESSAGE_DELIVERY_STATE_PENDING);
  conversation->set_last_created_at("2026-09-04 10:00:00");
  Expect(message_conversation_response.conversations_size() == 1 &&
             message_conversation_response.conversations(0).peer_user_id() == 10002,
         "ListConversationsResponse typed fields work");

  PersistGroupMessageRequest group_message_request;
  *group_message_request.mutable_meta() = meta;
  group_message_request.set_from_user_id(10001);
  group_message_request.set_group_id(47);
  group_message_request.set_client_message_id("m17b1-contract");
  group_message_request.set_message_type(1);
  group_message_request.set_content("hello-group");

  const auto* persist_group_descriptor = PersistGroupMessageRequest::descriptor();
  Expect(FindField(persist_group_descriptor, "meta") != nullptr &&
             FindField(persist_group_descriptor, "meta")->number() == 1,
         "PersistGroupMessageRequest.meta field number frozen at 1");
  Expect(FindField(persist_group_descriptor, "from_user_id") != nullptr &&
             FindField(persist_group_descriptor, "from_user_id")->number() == 2,
         "PersistGroupMessageRequest.from_user_id field number frozen at 2");
  Expect(FindField(persist_group_descriptor, "group_id") != nullptr &&
             FindField(persist_group_descriptor, "group_id")->number() == 3,
         "PersistGroupMessageRequest.group_id field number frozen at 3");
  Expect(FindField(persist_group_descriptor, "client_message_id") != nullptr &&
             FindField(persist_group_descriptor, "client_message_id")->number() == 4,
         "PersistGroupMessageRequest.client_message_id field number frozen at 4");
  Expect(FindField(persist_group_descriptor, "message_type") != nullptr &&
             FindField(persist_group_descriptor, "message_type")->number() == 5,
         "PersistGroupMessageRequest.message_type field number frozen at 5");
  Expect(FindField(persist_group_descriptor, "content") != nullptr &&
             FindField(persist_group_descriptor, "content")->number() == 6,
         "PersistGroupMessageRequest.content field number frozen at 6");

  PersistGroupMessageResponse group_message_response;
  group_message_response.set_result(
      tinyimx::message::v1::PERSIST_GROUP_MESSAGE_RESULT_CREATED);
  group_message_response.set_message_id(9001);
  GroupMessageRecord* group_message_record = group_message_response.mutable_record();
  group_message_record->set_message_id(9001);
  group_message_record->set_client_message_id("m17b1-contract");
  group_message_record->set_group_id(47);
  group_message_record->set_from_user_id(10001);
  group_message_record->set_message_type(1);
  group_message_record->set_content("hello-group");
  group_message_record->set_membership_epoch(1);
  group_message_record->set_member_version(4);
  group_message_record->set_authorized_role(1);
  group_message_record->set_created_at("2026-09-18 10:00:00");
  Expect(group_message_response.record().group_id() == 47 &&
             group_message_response.record().member_version() == 4,
         "PersistGroupMessageResponse typed durable authorization snapshot works");

  const auto* group_delivery_get = GetGroupMessageDeliveryRequest::descriptor();
  Expect(FindField(group_delivery_get, "message_id") != nullptr &&
             FindField(group_delivery_get, "message_id")->number() == 2 &&
             FindField(group_delivery_get, "recipient_user_id") != nullptr &&
             FindField(group_delivery_get, "recipient_user_id")->number() == 3,
         "M17-B2 GetGroupMessageDelivery durable identity frozen");
  const auto* group_delivery_claim = ClaimGroupMessageDeliveriesRequest::descriptor();
  Expect(FindField(group_delivery_claim, "lease_owner") != nullptr &&
             FindField(group_delivery_claim, "lease_owner")->number() == 2 &&
             FindField(group_delivery_claim, "message_id") != nullptr &&
             FindField(group_delivery_claim, "message_id")->number() == 6,
         "M17-B2 claim lease contract frozen");
  const auto* group_replay_claim =
      ClaimGroupMessageDeliveriesForRecipientRequest::descriptor();
  Expect(FindField(group_replay_claim, "meta") != nullptr &&
             FindField(group_replay_claim, "meta")->number() == 1 &&
             FindField(group_replay_claim, "recipient_user_id") != nullptr &&
             FindField(group_replay_claim, "recipient_user_id")->number() == 2 &&
             FindField(group_replay_claim, "lease_owner") != nullptr &&
             FindField(group_replay_claim, "lease_owner")->number() == 3 &&
             FindField(group_replay_claim, "lease_token") != nullptr &&
             FindField(group_replay_claim, "lease_token")->number() == 4 &&
             FindField(group_replay_claim, "limit") != nullptr &&
             FindField(group_replay_claim, "limit")->number() == 5 &&
             FindField(group_replay_claim, "lease_ms") != nullptr &&
             FindField(group_replay_claim, "lease_ms")->number() == 6,
         "M17-B3 recipient replay claim contract frozen");
  const auto* group_delivery_complete = CompleteGroupMessageDeliveryAttemptRequest::descriptor();
  Expect(FindField(group_delivery_complete, "lease_token") != nullptr &&
             FindField(group_delivery_complete, "lease_token")->number() == 4 &&
             FindField(group_delivery_complete, "outcome") != nullptr &&
             FindField(group_delivery_complete, "outcome")->number() == 5,
         "M17-B2 attempt completion contract frozen");
  const auto* group_delivery_confirm = ConfirmGroupMessageDeliveryRequest::descriptor();
  Expect(FindField(group_delivery_confirm, "recipient_user_id") != nullptr &&
             FindField(group_delivery_confirm, "recipient_user_id")->number() == 3,
         "M17-B2 receiver confirmation authority field frozen");

  using tinyimx::message::v1::GetPrivateMessageRequest;
  using tinyimx::message::v1::CountPendingRequest;
  using tinyimx::message::v1::ListPendingAfterRequest;
  using tinyimx::message::v1::ConfirmReceiverRequest;
  using tinyimx::message::v1::ConfirmReceiverBatchRequest;
  using tinyimx::message::v1::MarkDialogReadRequest;

  const auto* get_message_descriptor = GetPrivateMessageRequest::descriptor();
  Expect(FindField(get_message_descriptor, "meta") != nullptr &&
             FindField(get_message_descriptor, "meta")->number() == 1,
         "GetPrivateMessageRequest.meta field number frozen at 1");
  Expect(FindField(get_message_descriptor, "message_id") != nullptr &&
             FindField(get_message_descriptor, "message_id")->number() == 2,
         "GetPrivateMessageRequest.message_id field number frozen at 2");

  const auto* count_pending_descriptor = CountPendingRequest::descriptor();
  Expect(FindField(count_pending_descriptor, "meta") != nullptr &&
             FindField(count_pending_descriptor, "meta")->number() == 1,
         "CountPendingRequest.meta field number frozen at 1");
  Expect(FindField(count_pending_descriptor, "to_user_id") != nullptr &&
             FindField(count_pending_descriptor, "to_user_id")->number() == 2,
         "CountPendingRequest.to_user_id field number frozen at 2");

  const auto* list_pending_descriptor = ListPendingAfterRequest::descriptor();
  Expect(FindField(list_pending_descriptor, "meta") != nullptr &&
             FindField(list_pending_descriptor, "meta")->number() == 1,
         "ListPendingAfterRequest.meta field number frozen at 1");
  Expect(FindField(list_pending_descriptor, "to_user_id") != nullptr &&
             FindField(list_pending_descriptor, "to_user_id")->number() == 2,
         "ListPendingAfterRequest.to_user_id field number frozen at 2");
  Expect(FindField(list_pending_descriptor, "after_message_id") != nullptr &&
             FindField(list_pending_descriptor, "after_message_id")->number() == 3,
         "ListPendingAfterRequest.after_message_id field number frozen at 3");
  Expect(FindField(list_pending_descriptor, "limit") != nullptr &&
             FindField(list_pending_descriptor, "limit")->number() == 4,
         "ListPendingAfterRequest.limit field number frozen at 4");

  const auto* confirm_descriptor = ConfirmReceiverRequest::descriptor();
  Expect(FindField(confirm_descriptor, "meta") != nullptr &&
             FindField(confirm_descriptor, "meta")->number() == 1,
         "ConfirmReceiverRequest.meta field number frozen at 1");
  Expect(FindField(confirm_descriptor, "message_id") != nullptr &&
             FindField(confirm_descriptor, "message_id")->number() == 2,
         "ConfirmReceiverRequest.message_id field number frozen at 2");
  Expect(FindField(confirm_descriptor, "receiver_user_id") != nullptr &&
             FindField(confirm_descriptor, "receiver_user_id")->number() == 3,
         "ConfirmReceiverRequest.receiver_user_id field number frozen at 3");

  const auto* confirm_batch_descriptor =
      ConfirmReceiverBatchRequest::descriptor();
  Expect(FindField(confirm_batch_descriptor, "meta") != nullptr &&
             FindField(confirm_batch_descriptor, "meta")->number() == 1,
         "ConfirmReceiverBatchRequest.meta field number frozen at 1");
  Expect(FindField(confirm_batch_descriptor, "receiver_user_id") != nullptr &&
             FindField(confirm_batch_descriptor, "receiver_user_id")->number() == 2,
         "ConfirmReceiverBatchRequest.receiver_user_id field number frozen at 2");
  Expect(FindField(confirm_batch_descriptor, "message_ids") != nullptr &&
             FindField(confirm_batch_descriptor, "message_ids")->number() == 3,
         "ConfirmReceiverBatchRequest.message_ids field number frozen at 3");

  const auto* mark_read_descriptor = MarkDialogReadRequest::descriptor();
  Expect(FindField(mark_read_descriptor, "meta") != nullptr &&
             FindField(mark_read_descriptor, "meta")->number() == 1,
         "MarkDialogReadRequest.meta field number frozen at 1");
  Expect(FindField(mark_read_descriptor, "reader_user_id") != nullptr &&
             FindField(mark_read_descriptor, "reader_user_id")->number() == 2,
         "MarkDialogReadRequest.reader_user_id field number frozen at 2");
  Expect(FindField(mark_read_descriptor, "peer_user_id") != nullptr &&
             FindField(mark_read_descriptor, "peer_user_id")->number() == 3,
         "MarkDialogReadRequest.peer_user_id field number frozen at 3");

  const auto* message_file = ListHistoryRequest::descriptor()->file();
  const auto* message_service =
      message_file == nullptr ? nullptr :
      message_file->FindServiceByName("MessageService");
  Expect(message_service != nullptr && message_service->method_count() == 15,
         "MessageService method count frozen at 15 after M17-B3");

  const char* const expected_message_methods[] = {
      "PersistPrivateMessage",
      "PersistGroupMessage",
      "GetGroupMessageDelivery",
      "ClaimGroupMessageDeliveries",
      "CompleteGroupMessageDeliveryAttempt",
      "ConfirmGroupMessageDelivery",
      "GetPrivateMessage",
      "ListHistory",
      "ListConversations",
      "CountPending",
      "ListPendingAfter",
      "ConfirmReceiver",
      "ConfirmReceiverBatch",
      "MarkDialogRead",
      "ClaimGroupMessageDeliveriesForRecipient",
  };
  if (message_service != nullptr) {
    for (int i = 0; i < message_service->method_count() && i < 15; ++i) {
      const std::string label =
          std::string("MessageService method frozen: ") +
          expected_message_methods[i];
      Expect(message_service->method(i)->name() == expected_message_methods[i],
             label.c_str());
    }
  }

  std::cout << "==============================================\n";


  // M17-A1 GroupService contract gate. The complete M17-A service surface is
  // frozen now; A1 implements lifecycle RPCs and A2 fills membership RPCs.
  using tinyimx::group::v1::CreateGroupRequest;
  using tinyimx::group::v1::GroupRecord;
  using tinyimx::group::v1::GroupService;
  using tinyimx::group::v1::UpdateGroupRequest;

  static_assert(std::is_class_v<GroupService>);
  static_assert(std::is_class_v<GroupService::StubInterface>);

  CreateGroupRequest group_create_request;
  *group_create_request.mutable_meta() = meta;
  group_create_request.set_actor_user_id(10001);
  group_create_request.set_client_operation_id("m17-a1-create-1");
  group_create_request.set_name("backend-team");
  group_create_request.set_join_policy(
      tinyimx::group::v1::GROUP_JOIN_POLICY_INVITE_ONLY);
  group_create_request.set_max_members(500);

  const auto* group_create_descriptor = CreateGroupRequest::descriptor();
  Expect(FindField(group_create_descriptor, "meta") != nullptr &&
             FindField(group_create_descriptor, "meta")->number() == 1,
         "CreateGroupRequest.meta field number frozen at 1");
  Expect(FindField(group_create_descriptor, "actor_user_id") != nullptr &&
             FindField(group_create_descriptor, "actor_user_id")->number() == 2,
         "CreateGroupRequest.actor_user_id field number frozen at 2");
  Expect(FindField(group_create_descriptor, "client_operation_id") != nullptr &&
             FindField(group_create_descriptor, "client_operation_id")->number() == 3,
         "CreateGroupRequest.client_operation_id field number frozen at 3");

  GroupRecord group_record;
  group_record.set_group_id(9001);
  group_record.set_owner_user_id(10001);
  group_record.set_status(tinyimx::group::v1::GROUP_STATUS_ACTIVE);
  group_record.set_join_policy(tinyimx::group::v1::GROUP_JOIN_POLICY_INVITE_ONLY);
  group_record.set_version(1);
  group_record.set_member_version(1);
  Expect(group_record.group_id() == 9001 && group_record.version() == 1,
         "GroupRecord durable identity/version typed fields work");

  UpdateGroupRequest group_update_request;
  group_update_request.set_actor_user_id(10001);
  group_update_request.set_client_operation_id("m17-a1-update-1");
  group_update_request.set_group_id(9001);
  group_update_request.set_expected_version(1);
  group_update_request.set_name("platform-team");
  Expect(group_update_request.has_name(),
         "UpdateGroupRequest proto3 optional presence works");
  const auto* group_update_descriptor = UpdateGroupRequest::descriptor();
  Expect(FindField(group_update_descriptor, "expected_version") != nullptr &&
             FindField(group_update_descriptor, "expected_version")->number() == 5,
         "UpdateGroupRequest.expected_version field number frozen at 5");


  const auto* group_service =
      google::protobuf::DescriptorPool::generated_pool()->FindServiceByName(
          "tinyimx.group.v1.GroupService");
  Expect(group_service != nullptr && group_service->method_count() == 15,
         "GroupService 15-method M17-B2 contract frozen");
  static const char* kExpectedGroupMethods[] = {
      "CreateGroup",
      "GetGroup",
      "UpdateGroup",
      "DisbandGroup",
      "JoinGroup",
      "LeaveGroup",
      "InviteMember",
      "KickMember",
      "SetMemberRole",
      "SetMemberMute",
      "TransferOwnership",
      "ListGroupMembers",
      "ListMyGroups",
      "CheckGroupSendPermission",
      "PrepareGroupMessageSend",
  };
  if (group_service != nullptr) {
    for (int i = 0; i < group_service->method_count() && i < 15; ++i) {
      const std::string label =
          std::string("GroupService method frozen: ") + kExpectedGroupMethods[i];
      Expect(group_service->method(i)->name() == kExpectedGroupMethods[i],
             label.c_str());
    }
  }

  const auto* set_role_descriptor =
      tinyimx::group::v1::SetMemberRoleRequest::descriptor();
  Expect(FindField(set_role_descriptor, "role") != nullptr &&
             FindField(set_role_descriptor, "role")->number() == 6,
         "SetMemberRoleRequest.role field number frozen at 6");
  const auto* check_send_descriptor =
      tinyimx::group::v1::CheckGroupSendPermissionResponse::descriptor();
  Expect(FindField(check_send_descriptor, "member_version") != nullptr &&
             FindField(check_send_descriptor, "member_version")->number() == 4,
         "CheckGroupSendPermissionResponse.member_version frozen at 4");
  const auto* prepare_send_descriptor =
      tinyimx::group::v1::PrepareGroupMessageSendResponse::descriptor();
  Expect(FindField(prepare_send_descriptor, "recipient_user_ids") != nullptr &&
             FindField(prepare_send_descriptor, "recipient_user_ids")->number() == 5,
         "PrepareGroupMessageSendResponse.recipient_user_ids frozen at 5");
  // M18-A1 FileService control-plane contract gate. Payload/chunk transport
  // deliberately remains outside this contract until M18-B.
  using tinyimx::file::v1::BeginUploadRequest;
  using tinyimx::file::v1::FileService;
  using tinyimx::file::v1::UploadSessionRecord;

  static_assert(std::is_class_v<FileService>);
  static_assert(std::is_class_v<FileService::StubInterface>);

  BeginUploadRequest file_begin_request;
  *file_begin_request.mutable_meta() = meta;
  file_begin_request.set_actor_user_id(10001);
  file_begin_request.set_client_upload_id("m18-a1-upload-1");
  file_begin_request.set_file_name("report.pdf");
  file_begin_request.set_content_type("application/pdf");
  file_begin_request.set_total_size(4096);
  file_begin_request.set_checksum_algorithm("sha256");
  file_begin_request.set_expected_checksum(
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
  file_begin_request.set_preferred_chunk_size(4ULL * 1024ULL * 1024ULL);

  const auto* file_begin_descriptor = BeginUploadRequest::descriptor();
  Expect(FindField(file_begin_descriptor, "meta") != nullptr &&
             FindField(file_begin_descriptor, "meta")->number() == 1,
         "BeginUploadRequest.meta field number frozen at 1");
  Expect(FindField(file_begin_descriptor, "actor_user_id") != nullptr &&
             FindField(file_begin_descriptor, "actor_user_id")->number() == 2,
         "BeginUploadRequest.actor_user_id field number frozen at 2");
  Expect(FindField(file_begin_descriptor, "client_upload_id") != nullptr &&
             FindField(file_begin_descriptor, "client_upload_id")->number() == 3,
         "BeginUploadRequest.client_upload_id field number frozen at 3");
  Expect(FindField(file_begin_descriptor, "expected_checksum") != nullptr &&
             FindField(file_begin_descriptor, "expected_checksum")->number() == 8,
         "BeginUploadRequest.expected_checksum field number frozen at 8");
  Expect(FindField(file_begin_descriptor, "preferred_chunk_size") != nullptr &&
             FindField(file_begin_descriptor, "preferred_chunk_size")->number() == 9,
         "BeginUploadRequest.preferred_chunk_size field number frozen at 9");

  UploadSessionRecord upload_session_record;
  upload_session_record.set_upload_id(77);
  upload_session_record.set_file_id(7001);
  upload_session_record.set_owner_user_id(10001);
  upload_session_record.set_status(
      tinyimx::file::v1::UPLOAD_SESSION_STATUS_ACTIVE);
  Expect(upload_session_record.upload_id() == 77 &&
             upload_session_record.status() ==
                 tinyimx::file::v1::UPLOAD_SESSION_STATUS_ACTIVE,
         "UploadSessionRecord durable identity/state typed fields work");

  const auto* file_service =
      google::protobuf::DescriptorPool::generated_pool()->FindServiceByName(
          "tinyimx.file.v1.FileService");
  Expect(file_service != nullptr && file_service->method_count() == 4,
         "FileService 4-method M18-B1 contract frozen");
  static const char* kExpectedFileMethods[] = {
      "BeginUpload",
      "GetUploadSession",
      "CancelUpload",
      "UploadChunk",
  };
  if (file_service != nullptr) {
    for (int i = 0; i < file_service->method_count() && i < 4; ++i) {
      const std::string label =
          std::string("FileService method frozen: ") + kExpectedFileMethods[i];
      Expect(file_service->method(i)->name() == kExpectedFileMethods[i],
             label.c_str());
    }
  }


  using tinyimx::file::v1::UploadChunkRequest;
  const auto* upload_chunk_descriptor = UploadChunkRequest::descriptor();
  Expect(FindField(upload_chunk_descriptor, "upload_id") != nullptr &&
             FindField(upload_chunk_descriptor, "upload_id")->number() == 3,
         "UploadChunkRequest.upload_id frozen at 3");
  Expect(FindField(upload_chunk_descriptor, "chunk_index") != nullptr &&
             FindField(upload_chunk_descriptor, "chunk_index")->number() == 4,
         "UploadChunkRequest.chunk_index frozen at 4");
  Expect(FindField(upload_chunk_descriptor, "byte_offset") != nullptr &&
             FindField(upload_chunk_descriptor, "byte_offset")->number() == 5,
         "UploadChunkRequest.byte_offset frozen at 5");
  Expect(FindField(upload_chunk_descriptor, "data") != nullptr &&
             FindField(upload_chunk_descriptor, "data")->number() == 6,
         "UploadChunkRequest.data frozen at 6");
  Expect(FindField(upload_chunk_descriptor, "checksum") != nullptr &&
             FindField(upload_chunk_descriptor, "checksum")->number() == 8,
         "UploadChunkRequest.checksum frozen at 8");

  std::cout << "total_failed=" << g_failed << '\n';

  return g_failed == 0 ? 0 : 1;
}
