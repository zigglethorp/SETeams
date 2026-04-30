#include "duosort_engine.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <utility>

using namespace std;

namespace {
// Google input can be either an extracted folder or the original Takeout zip.
bool IsZipArchivePath(const filesystem::path& path) {
    string ext = path.extension().string();
    transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
        return static_cast<char>(tolower(ch));
    });
    return ext == ".zip";
}

string LastWindowsErrorMessage(DWORD errorCode) {
    if (errorCode == ERROR_SUCCESS) return "";

    LPSTR buffer = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD length = FormatMessageA(flags, nullptr, errorCode, 0, reinterpret_cast<LPSTR>(&buffer), 0, nullptr);
    string message = length > 0 && buffer ? string(buffer, length) : "Windows error " + to_string(errorCode);
    if (buffer) LocalFree(buffer);
    while (!message.empty() && (message.back() == '\r' || message.back() == '\n' || message.back() == '.')) {
        message.pop_back();
    }
    return message;
}

bool DeleteLocalFile(const string& localPath, string& failureReason) {
    namespace fs = filesystem;

    error_code ec;
    if (fs::remove(localPath, ec)) return true;
    if (!ec) {
        failureReason = "File was not found";
        return false;
    }

    const filesystem::path path(localPath);
    const wstring widePath = path.wstring();
    DWORD attributes = GetFileAttributesW(widePath.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_READONLY)) {
        SetFileAttributesW(widePath.c_str(), attributes & ~FILE_ATTRIBUTE_READONLY);
    }

    if (DeleteFileW(widePath.c_str())) return true;

    const DWORD winError = GetLastError();
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_READONLY)) {
        SetFileAttributesW(widePath.c_str(), attributes);
    }

    failureReason = LastWindowsErrorMessage(winError);
    if (failureReason.empty()) failureReason = ec.message();
    return false;
}
} // namespace

// Builds the engine with its helper classes ready for logger injection.
DuoSortEngine::DuoSortEngine() = default;

// Stores the logger and shares it with helper modules that also report progress.
void DuoSortEngine::SetLogger(LoggerFn logger) {
    logger_ = std::move(logger);
    imageLoader_.SetLogger(logger_);
}

// Updates the Apple source folder selected in the UI.
void DuoSortEngine::SetAppleRoot(std::string rootPath) {
    appleRoot_ = std::move(rootPath);
}

// Updates the Google Takeout source folder selected in the UI.
void DuoSortEngine::SetGoogleRoot(std::string rootPath) {
    googleRoot_ = std::move(rootPath);
}

// Returns how many Apple and Google photo rows are currently loaded.
size_t DuoSortEngine::PhotoCount() const {
    return photos_.size();
}

// Returns the number of duplicate groups produced by the last run.
size_t DuoSortEngine::DuplicateGroupCount() const {
    return duplicateGroups_.size();
}

// Exposes the current photo list for review rendering.
const vector<PhotoRecord>& DuoSortEngine::Photos() const {
    return photos_;
}

// Exposes the current duplicate groups for review navigation.
const vector<vector<size_t>>& DuoSortEngine::DuplicateGroups() const {
    return duplicateGroups_;
}

// Deletes a local photo after the user confirms the action in the review UI.
bool DuoSortEngine::DeletePhoto(size_t photoIndex, string& message) {
    if (photoIndex >= photos_.size()) {
        message = "Invalid photo selection.";
        return false;
    }

    auto& photo = photos_[photoIndex];
    if (photo.localPath.empty()) {
        message = "This photo does not have a local file path.";
        return false;
    }
    if (photo.deleted) {
        message = "That photo was already deleted.";
        return false;
    }

    // Delete from disk first, then mark the in-memory record so the review UI can skip it.
    string failureReason;
    if (!DeleteLocalFile(photo.localPath, failureReason)) {
        message = "Unable to delete file: " + photo.localPath;
        if (!failureReason.empty()) message += " (" + failureReason + ")";
        return false;
    }

    photo.deleted = true;
    message = "Deleted: " + photo.localPath;
    Log(message);
    return true;
}

