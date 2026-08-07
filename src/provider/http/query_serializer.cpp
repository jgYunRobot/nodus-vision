/** @file query_serializer.cpp @brief frame query response JSON을 구현한다. */
#include "query_serializer.hpp"
#include <sstream>
namespace nodus_vision {
std::string serializePixelPointResult(const PixelPointResult& r){std::ostringstream o;o<<"{\"schema_version\":1,\"frame_number\":"<<r.identity.frame_number<<",\"capture_generation\":"<<r.identity.capture_generation<<",\"x\":"<<r.pixel_x<<",\"y\":"<<r.pixel_y<<",\"valid\":"<<(r.valid?"true":"false");if(r.valid)o<<",\"depth_m\":"<<r.depth_m<<",\"optical_point_m\":["<<r.optical_point_m[0]<<","<<r.optical_point_m[1]<<","<<r.optical_point_m[2]<<"]";else o<<",\"reason\":\""<<r.invalid_reason<<"\"";o<<"}";return o.str();}
std::string serializeRoiDepthResult(const RoiDepthResult& r){std::ostringstream o;o<<"{\"schema_version\":1,\"frame_number\":"<<r.identity.frame_number<<",\"valid\":"<<(r.valid?"true":"false")<<",\"requested_roi\":["<<r.requested_roi.x<<","<<r.requested_roi.y<<","<<r.requested_roi.width<<","<<r.requested_roi.height<<"],\"clamped_roi\":["<<r.clamped_roi.x<<","<<r.clamped_roi.y<<","<<r.clamped_roi.width<<","<<r.clamped_roi.height<<"],\"valid_pixel_count\":"<<r.valid_pixel_count<<"}";return o.str();}
} // namespace nodus_vision
