# ============================================================================
# TinyIMX M16 final CTest policy
# ============================================================================
#
# unit:
#   deterministic, no live infrastructure dependency
#
# integration:
#   real shared MySQL and/or Redis
#
# external/reliability:
#   real RocketMQ, Broker/Proxy and process-death fault scenarios.
#   These are intentionally NOT ordinary CTest tests.

###############################################################################
# Integration registration
###############################################################################

if(TARGET message_outbox_integration_tests)
    if(NOT TEST message_outbox_integration_tests)
        add_test(
            NAME message_outbox_integration_tests
            COMMAND message_outbox_integration_tests
        )
    endif()
endif()

if(TARGET unread_projection_reader_integration_tests)
    if(NOT TEST unread_projection_reader_integration_tests)
        add_test(
            NAME unread_projection_reader_integration_tests
            COMMAND unread_projection_reader_integration_tests
        )
    endif()
endif()

if(TARGET unread_projection_cache_integration_tests)
    if(NOT TEST unread_projection_cache_integration_tests)
        add_test(
            NAME unread_projection_cache_integration_tests
            COMMAND unread_projection_cache_integration_tests
        )
    endif()
endif()

###############################################################################
# Deterministic tests
###############################################################################

if(TEST config_m16_b_tests)
    set_tests_properties(
        config_m16_b_tests
        PROPERTIES
            LABELS "m16;unit;config"
            TIMEOUT 30
    )
endif()

if(TEST outbox_relay_unit_tests)
    set_tests_properties(
        outbox_relay_unit_tests
        PROPERTIES
            LABELS "m16;unit;outbox"
            TIMEOUT 30
    )
endif()

if(TEST m16_crash_window_contract_tests)
    set_tests_properties(
        m16_crash_window_contract_tests
        PROPERTIES
            LABELS "m16;unit;reliability;deterministic"
            TIMEOUT 30
    )
endif()

###############################################################################
# Shared-infrastructure integration tests
###############################################################################

if(TEST message_outbox_integration_tests)
    set_tests_properties(
        message_outbox_integration_tests
        PROPERTIES
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            LABELS "m16;integration;mysql;outbox"
            RESOURCE_LOCK "tinyimx_mysql"
            TIMEOUT 90
    )
endif()

if(TEST unread_projection_reader_integration_tests)
    set_tests_properties(
        unread_projection_reader_integration_tests
        PROPERTIES
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            LABELS "m16;integration;mysql;projection"
            RESOURCE_LOCK "tinyimx_mysql"
            TIMEOUT 90
    )
endif()

if(TEST unread_projection_cache_integration_tests)
    set_tests_properties(
        unread_projection_cache_integration_tests
        PROPERTIES
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            LABELS "m16;integration;mysql;redis;projection"
            RESOURCE_LOCK "tinyimx_mysql;tinyimx_redis"
            TIMEOUT 90
    )
endif()

###############################################################################
# Intentionally NOT registered:
#
#   rocketmq_transport_integration_tests
#   m16_fault_recovery_runtime_test
#
# Both remain explicit acceptance executables because they depend on live
# RocketMQ resources; the reliability runner additionally contains deliberate
# process-death fault semantics.
###############################################################################
