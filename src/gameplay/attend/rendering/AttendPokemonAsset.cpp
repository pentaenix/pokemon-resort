#include "gameplay/attend/rendering/AttendPokemonAsset.hpp"

#include "core/crypto/Sha256.hpp"

#include <zstd.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

namespace pr::gameplay::attend::rendering {

namespace {

constexpr std::array<std::uint8_t, 8> kGlbzMagic{'P', 'R', 'G', 'L', 'B', 'Z', '0', '1'};
constexpr std::size_t kGlbzHeaderSize = 8 + 8 + 8 + 32;
constexpr std::uint64_t kMaximumGlbBytes = 512ull * 1024ull * 1024ull;

void fail(std::string* error, const std::string& message) {
    if (error) *error = message;
}

std::uint64_t readU64Le(const std::uint8_t* bytes) {
    std::uint64_t value = 0;
    for (unsigned shift = 0; shift < 64; shift += 8) {
        value |= static_cast<std::uint64_t>(bytes[shift / 8]) << shift;
    }
    return value;
}

std::string digestHex(const std::uint8_t* digest) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < 32; ++i) {
        out << std::setw(2) << static_cast<unsigned>(digest[i]);
    }
    return out.str();
}

bool readFile(const std::string& path, std::vector<std::uint8_t>& bytes, std::string* error) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        fail(error, "Could not open Attend Pokemon model asset: " + path);
        return false;
    }
    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    input.seekg(0, std::ios::beg);
    if (size < 0 || static_cast<std::uint64_t>(size) > kMaximumGlbBytes) {
        fail(error, "Attend Pokemon model asset has an unsupported size: " + path);
        return false;
    }
    bytes.resize(static_cast<std::size_t>(size));
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()), size);
    }
    if (!input) {
        fail(error, "Could not read Attend Pokemon model asset: " + path);
        return false;
    }
    return true;
}

bool decodeGlbz(
    const std::string& path,
    const std::vector<std::uint8_t>& container,
    std::vector<std::uint8_t>& glb_bytes,
    std::string* error) {
    if (container.size() < kGlbzHeaderSize) {
        fail(error, "Attend Pokemon GLBZ header is truncated: " + path);
        return false;
    }
    const std::uint64_t original_size = readU64Le(container.data() + 8);
    const std::uint64_t compressed_size = readU64Le(container.data() + 16);
    if (original_size == 0 || original_size > kMaximumGlbBytes ||
        original_size > std::numeric_limits<std::size_t>::max()) {
        fail(error, "Attend Pokemon GLBZ declares an unsupported GLB size: " + path);
        return false;
    }
    if (compressed_size != container.size() - kGlbzHeaderSize) {
        fail(error, "Attend Pokemon GLBZ payload size does not match its header: " + path);
        return false;
    }

    glb_bytes.resize(static_cast<std::size_t>(original_size));
    const std::size_t result = ZSTD_decompress(
        glb_bytes.data(),
        glb_bytes.size(),
        container.data() + kGlbzHeaderSize,
        static_cast<std::size_t>(compressed_size));
    if (ZSTD_isError(result)) {
        fail(error, "Attend Pokemon GLBZ decompression failed: " +
                std::string(ZSTD_getErrorName(result)) + ": " + path);
        glb_bytes.clear();
        return false;
    }
    if (result != glb_bytes.size()) {
        fail(error, "Attend Pokemon GLBZ decompressed size does not match its header: " + path);
        glb_bytes.clear();
        return false;
    }
    const std::string expected_hash = digestHex(container.data() + 24);
    if (sha256HexLowercase(glb_bytes) != expected_hash) {
        fail(error, "Attend Pokemon GLBZ decompressed SHA-256 does not match its header: " + path);
        glb_bytes.clear();
        return false;
    }
    return true;
}

} // namespace

bool loadAttendPokemonAssetBytes(
    const std::string& path,
    std::vector<std::uint8_t>& glb_bytes,
    std::string* error) {
    glb_bytes.clear();
    std::vector<std::uint8_t> file_bytes;
    if (!readFile(path, file_bytes, error)) return false;

    const bool has_glbz_magic = file_bytes.size() >= kGlbzMagic.size() &&
        std::equal(kGlbzMagic.begin(), kGlbzMagic.end(), file_bytes.begin());
    if (has_glbz_magic) {
        return decodeGlbz(path, file_bytes, glb_bytes, error);
    }
    if (std::filesystem::path(path).extension() == ".glbz") {
        fail(error, "Attend Pokemon GLBZ has an unsupported magic/version: " + path);
        return false;
    }
    glb_bytes = std::move(file_bytes);
    return true;
}

} // namespace pr::gameplay::attend::rendering
