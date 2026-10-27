# Configuration-only regression: no service, network or database is started.
add_executable(message_rpc_retention_policy_tests
    tests/message/message_rpc_retention_policy_test.cpp
)
target_include_directories(message_rpc_retention_policy_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

function(tinyimx_add_rpc_retention_test name enabled minimum maximum)
    add_test(NAME "message_rpc_retention_${name}"
        COMMAND "${CMAKE_COMMAND}" -E env
            --unset=TINYIMX_MESSAGE_RPC_POLLERS_ENABLE
            --unset=TINYIMX_MESSAGE_RPC_RETAIN_POLLERS
            ${ARGN}
            $<TARGET_FILE:message_rpc_retention_policy_tests>
            "${enabled}" "${minimum}" "${maximum}"
    )
endfunction()

tinyimx_add_rpc_retention_test(default 0 1 2)
tinyimx_add_rpc_retention_test(legacy 1 4 8
    TINYIMX_MESSAGE_RPC_POLLERS_ENABLE=1)
tinyimx_add_rpc_retention_test(retained32 1 1 32
    TINYIMX_MESSAGE_RPC_RETAIN_POLLERS=32)
tinyimx_add_rpc_retention_test(retained64 1 1 64
    TINYIMX_MESSAGE_RPC_RETAIN_POLLERS=64)
tinyimx_add_rpc_retention_test(precedence 1 1 64
    TINYIMX_MESSAGE_RPC_POLLERS_ENABLE=1
    TINYIMX_MESSAGE_RPC_RETAIN_POLLERS=64)
tinyimx_add_rpc_retention_test(invalid_default 0 1 2
    TINYIMX_MESSAGE_RPC_RETAIN_POLLERS=64x)
tinyimx_add_rpc_retention_test(invalid_legacy 1 4 8
    TINYIMX_MESSAGE_RPC_POLLERS_ENABLE=1
    TINYIMX_MESSAGE_RPC_RETAIN_POLLERS=64x)
tinyimx_add_rpc_retention_test(nonexact_number 0 1 2
    TINYIMX_MESSAGE_RPC_RETAIN_POLLERS=032)
