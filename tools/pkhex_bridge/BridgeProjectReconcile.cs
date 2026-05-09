using System.Collections.Generic;
using System.Globalization;
using System.Reflection;
using System.Text.Json;
using PKHeX.Core;

namespace PKHeXBridge;

/// <summary>
/// Cross-gen projection helpers: reconcile moves by Resort catalog names for the target format,
/// then apply pre-save review fields from canonical Resort metadata.
/// </summary>
internal static class BridgeProjectReconcile
{
    /// <summary>
    /// Optional catalog keys that are not identical to PKHeX property names. Values are tried in order
    /// after the JSON key itself (e.g. warm JSON may use <c>RibbonChampion</c> for Hoenn Champion).
    /// </summary>
    private static readonly IReadOnlyDictionary<string, string[]> RibbonCatalogToPkHeXPropertyAliases =
        new Dictionary<string, string[]>(StringComparer.Ordinal)
        {
            ["RibbonChampion"] = new[] { "RibbonChampionG3" },
        };

    internal static void TryConfigureStringsForPokemon(PKM pk)
    {
        _ = pk;
        // PKHeX initializes GameInfo.Strings for English at startup; move-name lookups use that table.
        // Language-aware routing can be layered later via SaveFile-backed GameStrings when projecting from saves.
    }

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

