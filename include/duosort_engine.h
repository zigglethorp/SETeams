#pragma once

#include "DuplicateFinder.h"
#include "ImageLoader.h"
#include "duosort_types.h"

#include <functional>
#include <string>
#include <vector>

// Core application service for DuoSort.
// Responsibilities:
// 1) Ingest Apple photos and Google Takeout media.
// 2) Compute hashes and run duplicate detection.
// 3) Support duplicate review and confirmed local deletion.
class DuoSortEngine {
public:
    using LoggerFn = std::function<void(const std::string&)>;

    // Creates the coordinator that ties loading, cataloging, and dedup together.
    DuoSortEngine();

    // Shares a logger callback with the engine and its helper modules.
    void SetLogger(LoggerFn logger);
    // Stores the Apple/iCloud folder chosen by the user.
    void SetAppleRoot(std::string rootPath);
    // Stores the Google Takeout folder chosen by the user.
    void SetGoogleRoot(std::string rootPath);
    // Locates a likely default iCloud folder for convenience in the UI.
    std::string FindDefaultAppleFolder() const;
    // Locates a likely extracted Google Takeout folder for convenience in the UI.
    std::string FindDefaultGoogleFolder() const;

    // Runs the full ingest and dedup workflow.
    void Run();
    // Runs ingest and dedup without any extra side effects.
    void RunScanOnly();

    // Returns how many photo rows are currently loaded in memory.
    size_t PhotoCount() const;
    // Returns how many duplicate groups were found in the most recent run.
    size_t DuplicateGroupCount() const;
    // Exposes the current photo list for review UI rendering.
    const std::vector<PhotoRecord>& Photos() const;
    // Exposes the current duplicate groups for review navigation.
    const std::vector<std::vector<size_t>>& DuplicateGroups() const;
    // Deletes a local photo file after the user confirms the action.
    bool DeletePhoto(size_t photoIndex, std::string& message);

private:
    // Sends a message to the active logger callback.
    void Log(const std::string& line) const;

    // Loads Apple media and Google Takeout media into the shared photo list.
    void Ingestor();
    // Builds duplicate groups from the current photo list.
    void Dedup();

    std::vector<PhotoRecord> photos_;
    std::vector<std::vector<size_t>> duplicateGroups_;
    std::string appleRoot_;
    std::string googleRoot_;
    ImageLoader imageLoader_;
    LoggerFn logger_;
};
