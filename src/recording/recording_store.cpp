/** @file recording_store.cpp @brief recording artifact layout을 구현한다. */

#include "recording_store.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>

namespace nodus_vision {
namespace {

void rejectSymlink(const std::filesystem::path& path) {
    const std::filesystem::file_status status = std::filesystem::symlink_status(path);
    if (std::filesystem::is_symlink(status)) {
        throw std::invalid_argument("Recording managed directory must not be a symlink.");
    }
}

void syncDirectory(const std::filesystem::path& directory) {
    const int descriptor = open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (descriptor < 0) {
        throw std::runtime_error("Recording directory cannot be opened for synchronization.");
    }
    const int result = fsync(descriptor);
    close(descriptor);
    if (result != 0) {
        throw std::runtime_error("Recording directory synchronization failed.");
    }
}

void writeAtomically(const std::filesystem::path& directory, const std::string& filename,
                     const std::string& contents) {
    const std::filesystem::path temporary = directory / (filename + ".tmp");
    const int descriptor = open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0640);
    if (descriptor < 0) {
        throw std::runtime_error("Recording request temporary file cannot be created.");
    }
    const ssize_t written = write(descriptor, contents.data(), contents.size());
    const int sync_result = fsync(descriptor);
    const int close_result = close(descriptor);
    if (written != static_cast<ssize_t>(contents.size()) || sync_result != 0 || close_result != 0) {
        throw std::runtime_error("Recording request file write failed.");
    }
    std::error_code error;
    std::filesystem::rename(temporary, directory / filename, error);
    if (error) {
        throw std::runtime_error("Recording request file activation failed.");
    }
    syncDirectory(directory);
}

}  // namespace

RecordingStore::RecordingStore(std::filesystem::path root) : m_root(std::move(root)) {
    if (!m_root.is_absolute()) {
        throw std::invalid_argument("Recording root must be absolute.");
    }
}

void RecordingStore::initialize() {
    std::error_code error;
    std::filesystem::create_directories(m_root, error);
    if (error || !std::filesystem::is_directory(m_root)) {
        throw std::runtime_error("Recording root cannot be prepared.");
    }
    rejectSymlink(m_root);
    for (const char* child : {".staging", "finalized"}) {
        const std::filesystem::path path = m_root / child;
        std::filesystem::create_directory(path, error);
        if (error && error != std::errc::file_exists) {
            throw std::runtime_error("Recording managed directory cannot be prepared.");
        }
        if (!std::filesystem::is_directory(path)) {
            throw std::runtime_error("Recording managed path is not a directory.");
        }
        rejectSymlink(path);
    }
}

RecordingArtifactPaths RecordingStore::createStaging(const RecordingStartRequest& request) {
    if (!isRecordingIdValid(request.recording_id)) {
        throw std::invalid_argument("Recording ID is unsafe.");
    }
    initialize();
    RecordingArtifactPaths paths;
    paths.staging_directory = m_root / ".staging" / request.recording_id;
    paths.finalized_directory = m_root / "finalized" / request.recording_id;
    if (std::filesystem::exists(paths.finalized_directory)) {
        throw std::runtime_error("Finalized recording identity already exists.");
    }
    std::error_code error;
    if (!std::filesystem::create_directory(paths.staging_directory, error) || error) {
        throw std::runtime_error("Recording staging identity already exists or cannot be created.");
    }
    writeAtomically(paths.staging_directory, "start_request.json",
                    serializeRecordingStartRequest(request));
    return paths;
}

std::size_t RecordingStore::getStagingCount() const {
    const std::filesystem::path staging = m_root / ".staging";
    if (!std::filesystem::exists(staging)) {
        return 0U;
    }
    rejectSymlink(staging);
    std::size_t count = 0U;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(staging)) {
        if (entry.is_symlink()) {
            throw std::runtime_error("Recording staging child must not be a symlink.");
        }
        if (entry.is_directory()) {
            ++count;
        }
    }
    return count;
}

}  // namespace nodus_vision