    internal static void ApplyPreSaveReview(
        PKM pk,
        JsonElement review,
        IList<string> notes)
    {
        if (review.ValueKind != JsonValueKind.Object)
            return;

        if (review.TryGetProperty("enabled", out var en) && en.ValueKind == JsonValueKind.False)
            return;

        var touched = false;

        if (review.TryGetProperty("friendship_ot", out var fot) && fot.ValueKind == JsonValueKind.Number)
        {
            pk.OriginalTrainerFriendship = ClampByte(fot.GetInt32());
            touched = true;
        }

        if (review.TryGetProperty("friendship_handling", out var fh) && fh.ValueKind == JsonValueKind.Number)
        {
            pk.HandlingTrainerFriendship = ClampByte(fh.GetInt32());
            touched = true;
        }

        if (review.TryGetProperty("friendship_current", out var fc) && fc.ValueKind == JsonValueKind.Number)
        {
            pk.CurrentFriendship = ClampByte(fc.GetInt32());
            touched = true;
        }

        if (review.TryGetProperty("nickname", out var nn) && nn.ValueKind == JsonValueKind.String)
        {
            var name = nn.GetString() ?? "";
            pk.Nickname = name;
            touched = true;
        }

        if (review.TryGetProperty("is_nicknamed", out var ink) && ink.ValueKind == JsonValueKind.True)
        {
            TrySetBoolProperty(pk, "IsNicknamed", true);
            touched = true;
        }
        else if (review.TryGetProperty("is_nicknamed", out ink) && ink.ValueKind == JsonValueKind.False)
        {
            ClearNicknameToSpeciesName(pk);
            touched = true;
        }

        if (review.TryGetProperty("pokerus_strain", out var ps) && ps.ValueKind == JsonValueKind.Number)
        {
            TrySetIntProperty(pk, "PokerusState", ClampByte(ps.GetInt32()));
            TrySetIntProperty(pk, "PokerusStrain", ClampByte(ps.GetInt32()));
            TrySetIntProperty(pk, "PKRS_Strain", ClampByte(ps.GetInt32()));
            touched = true;
        }

        if (review.TryGetProperty("pokerus_days", out var pd) && pd.ValueKind == JsonValueKind.Number)
        {
            TrySetIntProperty(pk, "PokerusDays", ClampByte(pd.GetInt32()));
            TrySetIntProperty(pk, "PKRS_Days", ClampByte(pd.GetInt32()));
            touched = true;
        }

        Nature? requestedNature = null;
        if (review.TryGetProperty("nature", out var natureEl) && natureEl.ValueKind == JsonValueKind.String &&
            Enum.TryParse<Nature>(natureEl.GetString(), ignoreCase: true, out var nature))
        {
            requestedNature = nature;
        }

        var applyStaticFields =
            review.TryGetProperty("apply_static_fields", out var applyStatic) &&
            applyStatic.ValueKind == JsonValueKind.True;
        if (applyStaticFields)
        {
            if (review.TryGetProperty("met_location_id", out var metLoc) && metLoc.ValueKind == JsonValueKind.Number)
            {
                pk.MetLocation = (ushort)Math.Clamp(metLoc.GetInt32(), 0, ushort.MaxValue);
                touched = true;
            }

            if (review.TryGetProperty("met_level", out var metLevel) && metLevel.ValueKind == JsonValueKind.Number)
            {
                pk.MetLevel = ClampByte(metLevel.GetInt32());
                touched = true;
            }

            if (review.TryGetProperty("ball_id", out var ballId) && ballId.ValueKind == JsonValueKind.Number)
            {
                pk.Ball = BallForTargetFormat(pk, ballId.GetInt32());
                touched = true;
            }

            if (review.TryGetProperty("origin_game", out var originGame) && originGame.ValueKind == JsonValueKind.Number)
            {
                pk.Version = (GameVersion)originGame.GetInt32();
                touched = true;
            }

            if (review.TryGetProperty("language", out var language) && language.ValueKind == JsonValueKind.Number)
            {
                pk.Language = language.GetInt32();
                touched = true;
            }

            if (review.TryGetProperty("ot_name", out var otName) && otName.ValueKind == JsonValueKind.String)
            {
                ApplyTrainerName(pk, otName.GetString() ?? "");
                touched = true;
            }

            if (review.TryGetProperty("tid16", out var tid16) && tid16.ValueKind == JsonValueKind.Number)
            {
                pk.TID16 = (ushort)Math.Clamp(tid16.GetInt32(), 0, ushort.MaxValue);
                touched = true;
            }

            if (review.TryGetProperty("sid16", out var sid16) && sid16.ValueKind == JsonValueKind.Number)
            {
                pk.SID16 = (ushort)Math.Clamp(sid16.GetInt32(), 0, ushort.MaxValue);
                touched = true;
            }

            if (review.TryGetProperty("pid", out var pid) && pid.ValueKind == JsonValueKind.Number)
            {
                pk.PID = pid.GetUInt32();
                touched = true;
            }

            if (review.TryGetProperty("encryption_constant", out var ec) && ec.ValueKind == JsonValueKind.Number)
            {
                pk.EncryptionConstant = ec.GetUInt32();
                touched = true;
            }

            if (review.TryGetProperty("fateful_encounter", out var fateful))
            {
                if (fateful.ValueKind is JsonValueKind.True or JsonValueKind.False)
                {
                    TrySetBoolProperty(pk, "FatefulEncounter", fateful.GetBoolean());
                    touched = true;
                }
            }
        }

        // Gen III/IV derive nature from PID. Apply this after any canonical PID/static overlay so the
        // transport PID can change to preserve Resort's visible nature instead of reverting to PID % 25.
        if (requestedNature is { } finalNature)
        {
            ApplyNature(pk, finalNature);
            touched = true;
        }

        if (touched)
            notes.Add(applyStaticFields
                ? "[pre_save_review] applied canonical mutable/static fields from Resort metadata."
                : "[pre_save_review] applied canonical mutable fields from Resort metadata.");
    }

