using System.Collections.Generic;
using System.Globalization;
using System.Reflection;
using System.Text.Json;
using PKHeX.Core;

namespace PKHeXBridge;

internal static partial class BridgeProjectReconcile
{
    internal static void FinalizeProjectedNickname(
        PKM pk,
        JsonElement? preSaveReview,
        JsonElement? hotMutableOverlay,
        IList<string> notes)
    {
        var nicknameFlag = ReadBool(hotMutableOverlay, "is_nicknamed") ?? ReadBool(preSaveReview, "is_nicknamed");

        if (nicknameFlag is not null)
        {
            if (nicknameFlag.Value)
            {
                var nickname =
                    ReadString(hotMutableOverlay, "nickname") ?? ReadString(preSaveReview, "nickname") ?? pk.Nickname;

                // Gen I–II store only a nickname string (no cartridge bit). PKHeX decides "nicknamed" by comparing
                // that string (after encoding) to the game's default species spelling (Red/Blue use ALL CAPS).
                // Resort `PokemonHot` can still carry `is_nicknamed: true` with plain species spelling (e.g. title case
                // from OHPKM) while the in-resort UI looks "unnamed" — finalize must clear like the cartridge would.
                if (ShouldClearGen12SpeciesDefaultNickname(pk, nickname))
                {
                    CommonEdits.ClearNickname(pk);
                    TrySetBoolProperty(pk, "IsNicknamed", false);
                    notes.Add("[finalize_nickname] gen1/2: species-name nickname with nicknamed flag; cleared to default.");
                    return;
                }
                else
                {
                    CommonEdits.SetNickname(pk, nickname);
                    TrySetBoolProperty(pk, "IsNicknamed", true);
                    notes.Add("[finalize_nickname] applied canonical nickname before serialization.");
                    return;
                }
            }
            else
            {
                CommonEdits.ClearNickname(pk);
                TrySetBoolProperty(pk, "IsNicknamed", false);
                notes.Add("[finalize_nickname] cleared nickname to target-format species default before serialization.");
            }
        }

        // Bridge requests often omit `is_nicknamed` for gen ≤2 (no cartridge bit); past projection / converters can
        // still leave title-case species text that differs from PKHeX's Game Boy encoded default spelling.
        CoerceGen12SpeciesDefaultNicknameForLegality(pk, notes);
    }

    /// <summary>
    /// Ensures Game Boy party/box nicknames use the same capitalization/encoding as PKHeX's unset default.
    /// Applies when the decoded display string is still just the species name (e.g. "Poliwhirl" vs stored "POLIWHIRL").
    /// </summary>
    private static void CoerceGen12SpeciesDefaultNicknameForLegality(PKM pk, IList<string> notes)
    {
        if (pk.Format > 2 || !ShouldClearGen12SpeciesDefaultNickname(pk, pk.Nickname))
            return;

        var before = pk.Nickname;
        var markedNicknamed = pk.IsNicknamed;
        CommonEdits.ClearNickname(pk);
        TrySetBoolProperty(pk, "IsNicknamed", false);

        if (!string.Equals(before, pk.Nickname, StringComparison.Ordinal) || markedNicknamed)
        {
            notes.Add(
                "[finalize_nickname] gen1/2: coerced species-default nickname (overlay omitted `is_nicknamed` or wrong casing / flag).");
        }
    }

    private static void ClearNicknameToSpeciesName(PKM pk)
    {
        try
        {
            CommonEdits.ClearNickname(pk);
            TrySetBoolProperty(pk, "IsNicknamed", false);
        }
        catch
        {
            TrySetBoolProperty(pk, "IsNicknamed", false);
        }
    }

    private static bool ShouldClearGen12SpeciesDefaultNickname(PKM pk, string nickname)
    {
        if (pk.Format > 2)
            return false;
        if (string.IsNullOrWhiteSpace(nickname))
            return false;

        var speciesName = GetSpeciesName(pk.Species);
        if (string.IsNullOrWhiteSpace(speciesName))
            return false;

        return string.Equals(nickname.Trim(), speciesName.Trim(), StringComparison.OrdinalIgnoreCase);
    }
}
