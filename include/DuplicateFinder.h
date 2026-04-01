#pragma once

#include "duosort_types.h"

#include <functional>
#include <string>
#include <vector>

// DuplicateFinder groups photos that appear to represent the same image.
class DuplicateFinder {
public:
    using LoggerFn = std::function<void(const std::string&)>;

    // Builds duplicate groups using exact hashes first, then weaker fallback hashes.
    static void BuildDuplicateGroups(const std::vector<PhotoRecord>& photos,
                                     std::vector<std::vector<size_t>>& duplicateGroups,
                                     const LoggerFn& logger);
};
