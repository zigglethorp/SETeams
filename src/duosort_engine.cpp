#include "duosort_engine.h"

#include "catalog_sqlite.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

using namespace std;

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

// Returns how many Apple and Google photo rows are currently loaded.
size_t DuoSortEngine::PhotoCount() const {
    return photos_.size();
}

// Returns the number of duplicate groups produced by the last run.
size_t DuoSortEngine::DuplicateGroupCount() const {
    return duplicateGroups_.size();
}

// Exposes the latest Google CSV validation report to the UI or tests.
const CsvLoadReport& DuoSortEngine::GoogleCsvReport() const {
    return googleCsvReport_;
}

// Sends a log line to the configured logger callback.
void DuoSortEngine::Log(const string& line) const {
    if (logger_) logger_(line);
}

// Delegates default-folder discovery to ImageLoader so folder logic stays in one place.
string DuoSortEngine::FindDefaultAppleFolder() const {
    return imageLoader_.FindDefaultAppleFolder();
}

// Loads Apple files and Google CSV rows into the shared in-memory catalog.
void DuoSortEngine::Ingestor() {
    imageLoader_.LoadPhotos(appleRoot_, "google_photos.csv", photos_, googleCsvReport_);
}

// Writes the combined photo catalog to SQLite when that optional dependency is available.
void DuoSortEngine::CatalogDb() {
    WritePhotoCatalogDb(photos_, [this](const string& line) { Log(line); });
}

// Runs duplicate detection across the current in-memory photo catalog.
void DuoSortEngine::Dedup() {
    DuplicateFinder::BuildDuplicateGroups(photos_, duplicateGroups_, [this](const string& line) { Log(line); });
}

// Executes the full scan, optional catalog export, and duplicate-detection pipeline.
void DuoSortEngine::Run() {
    namespace fs = filesystem;
    error_code ec;
    if (appleRoot_.empty() || !fs::exists(appleRoot_, ec) || !fs::is_directory(appleRoot_, ec)) {
        Log("Please select a valid Apple/iCloud folder first.");
        return;
    }

    // Keep orchestration explicit in one place for easier debugging.
    Log("Running DuoSort...");
    Log("Apple source: " + appleRoot_);
    Ingestor();
    CatalogDb();
    Dedup();
    Log("Run complete.");
}

// Executes the ingestion and duplicate scan without writing the SQLite catalog.
void DuoSortEngine::RunScanOnly() {
    namespace fs = filesystem;
    error_code ec;
    if (appleRoot_.empty() || !fs::exists(appleRoot_, ec) || !fs::is_directory(appleRoot_, ec)) {
        Log("Please select a valid Apple/iCloud folder first.");
        return;
    }

    Log("Running DuoSort scan-only mode...");
    Log("Apple source: " + appleRoot_);
    Ingestor();
    Dedup();
    Log("Scan complete.");
}

// Exports the Google URLs for duplicate groups so they can be reviewed manually.
void DuoSortEngine::ExportGoogleManualLinks(const string& outputPath) {
    ofstream out(outputPath);
    if (!out) {
        Log("Unable to write " + outputPath);
        return;
    }

    size_t groupsWithGoogle = 0;
    size_t links = 0;
    for (size_t g = 0; g < duplicateGroups_.size(); ++g) {
        bool wroteGroup = false;
        for (size_t idx : duplicateGroups_[g]) {
            const auto& p = photos_[idx];
            if (p.source == "google" && !p.googleUrl.empty()) {
                if (!wroteGroup) {
                    out << "Group " << g + 1 << "\n";
                    wroteGroup = true;
                    ++groupsWithGoogle;
                }
                out << p.googleUrl << "\n";
                ++links;
            }
        }
        if (wroteGroup) out << "\n";
    }

    Log("Google manual-fallback links exported: " + to_string(links) +
        " links across " + to_string(groupsWithGoogle) + " groups.");
}
