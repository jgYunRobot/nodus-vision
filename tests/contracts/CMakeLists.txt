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
