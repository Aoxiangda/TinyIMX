#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tinyimx::message {

enum class MessageApplicationStatus {
    kSucceeded = 0,
    kInvalidArgument,
    kNotFound,
    kPermissionDenied,
    kFailedPrecondition,
    kInvalidRecord,
    kStorageError,
};

enum class MessageDeliveryState : std::uint32_t {
    kPending = 0,
    kReceiverConfirmed = 1,
    kRead = 2,
    kFailed = 3,
};

enum class PersistPrivateMessageOutcome {
    kCreated = 0,
    kReused,
    kIdempotencyConflict,
};

struct MessageView {
    std::uint64_t message_id{0};
    std::string client_message_id;
    std::uint64_t from_user_id{0};
    std::uint64_t to_user_id{0};
    std::uint32_t message_type{0};
    std::string content;
    MessageDeliveryState delivery_state{MessageDeliveryState::kPending};
    std::string created_at;
    std::string receiver_confirmed_at;
    std::string read_at;
};

struct ConversationView {
    std::uint64_t peer_user_id{0};
    std::uint64_t last_message_id{0};
    std::string last_client_message_id;
    std::uint64_t last_from_user_id{0};
    std::uint64_t last_to_user_id{0};
    std::uint32_t last_message_type{0};
    std::string last_content;
    MessageDeliveryState last_delivery_state{MessageDeliveryState::kPending};
    std::string last_created_at;
    std::string last_receiver_confirmed_at;
    std::string last_read_at;
};

struct MessageRepositoryPersistResult {
    MessageApplicationStatus status{MessageApplicationStatus::kStorageError};
    PersistPrivateMessageOutcome outcome{PersistPrivateMessageOutcome::kCreated};
    std::uint64_t message_id{0};
    MessageView record;
    std::string message;

    [[nodiscard]] bool Completed() const noexcept {
        return status == MessageApplicationStatus::kSucceeded;
    }

    [[nodiscard]] bool Accepted() const noexcept {
        return Completed() &&
               (outcome == PersistPrivateMessageOutcome::kCreated ||
                outcome == PersistPrivateMessageOutcome::kReused);
    }
};

struct PersistPrivateMessageApplicationResult {
    MessageApplicationStatus status{MessageApplicationStatus::kStorageError};
    PersistPrivateMessageOutcome outcome{PersistPrivateMessageOutcome::kCreated};
    std::uint64_t message_id{0};
    MessageView record;
    std::string message;

    [[nodiscard]] bool Completed() const noexcept {
        return status == MessageApplicationStatus::kSucceeded;
    }

    [[nodiscard]] bool Accepted() const noexcept {
        return Completed() &&
               (outcome == PersistPrivateMessageOutcome::kCreated ||
                outcome == PersistPrivateMessageOutcome::kReused);
    }

    [[nodiscard]] bool Created() const noexcept {
        return Accepted() && outcome == PersistPrivateMessageOutcome::kCreated;
    }

    [[nodiscard]] bool Reused() const noexcept {
        return Accepted() && outcome == PersistPrivateMessageOutcome::kReused;
    }

    [[nodiscard]] bool Conflict() const noexcept {
        return Completed() &&
               outcome == PersistPrivateMessageOutcome::kIdempotencyConflict;
    }
};

struct MessageRepositoryGetResult {
    MessageApplicationStatus status{MessageApplicationStatus::kStorageError};
    bool found{false};
    MessageView record;
    std::string message;

    [[nodiscard]] bool Succeeded() const noexcept {
        return status == MessageApplicationStatus::kSucceeded;
    }

    [[nodiscard]] bool Found() const noexcept {
        return Succeeded() && found;
    }
};

struct GetPrivateMessageApplicationResult {
    MessageApplicationStatus status{MessageApplicationStatus::kStorageError};
    MessageView record;
    std::string message;

    [[nodiscard]] bool Succeeded() const noexcept {
        return status == MessageApplicationStatus::kSucceeded;
    }
};

struct MessageRepositoryHistoryResult {
    MessageApplicationStatus status{MessageApplicationStatus::kStorageError};
    std::vector<MessageView> records;
    std::string message;

    [[nodiscard]] bool Succeeded() const noexcept {
        return status == MessageApplicationStatus::kSucceeded;
    }
};

struct MessageRepositoryConversationResult {
    MessageApplicationStatus status{MessageApplicationStatus::kStorageError};
    std::vector<ConversationView> records;
    std::string message;

    [[nodiscard]] bool Succeeded() const noexcept {
        return status == MessageApplicationStatus::kSucceeded;
    }
};

struct MessageRepositoryCountResult {
    MessageApplicationStatus status{MessageApplicationStatus::kStorageError};
    std::uint64_t count{0};
    std::string message;

    [[nodiscard]] bool Succeeded() const noexcept {
        return status == MessageApplicationStatus::kSucceeded;
    }
};

struct MessageRepositoryPendingResult {
    MessageApplicationStatus status{MessageApplicationStatus::kStorageError};
    std::vector<MessageView> records;
    std::string message;

    [[nodiscard]] bool Succeeded() const noexcept {
        return status == MessageApplicationStatus::kSucceeded;
    }
};

struct MessageRepositoryMutationResult {
    MessageApplicationStatus status{MessageApplicationStatus::kStorageError};
    std::uint64_t affected_rows{0};
    std::string message;

    [[nodiscard]] bool Succeeded() const noexcept {
        return status == MessageApplicationStatus::kSucceeded;
    }
};

struct ListHistoryApplicationResult {
    MessageApplicationStatus status{MessageApplicationStatus::kStorageError};
    std::vector<MessageView> messages;
    bool has_more{false};
    std::string message;

    [[nodiscard]] bool Succeeded() const noexcept {
        return status == MessageApplicationStatus::kSucceeded;
    }
};

struct ListConversationsApplicationResult {
    MessageApplicationStatus status{MessageApplicationStatus::kStorageError};
    std::vector<ConversationView> conversations;
    bool has_more{false};
    std::string message;

    [[nodiscard]] bool Succeeded() const noexcept {
        return status == MessageApplicationStatus::kSucceeded;
    }
};

struct CountPendingApplicationResult {
    MessageApplicationStatus status{MessageApplicationStatus::kStorageError};
    std::uint64_t count{0};
    std::string message;

    [[nodiscard]] bool Succeeded() const noexcept {
        return status == MessageApplicationStatus::kSucceeded;
    }
};

struct ListPendingAfterApplicationResult {
    MessageApplicationStatus status{MessageApplicationStatus::kStorageError};
    std::vector<MessageView> messages;
    bool has_more{false};
    std::string message;

    [[nodiscard]] bool Succeeded() const noexcept {
        return status == MessageApplicationStatus::kSucceeded;
    }
};

struct MessageMutationApplicationResult {
    MessageApplicationStatus status{MessageApplicationStatus::kStorageError};
    std::uint64_t affected_rows{0};
    std::string message;

    [[nodiscard]] bool Succeeded() const noexcept {
        return status == MessageApplicationStatus::kSucceeded;
    }
};

}  // namespace tinyimx::message
