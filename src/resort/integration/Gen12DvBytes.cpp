#include "resort/integration/Gen12DvBytes.hpp"

#include "core/Sha256.hpp"

#include <cctype>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace pr::resort {
namespace {

constexpr const char* kTempTransferLog = "[TEMP_TRANSFER_LOG_DELETE]";

std::string lowerFormat(const std::string& format_name) {
    std::string out = format_name;
    for (char& ch : out) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return out;
}

std::uint16_t packedDvFromImported(const ImportedPokemon& imported) {
    if (imported.hot.dv16) {
        return *imported.hot.dv16;
    }
    if (imported.identity.dv16) {
        return *imported.identity.dv16;
    }
    return 0;
}

void writeBigEndian16(std::vector<unsigned char>& raw, std::size_t offset, std::uint16_t value) {
    raw[offset] = static_cast<unsigned char>((value >> 8) & 0xff);
    raw[offset + 1] = static_cast<unsigned char>(value & 0xff);
}

} // namespace

bool isGen12StorageFormat(const std::string& format_name) {
    const std::string fmt = lowerFormat(format_name);
    return fmt == "pk1" || fmt == "pk2";
}

bool patchPk12DvBytes(std::vector<unsigned char>& raw, const std::string& format_name, std::uint16_t dv16) {
    const std::string fmt = lowerFormat(format_name);
    std::size_t offset = 0;
    if (fmt == "pk1") {
        offset = 0x1b;
    } else if (fmt == "pk2") {
        offset = 0x15;
    } else {
        return false;
    }
    if (raw.size() < offset + 2) {
        return false;
    }
    writeBigEndian16(raw, offset, dv16);
    return true;
}

std::optional<std::uint16_t> readPk12Dv16FromRaw(
    const std::vector<unsigned char>& raw, const std::string& format_name) {
    const std::string fmt = lowerFormat(format_name);
    std::size_t offset = 0;
    if (fmt == "pk1") {
        offset = 0x1b;
    } else if (fmt == "pk2") {
        offset = 0x15;
    } else {
        return std::nullopt;
    }
    if (raw.size() < offset + 2) {
        return std::nullopt;
    }
    const std::uint16_t be = static_cast<std::uint16_t>(
        (static_cast<unsigned>(raw[offset]) << 8) | static_cast<unsigned>(raw[offset + 1]));
    return be;
}

std::string formatGen12Dv16ForLog(std::uint16_t dv16) {
    const unsigned atk = (static_cast<unsigned>(dv16) >> 12) & 0x0fu;
    const unsigned def = (static_cast<unsigned>(dv16) >> 8) & 0x0fu;
    const unsigned spd = (static_cast<unsigned>(dv16) >> 4) & 0x0fu;
    const unsigned spe = static_cast<unsigned>(dv16) & 0x0fu;
    const unsigned hp_nib =
        ((atk & 1u) << 3) | ((def & 1u) << 2) | ((spd & 1u) << 1u) | (spe & 1u);
    std::ostringstream out;
    out << "hex=0x" << std::hex << std::setw(4) << std::setfill('0') << static_cast<unsigned>(dv16)
        << std::dec << " dec=" << static_cast<unsigned>(dv16)
        << " atk/def/spd/spe=" << atk << '/' << def << '/' << spd << '/' << spe
        << " hp_nibble=" << hp_nib;
    return out.str();
}

std::uint16_t randomNonZeroGen12Dv16(std::mt19937& rng) {
    std::uniform_int_distribution<int> nibble(0, 15);
    for (;;) {
        const std::uint16_t v = static_cast<std::uint16_t>(
            (static_cast<unsigned>(nibble(rng)) << 12) | (static_cast<unsigned>(nibble(rng)) << 8) |
            (static_cast<unsigned>(nibble(rng)) << 4) | static_cast<unsigned>(nibble(rng)));
        if (v != 0) {
            return v;
        }
    }
}

void resolveGen12ZeroDvForResortImport(
    const std::optional<ResortPokemon>& existing_canonical,
    ImportedPokemon& imported,
    std::mt19937& rng) {
    if (!isGen12StorageFormat(imported.format_name)) {
        return;
    }
    const std::uint16_t incoming = packedDvFromImported(imported);
    if (incoming != 0) {
        std::cerr << kTempTransferLog << " Gen12 DV intake (no patch) " << formatGen12Dv16ForLog(incoming)
                  << " format=" << imported.format_name << '\n';
        return;
    }

    std::optional<std::uint16_t> use_dv;
    if (existing_canonical && existing_canonical->hot.dv16 && *existing_canonical->hot.dv16 != 0) {
        use_dv = *existing_canonical->hot.dv16;
        std::cerr << kTempTransferLog << " Gen12 DV placeholder (0); syncing raw PK from canonical "
                  << formatGen12Dv16ForLog(*use_dv) << " pkrid=" << existing_canonical->id.pkrid
                  << " format=" << imported.format_name << '\n';
    } else {
        use_dv = randomNonZeroGen12Dv16(rng);
        std::cerr << kTempTransferLog << " Gen12 DV placeholder (0); assigning random "
                  << formatGen12Dv16ForLog(*use_dv) << " format=" << imported.format_name << '\n';
    }

    if (!patchPk12DvBytes(imported.raw_bytes, imported.format_name, *use_dv)) {
        std::cerr << "Warning: Gen12 DV assign skipped (raw payload too small for format=" << imported.format_name
                  << " bytes=" << imported.raw_bytes.size() << ")\n";
        return;
    }
    if (const auto read_back = readPk12Dv16FromRaw(imported.raw_bytes, imported.format_name)) {
        std::cerr << kTempTransferLog << " Gen12 DV raw bytes after patch " << formatGen12Dv16ForLog(*read_back)
                  << " (verify read-back) format=" << imported.format_name << '\n';
    }
    imported.hot.dv16 = *use_dv;
    imported.identity.dv16 = *use_dv;
    imported.raw_hash_sha256 = pr::sha256HexLowercase(imported.raw_bytes);
}

} // namespace pr::resort