    internal static void ApplyHotMutableOverlay(PKM pk, JsonElement overlay, IList<string> notes)
    {
        if (overlay.ValueKind != JsonValueKind.Object)
            return;

        if (overlay.TryGetProperty("enabled", out var en) && en.ValueKind == JsonValueKind.False)
            return;

        var touched = false;

        if (overlay.TryGetProperty("species_id", out var species) && species.ValueKind == JsonValueKind.Number)
        {
            pk.Species = (ushort)Math.Clamp(species.GetInt32(), 0, ushort.MaxValue);
            touched = true;
        }

        if (overlay.TryGetProperty("form_id", out var form) && form.ValueKind == JsonValueKind.Number)
        {
            pk.Form = ClampByte(form.GetInt32());
            touched = true;
        }

        if (overlay.TryGetProperty("exp", out var exp) && exp.ValueKind == JsonValueKind.Number)
        {
            pk.EXP = exp.GetUInt32();
            touched = true;
        }

        var overlaySaysNotNicknamed = false;
        if (overlay.TryGetProperty("nickname", out var nn) && nn.ValueKind == JsonValueKind.String)
        {
            pk.Nickname = nn.GetString() ?? "";
            touched = true;
        }

        if (overlay.TryGetProperty("is_nicknamed", out var ink))
        {
            if (ink.ValueKind is JsonValueKind.True or JsonValueKind.False)
            {
                TrySetBoolProperty(pk, "IsNicknamed", ink.GetBoolean());
                overlaySaysNotNicknamed = !ink.GetBoolean();
                touched = true;
            }
        }

        if (overlaySaysNotNicknamed)
        {
            ClearNicknameToSpeciesName(pk);
        }

        if (overlay.TryGetProperty("gender", out var gender) && gender.ValueKind == JsonValueKind.Number)
        {
            pk.Gender = ClampByte(gender.GetInt32());
            touched = true;
        }

        if (overlay.TryGetProperty("ot_name", out var otName) && otName.ValueKind == JsonValueKind.String)
        {
            ApplyTrainerName(pk, otName.GetString() ?? "");
            touched = true;
        }

        if (overlay.TryGetProperty("tid16", out var tid16) && tid16.ValueKind == JsonValueKind.Number)
        {
            pk.TID16 = (ushort)Math.Clamp(tid16.GetInt32(), 0, ushort.MaxValue);
            touched = true;
        }

        if (overlay.TryGetProperty("sid16", out var sid16) && sid16.ValueKind == JsonValueKind.Number)
        {
            pk.SID16 = (ushort)Math.Clamp(sid16.GetInt32(), 0, ushort.MaxValue);
            touched = true;
        }

        if (overlay.TryGetProperty("tid32", out var tid32) && tid32.ValueKind == JsonValueKind.Number)
        {
            pk.ID32 = tid32.GetUInt32();
            touched = true;
        }

        if (overlay.TryGetProperty("held_item_id", out var held) && held.ValueKind == JsonValueKind.Number)
        {
            pk.HeldItem = (ushort)Math.Clamp(held.GetInt32(), 0, ushort.MaxValue);
            touched = true;
        }

        if (overlay.TryGetProperty("hp_current", out var hpCur) && hpCur.ValueKind == JsonValueKind.Number)
        {
            pk.Stat_HPCurrent = (ushort)Math.Clamp(hpCur.GetInt32(), 0, ushort.MaxValue);
            touched = true;
        }

        if (overlay.TryGetProperty("hp_max", out var hpMax) && hpMax.ValueKind == JsonValueKind.Number)
        {
            pk.Stat_HPMax = (ushort)Math.Clamp(hpMax.GetInt32(), 0, ushort.MaxValue);
            touched = true;
        }

        if (overlay.TryGetProperty("status_flags", out var status) && status.ValueKind == JsonValueKind.Number)
        {
            pk.Status_Condition = (int)Math.Clamp(status.GetInt64(), 0, int.MaxValue);
            touched = true;
        }

        if (overlay.TryGetProperty("moves", out var moves) && moves.ValueKind == JsonValueKind.Array)
        {
            ApplyOverlayMoves(pk, moves, notes);
            touched = true;
        }

        // Apply after OT/TID/SID/moves: those edits can break Gen III shiny XOR even when Resort says shiny.
        if (overlay.TryGetProperty("shiny", out var shinyEl) && shinyEl.ValueKind == JsonValueKind.True)
        {
            if (!pk.IsShiny && !CommonEdits.SetIsShiny(pk, true))
            {
                notes.Add(
                    "[hot_mutable_overlay] canonical shiny=true but PKHeX could not set shiny; bridge project will still run PID repair from source.");
            }
            touched = true;
        }

        if (touched)
            notes.Add("[hot_mutable_overlay] surgically applied canonical mutable hot fields to target payload.");
    }

