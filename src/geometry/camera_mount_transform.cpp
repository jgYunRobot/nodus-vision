/**
 * @file camera_mount_transform.cpp
 * @brief 카메라 mount 고정 변환의 합성과 검증을 구현한다.
 */

#include "camera_mount_transform.hpp"

#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace nodus_vision {
namespace {

constexpr double ROTATION_TOLERANCE = 1.0e-9;

std::array<double, 16> makeIdentityMatrix() {
    return {1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0};
}

std::array<double, 16> multiplyMatrices(const std::array<double, 16>& lhs,
                                        const std::array<double, 16>& rhs) {
    std::array<double, 16> product{};
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            for (int index = 0; index < 4; ++index) {
                product.at(row * 4 + column) +=
                    lhs.at(row * 4 + index) * rhs.at(index * 4 + column);
            }
        }
    }
    return product;
}

std::array<double, 16> makeAxisRotation(char axis, double angle) {
    std::array<double, 16> rotation = makeIdentityMatrix();
    const double cosine = std::cos(angle);
    const double sine = std::sin(angle);
    if (axis == 'X') {
        rotation.at(5) = cosine;
        rotation.at(6) = -sine;
        rotation.at(9) = sine;
        rotation.at(10) = cosine;
        return rotation;
    }
    if (axis == 'Y') {
        rotation.at(0) = cosine;
        rotation.at(2) = sine;
        rotation.at(8) = -sine;
        rotation.at(10) = cosine;
        return rotation;
    }
    if (axis == 'Z') {
        rotation.at(0) = cosine;
        rotation.at(1) = -sine;
        rotation.at(4) = sine;
        rotation.at(5) = cosine;
        return rotation;
    }
    throw std::invalid_argument("Camera mount Euler axis is invalid.");
}

void validateMatrix(const std::array<double, 16>& matrix) {
    for (const double value : matrix) {
        if (!std::isfinite(value)) {
            throw std::invalid_argument("Camera mount matrix must be finite.");
        }
    }
    if (std::abs(matrix.at(12)) > ROTATION_TOLERANCE ||
        std::abs(matrix.at(13)) > ROTATION_TOLERANCE ||
        std::abs(matrix.at(14)) > ROTATION_TOLERANCE ||
        std::abs(matrix.at(15) - 1.0) > ROTATION_TOLERANCE) {
        throw std::invalid_argument("Camera mount matrix homogeneous row is invalid.");
    }

    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            double dot_product = 0.0;
            for (int index = 0; index < 3; ++index) {
                dot_product += matrix.at(row * 4 + index) * matrix.at(column * 4 + index);
            }
            const double expected = row == column ? 1.0 : 0.0;
            if (std::abs(dot_product - expected) > ROTATION_TOLERANCE) {
                throw std::invalid_argument("Camera mount rotation is not orthonormal.");
            }
        }
    }
    const double determinant =
        matrix.at(0) * (matrix.at(5) * matrix.at(10) - matrix.at(6) * matrix.at(9)) -
        matrix.at(1) * (matrix.at(4) * matrix.at(10) - matrix.at(6) * matrix.at(8)) +
        matrix.at(2) * (matrix.at(4) * matrix.at(9) - matrix.at(5) * matrix.at(8));
    if (std::abs(determinant - 1.0) > ROTATION_TOLERANCE) {
        throw std::invalid_argument("Camera mount rotation determinant is invalid.");
    }
}

}  // namespace

CameraMountTransform buildCameraMountTransform(const CalibrationConfig& calibration) {
    if (calibration.calibration_id.empty() || calibration.sensor_frame.empty() ||
        calibration.mount_frame.empty() ||
        calibration.mount_local_transform.euler_type.size() != 3U ||
        !std::isfinite(calibration.mount_local_transform.x) ||
        !std::isfinite(calibration.mount_local_transform.y) ||
        !std::isfinite(calibration.mount_local_transform.z) ||
        !std::isfinite(calibration.mount_local_transform.r1) ||
        !std::isfinite(calibration.mount_local_transform.r2) ||
        !std::isfinite(calibration.mount_local_transform.r3)) {
        throw std::invalid_argument("Camera mount calibration is invalid.");
    }

    std::array<double, 16> mount_from_camera_body = makeIdentityMatrix();
    const std::array<double, 3> angles = {calibration.mount_local_transform.r1,
                                          calibration.mount_local_transform.r2,
                                          calibration.mount_local_transform.r3};
    for (std::size_t index = 0; index < angles.size(); ++index) {
        mount_from_camera_body = multiplyMatrices(
            mount_from_camera_body,
            makeAxisRotation(calibration.mount_local_transform.euler_type.at(index),
                             angles.at(index)));
    }
    mount_from_camera_body.at(3) = calibration.mount_local_transform.x;
    mount_from_camera_body.at(7) = calibration.mount_local_transform.y;
    mount_from_camera_body.at(11) = calibration.mount_local_transform.z;

    const std::array<double, 16> camera_body_from_camera_optical = {
        1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, -1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0,
    };
    CameraMountTransform transform;
    transform.calibration_id = calibration.calibration_id;
    transform.sensor_frame = calibration.sensor_frame;
    transform.mount_frame = calibration.mount_frame;
    transform.mount_from_camera_optical_matrix4x4 =
        multiplyMatrices(mount_from_camera_body, camera_body_from_camera_optical);
    validateMatrix(transform.mount_from_camera_optical_matrix4x4);
    return transform;
}

std::array<float, 3> transformCameraPointToMount(const std::array<float, 3>& point_camera_optical,
                                                 const CameraMountTransform& transform) {
    for (const float coordinate : point_camera_optical) {
        if (!std::isfinite(coordinate)) {
            throw std::invalid_argument("Camera optical point must be finite.");
        }
    }
    validateMatrix(transform.mount_from_camera_optical_matrix4x4);
    std::array<float, 3> point_mount{};
    for (int row = 0; row < 3; ++row) {
        double transformed = transform.mount_from_camera_optical_matrix4x4.at(row * 4 + 3);
        for (int column = 0; column < 3; ++column) {
            transformed += transform.mount_from_camera_optical_matrix4x4.at(row * 4 + column) *
                           static_cast<double>(point_camera_optical.at(column));
        }
        if (!std::isfinite(transformed) || transformed > std::numeric_limits<float>::max() ||
            transformed < -std::numeric_limits<float>::max()) {
            throw std::invalid_argument("Camera mount point is outside float range.");
        }
        point_mount.at(row) = static_cast<float>(transformed);
    }
    return point_mount;
}

std::array<float, 12> buildMountFromCameraMatrix3x4(const CameraMountTransform& transform) {
    validateMatrix(transform.mount_from_camera_optical_matrix4x4);
    std::array<float, 12> matrix3x4{};
    for (int index = 0; index < 12; ++index) {
        const double value = transform.mount_from_camera_optical_matrix4x4.at(index);
        if (value > std::numeric_limits<float>::max() ||
            value < -std::numeric_limits<float>::max()) {
            throw std::invalid_argument("Camera mount matrix is outside float range.");
        }
        matrix3x4.at(index) = static_cast<float>(value);
    }
    return matrix3x4;
}

}  // namespace nodus_vision
