add_executable(contract_test_public_headers test_public_headers.cpp)
set_target_properties(contract_test_public_headers PROPERTIES OUTPUT_NAME test_public_headers)
target_link_libraries(contract_test_public_headers PRIVATE nodus_vision_core GTest::gtest_main)
target_compile_features(contract_test_public_headers PRIVATE cxx_std_17)
add_test(NAME contract_test_public_headers COMMAND contract_test_public_headers)

add_test(
    NAME contract_test_no_vendor_identifiers
    COMMAND ${CMAKE_COMMAND} -DHEADER_ROOT=${CMAKE_SOURCE_DIR}/include/nodus_vision -P
        ${CMAKE_CURRENT_SOURCE_DIR}/verify_public_headers.cmake
)

add_test(
    NAME contract_test_pilot_openapi_pin
    COMMAND ${CMAKE_COMMAND}
        -DPILOT_OPENAPI=${CMAKE_SOURCE_DIR}/schemas/pilot/v1/openapi.yaml
        -DPILOT_PROVENANCE=${CMAKE_SOURCE_DIR}/schemas/pilot/v1/provenance.json
        -P ${CMAKE_CURRENT_SOURCE_DIR}/verify_pilot_contract.cmake
)

add_test(
    NAME contract_test_vision_health_openapi
    COMMAND ${CMAKE_COMMAND}
        -DVISION_OPENAPI=${CMAKE_SOURCE_DIR}/schemas/vision/v1/openapi.yaml
        -P ${CMAKE_CURRENT_SOURCE_DIR}/verify_vision_health_openapi.cmake
)

add_test(
    NAME contract_test_vision_recording_openapi
    COMMAND ${CMAKE_COMMAND}
        -DVISION_OPENAPI=${CMAKE_SOURCE_DIR}/schemas/vision/v1/openapi.yaml
        -P ${CMAKE_CURRENT_SOURCE_DIR}/verify_vision_recording_openapi.cmake
)