    internal static void FinalizeProjectedIdentity(
        PKM source,
        PKM pk,
        JsonElement? preSaveReview,
        JsonElement? hotMutableOverlay,
        IList<string> notes)
    {
        if (pk.Format is not (3 or 4))
            return;

        var desiredNature = ReadNature(preSaveReview) ?? pk.Nature;
        var desiredShiny = ReadBool(hotMutableOverlay, "shiny") ?? source.IsShiny;
        var desiredGender = ReadByte(hotMutableOverlay, "gender") ?? pk.Gender;
        var desiredForm = ReadByte(hotMutableOverlay, "form_id") ?? pk.Form;
        var desiredAbilityIndex = ReadAbilityIndex(hotMutableOverlay);

        pk.Form = desiredForm;
        pk.Gender = desiredGender;
        pk.Nature = desiredNature;

        if (desiredAbilityIndex is { } abilityIndex)
            CommonEdits.SetAbilityIndex(pk, abilityIndex);

        var seed = unchecked((int)(pk.PID ^ ((uint)pk.Species << 16) ^ ((uint)desiredNature << 8) ^ desiredForm));
        var rng = new Random(seed);
        var abilitySeed = desiredAbilityIndex is { } ai ? (uint)(Math.Clamp(ai, 0, 1) * 0x10001) : pk.PID;
        const int maxAttempts = 1_000_000;
        var solved = false;

        for (var attempt = 0; attempt < maxAttempts; attempt++)
        {
            pk.PID = EntityPID.GetRandomPID(
                rng,
                pk.Species,
                desiredGender,
                pk.Version,
                desiredNature,
                desiredForm,
                abilitySeed);
            pk.Nature = desiredNature;

            if (MatchesFinalIdentity(pk, desiredNature, desiredShiny, desiredGender, desiredForm, desiredAbilityIndex))
            {
                solved = true;
                break;
            }
        }

        if (!solved)
        {
            throw new InvalidOperationException(
                $"finalize_identity could not solve PID-derived constraints for format={pk.Format} species={pk.Species} " +
                $"form={desiredForm} nature={desiredNature} gender={desiredGender} shiny={desiredShiny} " +
                $"ability_index={(desiredAbilityIndex?.ToString(CultureInfo.InvariantCulture) ?? "unspecified")}.");
        }

        AddFinalIdentityNote(pk, notes, "finalize_identity");
    }

    internal static void ValidateFinalProjectedIdentity(
        PKM source,
        PKM pk,
        JsonElement? preSaveReview,
        JsonElement? hotMutableOverlay,
        IList<string> notes)
    {
        if (pk.Format is not (3 or 4))
            return;

        var desiredNature = ReadNature(preSaveReview) ?? pk.Nature;
        var desiredShiny = ReadBool(hotMutableOverlay, "shiny") ?? source.IsShiny;
        var desiredGender = ReadByte(hotMutableOverlay, "gender") ?? pk.Gender;
        var desiredForm = ReadByte(hotMutableOverlay, "form_id") ?? pk.Form;
        var desiredAbilityIndex = ReadAbilityIndex(hotMutableOverlay);
        if (!MatchesFinalIdentity(pk, desiredNature, desiredShiny, desiredGender, desiredForm, desiredAbilityIndex))
        {
            throw new InvalidOperationException(
                $"finalize_identity validation failed for format={pk.Format} species={pk.Species} " +
                $"form={pk.Form}/{desiredForm} nature={pk.Nature}/{desiredNature} gender={pk.Gender}/{desiredGender} " +
                $"shiny={pk.IsShiny}/{desiredShiny} pid={pk.PID} pid_mod_25={pk.PID % 25}.");
        }

        AddFinalIdentityNote(pk, notes, "finalize_identity_validate");
    }

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

    private static void ApplyTrainerName(PKM pk, string otName)
    {
        if (string.IsNullOrWhiteSpace(otName))
            return;
        otName = NormalizeTrainerName(pk, otName);
        if (pk.Format == 3)
        {
            try
            {
                StringConverter3.SetString(
                    pk.OriginalTrainerTrash,
                    otName,
                    pk.MaxStringLengthTrainer,
                    pk.Language == (int)LanguageID.Japanese,
                    StringConverterOption.ClearFF);
                return;
            }
            catch
            {
            }
        }
        pk.OriginalTrainerName = otName;
    }

    private static string NormalizeTrainerName(PKM pk, string otName)
    {
        var maxLength = pk.MaxStringLengthTrainer;
        if (maxLength <= 0 || otName.Length <= maxLength)
            return otName;

        return otName[..maxLength];
    }

    private static byte ClampByte(int v) => (byte)Math.Clamp(v, 0, 255);

