#pragma once

#include "resort/domain/ImportedPokemon.hpp"
#include "resort/domain/ExportedPokemon.hpp"
#include "resort/domain/MatchMerge.hpp"
#include "resort/domain/ResortPokemonRecord.hpp"

#include <string>

namespace pr::resort::transfer {

struct ReturnMatchResult {
    bool matched = false;
    bool uncertain = false;
    openhome::OpenHomeId openhome_id;
    std::string reason;
};

struct MergeResult {
    bool success = false;
    openhome::OpenHomeId openhome_id;
    std::string error;
};

class ITransferCore {
public:
    virtual ~ITransferCore() = default;

    virtual ImportResult importFromSave(const ImportedPokemon& imported, const ImportContext& context) = 0;
    virtual ExportResult moveToGame(const openhome::OpenHomeId& openhome_id, const ExportContext& context) = 0;
    virtual ReturnMatchResult recognizeReturningPokemon(const ImportedPokemon& imported) = 0;
    virtual MergeResult mergeReturnedPokemon(const ImportedPokemon& imported, const ReturnMatchResult& match) = 0;
};

} // namespace pr::resort::transfer
