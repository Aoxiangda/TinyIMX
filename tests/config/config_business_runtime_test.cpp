#include "common/config/Config.h"

#include <iostream>
#include <string>

namespace {

bool Expect(
    bool condition,
    const std::string& name
) {
    if (!condition) {
        std::cerr
            << "[FAIL] "
            << name
            << '\n';
        return false;
    }

    std::cout
        << "[PASS] "
        << name
        << '\n';

    return true;
}

bool Load(
    tinyimx::Config* config,
    const std::string& body
) {
    return
        config != nullptr &&
        config->LoadFromString(
            body,
            "<config_business_runtime_test>"
        );
}

bool ExpectLoadFailure(
    const std::string& name,
    const std::string& body,
    const std::string& expected_error
) {
    tinyimx::Config config;

    if (Load(&config, body)) {
        std::cerr
            << "[FAIL] "
            << name
            << ": expected load failure\n";
        return false;
    }

    if (config.LastError() != expected_error) {
        std::cerr
            << "[FAIL] "
            << name
            << ": unexpected error"
            << ", expected=\""
            << expected_error
            << "\", actual=\""
            << config.LastError()
            << "\"\n";
        return false;
    }

    std::cout
        << "[PASS] "
        << name
        << '\n';

    return true;
}

bool TestDefaultConfig() {
    tinyimx::Config config;

    if (!Load(&config, "{}")) {
        std::cerr
            << "[FAIL] default config loads"
            << ", error="
            << config.LastError()
            << '\n';
        return false;
    }

    const auto& runtime =
        config.BusinessRuntime();

    return
        Expect(
            runtime.worker_threads == 4,
            "business runtime default workers"
        ) &&
        Expect(
            runtime.max_pending_tasks == 1024,
            "business runtime default max pending"
        ) &&
        Expect(
            runtime.stripe_count == 64,
            "business runtime default stripes"
        ) &&
        Expect(
            runtime.per_stripe_queue_capacity == 32,
            "business runtime default per-stripe capacity"
        ) &&
        Expect(
            runtime.default_deadline_ms == 3000,
            "business runtime default deadline"
        ) &&
        Expect(
            runtime.shutdown_timeout_ms == 30000,
            "business runtime default shutdown timeout"
        );
}

bool TestLegacyThreadPoolInheritance() {
    tinyimx::Config config;

    if (
        !Load(
            &config,
            R"json(
{
  "thread_pool": {
    "worker_threads": 6,
    "queue_capacity": 512
  }
}
)json"
        )
    ) {
        std::cerr
            << "[FAIL] legacy thread_pool inheritance loads"
            << ", error="
            << config.LastError()
            << '\n';
        return false;
    }

    const auto& runtime =
        config.BusinessRuntime();

    return
        Expect(
            runtime.worker_threads == 6,
            "legacy thread_pool workers inherited"
        ) &&
        Expect(
            runtime.max_pending_tasks == 512,
            "legacy thread_pool queue inherited"
        ) &&
        Expect(
            runtime.stripe_count == 64,
            "legacy inheritance keeps runtime stripes"
        ) &&
        Expect(
            runtime.per_stripe_queue_capacity == 32,
            "legacy inheritance keeps runtime per-stripe default"
        );
}

bool TestSmallLegacyQueueCompatibility() {
    tinyimx::Config config;

    if (
        !Load(
            &config,
            R"json(
{
  "thread_pool": {
    "worker_threads": 2,
    "queue_capacity": 64
  }
}
)json"
        )
    ) {
        std::cerr
            << "[FAIL] small legacy queue loads"
            << ", error="
            << config.LastError()
            << '\n';
        return false;
    }

    const auto& runtime =
        config.BusinessRuntime();

    return
        Expect(
            runtime.max_pending_tasks == 64,
            "small legacy max pending inherited"
        ) &&
        Expect(
            runtime.per_stripe_queue_capacity == 32,
            "small legacy per-stripe capacity safely bounded"
        );
}

bool TestExplicitOverride() {
    tinyimx::Config config;

    if (
        !Load(
            &config,
            R"json(
{
  "thread_pool": {
    "worker_threads": 6,
    "queue_capacity": 512
  },
  "business_runtime": {
    "worker_threads": 4,
    "max_pending_tasks": 256,
    "stripe_count": 32,
    "per_stripe_queue_capacity": 16,
    "default_deadline_ms": 2000,
    "shutdown_timeout_ms": 10000
  }
}
)json"
        )
    ) {
        std::cerr
            << "[FAIL] explicit business runtime override loads"
            << ", error="
            << config.LastError()
            << '\n';
        return false;
    }

    const auto& runtime =
        config.BusinessRuntime();

    return
        Expect(
            runtime.worker_threads == 4,
            "explicit runtime workers override legacy"
        ) &&
        Expect(
            runtime.max_pending_tasks == 256,
            "explicit runtime max pending override legacy"
        ) &&
        Expect(
            runtime.stripe_count == 32,
            "explicit runtime stripes parsed"
        ) &&
        Expect(
            runtime.per_stripe_queue_capacity == 16,
            "explicit runtime per-stripe capacity parsed"
        ) &&
        Expect(
            runtime.default_deadline_ms == 2000,
            "explicit runtime deadline parsed"
        ) &&
        Expect(
            runtime.shutdown_timeout_ms == 10000,
            "explicit runtime shutdown timeout parsed"
        );
}

