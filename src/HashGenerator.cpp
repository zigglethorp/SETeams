#include "HashGenerator.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

using namespace std;

namespace {
// Formats a 64-bit hash as fixed-width hexadecimal for stable display and storage.
string ToHex64(uint64_t value) {
    stringstream ss;
    ss << hex << setw(16) << setfill('0') << value;
    return ss.str();
}

// Updates the FNV-1a state with a block of bytes from a file or metadata string.
uint64_t Fnv1a64Update(uint64_t hash, const unsigned char* data, size_t len) {
    constexpr uint64_t prime = 1099511628211ULL;
    for (size_t i = 0; i < len; ++i) {
        hash ^= static_cast<uint64_t>(data[i]);
        hash *= prime;
    }
    return hash;
}

// Reads the entire file to produce the strongest content-based duplicate hash.
string FullFileHash(const string& path) {
    ifstream in(path, ios::binary);
    if (!in) return "";

    constexpr size_t kBufSize = 1 << 16;
    vector<char> buffer(kBufSize);
    uint64_t hash = 1469598103934665603ULL;

    while (in) {
        in.read(buffer.data(), static_cast<streamsize>(buffer.size()));
        const streamsize got = in.gcount();
        if (got > 0) {
            hash = Fnv1a64Update(hash, reinterpret_cast<unsigned char*>(buffer.data()), static_cast<size_t>(got));
        }
    }
    return ToHex64(hash);
}

// Samples fixed windows across the file to create a cheaper fallback hash.
string SampleFileHash(const string& path) {
    ifstream in(path, ios::binary | ios::ate);
    if (!in) return "";
    const streamoff size = in.tellg();
    if (size <= 0) return "";

    constexpr int kSamples = 32;
    constexpr int kWindow = 256;
    uint64_t hash = 1469598103934665603ULL;
    vector<char> window(kWindow);

    for (int i = 0; i < kSamples; ++i) {
        const streamoff pos = (size - 1) * i / (kSamples - 1);
        in.seekg(pos, ios::beg);
        if (!in) break;
        in.read(window.data(), static_cast<streamsize>(window.size()));
        const streamsize got = in.gcount();
        if (got > 0) {
            hash = Fnv1a64Update(hash, reinterpret_cast<unsigned char*>(window.data()), static_cast<size_t>(got));
        }
    }
    return ToHex64(hash);
}
} // namespace

// Normalizes text so file extensions and source labels compare reliably.
string HashGenerator::ToLower(string text) {
    transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(tolower(c));
    });
    return text;
}

// Builds the full hash bundle that downstream duplicate matching uses.
HashBundle HashGenerator::ComputeHashes(const filesystem::directory_entry& entry) {
    HashBundle hashes;
    hashes.exactHash = FullFileHash(entry.path().string());
    hashes.sampleHash = SampleFileHash(entry.path().string());

    error_code ec;
    const auto fileSize = entry.file_size(ec);
    const auto lastWrite = filesystem::last_write_time(entry.path(), ec);
    const long long timestamp = ec ? 0LL : lastWrite.time_since_epoch().count();

    stringstream ss;
    ss << entry.path().filename().string() << "|" << fileSize << "|" << timestamp;
    const string metadata = ss.str();
    const uint64_t metaHash = Fnv1a64Update(
        1469598103934665603ULL,
        reinterpret_cast<const unsigned char*>(metadata.data()),
        metadata.size());
    hashes.metadataHash = ToHex64(metaHash);
    return hashes;
}
