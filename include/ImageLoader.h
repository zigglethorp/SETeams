#pragma once

#include "duosort_types.h"

#include <functional>
#include <string>
#include <vector>

// ImageLoader collects photo records from Apple folders and Google CSV exports.
class ImageLoader {
public:
    using LoggerFn = std::function<void(const std::string&)>;

    // Stores the logger used to report loader progress and validation issues.
    void SetLogger(LoggerFn logger);

    // Finds the first likely iCloud Photos folder for the current Windows user.
    std::string FindDefaultAppleFolder() const;

    // Scans the Apple folder and then appends Google metadata rows into one shared list.
    void LoadPhotos(const std::string& appleRoot,
                    const std::string& googleCsvPath,
                    std::vector<PhotoRecord>& photos,
                    CsvLoadReport& report) const;

    // Imports Google CSV metadata and appends it to an existing photo list.
    void LoadGoogleMetadataCsv(const std::string& csvPath,
                               std::vector<PhotoRecord>& photos,
                               CsvLoadReport& report) const;

private:
    // Sends a line to the active logger when one has been configured.
    void Log(const std::string& line) const;

    LoggerFn logger_;
};
