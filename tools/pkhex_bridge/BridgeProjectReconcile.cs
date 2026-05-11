using PKHeX.Core;

namespace PKHeXBridge;

/// <summary>
/// Cross-gen projection helpers. Implementation is split under tools/pkhex_bridge/reconcile by concern.
/// </summary>
internal static partial class BridgeProjectReconcile
{
    internal static void TryConfigureStringsForPokemon(PKM pk)
    {
        _ = pk;
        // PKHeX initializes GameInfo.Strings for English at startup; move-name lookups use that table.
        // Language-aware routing can be layered later via SaveFile-backed GameStrings when projecting from saves.
    }
}