bool TestPartialOverride() {
    tinyimx::Config config;

    if (
        !Load(
            &config,
            R"json(
{
  "thread_pool": {
    "worker_threads": 6,
    "queue_capacity": 512
  },
  "business_runtime": {
    "stripe_count": 32
  }
}
)json"
        )
    ) {
        std::cerr
            << "[FAIL] partial business runtime override loads"
            << ", error="
            << config.LastError()
            << '\n';
        return false;
    }

    const auto& runtime =
        config.BusinessRuntime();

    return
        Expect(
            runtime.worker_threads == 6,
            "partial runtime override inherits workers"
        ) &&
        Expect(
            runtime.max_pending_tasks == 512,
            "partial runtime override inherits max pending"
        ) &&
        Expect(
            runtime.stripe_count == 32,
            "partial runtime override applies stripe count"
        ) &&
        Expect(
            runtime.per_stripe_queue_capacity == 32,
            "partial runtime override keeps per-stripe default"
        ) &&
        Expect(
            runtime.default_deadline_ms == 3000,
            "partial runtime override keeps deadline default"
        ) &&
        Expect(
            runtime.shutdown_timeout_ms == 30000,
            "partial runtime override keeps shutdown default"
        );
}

bool TestPartialMaxPendingOverride() {
    tinyimx::Config config;

    if (
        !Load(
            &config,
            R"json(
{
  "business_runtime": {
    "max_pending_tasks": 64
  }
}
)json"
        )
    ) {
        std::cerr
            << "[FAIL] partial max pending override loads"
            << ", error="
            << config.LastError()
            << '\n';
        return false;
    }

    const auto& runtime =
        config.BusinessRuntime();

    return
        Expect(
            runtime.max_pending_tasks == 64,
            "partial max pending override applied"
        ) &&
        Expect(
            runtime.per_stripe_queue_capacity == 32,
            "implicit per-stripe capacity keeps production default within global bound"
        );
}


bool TestProductionBusinessRuntimeBaseline() {
    tinyimx::Config config;

    if (
        !Load(
            &config,
            R"json(
{
  "business_runtime": {
    "worker_threads": 4,
    "max_pending_tasks": 128,
    "stripe_count": 64,
    "per_stripe_queue_capacity": 32,
    "default_deadline_ms": 3000,
    "shutdown_timeout_ms": 30000
  }
}
)json"
        )
    ) {
        std::cerr
            << "[FAIL] production business runtime baseline loads"
            << ", error="
            << config.LastError()
            << '\n';
        return false;
    }

    const auto& runtime =
        config.BusinessRuntime();

    return
        Expect(
            runtime.worker_threads == 4,
            "production runtime workers"
        ) &&
        Expect(
            runtime.max_pending_tasks == 128,
            "production runtime max pending"
        ) &&
        Expect(
            runtime.stripe_count == 64,
            "production runtime stripes"
        ) &&
        Expect(
            runtime.per_stripe_queue_capacity == 32,
            "production runtime per-stripe capacity"
        ) &&
        Expect(
            runtime.default_deadline_ms == 3000,
            "production runtime deadline"
        ) &&
        Expect(
            runtime.shutdown_timeout_ms == 30000,
            "production runtime shutdown timeout"
        );
}

}  // namespace

int main() {
    bool ok = true;

    ok &= TestDefaultConfig();
    ok &= TestLegacyThreadPoolInheritance();
    ok &= TestSmallLegacyQueueCompatibility();
    ok &= TestExplicitOverride();
    ok &= TestPartialOverride();
    ok &= TestPartialMaxPendingOverride();

    ok &= TestProductionBusinessRuntimeBaseline();

    ok &= ExpectLoadFailure(
        "reject zero business workers",
        R"({"business_runtime":{"worker_threads":0}})",
        "business_runtime.worker_threads must be greater than 0"
    );

    ok &= ExpectLoadFailure(
        "reject zero max pending",
        R"({"business_runtime":{"max_pending_tasks":0}})",
        "business_runtime.max_pending_tasks must be greater than 0"
    );

    ok &= ExpectLoadFailure(
        "reject zero stripe count",
        R"({"business_runtime":{"stripe_count":0}})",
        "business_runtime.stripe_count must be greater than 0"
    );

    ok &= ExpectLoadFailure(
        "reject zero per-stripe capacity",
        R"({"business_runtime":{"per_stripe_queue_capacity":0}})",
        "business_runtime.per_stripe_queue_capacity must be greater than 0"
    );

    ok &= ExpectLoadFailure(
        "reject per-stripe capacity above global capacity",
        R"json(
{
  "business_runtime": {
    "max_pending_tasks": 64,
    "per_stripe_queue_capacity": 65
  }
}
)json",
        "business_runtime.per_stripe_queue_capacity must not exceed max_pending_tasks"
    );

    ok &= ExpectLoadFailure(
        "reject zero default deadline",
        R"({"business_runtime":{"default_deadline_ms":0}})",
        "business_runtime.default_deadline_ms must be greater than 0"
    );

    ok &= ExpectLoadFailure(
        "reject zero shutdown timeout",
        R"({"business_runtime":{"shutdown_timeout_ms":0}})",
        "business_runtime.shutdown_timeout_ms must be greater than 0"
    );

    if (!ok) {
        std::cerr
            << "[ConfigBusinessRuntimeTests] failed\n";
        return 1;
    }

    std::cout
        << "[ConfigBusinessRuntimeTests] all tests passed\n";

    return 0;
}
