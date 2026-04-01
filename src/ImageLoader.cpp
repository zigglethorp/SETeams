#include "ImageLoader.h"

#include "HashGenerator.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits.h>
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
// Removes surrounding whitespace so CSV parsing treats padded fields consistently.
string Trim(const string& text) {
    size_t start = 0;
    while (start < text.size() && isspace(static_cast<unsigned char>(text[start]))) ++start;
    size_t end = text.size();
    while (end > start && isspace(static_cast<unsigned char>(text[end - 1]))) --end;
    return text.substr(start, end - start);
}

// Splits a CSV line while honoring quoted fields and escaped quote pairs.
vector<string> ParseCsvLine(const string& line, char delimiter) {
    vector<string> cols;
    string cell;
    cell.reserve(line.size());
    bool inQuotes = false;

    for (size_t i = 0; i < line.size(); ++i) {
        const char ch = line[i];
        if (ch == '"') {
            if (inQuotes && i + 1 < line.size() && line[i + 1] == '"') {
                cell.push_back('"');
                ++i;
            } else {
                inQuotes = !inQuotes;
            }
        } else if (ch == delimiter && !inQuotes) {
            cols.push_back(Trim(cell));
            cell.clear();
        } else {
            cell.push_back(ch);
        }
    }

    cols.push_back(Trim(cell));
    return cols;
}

// Chooses the most likely delimiter by counting commas and semicolons in the header.
char DetectCsvDelimiter(const string& line) {
    const size_t commaCount = count(line.begin(), line.end(), ',');
    const size_t semicolonCount = count(line.begin(), line.end(), ';');
    return semicolonCount > commaCount ? ';' : ',';
}

// Resolves the executable folder so the CSV can be found next to the built app.
string GetExeDirectory() {
#if defined(_WIN32)
    char path[MAX_PATH] = {0};
    DWORD len = GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return "";
    return filesystem::path(path).parent_path().string();
#else
    char pathBuf[PATH_MAX] = {0};
    const ssize_t len = readlink("/proc/self/exe", pathBuf, sizeof(pathBuf) - 1);
    if (len <= 0 || static_cast<size_t>(len) >= sizeof(pathBuf)) return "";
    pathBuf[len] = '\0';
    return filesystem::path(pathBuf).parent_path().string();
#endif
}

// Resolves the current working directory for local command-line and script runs.
string GetWorkingDirectory() {
    error_code ec;
    const filesystem::path path = filesystem::current_path(ec);
    if (ec) return "";
    return path.string();
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
    namespace fs = filesystem;
    const char* userProfile = getenv("USERPROFILE");
    vector<string> candidates;
    if (userProfile != nullptr) {
        const string base(userProfile);
        candidates.push_back(base + "\\Pictures\\iCloud Photos\\Photos");
        candidates.push_back(base + "\\Pictures\\iCloud Photos");
        candidates.push_back(base + "\\iCloud Photos\\Photos");
        candidates.push_back(base + "\\iCloud Photos");
    }

    for (const string& path : candidates) {
        error_code ec;
        if (fs::exists(path, ec) && fs::is_directory(path, ec)) return path;
    }
    return "";
}

// Loads Apple files first and then appends Google metadata into the same photo vector.
void ImageLoader::LoadPhotos(const string& appleRoot,
                             const string& googleCsvPath,
                             vector<PhotoRecord>& photos,
                             CsvLoadReport& report) const {
    namespace fs = filesystem;
    photos.clear();
    report = CsvLoadReport{};

    if (appleRoot.empty()) {
        Log("No Apple/iCloud folder selected.");
        return;
    }

    static const unordered_set<string> exts = {
        ".jpg", ".jpeg", ".png", ".heic", ".heif", ".gif", ".bmp", ".webp", ".tiff"
    };

    error_code ec;
    fs::recursive_directory_iterator it(appleRoot, fs::directory_options::skip_permission_denied, ec);
    fs::recursive_directory_iterator end;

    size_t appleCount = 0;
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
        rec.source = "apple";
        rec.localPath = entry.path().string();
        rec.fileName = entry.path().filename().string();
        rec.fileSize = entry.file_size(ec);
        rec.modifiedTime = fs::last_write_time(entry.path(), ec).time_since_epoch().count();
        rec.hashes = HashGenerator::ComputeHashes(entry);
        photos.push_back(std::move(rec));
        ++appleCount;
    }

    Log("Apple photos indexed: " + to_string(appleCount));
    LoadGoogleMetadataCsv(googleCsvPath, photos, report);
}

