/** @file pcd1.cpp @brief PCD1 v2 little-endian codec을 구현한다. */
#include "pcd1.hpp"
#include <cstring>
#include <stdexcept>
namespace nodus_vision { namespace {
constexpr std::size_t HEADER_SIZE = 112U;
constexpr std::size_t POINT_SIZE = 15U;

template<typename T> void append(std::vector<std::uint8_t>& output, T value)
{
    const std::size_t offset = output.size();
    output.resize(offset + sizeof(T));
    std::memcpy(output.data() + offset, &value, sizeof(T));
}

template<typename T> T read(const std::vector<std::uint8_t>& input, std::size_t& offset)
{
    if (offset + sizeof(T) > input.size()) {
        throw std::invalid_argument("PCD1 payload is truncated.");
    }
    T value{};
    std::memcpy(&value, input.data() + offset, sizeof(T));
    offset += sizeof(T);
    return value;
}
}
std::vector<std::uint8_t> writePcd1V2(const PointCloudSnapshot& s){
 if(s.points.size()>(static_cast<std::size_t>(UINT32_MAX)-HEADER_SIZE)/POINT_SIZE)throw std::invalid_argument("PCD1 point count is too large.");
 std::vector<std::uint8_t> o; o.reserve(HEADER_SIZE+POINT_SIZE*s.points.size()); o.insert(o.end(),{'P','C','D','1'}); append<std::uint32_t>(o,2); append<std::uint64_t>(o,s.identity.frame_number); append<std::int64_t>(o,s.identity.capture_timestamp_ns); append<std::uint32_t>(o,s.source_profile.width); append<std::uint32_t>(o,s.source_profile.height); append<std::uint32_t>(o,s.requested_stride_pixels); append<std::uint32_t>(o,s.stride_pixels); append<std::uint32_t>(o,s.points.size()); append<std::uint32_t>(o,0); append<float>(o,s.source_intrinsics.fx);append<float>(o,s.source_intrinsics.fy);append<float>(o,s.source_intrinsics.ppx);append<float>(o,s.source_intrinsics.ppy); for(int i=0;i<12;++i)append<float>(o,(i%5==0)?1.F:0.F); for(const auto& p:s.points){append<float>(o,p.optical_point_m[0]);append<float>(o,p.optical_point_m[1]);append<float>(o,p.optical_point_m[2]);} for(const auto& p:s.points)o.insert(o.end(),p.color_rgb.begin(),p.color_rgb.end()); return o;
}
PointCloudSnapshot readPcd1V2(const std::vector<std::uint8_t>& b){
    if (b.size() < HEADER_SIZE || std::memcmp(b.data(), "PCD1", 4) != 0) {
        throw std::invalid_argument("PCD1 magic is invalid.");
    }
    std::size_t o = 4;
    if (read<std::uint32_t>(b, o) != 2) {
        throw std::invalid_argument("PCD1 version is invalid.");
    }
    PointCloudSnapshot s;
    s.identity.frame_number = read<std::uint64_t>(b, o);
    s.identity.capture_timestamp_ns = read<std::int64_t>(b, o);
    s.source_profile.width = read<std::uint32_t>(b, o);
    s.source_profile.height = read<std::uint32_t>(b, o);
    s.requested_stride_pixels = read<std::uint32_t>(b, o);
    s.stride_pixels = read<std::uint32_t>(b, o);
    const std::uint32_t point_count = read<std::uint32_t>(b, o);
    read<std::uint32_t>(b, o);
    s.source_intrinsics.fx = read<float>(b, o); s.source_intrinsics.fy = read<float>(b, o);
    s.source_intrinsics.ppx = read<float>(b, o); s.source_intrinsics.ppy = read<float>(b, o);
    for (int index = 0; index < 12; ++index) { read<float>(b, o); }
    if (b.size() != HEADER_SIZE + POINT_SIZE * static_cast<std::size_t>(point_count)) {
        throw std::invalid_argument("PCD1 length is invalid.");
    }
    s.points.resize(point_count);
    for (PointCloudPoint& point : s.points) {
        point.optical_point_m = {read<float>(b, o), read<float>(b, o), read<float>(b, o)};
    }
    for (PointCloudPoint& point : s.points) {
        point.color_rgb = {read<std::uint8_t>(b, o), read<std::uint8_t>(b, o), read<std::uint8_t>(b, o)};
    }
    return s;
}
} // namespace nodus_vision
