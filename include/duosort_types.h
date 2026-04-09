#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Holds all hash variants computed for one photo.
// exactHash: full-byte hash for strict duplicate detection.
// sampleHash: sampled-bytes hash for quick near-match grouping.
// metadataHash: hash of basic metadata as fallback matching signal.
struct HashBundle {
    std::string exactHash;
    std::string sampleHash;
    std::string metadataHash;
};

// Represents one photo record from either Apple or Google local media.
struct PhotoRecord {
    std::string source;      // "apple" or "google"
    std::string localPath;   // File path for local Apple media
    std::string fileName;
    uintmax_t fileSize = 0;
    long long modifiedTime = 0;
    HashBundle hashes;
    bool deleted = false;
};
