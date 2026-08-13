#include "mapmaker/document/OwmapDocument.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace pr::mapmaker {
namespace fs = std::filesystem;

namespace {

std::uint16_t readU16Le(const std::uint8_t* bytes) {
    return static_cast<std::uint16_t>(bytes[0]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[1]) << 8U);
}

std::uint32_t readU32Le(const std::uint8_t* bytes) {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

float readF32Le(const std::uint8_t* bytes) {
    const std::uint32_t bits = readU32Le(bytes);
    float value = 0.0F;
    static_assert(sizeof(value) == sizeof(bits), "OWMAP requires 32-bit float");
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

void appendU16Le(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
}

void appendU32Le(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
}

void appendF32Le(std::vector<std::uint8_t>& out, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    appendU32Le(out, bits);
}

std::vector<std::uint8_t> readBytes(const fs::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("Could not open OWMAP: " + path.string());
    const std::streamoff length = input.tellg();
    if (length < 0) throw std::runtime_error("Could not determine OWMAP size: " + path.string());
    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()), length);
        if (!input) throw std::runtime_error("Could not read complete OWMAP: " + path.string());
    }
    return bytes;
}

void writeBytes(const fs::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("Could not open temporary OWMAP: " + path.string());
    if (!bytes.empty()) output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    output.flush();
    if (!output) throw std::runtime_error("Could not write complete OWMAP: " + path.string());
}

fs::path defaultBackupPath(const fs::path& path) {
    return fs::path(path.string() + ".bak");
}

void removeWithoutThrow(const fs::path& path) {
    std::error_code error;
    fs::remove(path, error);
}

void renameReplacing(const fs::path& from, const fs::path& to) {
    std::error_code error;
    fs::rename(from, to, error);
    if (error) {
        throw std::runtime_error(
            "Could not atomically replace " + to.string() + ": " + error.message());
    }
}

void copyVerified(const fs::path& from, const fs::path& to) {
    const std::vector<std::uint8_t> expected = readBytes(from);
    writeBytes(to, expected);
    if (readBytes(to) != expected) {
        throw std::runtime_error("Backup read-back differs from source: " + to.string());
    }
}