// Resolves and validates the Google export CSV before appending imported rows.
void ImageLoader::LoadGoogleMetadataCsv(const string& csvPath,
                                        vector<PhotoRecord>& photos,
                                        CsvLoadReport& report) const {
    report = CsvLoadReport{};

    const string cwd = GetWorkingDirectory();
    const string exeDir = GetExeDirectory();
    vector<string> candidates;
    candidates.push_back(csvPath);
    if (!cwd.empty()) candidates.push_back((filesystem::path(cwd) / csvPath).string());
    if (!exeDir.empty()) candidates.push_back((filesystem::path(exeDir) / csvPath).string());

    string resolvedPath;
    ifstream in;
    for (const string& candidate : candidates) {
        ifstream test(candidate);
        if (test) {
            resolvedPath = candidate;
            in = std::move(test);
            break;
        }
    }

    if (!in) {
        Log("[google_photos.csv] file not found; skipping Google metadata import.");
        Log("Checked path(s):");
        for (const string& candidate : candidates) Log("  - " + candidate);
        return;
    }

    report.fileFound = true;
    Log("[google_photos.csv] using file: " + resolvedPath);

    string line;
    if (!getline(in, line)) {
        Log("[google_photos.csv] file is empty.");
        return;
    }

    const char delimiter = DetectCsvDelimiter(line);
    const vector<string> header = ParseCsvLine(line, delimiter);
    const vector<string> expectedHeader = {
        "fileName", "fileSize", "modifiedTime", "googleUrl",
        "exactHash", "sampleHash", "metadataHash"
    };

    if (header != expectedHeader) {
        Log("[google_photos.csv] invalid header.");
        Log("Expected: fileName,fileSize,modifiedTime,googleUrl,exactHash,sampleHash,metadataHash");
        Log("Found   : " + line);
        return;
    }

    report.headerValid = true;
    Log(string("[google_photos.csv] delimiter detected: ") + delimiter);

    size_t lineNumber = 1;
    while (getline(in, line)) {
        ++lineNumber;
        if (Trim(line).empty()) continue;
        ++report.rowsRead;

        const vector<string> cols = ParseCsvLine(line, delimiter);
        if (cols.size() != 7) {
            ++report.rowsSkipped;
            if (report.errors.size() < 20) {
                report.errors.push_back(
                    "line " + to_string(lineNumber) + ": expected 7 columns, got " + to_string(cols.size()));
            }
            continue;
        }
        if (cols[0].empty()) {
            ++report.rowsSkipped;
            if (report.errors.size() < 20) {
                report.errors.push_back("line " + to_string(lineNumber) + ": fileName is required");
            }
            continue;
        }
        if (cols[3].empty()) {
            ++report.rowsSkipped;
            if (report.errors.size() < 20) {
                report.errors.push_back(
                    "line " + to_string(lineNumber) + ": googleUrl is required for manual fallback");
            }
            continue;
        }

        PhotoRecord rec;
        rec.source = "google";
        rec.fileName = cols[0];
        try {
            rec.fileSize = cols[1].empty() ? 0 : static_cast<uintmax_t>(stoull(cols[1]));
        } catch (...) {
            ++report.rowsSkipped;
            if (report.errors.size() < 20) {
                report.errors.push_back(
                    "line " + to_string(lineNumber) + ": fileSize must be an unsigned integer");
            }
            continue;
        }
        try {
            rec.modifiedTime = cols[2].empty() ? 0 : stoll(cols[2]);
        } catch (...) {
            ++report.rowsSkipped;
            if (report.errors.size() < 20) {
                report.errors.push_back(
                    "line " + to_string(lineNumber) + ": modifiedTime must be an integer");
            }
            continue;
        }

        rec.googleUrl = cols[3];
        rec.hashes.exactHash = cols[4];
        rec.hashes.sampleHash = cols[5];
        rec.hashes.metadataHash = cols[6];
        photos.push_back(std::move(rec));
        ++report.rowsLoaded;
    }

    Log("[google_photos.csv] import report: rows read=" + to_string(report.rowsRead) +
        ", loaded=" + to_string(report.rowsLoaded) +
        ", skipped=" + to_string(report.rowsSkipped));

    if (!report.errors.empty()) {
        Log("[google_photos.csv] validation errors (showing up to 20):");
        for (const string& err : report.errors) Log("  - " + err);
    }
}
