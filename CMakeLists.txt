cmake_minimum_required(VERSION 3.20)

project(trafficmon VERSION 0.1.0 LANGUAGES C)

option(TRAFFICMON_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_library(trafficmon_core STATIC
    src/aggregator.c
    src/classifier.c
    src/collector.c
    src/conntrack_parser.c
    src/csv_writer.c
    src/daemon.c
    src/database.c
    src/device_query.c
    src/flow.c
    src/identity.c
    src/ipc_protocol.c
    src/ipc_client.c
    src/ipc_server.c
    src/ipc_socket.c
    src/json_writer.c
    src/period.c
    src/period_store.c
    src/protocol_db.c
    src/query.c
    src/storage.c
    src/subnet.c
    src/text_buffer.c
)
target_include_directories(trafficmon_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
if(UNIX)
    target_compile_definitions(trafficmon_core PRIVATE _POSIX_C_SOURCE=200809L)
endif()
if(WIN32 AND CMAKE_C_COMPILER_ID STREQUAL "GNU")
    # GCC's POSIX fd model treats Winsock's unsigned SOCKET/INVALID_SOCKET as a Unix int fd,
    # producing contradictory leak/use-without-check diagnostics. Keep all other analyzer checks.
    set_source_files_properties(src/ipc_socket.c PROPERTIES COMPILE_OPTIONS
        "-Wno-analyzer-fd-leak;-Wno-analyzer-fd-use-without-check")
endif()

add_executable(trafficmon src/main.c)
target_link_libraries(trafficmon PRIVATE trafficmon_core)

add_executable(trafficctl cli/trafficctl.c src/cli.c)
target_link_libraries(trafficctl PRIVATE trafficmon_core)

install(TARGETS trafficmon trafficctl RUNTIME DESTINATION bin)

if(MSVC)
    set(TRAFFICMON_WARNING_FLAGS /W4 /WX)
else()
    set(TRAFFICMON_WARNING_FLAGS -Wall -Wextra -Wpedantic -Werror)
endif()

target_compile_options(trafficmon_core PRIVATE ${TRAFFICMON_WARNING_FLAGS})
target_compile_options(trafficmon PRIVATE ${TRAFFICMON_WARNING_FLAGS})
target_compile_options(trafficctl PRIVATE ${TRAFFICMON_WARNING_FLAGS})

if(WIN32)
    target_link_libraries(trafficmon_core PUBLIC ws2_32)
endif()

if(TRAFFICMON_ENABLE_SANITIZERS)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang" OR
       (CMAKE_C_COMPILER_ID STREQUAL "GNU" AND NOT WIN32))
        set(TRAFFICMON_SANITIZER_FLAGS -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_compile_options(trafficmon_core PRIVATE ${TRAFFICMON_SANITIZER_FLAGS})
        target_link_options(trafficmon_core INTERFACE ${TRAFFICMON_SANITIZER_FLAGS})
    else()
        message(FATAL_ERROR "Sanitizers require Clang, or GCC on a non-Windows host")
    endif()
endif()

if(WIN32 AND CMAKE_C_COMPILER_ID MATCHES "Clang" AND TRAFFICMON_ENABLE_SANITIZERS)
    get_filename_component(TM_CLANG_RUNTIME_DIR "${CMAKE_C_COMPILER}" DIRECTORY)
    function(tm_copy_sanitizer_runtime target_name)
        foreach(runtime_name libclang_rt.asan_dynamic-x86_64.dll libc++.dll libunwind.dll)
            add_custom_command(TARGET ${target_name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${TM_CLANG_RUNTIME_DIR}/${runtime_name}"
                        "$<TARGET_FILE_DIR:${target_name}>"
                VERBATIM)
        endforeach()
    endfunction()
    tm_copy_sanitizer_runtime(trafficmon)
    tm_copy_sanitizer_runtime(trafficctl)
endif()

include(CTest)
if(BUILD_TESTING)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_executable(trafficmon_identity_bench tests/identity_bench.c)
        target_link_libraries(trafficmon_identity_bench PRIVATE trafficmon_core)
        target_compile_options(trafficmon_identity_bench PRIVATE ${TRAFFICMON_WARNING_FLAGS})
    endif()
    add_executable(trafficmon_test src/main.c)
    target_link_libraries(trafficmon_test PRIVATE trafficmon_core)
    target_compile_options(trafficmon_test PRIVATE ${TRAFFICMON_WARNING_FLAGS})
    target_compile_definitions(trafficmon_test PRIVATE TRAFFICMON_ENABLE_TEST_HOOKS=1)

    add_executable(trafficctl_test cli/trafficctl.c src/cli.c)
    target_link_libraries(trafficctl_test PRIVATE trafficmon_core)
    target_compile_options(trafficctl_test PRIVATE ${TRAFFICMON_WARNING_FLAGS})
    target_compile_definitions(trafficctl_test PRIVATE TRAFFICMON_ENABLE_TEST_HOOKS=1)
    if(TRAFFICMON_ENABLE_SANITIZERS)
        target_compile_options(trafficmon_test PRIVATE ${TRAFFICMON_SANITIZER_FLAGS})
        target_link_options(trafficmon_test PRIVATE ${TRAFFICMON_SANITIZER_FLAGS})
        target_compile_options(trafficctl_test PRIVATE ${TRAFFICMON_SANITIZER_FLAGS})
        target_link_options(trafficctl_test PRIVATE ${TRAFFICMON_SANITIZER_FLAGS})
    endif()
    if(COMMAND tm_copy_sanitizer_runtime)
        tm_copy_sanitizer_runtime(trafficmon_test)
        tm_copy_sanitizer_runtime(trafficctl_test)
    endif()
    add_subdirectory(tests)
endif()
