/**
 * @file pcd1.cpp
 * @brief PCD1 v2 little-endian codec을 구현한다.
 */

#include "pcd1.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

namespace nodus_vision {
namespace {

constexpr std::size_t HEADER_SIZE = 112U;
constexpr std::size_t POINT_SIZE = 15U;
constexpr std::uint32_t FORMAT_VERSION = 2U;
constexpr float MATRIX_TOLERANCE = 1.0e-5F;

static_assert(sizeof(float) == 4U && std::numeric_limits<float>::is_iec559,
              "PCD1 v2 requires IEEE-754 binary32 float.");

void appendUint32(std::vector<std::uint8_t>& output, std::uint32_t value) {
    for (int byte_index = 0; byte_index < 4; ++byte_index) {
        output.push_back(static_cast<std::uint8_t>((value >> (byte_index * 8)) & 0xFFU));
    }
}

void appendUint64(std::vector<std::uint8_t>& output, std::uint64_t value) {
    for (int byte_index = 0; byte_index < 8; ++byte_index) {
        output.push_back(static_cast<std::uint8_t>((value >> (byte_index * 8)) & 0xFFU));
    }
}

void appendInt64(std::vector<std::uint8_t>& output, std::int64_t value) {
    std::uint64_t storage{0};
    std::memcpy(&storage, &value, sizeof(storage));
    appendUint64(output, storage);
}

void appendFloat32(std::vector<std::uint8_t>& output, float value) {
    std::uint32_t storage{0};
    std::memcpy(&storage, &value, sizeof(storage));
    appendUint32(output, storage);
}

std::uint32_t readUint32(const std::vector<std::uint8_t>& input, std::size_t& offset) {
    if (offset > input.size() || input.size() - offset < 4U) {
        throw std::invalid_argument("PCD1 payload is truncated.");
    }
    std::uint32_t value{0};
    for (int byte_index = 0; byte_index < 4; ++byte_index) {
        value |= static_cast<std::uint32_t>(input.at(offset++)) << (byte_index * 8);
    }
    return value;
}

std::uint64_t readUint64(const std::vector<std::uint8_t>& input, std::size_t& offset) {
    if (offset > input.size() || input.size() - offset < 8U) {
        throw std::invalid_argument("PCD1 payload is truncated.");
    }
    std::uint64_t value{0};
    for (int byte_index = 0; byte_index < 8; ++byte_index) {
        value |= static_cast<std::uint64_t>(input.at(offset++)) << (byte_index * 8);
    }
    return value;
}

std::int64_t readInt64(const std::vector<std::uint8_t>& input, std::size_t& offset) {
    const std::uint64_t storage = readUint64(input, offset);
    std::int64_t value{0};
    std::memcpy(&value, &storage, sizeof(value));
    return value;
}

float readFloat32(const std::vector<std::uint8_t>& input, std::size_t& offset) {
    const std::uint32_t storage = readUint32(input, offset);
    float value{0.0F};
    std::memcpy(&value, &storage, sizeof(value));
    return value;
}

void validatePointCloud(const PointCloudSnapshot& snapshot) {
    if (snapshot.source_profile.width <= 0 || snapshot.source_profile.height <= 0 ||
        snapshot.requested_stride_pixels <= 0 || snapshot.stride_pixels <= 0) {
        throw std::invalid_argument("PCD1 source dimensions and strides must be positive.");
    }
    if (snapshot.points.size() > std::numeric_limits<std::uint32_t>::max() ||
        snapshot.points.size() >
            (std::numeric_limits<std::size_t>::max() - HEADER_SIZE) / POINT_SIZE) {
        throw std::invalid_argument("PCD1 point count is too large.");
    }
    for (const PointCloudPoint& point : snapshot.points) {
        for (const float coordinate : point.optical_point_m) {
            if (!std::isfinite(coordinate)) {
                throw std::invalid_argument("PCD1 point coordinate must be finite.");
            }
        }
    }
    for (const float value : snapshot.mount_from_camera_optical_matrix3x4) {
        if (!std::isfinite(value)) {
            throw std::invalid_argument("PCD1 mount matrix value must be finite.");
        }
    }
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            float dot_product = 0.0F;
            for (int index = 0; index < 3; ++index) {
                dot_product += snapshot.mount_from_camera_optical_matrix3x4.at(row * 4 + index) *
                               snapshot.mount_from_camera_optical_matrix3x4.at(column * 4 + index);
            }
            const float expected = row == column ? 1.0F : 0.0F;
            if (std::abs(dot_product - expected) > MATRIX_TOLERANCE) {
                throw std::invalid_argument("PCD1 mount matrix rotation is not orthonormal.");
            }
        }
    }
    const std::array<float, 12>& matrix = snapshot.mount_from_camera_optical_matrix3x4;
    const float determinant =
        matrix.at(0) * (matrix.at(5) * matrix.at(10) - matrix.at(6) * matrix.at(9)) -
        matrix.at(1) * (matrix.at(4) * matrix.at(10) - matrix.at(6) * matrix.at(8)) +
        matrix.at(2) * (matrix.at(4) * matrix.at(9) - matrix.at(5) * matrix.at(8));
    if (std::abs(determinant - 1.0F) > MATRIX_TOLERANCE) {
        throw std::invalid_argument("PCD1 mount matrix rotation determinant is invalid.");
    }
}

}  // namespace

