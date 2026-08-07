add_library(
    nodus_vision_pilot_contracts
    STATIC
    pilot_contract_codec.cpp
    pilot_http_transport.cpp
    vision_endpoint_catalog.cpp
)
target_include_directories(nodus_vision_pilot_contracts PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(nodus_vision_pilot_contracts PUBLIC nodus_vision_config Boost::json)
target_compile_features(nodus_vision_pilot_contracts PUBLIC cxx_std_17)
target_compile_options(nodus_vision_pilot_contracts PRIVATE -Wall -Wextra)
