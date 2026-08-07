set(expected_sha256 "4c536a3b099a478f6af5420af63b5a32965913f8fc5943c655a55e8d2a3ed5a8")

file(SHA256 "${PILOT_OPENAPI}" actual_sha256)
if(NOT actual_sha256 STREQUAL expected_sha256)
    message(FATAL_ERROR "Pilot OpenAPI digest does not match the pinned provenance.")
endif()

file(READ "${PILOT_PROVENANCE}" provenance)
foreach(required_value IN ITEMS
        "\"semantic_version\": \"1.0.2\""
        "\"sha256\": \"${expected_sha256}\"")
    string(FIND "${provenance}" "${required_value}" value_index)
    if(value_index EQUAL -1)
        message(FATAL_ERROR "Pilot provenance is missing a required pinned value.")
    endif()
endforeach()

file(READ "${PILOT_OPENAPI}" openapi)
foreach(required_value IN ITEMS
        "\"openapi\": \"3.1.0\""
        "\"version\": \"1.0.2\""
        "\"/api/v1/components/register\""
        "\"/api/v1/components/{session_id}/heartbeat\""
        "\"/api/v1/components/{session_id}/state\""
        "\"/api/v1/components/{session_id}/disconnect\""
        "\"/api/v1/components/{session_id}/endpoint-catalog\"")
    string(FIND "${openapi}" "${required_value}" value_index)
    if(value_index EQUAL -1)
        message(FATAL_ERROR "Pinned Pilot OpenAPI is missing a required public lifecycle route.")
    endif()
endforeach()
