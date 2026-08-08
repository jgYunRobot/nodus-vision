file(READ "${VISION_OPENAPI}" openapi)

foreach(required_value IN ITEMS
        "version: 1.3.0"
        "required: [schema_version, state, server, camera, pilot, recording, last_error]"
        "pilot:"
        "enum: [disabled, registering, online, recovering, contract_fault, stopping, stopped]"
        "server_instance_id:"
        "type: [string, 'null']"
        "last_success_age_ms:"
        "type: [integer, 'null']"
        "maximum: 2147483647"
        "const: '1.3.0'")
    string(FIND "${openapi}" "${required_value}" value_index)
    if(value_index EQUAL -1)
        message(FATAL_ERROR "Vision OpenAPI health contract is missing: ${required_value}")
    endif()
endforeach()
