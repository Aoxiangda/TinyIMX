#include "services/rpc/RpcCallOptions.h"
#include "services/rpc/StaticServiceEndpointProvider.h"
#include "services/rpc/UserRpcClient.h"
#include "services/user/application/UserApplicationService.h"
#include "services/user/server/UserServiceServer.h"
#include "services/user/service/UserServiceImpl.h"
#include "common/concurrency/ThreadPool.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

namespace {

class FakeUserRepositoryPort final
    : public tinyimx::user::UserRepositoryPort {
public:
    tinyimx::user::AuthenticateRepositoryResult Authenticate(
        const std::string& username,
        const std::string& password
    ) override {
        ++authenticate_calls;
        last_username = username;
        last_password = password;

        tinyimx::user::AuthenticateRepositoryResult result;
        result.status =
            tinyimx::user::UserApplicationStatus::kSucceeded;

        if (password == "wrong") {
            result.outcome =
                tinyimx::user::AuthenticateOutcome::kWrongPassword;
            result.message = "wrong password";
            return result;
        }

        result.outcome =
            tinyimx::user::AuthenticateOutcome::kAuthenticated;
        result.profile = MakeProfile(10001, username);
        result.message = "login accepted";
        return result;
    }

    tinyimx::user::UserProfileRepositoryResult GetProfile(
        std::uint64_t user_id
    ) override {
        ++profile_calls;
        last_user_id = user_id;

        tinyimx::user::UserProfileRepositoryResult result;
        if (user_id == 99999) {
            result.status =
                tinyimx::user::UserApplicationStatus::kNotFound;
            result.message = "user not found";
            return result;
        }

        result.status =
            tinyimx::user::UserApplicationStatus::kSucceeded;
        result.profile = MakeProfile(user_id, "user" + std::to_string(user_id));
        result.message = "user found";
        return result;
    }

    bool RecordSuccessfulLogin(
        std::uint64_t user_id
    ) override {
        record_successful_login_calls.fetch_add(1, std::memory_order_relaxed);
        last_recorded_user_id.store(user_id, std::memory_order_relaxed);
        return record_successful_login_result;
    }

    static tinyimx::user::UserProfileView MakeProfile(
        std::uint64_t user_id,
        const std::string& username
    ) {
        tinyimx::user::UserProfileView profile;
        profile.user_id = user_id;
        profile.username = username;
        profile.nickname = "User";
        profile.avatar_url = "avatar";
        profile.user_status = 1;
        return profile;
    }

    std::size_t authenticate_calls{0};
    std::size_t profile_calls{0};
    std::string last_username;
    std::string last_password;
    std::uint64_t last_user_id{0};

    bool record_successful_login_result{true};
    std::atomic<std::size_t> record_successful_login_calls{0};
    std::atomic<std::uint64_t> last_recorded_user_id{0};
};

bool Expect(bool condition, const char* name) {
    if (condition) {
        std::cout << "[PASS] " << name << '\n';
        return true;
    }

    std::cerr << "[FAIL] " << name << '\n';
    return false;
}

bool TestRealGrpcUserService() {
    FakeUserRepositoryPort repository;
    repository.record_successful_login_result = false;
    tinyimx::user::UserApplicationService application(&repository);

    tinyimx::ThreadPoolOptions metadata_options;
    metadata_options.name = "user-service-test-metadata";
    metadata_options.worker_threads = 1;
    metadata_options.queue_capacity = 8;
    metadata_options.queue_full_policy = tinyimx::QueueFullPolicy::kDiscard;
    metadata_options.enable_dynamic_resize = false;
    tinyimx::ThreadPool metadata_executor(metadata_options);
    if (!metadata_executor.Start()) {
        return Expect(false, "UserService.MetadataExecutorStart");
    }

    tinyimx::user::UserServiceImpl service_impl(
        &application,
        &metadata_executor
    );
    tinyimx::user::UserServiceServer server(&service_impl);

    if (!server.Start("127.0.0.1:0")) {
        metadata_executor.Shutdown(
            tinyimx::ShutdownMode::kGraceful,
            std::chrono::milliseconds(1000)
        );
        return Expect(false, "UserService.RealGrpcServerStart");
    }

    const auto provider =
        std::make_shared<
            tinyimx::rpc::StaticServiceEndpointProvider
        >("", server.BoundTarget());

    tinyimx::rpc::UserRpcClient client(provider);

    tinyimx::rpc::RpcCallOptions options;
    options.request_id = "m14-b1-request-1";
    options.trace_id = "m14-b1-trace-1";
    options.caller_service = "gateway-test";
    options.caller_instance = "gateway-test-a";
    options.remaining_timeout = std::chrono::seconds(1);

    tinyimx::rpc::AuthenticateRpcRequest auth_request;
    auth_request.username = "user10001";
    auth_request.password = "secret";

    const auto auth_result =
        client.Authenticate(auth_request, options);

    bool ok =
        Expect(
            auth_result.ok() &&
            auth_result.value->authenticated() &&
            auth_result.value->profile->user_id == 10001,
            "UserService.RealGrpcAuthenticateSuccess"
        );

    for (int i = 0;
         i < 100 &&
         repository.record_successful_login_calls.load(std::memory_order_relaxed) == 0;
         ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    ok = Expect(
        repository.record_successful_login_calls.load(std::memory_order_relaxed) == 1 &&
        repository.last_recorded_user_id.load(std::memory_order_relaxed) == 10001,
        "UserService.LastLoginMetadataBestEffort"
    ) && ok;

    // Metadata persistence failure is explicitly non-authoritative: the RPC
    // above must still authenticate successfully even though the fake returns
    // false from RecordSuccessfulLogin().
    auth_request.password = "wrong";
    const auto wrong_result =
        client.Authenticate(auth_request, options);

    ok = Expect(
        wrong_result.ok() &&
        !wrong_result.value->authenticated() &&
        wrong_result.value->outcome ==
            tinyimx::rpc::AuthenticateRpcOutcome::kWrongPassword,
        "UserService.AuthenticationOutcomeMapping"
    ) && ok;

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    ok = Expect(
        repository.record_successful_login_calls.load(std::memory_order_relaxed) == 1,
        "UserService.WrongPasswordSkipsLastLoginMetadata"
    ) && ok;

    tinyimx::rpc::GetUserProfileRpcRequest profile_request;
    profile_request.user_id = 10002;

    const auto profile_result =
        client.GetUserProfile(profile_request, options);

    ok = Expect(
        profile_result.ok() &&
        profile_result.value->profile.user_id == 10002 &&
        repository.profile_calls == 1 &&
        repository.last_user_id == 10002,
        "UserService.RealGrpcGetUserProfile"
    ) && ok;

    profile_request.user_id = 99999;
    const auto missing_result =
        client.GetUserProfile(profile_request, options);

    ok = Expect(
        !missing_result.ok() &&
        missing_result.status.code ==
            tinyimx::rpc::RpcErrorCode::kNotFound,
        "UserService.NotFoundStatusMapping"
    ) && ok;

    ok = Expect(
        repository.authenticate_calls == 2 &&
        repository.last_username == "user10001",
        "UserService.ApplicationRepositoryBoundary"
    ) && ok;

    server.Shutdown();
    server.Wait();
    metadata_executor.Shutdown(
        tinyimx::ShutdownMode::kGraceful,
        std::chrono::milliseconds(1000)
    );
    return ok;
}

}  // namespace

int main() {
    std::cout
        << "========== TinyIMX M14-B1 UserService Integration Tests ==========\n";

    const bool ok = TestRealGrpcUserService();
    const std::size_t failed = ok ? 0 : 1;

    std::cout
        << "=================================================================\n"
        << "total = 1, failed = " << failed << '\n';

    return ok ? 0 : 1;
}