std::vector<std::uint8_t> writePcd1V2(const PointCloudSnapshot& snapshot) {
    validatePointCloud(snapshot);
    std::vector<std::uint8_t> output;
    output.reserve(HEADER_SIZE + POINT_SIZE * snapshot.points.size());
    output.insert(output.end(), {'P', 'C', 'D', '1'});
    appendUint32(output, FORMAT_VERSION);
    appendUint64(output, snapshot.identity.frame_number);
    appendInt64(output, snapshot.identity.capture_timestamp_ns);
    appendUint32(output, static_cast<std::uint32_t>(snapshot.source_profile.width));
    appendUint32(output, static_cast<std::uint32_t>(snapshot.source_profile.height));
    appendUint32(output, static_cast<std::uint32_t>(snapshot.requested_stride_pixels));
    appendUint32(output, static_cast<std::uint32_t>(snapshot.stride_pixels));
    appendUint32(output, static_cast<std::uint32_t>(snapshot.points.size()));
    appendUint32(output, 0U);
    appendFloat32(output, snapshot.source_intrinsics.fx);
    appendFloat32(output, snapshot.source_intrinsics.fy);
    appendFloat32(output, snapshot.source_intrinsics.ppx);
    appendFloat32(output, snapshot.source_intrinsics.ppy);
    for (const float value : snapshot.mount_from_camera_optical_matrix3x4) {
        appendFloat32(output, value);
    }
    for (const PointCloudPoint& point : snapshot.points) {
        appendFloat32(output, point.optical_point_m.at(0));
        appendFloat32(output, point.optical_point_m.at(1));
        appendFloat32(output, point.optical_point_m.at(2));
    }
    for (const PointCloudPoint& point : snapshot.points) {
        output.insert(output.end(), point.color_rgb.begin(), point.color_rgb.end());
    }
    return output;
}

PointCloudSnapshot readPcd1V2(const std::vector<std::uint8_t>& input) {
    if (input.size() < HEADER_SIZE || std::memcmp(input.data(), "PCD1", 4U) != 0) {
        throw std::invalid_argument("PCD1 magic is invalid.");
    }
    std::size_t offset = 4U;
    if (readUint32(input, offset) != FORMAT_VERSION) {
        throw std::invalid_argument("PCD1 version is invalid.");
    }

    PointCloudSnapshot snapshot;
    snapshot.identity.frame_number = readUint64(input, offset);
    snapshot.identity.capture_timestamp_ns = readInt64(input, offset);
    snapshot.source_profile.width = static_cast<int>(readUint32(input, offset));
    snapshot.source_profile.height = static_cast<int>(readUint32(input, offset));
    snapshot.requested_stride_pixels = static_cast<int>(readUint32(input, offset));
    snapshot.stride_pixels = static_cast<int>(readUint32(input, offset));
    const std::uint32_t point_count = readUint32(input, offset);
    if (readUint32(input, offset) != 0U) {
        throw std::invalid_argument("PCD1 reserved field is invalid.");
    }
    snapshot.source_intrinsics.fx = readFloat32(input, offset);
    snapshot.source_intrinsics.fy = readFloat32(input, offset);
    snapshot.source_intrinsics.ppx = readFloat32(input, offset);
    snapshot.source_intrinsics.ppy = readFloat32(input, offset);
    for (float& value : snapshot.mount_from_camera_optical_matrix3x4) {
        value = readFloat32(input, offset);
    }

    const std::size_t expected_size =
        HEADER_SIZE + POINT_SIZE * static_cast<std::size_t>(point_count);
    if (input.size() != expected_size) {
        throw std::invalid_argument("PCD1 length is invalid.");
    }
    snapshot.points.resize(point_count);
    for (PointCloudPoint& point : snapshot.points) {
        point.optical_point_m = {
            readFloat32(input, offset),
            readFloat32(input, offset),
            readFloat32(input, offset),
        };
    }
    for (PointCloudPoint& point : snapshot.points) {
        point.color_rgb = {
            input.at(offset++),
            input.at(offset++),
            input.at(offset++),
        };
    }
    validatePointCloud(snapshot);
    return snapshot;
}

}  // namespace nodus_vision
