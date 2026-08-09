#include "common/cache/RedisConnection.h"

#include "common/logging/LogMacros.h"

#include <cstring>
#include <memory>
#include <utility>
#include <vector>

namespace tinyimx {
namespace {

using RedisReplyPtr =
    std::unique_ptr<
        redisReply,
        decltype(&freeReplyObject)
    >;

std::string ReplyText(
    const redisReply* reply
) {
    if (reply == nullptr ||
        reply->str == nullptr ||
        reply->len == 0) {
        return {};
    }

    return std::string(
        reply->str,
        reply->len
    );
}

}  // namespace

RedisConnection::~RedisConnection() {
    Close();
}

bool RedisConnection::Connect(
    const RedisConfig& config
) {
    Close();

    timeval timeout {};
    timeout.tv_sec = 3;
    timeout.tv_usec = 0;

    context_ =
        redisConnectWithTimeout(
            config.host.c_str(),
            config.port,
            timeout
        );

    if (context_ == nullptr) {
        SetError(
            "redis connect failed: "
            "context is null"
        );

        return false;
    }

    if (context_->err != 0) {
        SetRedisError(
            "redis connect failed"
        );

        Close();

        return false;
    }

    /*
     * context已经成功建立，此时必须先允许Command()工作。
     *
     * 原来的实现直到AUTH和SELECT之后才把connected_设为true，
     * 但AUTH和SELECT本身正是通过Command()执行的。
     */
    connected_ = true;

    if (!config.password.empty()) {
        RedisReplyPtr reply(
            Command({
                "AUTH",
                config.password
            }),
            freeReplyObject
        );

        if (!reply ||
            !CheckStatusReply(
                reply.get(),
                "OK"
            )) {
            Close();
            return false;
        }
    }

    if (config.db != 0) {
        RedisReplyPtr reply(
            Command({
                "SELECT",
                std::to_string(
                    config.db
                )
            }),
            freeReplyObject
        );

        if (!reply ||
            !CheckStatusReply(
                reply.get(),
                "OK"
            )) {
            Close();
            return false;
        }
    }

    last_error_.clear();

    LOG_INFO(
        "redis connected"
        << ", host=" << config.host
        << ", port=" << config.port
        << ", db=" << config.db
    );

    return true;
}

void RedisConnection::Close() {
    if (context_ != nullptr) {
        redisFree(context_);
        context_ = nullptr;
    }

    connected_ = false;
}

bool RedisConnection::IsConnected() const {
    return connected_ &&
           context_ != nullptr &&
           context_->err == 0;
}

bool RedisConnection::Ping() {
    RedisReplyPtr reply(
        Command({"PING"}),
        freeReplyObject
    );

    if (!reply) {
        return false;
    }

    if (reply->type !=
        REDIS_REPLY_STATUS) {
        SetError(
            "redis ping failed: "
            "invalid reply type"
        );

        return false;
    }

    if (ReplyText(reply.get()) !=
        "PONG") {
        SetError(
            "redis ping failed: "
            "reply is not PONG"
        );

        return false;
    }

    last_error_.clear();

    return true;
}

bool RedisConnection::Set(
    const std::string& key,
    const std::string& value
) {
    RedisReplyPtr reply(
        Command({
            "SET",
            key,
            value
        }),
        freeReplyObject
    );

    return reply &&
           CheckStatusReply(
               reply.get(),
               "OK"
           );
}

bool RedisConnection::SetEx(
    const std::string& key,
    const std::string& value,
    int ttl_seconds
) {
    RedisReplyPtr reply(
        Command({
            "SETEX",
            key,
            std::to_string(
                ttl_seconds
            ),
            value
        }),
        freeReplyObject
    );

    return reply &&
           CheckStatusReply(
               reply.get(),
               "OK"
           );
}

std::optional<std::string>
RedisConnection::Get(
    const std::string& key
) {
    RedisReplyPtr reply(
        Command({
            "GET",
            key
        }),
        freeReplyObject
    );

    if (!reply) {
        return std::nullopt;
    }

    if (reply->type ==
        REDIS_REPLY_NIL) {
        last_error_.clear();
        return std::nullopt;
    }

    if (reply->type !=
        REDIS_REPLY_STRING) {
        SetError(
            "redis get failed: "
            "invalid reply type"
        );

        return std::nullopt;
    }

    last_error_.clear();

    return ReplyText(
        reply.get()
    );
}

bool RedisConnection::Del(
    const std::string& key
) {
    RedisReplyPtr reply(
        Command({
            "DEL",
            key
        }),
        freeReplyObject
    );

    if (!reply ||
        reply->type !=
            REDIS_REPLY_INTEGER) {
        SetError(
            "redis del failed: "
            "invalid reply"
        );

        return false;
    }

    last_error_.clear();

    return reply->integer >= 0;
}

std::optional<std::int64_t>
RedisConnection::Incr(
    const std::string& key
) {
    RedisReplyPtr reply(
        Command({
            "INCR",
            key
        }),
        freeReplyObject
    );

    if (!reply ||
        reply->type !=
            REDIS_REPLY_INTEGER) {
        SetError(
            "redis incr failed: "
            "invalid reply"
        );

        return std::nullopt;
    }

    last_error_.clear();

    return static_cast<std::int64_t>(
        reply->integer
    );
}

std::optional<std::int64_t>
RedisConnection::DecrBy(
    const std::string& key,
    std::int64_t delta
) {
    RedisReplyPtr reply(
        Command({
            "DECRBY",
            key,
            std::to_string(delta)
        }),
        freeReplyObject
    );

    if (!reply ||
        reply->type !=
            REDIS_REPLY_INTEGER) {
        SetError(
            "redis decrby failed: "
            "invalid reply"
        );

        return std::nullopt;
    }

    last_error_.clear();

    return static_cast<std::int64_t>(
        reply->integer
    );
}

bool RedisConnection::Expire(
    const std::string& key,
    int ttl_seconds
) {
    RedisReplyPtr reply(
        Command({
            "EXPIRE",
            key,
            std::to_string(
                ttl_seconds
            )
        }),
        freeReplyObject
    );

    if (!reply ||
        reply->type !=
            REDIS_REPLY_INTEGER) {
        SetError(
            "redis expire failed: "
            "invalid reply"
        );

        return false;
    }

    last_error_.clear();

    return reply->integer == 1;
}

std::optional<std::int64_t>
RedisConnection::EvalInteger(
    const std::string& script,
    const std::vector<std::string>& keys,
    const std::vector<std::string>& arguments
) {
    if (script.empty()) {
        SetError(
            "redis eval failed: "
            "script is empty"
        );

        return std::nullopt;
    }

    std::vector<std::string>
        command_arguments;

    command_arguments.reserve(
        3 +
        keys.size() +
        arguments.size()
    );

    command_arguments.emplace_back(
        "EVAL"
    );

    command_arguments.push_back(
        script
    );

    command_arguments.push_back(
        std::to_string(
            keys.size()
        )
    );

    for (const auto& key : keys) {
        command_arguments.push_back(
            key
        );
    }

    for (const auto& argument :
         arguments) {
        command_arguments.push_back(
            argument
        );
    }

    RedisReplyPtr reply(
        Command(command_arguments),
        freeReplyObject
    );

    if (!reply) {
        return std::nullopt;
    }

    if (reply->type ==
        REDIS_REPLY_ERROR) {
        const std::string error_text =
            ReplyText(
                reply.get()
            );

        SetError(
            "redis eval failed"
            + (
                error_text.empty()
                    ? std::string{}
                    : ": " + error_text
            )
        );

        return std::nullopt;
    }

    if (reply->type !=
        REDIS_REPLY_INTEGER) {
        SetError(
            "redis eval failed: "
            "reply is not integer"
        );

        return std::nullopt;
    }

    last_error_.clear();

    return static_cast<std::int64_t>(
        reply->integer
    );
}

const std::string& RedisConnection::LastError() const {
    return last_error_;
}

redisReply* RedisConnection::Command(
    const std::vector<std::string>& args
) {
    if (!IsConnected()) {
        SetError(
            "redis command failed: "
            "not connected"
        );

        return nullptr;
    }

    if (args.empty()) {
        SetError(
            "redis command failed: "
            "arguments are empty"
        );

        return nullptr;
    }

    std::vector<const char*> argv;

    std::vector<std::size_t>
        argv_lengths;

    argv.reserve(
        args.size()
    );

    argv_lengths.reserve(
        args.size()
    );

    for (const auto& arg : args) {
        argv.push_back(
            arg.data()
        );

        argv_lengths.push_back(
            arg.size()
        );
    }

    auto* reply =
        static_cast<redisReply*>(
            redisCommandArgv(
                context_,
                static_cast<int>(
                    argv.size()
                ),
                argv.data(),
                argv_lengths.data()
            )
        );

    if (reply == nullptr) {
        SetRedisError(
            "redis command failed"
        );

        connected_ = false;

        return nullptr;
    }

    return reply;
}

bool RedisConnection::CheckStatusReply(
    redisReply* reply,
    const std::string& expected_status
) {
    if (reply == nullptr) {
        SetError(
            "redis status check failed: "
            "reply is null"
        );

        return false;
    }

    if (reply->type ==
        REDIS_REPLY_ERROR) {
        SetError(
            "redis status check failed: "
            + ReplyText(reply)
        );

        return false;
    }

    if (reply->type !=
        REDIS_REPLY_STATUS) {
        SetError(
            "redis status check failed: "
            "invalid reply type"
        );

        return false;
    }

    const std::string status =
        ReplyText(reply);

    if (status != expected_status) {
        SetError(
            "redis status check failed: "
            "status=" +
            status
        );

        return false;
    }

    last_error_.clear();

    return true;
}

void RedisConnection::SetError(
    const std::string& error_message
) {
    last_error_ =
        error_message;

    LOG_ERROR(
        error_message
    );
}

void RedisConnection::SetRedisError(
    const std::string& prefix
) {
    if (context_ == nullptr) {
        SetError(
            prefix +
            ": context is null"
        );

        return;
    }

    last_error_ =
        prefix +
        ", err=" +
        std::to_string(
            context_->err
        ) +
        ", error=" +
        context_->errstr;

    LOG_ERROR(
        last_error_
    );
}

}  // namespace tinyimx