bool isValidOwmapFile(const fs::path& path) noexcept {
    try {
        OwmapDocument document = OwmapDocument::fromBytes(readBytes(path));
        document.validateOrThrow();
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

const JsonValue* objectChild(const JsonValue& value, const char* key) {
    return value.isObject() ? value.get(key) : nullptr;
}

bool exactNumber(const JsonValue* value, double expected) {
    return value && value->isNumber() && value->asNumber() == expected;
}

bool exactFloat(const JsonValue* value, float expected) {
    return value && value->isNumber() && std::isfinite(value->asNumber()) &&
           static_cast<float>(value->asNumber()) == expected;
}

bool sameFloatBits(float lhs, float rhs) {
    std::uint32_t lhs_bits = 0;
    std::uint32_t rhs_bits = 0;
    std::memcpy(&lhs_bits, &lhs, sizeof(lhs_bits));
    std::memcpy(&rhs_bits, &rhs, sizeof(rhs_bits));
    return lhs_bits == rhs_bits;
}

} // namespace

OwmapDocument OwmapDocument::fromBytes(std::vector<std::uint8_t> bytes) {
    if (bytes.size() < kHeaderSize) throw std::runtime_error("OWMAP is smaller than its 18-byte header");
    if (readU32Le(bytes.data()) != kMagic) throw std::runtime_error("OWMAP has invalid magic");
    if (readU16Le(bytes.data() + 4U) != kVersion) throw std::runtime_error("Unsupported OWMAP version");

    OwmapDocument document;
    document.width_ = readU16Le(bytes.data() + 6U);
    document.height_ = readU16Le(bytes.data() + 8U);
    document.tile_size_ = readF32Le(bytes.data() + 10U);
    if (document.width_ == 0U || document.height_ == 0U ||
        document.width_ > kMaxDimension || document.height_ > kMaxDimension) {
        throw std::runtime_error("OWMAP dimensions must be in the range 1..256");
    }

    const std::size_t metadata_size = readU32Le(bytes.data() + 14U);
    if (metadata_size > bytes.size() - kHeaderSize) throw std::runtime_error("OWMAP metadata is truncated");
    const std::size_t cells = static_cast<std::size_t>(document.width_) * document.height_;
    const std::size_t collision_size = (cells + 7U) / 8U;
    const std::size_t metadata_end = kHeaderSize + metadata_size;
    if (cells > (std::numeric_limits<std::size_t>::max() - metadata_end - collision_size) / 2U) {
        throw std::runtime_error("OWMAP payload size overflows this platform");
    }
    const std::size_t expected_size = metadata_end + cells * 2U + collision_size;
    if (expected_size > bytes.size()) throw std::runtime_error("OWMAP terrain payload is truncated");

    const std::string metadata_text(
        reinterpret_cast<const char*>(bytes.data() + kHeaderSize), metadata_size);
    document.metadata_ = parseJsonText(metadata_text);
    auto cursor = bytes.begin() + static_cast<std::ptrdiff_t>(metadata_end);
    document.heights_.assign(cursor, cursor + static_cast<std::ptrdiff_t>(cells));
    cursor += static_cast<std::ptrdiff_t>(cells);
    document.specials_.assign(cursor, cursor + static_cast<std::ptrdiff_t>(cells));
    cursor += static_cast<std::ptrdiff_t>(cells);
    document.collision_seed_bytes_.assign(cursor, cursor + static_cast<std::ptrdiff_t>(collision_size));
    cursor += static_cast<std::ptrdiff_t>(collision_size);
    document.collision_.assign(cells, 0U);
    for (std::size_t i = 0; i < cells; ++i) {
        document.collision_[i] = static_cast<std::uint8_t>(
            (document.collision_seed_bytes_[i >> 3U] >> (i & 7U)) & 1U);
    }
    document.trailing_bytes_.assign(cursor, bytes.end());

    document.has_original_ = true;
    document.original_bytes_ = std::move(bytes);
    document.original_width_ = document.width_;
    document.original_height_ = document.height_;
    document.original_tile_size_ = document.tile_size_;
    document.original_metadata_ = document.metadata_;
    document.original_heights_ = document.heights_;
    document.original_specials_ = document.specials_;
    document.original_collision_ = document.collision_;
    document.original_trailing_bytes_ = document.trailing_bytes_;
    return document;
}

OwmapDocument OwmapDocument::load(const fs::path& path) {
    OwmapDocument document = fromBytes(readBytes(path));
    document.loaded_path_ = path;
    return document;
}

OwmapDocument OwmapDocument::loadWithRecovery(const fs::path& path, const fs::path& backup_path) {
    std::string primary_error;
    try {
        OwmapDocument document = load(path);
        document.validateOrThrow();
        return document;
    } catch (const std::exception& error) {
        primary_error = error.what();
    }

    const fs::path backup = backup_path.empty() ? defaultBackupPath(path) : backup_path;
    try {
        OwmapDocument document = load(backup);
        document.validateOrThrow();
        document.recovered_from_backup_ = true;
        return document;
    } catch (const std::exception& error) {
        throw std::runtime_error(
            "Could not load OWMAP primary (" + primary_error + ") or backup (" + error.what() + ")");
    }
}

OwmapDocument OwmapDocument::create(
    std::uint16_t width,
    std::uint16_t height,
    float tile_size,
    JsonValue metadata) {
    if (width == 0U || height == 0U || width > kMaxDimension || height > kMaxDimension) {
        throw std::runtime_error("OWMAP dimensions must be in the range 1..256");
    }
    OwmapDocument document;
    document.width_ = width;
    document.height_ = height;
    document.tile_size_ = tile_size;
    document.metadata_ = std::move(metadata);
    const std::size_t cells = static_cast<std::size_t>(width) * height;
    document.heights_.assign(cells, 0U);
    document.specials_.assign(cells, 0U);
    document.collision_.assign(cells, 0U);
    document.collision_seed_bytes_.assign((cells + 7U) / 8U, 0U);
    document.synchronizeMetadataGrid();
    document.validateOrThrow();
    return document;
}

std::size_t OwmapDocument::cellIndex(std::uint16_t x, std::uint16_t y) const {
    if (x >= width_ || y >= height_) throw std::out_of_range("OWMAP cell is outside document bounds");
    return static_cast<std::size_t>(y) * width_ + x;
}

std::uint8_t& OwmapDocument::heightAt(std::uint16_t x, std::uint16_t y) { return heights_.at(cellIndex(x, y)); }
const std::uint8_t& OwmapDocument::heightAt(std::uint16_t x, std::uint16_t y) const { return heights_.at(cellIndex(x, y)); }
std::uint8_t& OwmapDocument::specialAt(std::uint16_t x, std::uint16_t y) { return specials_.at(cellIndex(x, y)); }
const std::uint8_t& OwmapDocument::specialAt(std::uint16_t x, std::uint16_t y) const { return specials_.at(cellIndex(x, y)); }
std::uint8_t& OwmapDocument::collisionAt(std::uint16_t x, std::uint16_t y) { return collision_.at(cellIndex(x, y)); }
const std::uint8_t& OwmapDocument::collisionAt(std::uint16_t x, std::uint16_t y) const { return collision_.at(cellIndex(x, y)); }

void OwmapDocument::synchronizeMetadataGrid() {
    if (metadata_.isNull()) metadata_.value() = JsonValue::Object{};
    if (!metadata_.isObject()) throw std::runtime_error("OWMAP metadata root must be an object");
    JsonValue& grid = metadata_["grid"];
    if (grid.isNull()) grid.value() = JsonValue::Object{};
    if (!grid.isObject()) throw std::runtime_error("OWMAP metadata.grid must be an object");
    grid["width"] = JsonValue(static_cast<double>(width_));
    grid["height"] = JsonValue(static_cast<double>(height_));
    grid["tileSize"] = JsonValue(static_cast<double>(tile_size_));
}

void OwmapDocument::resize(std::uint16_t width, std::uint16_t height) {
    if (width == 0U || height == 0U || width > kMaxDimension || height > kMaxDimension) {
        throw std::runtime_error("OWMAP dimensions must be in the range 1..256");
    }
    const std::size_t cells = static_cast<std::size_t>(width) * height;
    std::vector<std::uint8_t> next_heights(cells, 0U);
    std::vector<std::uint8_t> next_specials(cells, 0U);
    std::vector<std::uint8_t> next_collision(cells, 0U);
    const std::uint16_t copy_width = std::min(width_, width);
    const std::uint16_t copy_height = std::min(height_, height);
    for (std::uint16_t y = 0; y < copy_height; ++y) {
        for (std::uint16_t x = 0; x < copy_width; ++x) {
            const std::size_t old_index = static_cast<std::size_t>(y) * width_ + x;
            const std::size_t new_index = static_cast<std::size_t>(y) * width + x;
            next_heights[new_index] = heights_.at(old_index);
            next_specials[new_index] = specials_.at(old_index);
            next_collision[new_index] = collision_.at(old_index);
        }
    }
    width_ = width;
    height_ = height;
    heights_ = std::move(next_heights);
    specials_ = std::move(next_specials);
    collision_ = std::move(next_collision);
    collision_seed_bytes_.assign((cells + 7U) / 8U, 0U);
    synchronizeMetadataGrid();
}

void OwmapDocument::setTileSize(float tile_size) {
    tile_size_ = tile_size;
    synchronizeMetadataGrid();
}

bool OwmapDocument::isPristine() const {
    return has_original_ && width_ == original_width_ && height_ == original_height_ &&
           sameFloatBits(tile_size_, original_tile_size_) && metadata_ == original_metadata_ &&
           heights_ == original_heights_ && specials_ == original_specials_ &&
           collision_ == original_collision_ && trailing_bytes_ == original_trailing_bytes_;
}

std::vector<OwmapValidationIssue> OwmapDocument::validate() const {
    std::vector<OwmapValidationIssue> issues;
    const auto error = [&issues](std::string code, std::string message) {
        issues.push_back({OwmapIssueSeverity::Error, std::move(code), std::move(message)});
    };
    if (width_ == 0U || height_ == 0U || width_ > kMaxDimension || height_ > kMaxDimension) {
        error("dimensions.range", "header width and height must each be in 1..256");
    }
    if (!std::isfinite(tile_size_) || tile_size_ <= 0.0F) {
        error("tile_size.invalid", "header tileSize must be finite and greater than zero");
    }
    const std::size_t cells = static_cast<std::size_t>(width_) * height_;
    if (heights_.size() != cells) error("terrain.height.size", "height plane does not match header dimensions");
    if (specials_.size() != cells) error("terrain.special.size", "special plane does not match header dimensions");
    if (collision_.size() != cells) error("terrain.collision.size", "collision plane does not match header dimensions");
    if (std::find(specials_.begin(), specials_.end(), 1U) != specials_.end()) {
        error("terrain.special.unresolved", "special=1 is editor-only and must be baked before saving");
    }
    if (std::any_of(collision_.begin(), collision_.end(), [](std::uint8_t value) { return value > 1U; })) {
        error("terrain.collision.value", "collision cells must be zero or one");
    }

    if (!metadata_.isObject()) {
        error("metadata.root", "metadata root must be an object");
    } else {
        const JsonValue* grid = objectChild(metadata_, "grid");
        if (!grid || !grid->isObject()) {
            error("metadata.grid", "metadata.grid must be an object");
        } else {
            if (!exactNumber(grid->get("width"), width_)) {
                error("metadata.grid.width", "metadata grid width does not match the binary header");
            }
            if (!exactNumber(grid->get("height"), height_)) {
                error("metadata.grid.height", "metadata grid height does not match the binary header");
            }
            if (!exactFloat(grid->get("tileSize"), tile_size_)) {
                error("metadata.grid.tile_size", "metadata grid tileSize does not match the binary header");
            }
        }
        try {
            (void)serializeJsonValue(metadata_);
        } catch (const std::exception& exception) {
            error("metadata.serialization", exception.what());
        }
    }
    return issues;
}

void OwmapDocument::validateOrThrow() const {
    std::ostringstream message;
    for (const OwmapValidationIssue& issue : validate()) {
        if (issue.severity != OwmapIssueSeverity::Error) continue;
        if (message.tellp() > 0) message << "; ";
        message << issue.code << ": " << issue.message;
    }
    if (message.tellp() > 0) throw std::runtime_error("Invalid OWMAP document: " + message.str());
}

std::vector<std::uint8_t> OwmapDocument::serialize(JsonStyle metadata_style, std::size_t indent_size) const {
    if (isPristine()) return original_bytes_;
    validateOrThrow();
    const std::string metadata_text = serializeJsonValue(metadata_, metadata_style, indent_size);
    if (metadata_text.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("OWMAP metadata exceeds the v1 32-bit length field");
    }
    const std::size_t cells = static_cast<std::size_t>(width_) * height_;
    const std::size_t collision_size = (cells + 7U) / 8U;
    std::vector<std::uint8_t> packed_collision = collision_seed_bytes_;
    packed_collision.resize(collision_size, 0U);
    for (std::size_t i = 0; i < cells; ++i) {
        const std::uint8_t mask = static_cast<std::uint8_t>(1U << (i & 7U));
        if (collision_[i] != 0U) packed_collision[i >> 3U] |= mask;
        else packed_collision[i >> 3U] &= static_cast<std::uint8_t>(~mask);
    }

    std::vector<std::uint8_t> bytes;
    bytes.reserve(kHeaderSize + metadata_text.size() + cells * 2U + collision_size + trailing_bytes_.size());
    appendU32Le(bytes, kMagic);
    appendU16Le(bytes, kVersion);
    appendU16Le(bytes, width_);
    appendU16Le(bytes, height_);
    appendF32Le(bytes, tile_size_);
    appendU32Le(bytes, static_cast<std::uint32_t>(metadata_text.size()));
    bytes.insert(bytes.end(), metadata_text.begin(), metadata_text.end());
    bytes.insert(bytes.end(), heights_.begin(), heights_.end());
    bytes.insert(bytes.end(), specials_.begin(), specials_.end());
    bytes.insert(bytes.end(), packed_collision.begin(), packed_collision.end());
    bytes.insert(bytes.end(), trailing_bytes_.begin(), trailing_bytes_.end());
    return bytes;
}

void OwmapDocument::saveAtomic(const fs::path& path, const fs::path& backup_path) {
    validateOrThrow();
    if (path.empty()) throw std::runtime_error("OWMAP save path is empty");
    const fs::path backup = backup_path.empty() ? defaultBackupPath(path) : backup_path;
    const fs::path temp(path.string() + ".tmp");
    const fs::path backup_temp(backup.string() + ".tmp");
    if (path == backup || path == temp || backup == temp) {
        throw std::runtime_error("OWMAP target, temporary, and backup paths must be distinct");
    }
    if (!path.parent_path().empty()) fs::create_directories(path.parent_path());
    if (!backup.parent_path().empty()) fs::create_directories(backup.parent_path());

    const std::vector<std::uint8_t> bytes = serialize();
    removeWithoutThrow(temp);
    removeWithoutThrow(backup_temp);
    try {
        writeBytes(temp, bytes);
        const std::vector<std::uint8_t> temp_bytes = readBytes(temp);
        if (temp_bytes != bytes) throw std::runtime_error("Temporary OWMAP differs after read-back");
        OwmapDocument verified = fromBytes(temp_bytes);
        verified.validateOrThrow();

        std::error_code exists_error;
        const bool had_target = fs::exists(path, exists_error) && !exists_error;
        bool has_recovery_backup = false;
        if (had_target) {
            if (isValidOwmapFile(path)) {
                copyVerified(path, backup_temp);
                renameReplacing(backup_temp, backup);
                has_recovery_backup = true;
            } else {
                // A recovery load means the primary may be corrupt while .bak is
                // the only verified copy. Never replace that backup with the bad
                // primary during the first repaired save.
                has_recovery_backup = isValidOwmapFile(backup);
            }
        }
        renameReplacing(temp, path);

        try {
            const std::vector<std::uint8_t> persisted = readBytes(path);
            if (persisted != bytes) throw std::runtime_error("Saved OWMAP differs after read-back");
            OwmapDocument saved = fromBytes(persisted);
            saved.validateOrThrow();
            saved.loaded_path_ = path;
            *this = std::move(saved);
        } catch (const std::exception& validation_error) {
            if (has_recovery_backup && fs::exists(backup)) {
                const fs::path recovery_temp(path.string() + ".recovery.tmp");
                removeWithoutThrow(recovery_temp);
                try {
                    copyVerified(backup, recovery_temp);
                    renameReplacing(recovery_temp, path);
                } catch (const std::runtime_error& recovery_error) {
                    removeWithoutThrow(recovery_temp);
                    throw std::runtime_error(
                        std::string("Saved OWMAP failed validation and backup recovery failed: ") +
                        validation_error.what() + "; recovery: " + recovery_error.what());
                }
                throw std::runtime_error(
                    std::string("Saved OWMAP failed validation; restored backup: ") +
                    validation_error.what());
            }
            removeWithoutThrow(path);
            throw std::runtime_error(
                std::string("Saved OWMAP failed validation and no prior file existed: ") + validation_error.what());
        }
    } catch (...) {
        removeWithoutThrow(temp);
        removeWithoutThrow(backup_temp);
        throw;
    }
}

} // namespace pr::mapmaker
