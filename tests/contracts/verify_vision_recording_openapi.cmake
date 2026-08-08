file(READ "${VISION_OPENAPI}" openapi)

foreach(required_value IN ITEMS
        "recording-start:"
        "request_schema_id: nodus.vision.recording.start.request.v1"
        "response_schema_id: nodus.vision.recording.start.response.v1"
        "recording-stop:"
        "request_schema_id: nodus.vision.recording.stop.request.v1"
        "response_schema_id: nodus.vision.recording.stop.response.v1"
        "recording-current:"
        "response_schema_id: nodus.vision.recording.current.response.v1"
        "/recordings/start:"
        "/recordings/stop:"
        "/recordings/current:"
        "RecordingStartRequest:"
        "RecordingStopRequest:"
        "RecordingCommandResponse:"
        "RecordingCurrentResponse:"
        "RecordingHealth:")
    string(FIND "${openapi}" "${required_value}" value_index)
    if(value_index EQUAL -1)
        message(FATAL_ERROR "Vision OpenAPI recording contract is missing: ${required_value}")
    endif()
endforeach()