    private static byte BallForTargetFormat(PKM pk, int requested)
    {
        const byte pokeBall = 4;
        if (requested <= 0)
            return pokeBall;

        var max = pk.Format switch
        {
            <= 2 => 0,
            3 => 12,
            4 => 16,
            5 => 16,
            _ => 255
        };
        if (max <= 0)
            return 0;
        return requested <= max ? ClampByte(requested) : pokeBall;
    }

    private static void ApplyNature(PKM pk, Nature nature)
    {
        CommonEdits.SetNature(pk, nature);
        if (pk.Format is >= 5 and < 8)
            pk.Nature = nature;
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
    /// </summary>
    internal static int ApplyCanonicalRibbonCatalog(PKM pk, JsonElement ribbonCatalog, IList<string> notes)
    {
        var t = pk.GetType();
        var applied = 0;
        foreach (var prop in ribbonCatalog.EnumerateObject())
        {
            if (prop.Value.ValueKind == JsonValueKind.True)
            {
                if (TrySetRibbonBoolProperty(t, pk, prop.Name))
                {
                    applied++;
                    continue;
                }

                if (RibbonCatalogToPkHeXPropertyAliases.TryGetValue(prop.Name, out var aliases))
                {
                    foreach (var alt in aliases)
                    {
                        if (TrySetRibbonBoolProperty(t, pk, alt))
                        {
                            applied++;
                            break;
                        }
                    }
                }

                continue;
            }

            if (prop.Value.ValueKind == JsonValueKind.Number &&
                TryApplyRibbonIntegralMax(t, pk, prop.Name, prop.Value.GetInt32()))
            {
                applied++;
            }
        }

        if (applied > 0)
        {
            notes.Add(
                $"[ribbon_catalog] replayed {applied.ToString(CultureInfo.InvariantCulture)} canonical ribbon catalog field(s) onto {t.Name}.");
        }

        return applied;
    }

    private static bool TryApplyRibbonIntegralMax(Type t, PKM pk, string propertyName, int incoming)
    {
        var pi = t.GetProperty(propertyName, BindingFlags.Public | BindingFlags.Instance);
        if (pi?.CanWrite != true)
            return false;

        var pt = pi.PropertyType;
        if (pt != typeof(byte) && pt != typeof(sbyte) && pt != typeof(short) && pt != typeof(ushort) &&
            pt != typeof(int) && pt != typeof(uint))
            return false;

        try
        {
            var cur = 0;
            if (pi.CanRead)
            {
                var raw = pi.GetValue(pk);
                if (raw is not null)
                    cur = Convert.ToInt32(raw, CultureInfo.InvariantCulture);
            }

            var v = Math.Max(cur, incoming);
            object converted = pt switch
            {
                _ when pt == typeof(byte) => (byte)Math.Clamp(v, byte.MinValue, byte.MaxValue),
                _ when pt == typeof(sbyte) => (sbyte)Math.Clamp(v, sbyte.MinValue, sbyte.MaxValue),
                _ when pt == typeof(short) => (short)Math.Clamp(v, short.MinValue, short.MaxValue),
                _ when pt == typeof(ushort) => (ushort)Math.Clamp(v, ushort.MinValue, ushort.MaxValue),
                _ when pt == typeof(uint) => (uint)Math.Clamp((long)v, uint.MinValue, uint.MaxValue),
                _ => Math.Clamp(v, int.MinValue, int.MaxValue)
            };
            pi.SetValue(pk, converted);
            return true;
        }
        catch
        {
            return false;
        }
    }

    private static bool TrySetRibbonBoolProperty(Type t, PKM pk, string propertyName)
    {
        var pi = t.GetProperty(propertyName, BindingFlags.Public | BindingFlags.Instance);
        if (pi?.PropertyType != typeof(bool) || !pi.CanWrite)
            return false;

        try
        {
            pi.SetValue(pk, true);
            return true;
        }
        catch
        {
            // Ribbon accessors may throw on illegal combinations for this format; skip.
            return false;
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

    private static bool MatchesFinalIdentity(
        PKM pk,
        Nature desiredNature,
        bool desiredShiny,
        byte desiredGender,
        byte desiredForm,
        int? desiredAbilityIndex)
    {
        if (pk.Nature != desiredNature)
            return false;
        if (pk.PID % 25 != (uint)desiredNature)
            return false;
        if (pk.IsShiny != desiredShiny)
            return false;
        if (pk.Gender != desiredGender)
            return false;
        if (pk.Form != desiredForm)
            return false;
        if (desiredAbilityIndex is { } abilityIndex && Math.Clamp(abilityIndex, 0, 1) != pk.PIDAbility)
            return false;
        return true;
    }

    private static void AddFinalIdentityNote(PKM pk, IList<string> notes, string tag)
    {
        notes.Add(
            $"[{tag}] format={pk.Format} species={pk.Species} form={pk.Form} nature={pk.Nature} " +
            $"gender={pk.Gender} shiny={pk.IsShiny.ToString().ToLowerInvariant()} pid={pk.PID} " +
            $"pid_mod_25={pk.PID % 25} ability_index={pk.PIDAbility}.");
    }

    private static Nature? ReadNature(JsonElement? obj)
    {
        if (obj is not { ValueKind: JsonValueKind.Object } value)
            return null;
        if (!value.TryGetProperty("nature", out var natureEl) || natureEl.ValueKind != JsonValueKind.String)
            return null;
        return Enum.TryParse<Nature>(natureEl.GetString(), ignoreCase: true, out var nature) ? nature : null;
    }

    private static bool? ReadBool(JsonElement? obj, string property)
    {
        if (obj is not { ValueKind: JsonValueKind.Object } value)
            return null;
        if (!value.TryGetProperty(property, out var el))
            return null;
        return el.ValueKind switch
        {
            JsonValueKind.True => true,
            JsonValueKind.False => false,
            _ => null
        };
    }

    private static byte? ReadByte(JsonElement? obj, string property)
    {
        if (obj is not { ValueKind: JsonValueKind.Object } value)
            return null;
        if (!value.TryGetProperty(property, out var el) || el.ValueKind != JsonValueKind.Number)
            return null;
        return ClampByte(el.GetInt32());
    }

    private static string? ReadString(JsonElement? obj, string property)
    {
        if (obj is not { ValueKind: JsonValueKind.Object } value)
            return null;
        if (!value.TryGetProperty(property, out var el) || el.ValueKind != JsonValueKind.String)
            return null;
        return el.GetString();
    }

    private static int? ReadAbilityIndex(JsonElement? obj)
    {
        if (obj is not { ValueKind: JsonValueKind.Object } value)
            return null;

        foreach (var property in new[] { "ability_index", "ability_slot", "pid_ability" })
        {
            if (!value.TryGetProperty(property, out var el) || el.ValueKind != JsonValueKind.Number)
                continue;
            return Math.Clamp(el.GetInt32(), 0, 1);
        }

        if (value.TryGetProperty("ability_number", out var abilityNumber) && abilityNumber.ValueKind == JsonValueKind.Number)
            return Math.Clamp(abilityNumber.GetInt32() - 1, 0, 1);

        return null;
    }

    private static string GetSpeciesName(int species)
    {
        try
        {
            var table = GameInfo.Strings.GetType().GetProperty("Species")?.GetValue(GameInfo.Strings) as IReadOnlyList<string>;
            if (table is not null && species > 0 && species < table.Count && !string.IsNullOrWhiteSpace(table[species]))
                return table[species]!;
        }
        catch
        {
        }

        return species > 0 ? ((Species)species).ToString() : "";
    }

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

    private static void TrySetIntProperty(object instance, string name, int value)
    {
        try
        {
            var prop = instance.GetType().GetProperty(name);
            if (prop is null || !prop.CanWrite)
                return;
            var pt = prop.PropertyType;
            object boxed = pt.Name switch
            {
                nameof(Byte) => (byte)value,
                nameof(SByte) => (sbyte)value,
                nameof(Int16) => (short)value,
                nameof(UInt16) => (ushort)value,
                nameof(Int32) => value,
                nameof(UInt32) => (uint)value,
                _ => null!
            };
            if (boxed is null)
                return;
            prop.SetValue(instance, boxed);
        }
        catch
        {
        }
    }

    private static void TrySetBoolProperty(object instance, string name, bool value)
    {
        try
        {
            var prop = instance.GetType().GetProperty(name);
            if (prop is null || !prop.CanWrite || prop.PropertyType != typeof(bool))
                return;
            prop.SetValue(instance, value);
        }
        catch
        {
        }
    }
}
