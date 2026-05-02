#include "resort/services/BridgeImportService.hpp"

#include "core/bridge/SaveBridgeClient.hpp"
#include "resort/integration/BridgeImportAdapter.hpp"

#include <utility>

namespace pr::resort {

BridgeImportService::BridgeImportService(
    PokemonResortService& resort_service,
    std::string project_root,
    const char* argv0)
    : resort_service_(resort_service),
      project_root_(std::move(project_root)),
      argv0_(argv0) {}

BridgeSaveImportResult BridgeImportService::importSave(
    const std::string& save_path,
    const BridgeSaveImportOptions& options) {
    BridgeSaveImportResult result;
    const pr::SaveBridgeProbeResult bridge = pr::importSaveWithBridge(project_root_, argv0_, save_path);
    result.bridge_command = bridge.command;
    if (!bridge.launched || !bridge.error_message.empty()) {
        result.error = bridge.error_message.empty()
            ? "PKHeX import bridge did not launch"
            : bridge.error_message;
        return result;
    }
    if (!bridge.success) {
        result.error = bridge.stderr_text.empty()
            ? "PKHeX import bridge returned failure"
            : bridge.stderr_text;
        return result;
    }

    BridgeImportParseResult parsed = parseBridgeImportPayload(bridge.stdout_text);
    if (!parsed.success) {
        result.error = parsed.error;
        return result;
    }

    std::size_t count = 0;
    for (const ImportedPokemon& pokemon : parsed.pokemon) {
        if (options.max_pokemon > 0 && count >= options.max_pokemon) {
            break;
        }
        ImportContext context;
        context.profile_id = options.profile_id;
        const int absolute_slot = options.start_slot + static_cast<int>(count);
        context.target_location = BoxLocation{
            options.profile_id,
            options.start_box + (absolute_slot / 30),
            absolute_slot % 30
        };
        context.placement_policy = options.placement_policy;
        ImportResult import = resort_service_.importParsedPokemon(pokemon, context);
        result.imports.push_back(import);
        if (!import.success) {
            result.error = import.error;
            return result;
        }
        ++count;
    }

    result.imported_count = count;
    result.success = true;
    return result;
}

} // namespace pr::resort
