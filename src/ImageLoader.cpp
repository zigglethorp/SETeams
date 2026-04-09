#include "ImageLoader.h"

#include "HashGenerator.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <limits.h>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>
#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

using namespace std;

namespace {
// Returns the first directory from the candidate list that exists.
string FindExistingDirectory(const vector<filesystem::path>& candidates) {
    namespace fs = filesystem;
    for (const auto& candidate : candidates) {
        error_code ec;
        if (fs::exists(candidate, ec) && fs::is_directory(candidate, ec)) {
            return candidate.string();
        }
    }
    return "";
}

// Returns true when the path points to a .zip archive.
bool IsZipPath(const filesystem::path& path) {
    return HashGenerator::ToLower(path.extension().string()) == ".zip";
}

// Escapes a path for use inside a single-quoted PowerShell string literal.
string EscapePowerShellLiteral(string value) {
    size_t pos = 0;
    while ((pos = value.find('\'', pos)) != string::npos) {
        value.insert(pos, "'");
        pos += 2;
    }
    return value;
}

// Finds the actual Google Photos directory inside a Takeout extraction tree.
string FindGooglePhotosDirectory(const filesystem::path& basePath) {
    namespace fs = filesystem;
    error_code ec;
    if (!fs::exists(basePath, ec)) return "";

    const vector<fs::path> candidates = {
        basePath / "Takeout" / "Google Photos",
        basePath / "Google Photos"
    };
    const string directMatch = FindExistingDirectory(candidates);
    if (!directMatch.empty()) return directMatch;

    const string leaf = HashGenerator::ToLower(basePath.filename().string());
    if (leaf == "google photos" && fs::is_directory(basePath, ec)) {
        return basePath.string();
    }

    // Some Takeout exports add another nesting layer, so fall back to a wider search.
    for (const auto& entry : fs::recursive_directory_iterator(basePath, fs::directory_options::skip_permission_denied, ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        if (!entry.is_directory(ec)) continue;
        if (HashGenerator::ToLower(entry.path().filename().string()) == "google photos") {
            return entry.path().string();
        }
    }

    return "";
}

// Runs a hidden PowerShell command and waits for completion.
bool RunHiddenPowerShell(const string& commandLine) {
#if defined(_WIN32)
    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};

    vector<char> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back('\0');

    // CreateProcess avoids flashing a PowerShell window while the zip is extracted.
    const BOOL started = CreateProcessA(
        nullptr,
        mutableCommand.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &si,
        &pi);
    if (!started) return false;

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return exitCode == 0;
#else
    (void)commandLine;
    return false;
#endif
}
} // namespace

// Stores the logger callback so later load operations can report progress.
void ImageLoader::SetLogger(LoggerFn logger) {
    logger_ = std::move(logger);
}

// Sends a loader message to the active logger when one is available.
void ImageLoader::Log(const string& line) const {
    if (logger_) logger_(line);
}

// Probes a few common iCloud locations and returns the first folder that exists.
string ImageLoader::FindDefaultAppleFolder() const {
    const char* userProfile = getenv("USERPROFILE");
    if (userProfile == nullptr) return "";

    const filesystem::path base(userProfile);
    const vector<filesystem::path> candidates = {
        base / "Pictures" / "iCloud Photos" / "Photos",
        base / "Pictures" / "iCloud Photos",
        base / "iCloud Photos" / "Photos",
        base / "iCloud Photos"
    };
    return FindExistingDirectory(candidates);
}

// Probes common Downloads extraction locations for Google Takeout photo folders or zip archives.
string ImageLoader::FindDefaultGoogleFolder() const {
    namespace fs = filesystem;
    const char* userProfile = getenv("USERPROFILE");
    if (userProfile == nullptr) return "";

    const fs::path base(userProfile);
    const fs::path downloads = base / "Downloads";
    const vector<fs::path> fixedCandidates = {
        downloads / "Takeout" / "Google Photos",
        downloads / "Google Photos",
        base / "Pictures" / "Google Photos",
        downloads / "Takeout",
        downloads / "Google Photos"
    };

    string fixedMatch = "";
    for (const auto& candidate : fixedCandidates) {
        fixedMatch = FindGooglePhotosDirectory(candidate);
        if (!fixedMatch.empty()) return fixedMatch;
    }

    filesystem::path latestZip;
    fs::file_time_type latestZipTime{};

    error_code ec;
    if (!fs::exists(downloads, ec) || !fs::is_directory(downloads, ec)) return "";

    // If no extracted folder is obvious, prefer the newest Takeout zip in Downloads.
    for (const auto& entry : fs::directory_iterator(downloads, fs::directory_options::skip_permission_denied, ec)) {
        if (ec) {
            ec.clear();
            continue;
        }

        const string name = HashGenerator::ToLower(entry.path().filename().string());
        if (entry.is_directory(ec)) {
            if (name.rfind("takeout-", 0) == 0 || name == "takeout") {
                const string match = FindGooglePhotosDirectory(entry.path());
                if (!match.empty()) return match;
            }
            continue;
        }

        if (!entry.is_regular_file(ec) || !IsZipPath(entry.path())) continue;
        if (name.find("takeout") == string::npos) continue;

        const auto writeTime = entry.last_write_time(ec);
        if (!latestZip.empty() && writeTime <= latestZipTime) continue;
        latestZip = entry.path();
        latestZipTime = writeTime;
    }

    if (!latestZip.empty()) return latestZip.string();

    if (!fixedMatch.empty()) return fixedMatch;

    return "";
}

