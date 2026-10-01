find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(GENERATED_VECTOR_HEADER ${CMAKE_CURRENT_BINARY_DIR}/generated_vectors.h)
add_custom_command(
    OUTPUT ${GENERATED_VECTOR_HEADER}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/reference_model.py
            ${CMAKE_CURRENT_SOURCE_DIR}/vectors.json --emit-c ${GENERATED_VECTOR_HEADER}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/reference_model.py ${CMAKE_CURRENT_SOURCE_DIR}/vectors.json
    VERBATIM
)

add_executable(trafficmon_tests
    ${GENERATED_VECTOR_HEADER}
    test_main.c
    integration/test_oracle_vectors.c
    unit/test_aggregator.c
    unit/test_database.c
    unit/test_flow.c
    unit/test_period.c
    unit/test_period_store.c
    unit/test_ipc_protocol.c
    unit/test_identity.c
    unit/test_protocol_db.c
    unit/test_query.c
    unit/test_subnet.c
    unit/test_writers.c
)
if(UNIX AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(trafficmon_tests PRIVATE unit/test_conntrack_parser.c)
    target_compile_definitions(trafficmon_tests PRIVATE TM_HAVE_CONNTRACK_TESTS=1)
endif()
target_include_directories(trafficmon_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(trafficmon_tests PRIVATE trafficmon_core)
target_compile_options(trafficmon_tests PRIVATE ${TRAFFICMON_WARNING_FLAGS})
if(TRAFFICMON_ENABLE_SANITIZERS)
    target_compile_options(trafficmon_tests PRIVATE ${TRAFFICMON_SANITIZER_FLAGS})
    target_link_options(trafficmon_tests PRIVATE ${TRAFFICMON_SANITIZER_FLAGS})
endif()
target_compile_definitions(trafficmon_tests PRIVATE TM_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(trafficmon_tests PRIVATE TM_TEST_BINARY_DIR="${CMAKE_CURRENT_BINARY_DIR}")

add_test(NAME core COMMAND trafficmon_tests)
add_test(NAME reference_oracle
         COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/reference_model.py
                 ${CMAKE_CURRENT_SOURCE_DIR}/vectors.json)

add_test(NAME cli_integration
         COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_cli.py
                 --daemon $<TARGET_FILE:trafficmon_test>
                 --cli $<TARGET_FILE:trafficctl_test>
                 --workdir ${CMAKE_CURRENT_BINARY_DIR}/cli-integration)
set_tests_properties(cli_integration PROPERTIES RUN_SERIAL TRUE TIMEOUT 30)
set_tests_properties(core PROPERTIES LABELS "unit")
set_tests_properties(reference_oracle PROPERTIES LABELS "unit")
set_tests_properties(cli_integration PROPERTIES LABELS "integration")

find_program(TRAFFICMON_NODE_EXECUTABLE node)
if(TRAFFICMON_NODE_EXECUTABLE)
    add_test(NAME luci_xss COMMAND ${TRAFFICMON_NODE_EXECUTABLE}
             ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_xss.js)
    add_test(NAME luci_pagination COMMAND ${TRAFFICMON_NODE_EXECUTABLE}
             ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_pagination.js)
    add_test(NAME luci_detail_settings COMMAND ${TRAFFICMON_NODE_EXECUTABLE}
             ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_detail_settings.js)
    set_tests_properties(luci_xss luci_pagination luci_detail_settings
                         PROPERTIES LABELS "luci;unit")
endif()

if(UNIX)
    add_test(NAME network_signature
             COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_network_signature.sh
                     ${CMAKE_SOURCE_DIR}/openwrt/trafficmon/files/network-signature.sh
                     ${CMAKE_SOURCE_DIR}/openwrt/trafficmon/files/95-trafficmon
                     ${CMAKE_CURRENT_BINARY_DIR}/network-signature)
    set_tests_properties(network_signature PROPERTIES
                         LABELS "integration;openwrt")
    add_test(NAME signal_integration
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_signals.py
                     --daemon $<TARGET_FILE:trafficmon_test>
                     --cli $<TARGET_FILE:trafficctl_test>
                     --workdir ${CMAKE_CURRENT_BINARY_DIR}/signal-integration)
    set_tests_properties(signal_integration PROPERTIES
                         RUN_SERIAL TRUE TIMEOUT 30 LABELS "integration;linux")
endif()

if(UNIX AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_test(NAME conntrack_namespace
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_conntrack_namespace.py
                     --daemon $<TARGET_FILE:trafficmon_test>
                     --cli $<TARGET_FILE:trafficctl_test>
                     --workdir ${CMAKE_CURRENT_BINARY_DIR}/conntrack-namespace)
    set_tests_properties(conntrack_namespace PROPERTIES
                         TIMEOUT 90 SKIP_RETURN_CODE 77
                         LABELS "integration;linux;privileged;conntrack"
                         RUN_SERIAL TRUE)
endif()

if(COMMAND tm_copy_sanitizer_runtime)
    tm_copy_sanitizer_runtime(trafficmon_tests)
endif()
