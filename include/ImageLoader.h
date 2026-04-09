#pragma once

#include "duosort_types.h"

#include <functional>
#include <string>
#include <vector>

// ImageLoader collects photo records from Apple and Google Takeout sources.
class ImageLoader {
public:
    using LoggerFn = std::function<void(const std::string&)>;

    // Stores the logger used to report loader progress and validation issues.
    void SetLogger(LoggerFn logger);

    // Finds the first likely iCloud Photos folder for the current Windows user.
    std::string FindDefaultAppleFolder() const;
    // Finds the first likely extracted Google Takeout folder or zip archive for the current user.
    std::string FindDefaultGoogleFolder() const;

    // Scans local Apple media and Google Takeout media into one shared list.
    void LoadPhotos(const std::string& appleRoot,
                    const std::string& googleRoot,
                    std::vector<PhotoRecord>& photos) const;

private:
    // Sends a line to the active logger when one has been configured.
    void Log(const std::string& line) const;
    // Indexes one local photo folder into the shared record list.
    size_t LoadLocalPhotoFolder(const std::string& rootPath,
                                const std::string& sourceLabel,
                                std::vector<PhotoRecord>& photos) const;
    // Resolves a Google Takeout folder or zip archive to an extracted photo directory.
    std::string ResolveGoogleTakeoutRoot(const std::string& inputPath) const;

    LoggerFn logger_;
};