// Resolves a Google Takeout folder or zip archive to a folder that actually contains photos.
string ImageLoader::ResolveGoogleTakeoutRoot(const string& inputPath) const {
    namespace fs = filesystem;
    if (inputPath.empty()) return "";

    error_code ec;
    const fs::path path(inputPath);
    if (fs::is_directory(path, ec)) {
        const string match = FindGooglePhotosDirectory(path);
        if (match.empty()) {
            Log("Google Takeout path did not contain a Google Photos folder: " + inputPath);
        }
        return match;
    }

    ec.clear();
    if (!fs::is_regular_file(path, ec) || !IsZipPath(path)) return "";

    fs::path extractRoot = fs::temp_directory_path(ec);
    if (ec) extractRoot = path.parent_path();
    extractRoot /= "DuoSortExtractedTakeout";
    extractRoot /= path.stem();

    // Reuse a previous extraction so repeated scans do not keep unpacking the same archive.
    const string existingMatch = FindGooglePhotosDirectory(extractRoot);
    if (!existingMatch.empty()) {
        Log("Using previously extracted Google Takeout archive: " + existingMatch);
        return existingMatch;
    }

    fs::create_directories(extractRoot, ec);
    if (ec) {
        Log("Unable to create extraction folder for Google Takeout archive.");
        return "";
    }

    Log("Extracting Google Takeout archive: " + inputPath);
    const string command =
        "powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command \"& { "
        "Expand-Archive -LiteralPath '" + EscapePowerShellLiteral(path.string()) +
        "' -DestinationPath '" + EscapePowerShellLiteral(extractRoot.string()) +
        "' -Force }\"";
    if (!RunHiddenPowerShell(command)) {
        Log("Unable to extract Google Takeout zip automatically. Please extract it manually and browse to the Google Photos folder.");
        return "";
    }

    const string extractedMatch = FindGooglePhotosDirectory(extractRoot);
    if (extractedMatch.empty()) {
        Log("Google Photos folder was not found inside the extracted Takeout archive.");
        return "";
    }

    Log("Google Takeout extracted to: " + extractedMatch);
    return extractedMatch;
}

// Indexes one local folder and tags every discovered record with its source label.
size_t ImageLoader::LoadLocalPhotoFolder(const string& rootPath,
                                         const string& sourceLabel,
                                         vector<PhotoRecord>& photos) const {
    namespace fs = filesystem;
    if (rootPath.empty()) return 0;

    // Keep supported extensions in one place so Apple and Google local scans behave the same.
    static const unordered_set<string> exts = {
        ".jpg", ".jpeg", ".png", ".heic", ".heif", ".gif", ".bmp", ".webp", ".tiff"
    };

    error_code ec;
    if (!fs::exists(rootPath, ec) || !fs::is_directory(rootPath, ec)) return 0;

    fs::recursive_directory_iterator it(rootPath, fs::directory_options::skip_permission_denied, ec);
    fs::recursive_directory_iterator end;

    size_t count = 0;
    for (; it != end; it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }

        const auto& entry = *it;
        if (!entry.is_regular_file(ec)) continue;

        const string ext = HashGenerator::ToLower(entry.path().extension().string());
        if (!exts.count(ext)) continue;

        PhotoRecord rec;
        rec.source = sourceLabel;
        rec.localPath = entry.path().string();
        rec.fileName = entry.path().filename().string();
        rec.fileSize = entry.file_size(ec);
        rec.modifiedTime = fs::last_write_time(entry.path(), ec).time_since_epoch().count();
        // Hashes are computed during indexing so duplicate detection can stay purely in-memory.
        rec.hashes = HashGenerator::ComputeHashes(entry);
        photos.push_back(std::move(rec));
        ++count;
    }

    return count;
}

// Loads local Apple and Google Takeout files into one shared photo vector.
void ImageLoader::LoadPhotos(const string& appleRoot,
                             const string& googleRoot,
                             vector<PhotoRecord>& photos) const {
    // Each run rebuilds the in-memory catalog from scratch so the review results match the latest scan.
    photos.clear();

    const size_t appleCount = LoadLocalPhotoFolder(appleRoot, "apple", photos);
    Log("Apple photos indexed: " + to_string(appleCount));

    const string resolvedGoogleRoot = ResolveGoogleTakeoutRoot(googleRoot);
    const size_t googleCount = LoadLocalPhotoFolder(resolvedGoogleRoot, "google", photos);
    Log("Google Takeout photos indexed: " + to_string(googleCount));
}
