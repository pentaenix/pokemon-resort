#pragma once

#include "core/config/Json.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace pr::mapmaker {

enum class OwmapIssueSeverity {
    Warning,
    Error,
};

struct OwmapValidationIssue {
    OwmapIssueSeverity severity = OwmapIssueSeverity::Error;
    std::string code;
    std::string message;
};

// Editable OWMAP v1 document. Parsed metadata remains a complete JsonValue so
// fields unknown to the editor survive edits. An untouched loaded document
// serializes to its original bytes, including collision spare and trailing bytes.
class OwmapDocument {
public:
    static constexpr std::uint32_t kMagic = 0x4F574D31U;
    static constexpr std::uint16_t kVersion = 1U;
    static constexpr std::size_t kHeaderSize = 18U;
    static constexpr std::uint16_t kMaxDimension = 256U;

    static OwmapDocument fromBytes(std::vector<std::uint8_t> bytes);
    static OwmapDocument load(const std::filesystem::path& path);
    static OwmapDocument loadWithRecovery(
        const std::filesystem::path& path,
        const std::filesystem::path& backup_path = {});
    static OwmapDocument create(
        std::uint16_t width,
        std::uint16_t height,
        float tile_size,
        JsonValue metadata = JsonValue(JsonValue::Object{}));

    std::uint16_t width() const { return width_; }
    std::uint16_t height() const { return height_; }
    float tileSize() const { return tile_size_; }
    const std::filesystem::path& loadedPath() const { return loaded_path_; }
    bool recoveredFromBackup() const { return recovered_from_backup_; }

    JsonValue& metadata() { return metadata_; }
    const JsonValue& metadata() const { return metadata_; }
    std::vector<std::uint8_t>& heights() { return heights_; }
    const std::vector<std::uint8_t>& heights() const { return heights_; }
    std::vector<std::uint8_t>& specials() { return specials_; }
    const std::vector<std::uint8_t>& specials() const { return specials_; }
    std::vector<std::uint8_t>& collision() { return collision_; }
    const std::vector<std::uint8_t>& collision() const { return collision_; }
    std::vector<std::uint8_t>& trailingBytes() { return trailing_bytes_; }
    const std::vector<std::uint8_t>& trailingBytes() const { return trailing_bytes_; }

    std::size_t cellIndex(std::uint16_t x, std::uint16_t y) const;
    std::uint8_t& heightAt(std::uint16_t x, std::uint16_t y);
    const std::uint8_t& heightAt(std::uint16_t x, std::uint16_t y) const;
    std::uint8_t& specialAt(std::uint16_t x, std::uint16_t y);
    const std::uint8_t& specialAt(std::uint16_t x, std::uint16_t y) const;
    std::uint8_t& collisionAt(std::uint16_t x, std::uint16_t y);
    const std::uint8_t& collisionAt(std::uint16_t x, std::uint16_t y) const;

    void resize(std::uint16_t width, std::uint16_t height);
    void setTileSize(float tile_size);

    bool isPristine() const;
    std::vector<OwmapValidationIssue> validate() const;
    void validateOrThrow() const;
    std::vector<std::uint8_t> serialize(
        JsonStyle metadata_style = JsonStyle::Compact,
        std::size_t indent_size = 2) const;

    // Uses a sibling .tmp, validates its exact bytes and decoded document, then
    // atomically renames it into place. Existing targets are copied to .bak (or
    // backup_path); a failed post-replace validation restores that backup.
    void saveAtomic(
        const std::filesystem::path& path,
        const std::filesystem::path& backup_path = {});

private:
    void synchronizeMetadataGrid();

    std::uint16_t width_ = 0;
    std::uint16_t height_ = 0;
    float tile_size_ = 0.0F;
    JsonValue metadata_;
    std::vector<std::uint8_t> heights_;
    std::vector<std::uint8_t> specials_;
    std::vector<std::uint8_t> collision_;
    std::vector<std::uint8_t> collision_seed_bytes_;
    std::vector<std::uint8_t> trailing_bytes_;

    bool has_original_ = false;
    std::vector<std::uint8_t> original_bytes_;
    std::uint16_t original_width_ = 0;
    std::uint16_t original_height_ = 0;
    float original_tile_size_ = 0.0F;
    JsonValue original_metadata_;
    std::vector<std::uint8_t> original_heights_;
    std::vector<std::uint8_t> original_specials_;
    std::vector<std::uint8_t> original_collision_;
    std::vector<std::uint8_t> original_trailing_bytes_;

    std::filesystem::path loaded_path_;
    bool recovered_from_backup_ = false;
};

} // namespace pr::mapmaker
