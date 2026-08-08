/** @file recording_store.cpp @brief recording artifact layout을 구현한다. */

#include "recording_store.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <optional>
#include <stdexcept>

namespace nodus_vision {
namespace {

constexpr std::uintmax_t MAX_REQUEST_FILE_BYTES = 1048576U;

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
    std::size_t total_written = 0U;
    while (total_written < contents.size()) {
        const ssize_t written =
            write(descriptor, contents.data() + total_written, contents.size() - total_written);
        if (written > 0) {
            total_written += static_cast<std::size_t>(written);
            continue;
        }
        if (written < 0 && errno == EINTR) {
            continue;
        }
        close(descriptor);
        throw std::runtime_error("Recording request file write failed.");
    }
    const int sync_result = fsync(descriptor);
    const int close_result = close(descriptor);
    if (sync_result != 0 || close_result != 0) {
        throw std::runtime_error("Recording request file write failed.");
    }
    std::error_code error;
    std::filesystem::rename(temporary, directory / filename, error);
    if (error) {
        throw std::runtime_error("Recording request file activation failed.");
    }
    syncDirectory(directory);
}

std::string readBoundedRegularFile(const std::filesystem::path& path) {
    const std::filesystem::file_status status = std::filesystem::symlink_status(path);
    if (std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status)) {
        throw std::runtime_error("Recording request evidence must be a regular file.");
    }
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error || size > MAX_REQUEST_FILE_BYTES) {
        throw std::runtime_error("Recording request evidence exceeds its bounded size.");
    }
    std::ifstream input(path, std::ios::in | std::ios::binary);
    if (!input.is_open()) {
        throw std::runtime_error("Recording request evidence cannot be opened.");
    }
    std::string contents(static_cast<std::size_t>(size), '\0');
    input.read(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!input || input.gcount() != static_cast<std::streamsize>(contents.size())) {
        throw std::runtime_error("Recording request evidence cannot be read completely.");
    }
    return contents;
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

void RecordingStore::activateFinalized(const RecordingArtifactPaths& paths) {
    const std::filesystem::path staging_parent = m_root / ".staging";
    const std::filesystem::path finalized_parent = m_root / "finalized";
    if (paths.staging_directory.parent_path() != staging_parent ||
        paths.finalized_directory.parent_path() != finalized_parent ||
        !std::filesystem::is_directory(paths.staging_directory) ||
        std::filesystem::exists(paths.finalized_directory)) {
        throw std::runtime_error("Recording artifact activation paths are invalid.");
    }
    rejectSymlink(paths.staging_directory);
    std::error_code error;
    std::filesystem::rename(paths.staging_directory, paths.finalized_directory, error);
    if (error) {
        throw std::runtime_error("Recording artifact activation failed.");
    }
    syncDirectory(staging_parent);
    syncDirectory(finalized_parent);
}

void RecordingStore::writeStopRequest(const RecordingArtifactPaths& paths,
                                      const std::string& contents) {
    if (paths.staging_directory.parent_path() != m_root / ".staging" ||
        std::filesystem::is_symlink(std::filesystem::symlink_status(paths.staging_directory))) {
        throw std::invalid_argument("Recording stop staging path is unsafe.");
    }
    writeAtomically(paths.staging_directory, "stop_request.json", contents);
}

void RecordingStore::writeFinalizedManifest(const RecordingArtifactPaths& paths,
                                            const std::string& contents) {
    if (paths.staging_directory.parent_path() != m_root / ".staging" ||
        std::filesystem::is_symlink(std::filesystem::symlink_status(paths.staging_directory))) {
        throw std::invalid_argument("Recording manifest staging path is unsafe.");
    }
    writeAtomically(paths.staging_directory, "recording_manifest.json", contents);
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

std::optional<PersistedRecordingArtifact> RecordingStore::findPersistedArtifact(
    const std::string& recording_id) const {
    if (!isRecordingIdValid(recording_id)) {
        throw std::invalid_argument("Recording ID is unsafe.");
    }
    const RecordingArtifactPaths paths{m_root / ".staging" / recording_id,
                                       m_root / "finalized" / recording_id};
    if (std::filesystem::exists(m_root)) {
        rejectSymlink(m_root);
    }
    for (const std::filesystem::path& managed_parent :
         {m_root / ".staging", m_root / "finalized"}) {
        if (std::filesystem::exists(managed_parent)) {
            rejectSymlink(managed_parent);
        }
    }
    const bool staging_exists = std::filesystem::exists(paths.staging_directory);
    const bool finalized_exists = std::filesystem::exists(paths.finalized_directory);
    if (staging_exists && finalized_exists) {
        throw std::runtime_error("Recording identity exists in staging and finalized storage.");
    }
    if (!staging_exists && !finalized_exists) {
        return std::nullopt;
    }
    const bool finalized = finalized_exists;
    const std::filesystem::path& directory =
        finalized ? paths.finalized_directory : paths.staging_directory;
    rejectSymlink(directory);
    if (!std::filesystem::is_directory(directory)) {
        throw std::runtime_error("Recording artifact identity is not a directory.");
    }
    PersistedRecordingArtifact artifact;
    artifact.paths = paths;
    artifact.finalized = finalized;
    artifact.start_request = readBoundedRegularFile(directory / "start_request.json");
    const std::filesystem::path stop_request = directory / "stop_request.json";
    if (std::filesystem::exists(stop_request)) {
        artifact.stop_request = readBoundedRegularFile(stop_request);
    }
    return artifact;
}

}  // namespace nodus_vision
