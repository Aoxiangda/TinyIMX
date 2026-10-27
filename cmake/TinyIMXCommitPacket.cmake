# Real SQL/transport fixtures are opt-in and need an owned resource database.
add_executable(private_outbox_commit_packet_tests EXCLUDE_FROM_ALL
    tests/outbox/private_outbox_commit_packet_test.cpp)
target_compile_features(private_outbox_commit_packet_tests PRIVATE cxx_std_20)
target_link_libraries(private_outbox_commit_packet_tests PRIVATE tinyimx_message_core tinyimx_config)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_executable(private_outbox_commit_packet_sanitizer_tests EXCLUDE_FROM_ALL
        tests/outbox/private_outbox_commit_packet_test.cpp
        common/db/MySqlConnection.cpp
        services/repository/MessageRepository.cpp
        services/outbox/OutboxRepository.cpp
        services/message/repository/MessageRepositoryAdapter.cpp)
    target_compile_features(private_outbox_commit_packet_sanitizer_tests PRIVATE cxx_std_20)
    target_compile_options(private_outbox_commit_packet_sanitizer_tests PRIVATE
        -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie)
    target_link_options(private_outbox_commit_packet_sanitizer_tests PRIVATE
        -fsanitize=address,undefined -no-pie)
    target_link_libraries(private_outbox_commit_packet_sanitizer_tests PRIVATE
        tinyimx_message_core tinyimx_config)
endif()
