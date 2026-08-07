/**
 * @file query_serializer.cpp
 * @brief frame query response JSON을 구현한다.
 */

#include "query_serializer.hpp"

#include <array>
#include <boost/json.hpp>
#include <utility>

namespace nodus_vision {
namespace {

boost::json::object makeFrameIdentity(const FrameIdentity& identity) {
    boost::json::object frame;
    frame["capture_generation"] = identity.capture_generation;
    frame["frame_number"] = identity.frame_number;
    frame["capture_timestamp_ns"] = identity.capture_timestamp_ns;
    frame["capture_unix_epoch_ns"] = identity.capture_unix_epoch_ns;
    return frame;
}

boost::json::object makePixelRoi(const PixelRoi& roi) {
    boost::json::object value;
    value["x"] = roi.x;
    value["y"] = roi.y;
    value["width"] = roi.width;
    value["height"] = roi.height;
    return value;
}

boost::json::object makePoint(const std::array<float, 3>& point) {
    boost::json::object value;
    value["x"] = point.at(0);
    value["y"] = point.at(1);
    value["z"] = point.at(2);
    return value;
}

}  // namespace

std::string serializePixelPointResult(const PixelPointResult& result) {
    boost::json::object pixel;
    pixel["x"] = result.pixel_x;
    pixel["y"] = result.pixel_y;

    boost::json::object root;
    root["schema_version"] = 1;
    root["frame"] = makeFrameIdentity(result.identity);
    root["pixel"] = std::move(pixel);
    root["valid"] = result.valid;
    if (result.valid) {
        root["depth_m"] = result.depth_m;
        root["point_camera_m"] = makePoint(result.optical_point_m);
    } else {
        root["reason"] = result.invalid_reason;
    }
    return boost::json::serialize(root);
}

std::string serializeRoiDepthResult(const RoiDepthResult& result) {
    boost::json::object stats;
    stats["valid"] = result.valid;
    stats["pixel_count"] = result.pixel_count;
    stats["valid_pixel_count"] = result.valid_pixel_count;
    stats["fill_rate"] = result.fill_rate;
    stats["min_depth_m"] = result.min_depth_m;
    stats["max_depth_m"] = result.max_depth_m;
    stats["mean_depth_m"] = result.mean_depth_m;
    stats["median_depth_m"] = result.median_depth_m;

    boost::json::object median_pixel;
    median_pixel["x"] = result.median_pixel_x;
    median_pixel["y"] = result.median_pixel_y;

    boost::json::object root;
    root["schema_version"] = 1;
    root["frame"] = makeFrameIdentity(result.identity);
    root["requested_roi"] = makePixelRoi(result.requested_roi);
    root["roi"] = makePixelRoi(result.clamped_roi);
    root["stats"] = std::move(stats);
    root["median_pixel"] = std::move(median_pixel);
    root["median_point_camera_m"] = makePoint(result.median_optical_point_m);
    return boost::json::serialize(root);
}

}  // namespace nodus_vision
