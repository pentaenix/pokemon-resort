using System.Collections.Generic;
using System.Globalization;
using System.Reflection;
using System.Text.Json;
using PKHeX.Core;

namespace PKHeXBridge;

internal static partial class BridgeProjectReconcile
{
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
                pk.Language = NormalizeLanguageForFormat(pk, language.GetInt32());
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
}
