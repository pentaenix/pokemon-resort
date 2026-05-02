#pragma once

#include "resort/domain/ImportedPokemon.hpp"
#include "resort/domain/ResortTypes.hpp"

#include <optional>
#include <random>
#include <string>
#include <vector>

namespace pr::resort {

bool isGen12StorageFormat(const std::string& format_name);

/// PKHeX `DV16` offsets on decrypted GB PKM payloads (`pk1` / `pk2`).
bool patchPk12DvBytes(std::vector<unsigned char>& raw, const std::string& format_name, std::uint16_t dv16);

/// Big-endian DV word read from raw at format-specific offset (same layout as patch).
std::optional<std::uint16_t> readPk12Dv16FromRaw(
    const std::vector<unsigned char>& raw, const std::string& format_name);

/// Human-readable `hex=… dec=… atk/def/spd/spe=… hp_nibble=…` for stderr / support logs.
std::string formatGen12Dv16ForLog(std::uint16_t dv16);

/// Random packed Gen 1/2 DV word for replacing all-zero placeholder DVs.
std::uint16_t randomNonZeroGen12Dv16(std::mt19937& rng);

/// When packed DV16 is 0 (PKHex-style placeholder / degenerate), assign random DVs on first Resort intake,
/// or sync raw bytes from canonical when the save still reports zeros after Resort already assigned DVs.
void resolveGen12ZeroDvForResortImport(
    const std::optional<ResortPokemon>& existing_canonical,
    ImportedPokemon& imported,
    std::mt19937& rng);

} // namespace pr::resort