// Sends a log line to the configured logger callback.
void DuoSortEngine::Log(const string& line) const {
    if (logger_) logger_(line);
}

// Delegates default-folder discovery to ImageLoader so folder logic stays in one place.
string DuoSortEngine::FindDefaultAppleFolder() const {
    return imageLoader_.FindDefaultAppleFolder();
}

// Delegates Google Takeout discovery to ImageLoader so folder logic stays in one place.
string DuoSortEngine::FindDefaultGoogleFolder() const {
    return imageLoader_.FindDefaultGoogleFolder();
}

// Loads Apple and Google Takeout files into the shared in-memory catalog.
void DuoSortEngine::Ingestor() {
    imageLoader_.LoadPhotos(appleRoot_, googleRoot_, photos_);
}

// Runs duplicate detection across the current in-memory photo catalog.
void DuoSortEngine::Dedup() {
    DuplicateFinder::BuildDuplicateGroups(photos_, duplicateGroups_, [this](const string& line) { Log(line); });
}

// Executes the full scan and duplicate-detection pipeline.
void DuoSortEngine::Run() {
    namespace fs = filesystem;
    error_code ec;
    const bool hasApple = !appleRoot_.empty() && fs::exists(appleRoot_, ec) && fs::is_directory(appleRoot_, ec);
    ec.clear();
    const fs::path googlePath(googleRoot_);
    // Google Takeout can arrive as either a folder or a zip the loader knows how to unpack.
    const bool hasGoogle = !googleRoot_.empty() && fs::exists(googlePath, ec) &&
                           (fs::is_directory(googlePath, ec) ||
                            (fs::is_regular_file(googlePath, ec) && IsZipArchivePath(googlePath)));

    if (!hasApple && !hasGoogle) {
        Log("No valid photo folders were found automatically.");
        Log("Please browse to your Apple/iCloud folder or your extracted Google Takeout\\Google Photos folder and try again.");
        return;
    }

    // Clear invalid roots so the loader only sees sources that passed validation here.
    if (!hasApple) appleRoot_.clear();
    if (!hasGoogle) googleRoot_.clear();

    // Keep orchestration explicit in one place for easier debugging.
    Log("Running DuoSort...");
    if (hasApple) Log("Apple source: " + appleRoot_);
    if (hasGoogle) Log("Google source: " + googleRoot_);
    Ingestor();
    Dedup();
    Log("Run complete.");
}

// Executes the ingestion and duplicate scan without writing the SQLite catalog.
void DuoSortEngine::RunScanOnly() {
    namespace fs = filesystem;
    error_code ec;
    const bool hasApple = !appleRoot_.empty() && fs::exists(appleRoot_, ec) && fs::is_directory(appleRoot_, ec);
    ec.clear();
    const fs::path googlePath(googleRoot_);
    // Keep scan-only validation identical to the main run path.
    const bool hasGoogle = !googleRoot_.empty() && fs::exists(googlePath, ec) &&
                           (fs::is_directory(googlePath, ec) ||
                            (fs::is_regular_file(googlePath, ec) && IsZipArchivePath(googlePath)));

    if (!hasApple && !hasGoogle) {
        Log("No valid photo folders were found automatically.");
        Log("Please browse to your Apple/iCloud folder or your extracted Google Takeout\\Google Photos folder and try again.");
        return;
    }

    if (!hasApple) appleRoot_.clear();
    if (!hasGoogle) googleRoot_.clear();

    Log("Running DuoSort scan-only mode...");
    if (hasApple) Log("Apple source: " + appleRoot_);
    if (hasGoogle) Log("Google source: " + googleRoot_);
    Ingestor();
    Dedup();
    Log("Scan complete.");
}
