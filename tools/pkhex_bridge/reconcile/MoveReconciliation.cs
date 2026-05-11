using System.Collections.Generic;
using System.Globalization;
using System.Reflection;
using System.Text.Json;
using PKHeX.Core;

namespace PKHeXBridge;

internal static partial class BridgeProjectReconcile
{
    internal static void ApplyMoveReconciliation(
        PKM pk,
        JsonElement moveReconciliation,
        IList<string> notes,
        ICollection<string> lostCategories)
    {
        if (moveReconciliation.ValueKind != JsonValueKind.Object)
            return;

        if (moveReconciliation.TryGetProperty("enabled", out var en) &&
            en.ValueKind == JsonValueKind.False)
        {
            return;
        }

        if (!moveReconciliation.TryGetProperty("moves", out var movesEl) ||
            movesEl.ValueKind != JsonValueKind.Array)
        {
            return;
        }

        TryConfigureStringsForPokemon(pk);

        var desired = new ushort[4];
        for (var i = 0; i < 4; i++)
            desired[i] = (ushort)pk.GetMove(i);

        foreach (var el in movesEl.EnumerateArray())
        {
            if (el.ValueKind != JsonValueKind.Object)
                continue;
            if (!el.TryGetProperty("slot_index", out var si) || si.ValueKind != JsonValueKind.Number)
                continue;
            var slot = si.GetInt32();
            if (slot < 0 || slot > 3)
                continue;

            var moveName = "";
            if (el.TryGetProperty("move_name", out var mn) && mn.ValueKind == JsonValueKind.String)
                moveName = mn.GetString() ?? "";

            var catalogMoveId = 0;
            if (el.TryGetProperty("move_id", out var mid) && mid.ValueKind == JsonValueKind.Number)
                catalogMoveId = mid.GetInt32();

            var before = desired[slot];
            var resolved = ResolveMoveIdForProjection(pk, moveName.Trim(), catalogMoveId, notes);
            desired[slot] = (ushort)resolved;
            if (resolved != before && before != 0)
                notes.Add($"[move] slot {slot}: replaced move id {before} with {resolved} (catalog name \"{moveName}\").");
        }

        // Ensure no empty learnset: fill zero slots with a safe placeholder.
        var placeholder = ResolvePlaceholderMoveId(pk);
        var hadPlaceholder = false;
        for (var i = 0; i < 4; i++)
        {
            if (desired[i] != 0)
                continue;
            // If every slot is zero, we still fill all four with placeholder (valid simple move).
            desired[i] = placeholder;
            hadPlaceholder = true;
        }

        if (hadPlaceholder)
        {
            lostCategories.Add("move_placeholder_fill");
            notes.Add($"[move] applied placeholder move id {placeholder} ({GetMoveNameOrFallback(placeholder)}) to empty slot(s).");
        }

        ApplyMoveArray(pk, desired);

        // Refresh PP to legal max for the format (PKHeX MoveInfo).
        HealMovePp(pk);
    }

    private static void ApplyOverlayMoves(PKM pk, JsonElement moves, IList<string> notes)
    {
        var entries = new List<(ushort Move, byte Pp, byte PpUps)>(4);

        foreach (var move in moves.EnumerateArray())
        {
            if (move.ValueKind != JsonValueKind.Object)
                continue;
            if (!move.TryGetProperty("slot_index", out var slotEl) || slotEl.ValueKind != JsonValueKind.Number)
                continue;
            var slot = slotEl.GetInt32();
            if (slot < 0 || slot > 3)
                continue;

            ushort moveId = 0;
            if (move.TryGetProperty("move_id", out var moveIdEl) && moveIdEl.ValueKind == JsonValueKind.Number)
                moveId = (ushort)Math.Clamp(moveIdEl.GetInt32(), 0, ushort.MaxValue);
            if (moveId == 0)
                continue;
            if (!MoveHasTargetPp(pk, moveId))
            {
                notes.Add($"[hot_mutable_overlay] dropped move id {moveId}; not valid for target {pk.GetType().Name}.");
                continue;
            }

            var requestedPp = move.TryGetProperty("current_pp", out var pp) && pp.ValueKind == JsonValueKind.Number
                ? pp.GetInt32()
                : SafeMovePp(pk, moveId);
            var requestedPpUps = move.TryGetProperty("pp_ups", out var ppUp) && ppUp.ValueKind == JsonValueKind.Number
                ? ppUp.GetInt32()
                : 0;
            entries.Add((moveId, ClampMovePp(pk, moveId, requestedPp), (byte)Math.Clamp(requestedPpUps, 0, 3)));
        }

        if (entries.Count == 0)
        {
            const ushort tackle = 33;
            entries.Add((tackle, SafeMovePp(pk, tackle), 0));
            notes.Add("[hot_mutable_overlay] filled empty/invalid moveset with Tackle.");
        }

        var moveIds = new ushort[4];
        var pps = new byte[4];
        var ppUps = new byte[4];
        for (var i = 0; i < Math.Min(4, entries.Count); i++)
        {
            moveIds[i] = entries[i].Move;
            pps[i] = entries[i].Pp;
            ppUps[i] = entries[i].PpUps;
        }

        pk.Move1 = moveIds[0];
        pk.Move2 = moveIds[1];
        pk.Move3 = moveIds[2];
        pk.Move4 = moveIds[3];
        pk.Move1_PP = pps[0];
        pk.Move2_PP = pps[1];
        pk.Move3_PP = pps[2];
        pk.Move4_PP = pps[3];
        pk.Move1_PPUps = ppUps[0];
        pk.Move2_PPUps = ppUps[1];
        pk.Move3_PPUps = ppUps[2];
        pk.Move4_PPUps = ppUps[3];
    }

