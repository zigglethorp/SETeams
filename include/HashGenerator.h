#pragma once

#include "duosort_types.h"

#include <filesystem>
#include <string>

// HashGenerator centralizes all file-hash utilities used by duplicate detection.
class HashGenerator {
public:
    // Converts a string to lowercase so extension comparisons stay case-insensitive.
    static std::string ToLower(std::string text);

    // Computes the exact, sampled, and metadata hashes for one filesystem entry.
    static HashBundle ComputeHashes(const std::filesystem::directory_entry& entry);
};
