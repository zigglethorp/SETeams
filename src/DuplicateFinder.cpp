#include "DuplicateFinder.h"

#include <unordered_map>
#include <vector>

using namespace std;

namespace {
// Writes a log line only when the caller supplied a logger callback.
void LogLine(const DuplicateFinder::LoggerFn& logger, const string& line) {
    if (logger) logger(line);
}
} // namespace

// Groups duplicates by confidence level and emits a short per-source summary.
void DuplicateFinder::BuildDuplicateGroups(const vector<PhotoRecord>& photos,
                                           vector<vector<size_t>>& duplicateGroups,
                                           const LoggerFn& logger) {
    duplicateGroups.clear();
    if (photos.empty()) return;

    vector<bool> used(photos.size(), false);
    unordered_map<string, vector<size_t>> byExact;
    unordered_map<string, vector<size_t>> bySample;
    unordered_map<string, vector<size_t>> byMeta;

    // Build lookup tables once, then walk them in confidence order below.
    for (size_t i = 0; i < photos.size(); ++i) {
        if (!photos[i].hashes.exactHash.empty()) byExact[photos[i].hashes.exactHash].push_back(i);
        if (!photos[i].hashes.sampleHash.empty()) bySample[photos[i].hashes.sampleHash].push_back(i);
        if (!photos[i].hashes.metadataHash.empty()) byMeta[photos[i].hashes.metadataHash].push_back(i);
    }

    // Exact matches win first so weaker fallback hashes do not split or duplicate those groups.
    for (const auto& kv : byExact) {
        if (kv.second.size() > 1) {
            duplicateGroups.push_back(kv.second);
            for (size_t idx : kv.second) used[idx] = true;
        }
    }

    // Sample-hash groups only absorb photos that were not already matched exactly.
    for (const auto& kv : bySample) {
        if (kv.second.size() <= 1) continue;
        vector<size_t> group;
        for (size_t idx : kv.second) {
            if (!used[idx]) group.push_back(idx);
        }
        if (group.size() > 1) {
            duplicateGroups.push_back(group);
            for (size_t idx : group) used[idx] = true;
        }
    }

    // Metadata hashing is the weakest signal, so it only groups photos still unmatched.
    for (const auto& kv : byMeta) {
        if (kv.second.size() <= 1) continue;
        vector<size_t> group;
        for (size_t idx : kv.second) {
            if (!used[idx]) group.push_back(idx);
        }
        if (group.size() > 1) {
            duplicateGroups.push_back(group);
            for (size_t idx : group) used[idx] = true;
        }
    }

    size_t appleDuplicatePhotos = 0;
    size_t googleDuplicatePhotos = 0;
    size_t appleOnlyGroups = 0;
    size_t googleOnlyGroups = 0;
    size_t mixedGroups = 0;

    for (const auto& group : duplicateGroups) {
        bool hasApple = false;
        bool hasGoogle = false;
        for (size_t idx : group) {
            if (photos[idx].source == "apple") {
                hasApple = true;
                ++appleDuplicatePhotos;
            } else if (photos[idx].source == "google") {
                hasGoogle = true;
                ++googleDuplicatePhotos;
            }
        }

        if (hasApple && hasGoogle) {
            ++mixedGroups;
        } else if (hasApple) {
            ++appleOnlyGroups;
        } else if (hasGoogle) {
            ++googleOnlyGroups;
        }
    }

    LogLine(logger, "Photos indexed total: " + to_string(photos.size()));
    LogLine(logger, "Duplicate groups found: " + to_string(duplicateGroups.size()) +
                    " (apple-only=" + to_string(appleOnlyGroups) +
                    ", google-only=" + to_string(googleOnlyGroups) +
                    ", mixed=" + to_string(mixedGroups) + ")");
    LogLine(logger, "Duplicate photos by source: apple=" + to_string(appleDuplicatePhotos) +
                    ", google=" + to_string(googleDuplicatePhotos));
}