    private static int ResolveMoveIdForProjection(PKM pk, string moveName, int catalogMoveId, IList<string> notes)
    {
        if (!string.IsNullOrEmpty(moveName))
        {
            var byName = FindMoveIdByName(moveName);
            if (byName > 0)
                return byName;
            notes.Add($"[move] unknown move name \"{moveName}\" for target string table; trying catalog move_id fallback.");
        }

        if (catalogMoveId > 0 && MoveExistsInStringTable(catalogMoveId))
            return catalogMoveId;

        if (catalogMoveId > 0)
            notes.Add($"[move] catalog move_id {catalogMoveId} is not a known move index in the target table.");

        return 0;
    }

    private static byte ClampMovePp(PKM pk, ushort moveId, int requestedPp)
    {
        if (moveId == 0)
            return 0;
        try
        {
            var max = Math.Clamp((int)MoveInfo.GetPP(pk.Context, moveId), 1, 255);
            return (byte)Math.Clamp(requestedPp, 1, max);
        }
        catch
        {
            return ClampByte(requestedPp);
        }
    }

    private static bool MoveHasTargetPp(PKM pk, ushort moveId)
    {
        try
        {
            return MoveInfo.GetPP(pk.Context, moveId) > 0;
        }
        catch
        {
            return false;
        }
    }

    private static byte SafeMovePp(PKM pk, ushort moveId)
    {
        try
        {
            return (byte)Math.Clamp((int)MoveInfo.GetPP(pk.Context, moveId), 1, 255);
        }
        catch
        {
            return 35;
        }
    }

    private static bool MoveExistsInStringTable(int moveId)
    {
        var table = GetMoveStringTable();
        return table is not null && moveId > 0 && moveId < table.Count && !string.IsNullOrEmpty(table[moveId]);
    }

    private static IReadOnlyList<string>? GetMoveStringTable()
    {
        try
        {
            return GameInfo.Strings.GetType().GetProperty("Move")?.GetValue(GameInfo.Strings) as IReadOnlyList<string>;
        }
        catch
        {
            return null;
        }
    }

    private static int FindMoveIdByName(string moveName)
    {
        var table = GetMoveStringTable();
        if (table is null || string.IsNullOrWhiteSpace(moveName))
            return 0;

        for (var i = 1; i < table.Count; i++)
        {
            var cell = table[i];
            if (string.IsNullOrEmpty(cell))
                continue;
            if (string.Equals(cell, moveName, StringComparison.OrdinalIgnoreCase))
                return i;
        }

        return 0;
    }

    private static ushort ResolvePlaceholderMoveId(PKM pk)
    {
        TryConfigureStringsForPokemon(pk);
        foreach (var candidate in new[] { "Tackle", "Pound", "Growl", "Leer", "Scratch" })
        {
            var id = FindMoveIdByName(candidate);
            if (id > 0)
                return (ushort)id;
        }

        // Last resort: first non-empty entry in the move table (usually index 1).
        var table = GetMoveStringTable();
        if (table is not null)
        {
            for (var i = 1; i < table.Count; i++)
            {
                if (!string.IsNullOrEmpty(table[i]))
                    return (ushort)i;
            }
        }

        return 1;
    }

    private static string GetMoveNameOrFallback(int moveId)
    {
        var table = GetMoveStringTable();
        if (table is not null && moveId > 0 && moveId < table.Count && !string.IsNullOrEmpty(table[moveId]))
            return table[moveId]!;
        return "move_" + moveId.ToString(CultureInfo.InvariantCulture);
    }

    /// <summary>
    /// Replays Resort canonical ribbon catalog onto the projected PKM after conversion and overlays.
    /// Booleans (true) map to writable ribbon bool properties; numbers use max(existing, catalog) for
    /// tier/count fields. Unknown keys or formats without the property are skipped.

    private static void ApplyMoveArray(PKM pk, ushort[] moves4)
    {
        pk.Move1 = moves4[0];
        pk.Move2 = moves4[1];
        pk.Move3 = moves4[2];
        pk.Move4 = moves4[3];
    }

    private static void HealMovePp(PKM pk)
    {
        TrySetMovePp(pk, 0, pk.Move1);
        TrySetMovePp(pk, 1, pk.Move2);
        TrySetMovePp(pk, 2, pk.Move3);
        TrySetMovePp(pk, 3, pk.Move4);
    }

    private static void TrySetMovePp(PKM pk, int slot, ushort moveId)
    {
        if (moveId == 0)
            return;
        try
        {
            var max = MoveInfo.GetPP(pk.Context, moveId);
            var pp = (byte)Math.Clamp((int)max, 0, 255);
            switch (slot)
            {
                case 0:
                    pk.Move1_PP = pp;
                    break;
                case 1:
                    pk.Move2_PP = pp;
                    break;
                case 2:
                    pk.Move3_PP = pp;
                    break;
                default:
                    pk.Move4_PP = pp;
                    break;
            }
        }
        catch
        {
            // Ignore PP heal failures on exotic formats.
        }
    }
}
