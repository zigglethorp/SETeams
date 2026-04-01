#pragma once

#include "DuplicateFinder.h"
#include "ImageLoader.h"
#include "duosort_types.h"

#include <functional>
#include <string>
#include <vector>

// Core application service for DuoSort.
// Responsibilities:
// 1) Ingest Apple photos and Google CSV metadata.
// 2) Compute/store hashes and write optional SQLite catalog.
// 3) Run multi-pass duplicate detection.
// 4) Export manual Google delete links for matched groups.
class DuoSortEngine {
public:
    using LoggerFn = std::function<void(const std::string&)>;

    // Creates the coordinator that ties loading, cataloging, and dedup together.
    DuoSortEngine();

    // Shares a logger callback with the engine and its helper modules.
    void SetLogger(LoggerFn logger);
    // Stores the Apple/iCloud folder chosen by the user.
    void SetAppleRoot(std::string rootPath);
    // Locates a likely default iCloud folder for convenience in the UI.
    std::string FindDefaultAppleFolder() const;

    // Runs the full ingest, catalog, and dedup workflow.
    void Run();
    // Runs ingest and dedup without writing the optional SQLite catalog.
    void RunScanOnly();
    // Writes grouped Google URLs to a manual-review text file.
    void ExportGoogleManualLinks(const std::string& outputPath = "google_manual_delete_links.txt");

    // Returns how many photo rows are currently loaded in memory.
    size_t PhotoCount() const;
    // Returns how many duplicate groups were found in the most recent run.
    size_t DuplicateGroupCount() const;
    // Returns validation details from the latest Google CSV import.
    const CsvLoadReport& GoogleCsvReport() const;

private:
    // Sends a message to the active logger callback.
    void Log(const std::string& line) const;

    // Loads Apple media and Google CSV metadata into the shared photo list.
    void Ingestor();
    // Persists the current in-memory catalog to SQLite when available.
    void CatalogDb();
    // Builds duplicate groups from the current photo list.
    void Dedup();

    std::vector<PhotoRecord> photos_;
    std::vector<std::vector<size_t>> duplicateGroups_;
    CsvLoadReport googleCsvReport_;
    std::string appleRoot_;
    ImageLoader imageLoader_;
    LoggerFn logger_;
};
