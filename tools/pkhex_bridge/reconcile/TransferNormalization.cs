using System.Collections.Generic;
using System.Globalization;
using System.Reflection;
using System.Text.Json;
using PKHeX.Core;

namespace PKHeXBridge;

internal static partial class BridgeProjectReconcile
{
    internal static void NormalizeProjectedTransferFields(
        PKM source,
        PKM pk,
        JsonElement? preSaveReview,
        IList<string> notes)
    {
        // Gen I–II → Gen III+: PKHeX Gen3/4 EV legality treats EXP equal to the minimum for met/current level
        // as "no battle EXP", which clashes with GB-derived EV totals that are not vitamin-shaped.
        // Nudging off the floor is harmless when still below the next-level threshold (skipped at level 100).
        var touched = false;
        if (source.Format <= 2 && pk.Format >= 3 && TryBumpGbDirectTransferMinimumExperience(pk, notes))
            touched = true;

        if (pk.Format is not (3 or 4 or 5 or 6))
            return;

        var language = NormalizeLanguageForFormat(pk, pk.Language);
        if (language != pk.Language)
        {
            pk.Language = language;
            touched = true;
        }

        if (ShouldUseGbOriginEvCap(source, preSaveReview) ? NormalizeGbOriginEvTotal(pk) : NormalizeEvTotal(pk))
            touched = true;

        if (pk.Format == 5 && ShouldUseGbOriginEvCap(source, preSaveReview))
        {
            var origin = ReadInt(preSaveReview, "origin_game");
            if ((origin is >= 4 and <= 8 || origin is >= 20 and <= 23) && (int)pk.Version != origin.Value)
            {
                pk.Version = (GameVersion)origin.Value;
                touched = true;
            }
            if (pk.MetLocation != 30001)
            {
                pk.MetLocation = 30001;
                touched = true;
            }
            if (!IsValidDate(pk.MetDate))
            {
                pk.MetDate = DateOnly.FromDateTime(DateTime.UtcNow);
                touched = true;
            }
            if (pk.MetLevel == 0)
            {
                pk.MetLevel = (byte)Math.Clamp((int)pk.CurrentLevel, 1, 100);
                touched = true;
            }
            if (pk.IsEgg)
            {
                TrySetBoolProperty(pk, "IsEgg", false);
                touched = true;
            }
            if (pk.EggLocation != 0)
            {
                pk.EggLocation = 0;
                touched = true;
            }
            if (pk.PID == 0)
            {
                pk.PID = GenerateDeterministicPid(pk);
                touched = true;
            }
        }

        if (pk.Format == 6 && ShouldUseGbOriginEvCap(source, preSaveReview))
        {
            var origin = ReadInt(preSaveReview, "origin_game");
            if (origin is >= 4 and <= 8 && (int)pk.Version != origin.Value)
            {
                pk.Version = (GameVersion)origin.Value;
                touched = true;
            }
            if (pk.MetLocation != 30001)
            {
                pk.MetLocation = 30001;
                touched = true;
            }
            if (!IsValidDate(pk.MetDate))
            {
                pk.MetDate = DateOnly.FromDateTime(DateTime.UtcNow);
                touched = true;
            }
            if (pk.MetLevel == 0)
            {
                pk.MetLevel = (byte)Math.Clamp((int)pk.CurrentLevel, 1, 100);
                touched = true;
            }
            if (pk.IsEgg)
            {
                TrySetBoolProperty(pk, "IsEgg", false);
                touched = true;
            }
            if (pk.EggLocation != 0)
            {
                pk.EggLocation = 0;
                touched = true;
            }
            if (pk.PID == 0)
            {
                pk.PID = GenerateDeterministicPid(pk);
                touched = true;
            }
            if (pk.EncryptionConstant != pk.PID)
            {
                pk.EncryptionConstant = pk.PID;
                touched = true;
            }
            TrySetAbilityIndex(pk, pk.PIDAbility);
            TrySetGbOriginEncounterAbility(pk);
            NormalizeGen6GeoAndMemory(pk);
            touched = true;
        }

        if (pk.MetLocation == 55 && !IsValidDate(pk.MetDate))
        {
            pk.MetDate = DateOnly.FromDateTime(DateTime.UtcNow);
            touched = true;
        }

        if (pk.MetLocation == 55 && pk.MetLevel == 0)
        {
            pk.MetLevel = (byte)Math.Clamp((int)pk.CurrentLevel, 1, 100);
            touched = true;
        }

        if (pk.MetLocation == 55 && pk.EggLocation != 0)
        {
            pk.EggLocation = 0;
            touched = true;
        }

        if (touched)
            notes.Add("[transfer_normalize] normalized language, EV totals, and transfer fields.");
    }

    /// <summary>
    /// When converting directly from Gen I/II to a newer format, if EXP sits exactly on the minimum value for the
    /// current level (or met level), increment by 1 so PKHeX does not infer "zero battle EXP" alongside non-vitamin EVs.
    /// Applies to any target generation (III+); does not run for eggs or level 100.
    /// </summary>
    private static bool TryBumpGbDirectTransferMinimumExperience(PKM pk, IList<string> notes)
    {
        if (pk.IsEgg || pk.CurrentLevel >= 100)
            return false;

        var growth = pk.PersonalInfo.EXPGrowth;
        var currentLv = (int)pk.CurrentLevel;
        var levelByte = (byte)Math.Clamp(currentLv, 1, 100);
        var expAtCurrent = Experience.GetEXP(levelByte, growth);
        var metLevel = pk.MetLevel > 0 ? (int)pk.MetLevel : currentLv;
        metLevel = Math.Clamp(metLevel, 1, 100);
        var expAtMet = Experience.GetEXP((byte)metLevel, growth);

        var cur = pk.EXP;
        if (cur != expAtCurrent && cur != expAtMet)
            return false;

        var nextLevelThreshold = Experience.GetEXP((byte)Math.Clamp(currentLv + 1, 2, 100), growth);
        if (cur + 1 >= nextLevelThreshold)
            return false;

        pk.EXP = cur + 1;
        notes.Add(
            "[transfer_normalize] gb_origin: bumped EXP by 1 (was minimum for current or met level; avoids PKHeX Gen3/4 EV legality false positives on GB transfers).");
        return true;
    }

    private static int NormalizeLanguageForFormat(PKM pk, int language)
    {
        if (pk.Format is >= 3 and <= 6 && (language < (int)LanguageID.Japanese || language > (int)LanguageID.Spanish))
            return (int)LanguageID.English;
        return language;
    }

    private static uint GenerateDeterministicPid(PKM pk)
    {
        var seed = unchecked((uint)(
            ((pk.Species & 0xffff) << 16) ^
            ((pk.TID16 & 0xffff) << 1) ^
            (pk.SID16 & 0xffff) ^
            ((int)pk.Nature << 24)));
        if (seed == 0)
            seed = 0xA5366B4D;
        seed = unchecked((seed * 0x41C64E6Du) + 0x6073u);
        return seed == 0 ? 1u : seed;
    }

    private static void NormalizeGen6GeoAndMemory(PKM pk)
    {
        const int countryUnitedStates = 49;
        const int regionCalifornia = 7;
        const int consoleRegionAmericas = 1;

        TrySetIntProperty(pk, "Country", countryUnitedStates);
        TrySetIntProperty(pk, "Region", regionCalifornia);
        TrySetIntProperty(pk, "ConsoleRegion", consoleRegionAmericas);
        TrySetIntProperty(pk, "Geo1_Country", countryUnitedStates);
        TrySetIntProperty(pk, "Geo1_Region", regionCalifornia);
        TrySetIntProperty(pk, "Geo2_Country", 0);
        TrySetIntProperty(pk, "Geo2_Region", 0);
        TrySetIntProperty(pk, "Geo3_Country", 0);
        TrySetIntProperty(pk, "Geo3_Region", 0);
        TrySetIntProperty(pk, "Geo4_Country", 0);
        TrySetIntProperty(pk, "Geo4_Region", 0);
        TrySetIntProperty(pk, "Geo5_Country", 0);
        TrySetIntProperty(pk, "Geo5_Region", 0);

        TrySetIntProperty(pk, "OriginalTrainerMemory", 0);
        TrySetIntProperty(pk, "OriginalTrainerMemoryIntensity", 0);
        TrySetIntProperty(pk, "OriginalTrainerMemoryFeeling", 0);
        TrySetIntProperty(pk, "OriginalTrainerMemoryVariable", 0);
        TrySetIntProperty(pk, "HandlingTrainerMemory", 0);
        TrySetIntProperty(pk, "HandlingTrainerMemoryIntensity", 0);
        TrySetIntProperty(pk, "HandlingTrainerMemoryFeeling", 0);
        TrySetIntProperty(pk, "HandlingTrainerMemoryVariable", 0);
        TrySetIntProperty(pk, "HT_Memory", 0);
        TrySetIntProperty(pk, "HT_Intensity", 0);
        TrySetIntProperty(pk, "HT_Feeling", 0);
        TrySetIntProperty(pk, "HT_TextVar", 0);
    }

    private static void TrySetAbilityIndex(PKM pk, int abilityIndex)
    {
        try
        {
            CommonEdits.SetAbilityIndex(pk, Math.Clamp(abilityIndex, 0, 1));
        }
        catch
        {
            TrySetIntProperty(pk, "AbilityNumber", Math.Clamp(abilityIndex, 0, 1) << 1);
        }
    }

    private static void TrySetGbOriginEncounterAbility(PKM pk)
    {
        if (pk.Format != 6)
            return;

        try
        {
            var abilityIndex = Math.Clamp(pk.PIDAbility, 0, 1);
            var info = pk.Version switch
            {
                GameVersion.FR or GameVersion.LG => PersonalTable.FR.GetFormEntry(pk.Species, pk.Form),
                _ => null
            };
            var ability = info?.GetAbilityAtIndex(abilityIndex) ?? 0;
            if (ability <= 0)
                return;

            pk.Ability = ability;
            pk.AbilityNumber = abilityIndex + 1;
        }
        catch
        {
        }
    }

    private static bool IsValidDate(DateOnly? date)
    {
        if (date is not { } value)
            return false;
        return value.Year is >= 2000 and <= 2099;
    }

    private static bool NormalizeEvTotal(PKM pk)
    {
        const int MaxEv = 255;
        const int MaxTotal = 510;
        var evs = new[]
        {
            Math.Clamp(pk.EV_HP, 0, MaxEv),
            Math.Clamp(pk.EV_ATK, 0, MaxEv),
            Math.Clamp(pk.EV_DEF, 0, MaxEv),
            Math.Clamp(pk.EV_SPA, 0, MaxEv),
            Math.Clamp(pk.EV_SPD, 0, MaxEv),
            Math.Clamp(pk.EV_SPE, 0, MaxEv),
        };
        var total = evs.Sum();
        if (total <= MaxTotal)
        {
            var clampedOnly =
                evs[0] != pk.EV_HP ||
                evs[1] != pk.EV_ATK ||
                evs[2] != pk.EV_DEF ||
                evs[3] != pk.EV_SPA ||
                evs[4] != pk.EV_SPD ||
                evs[5] != pk.EV_SPE;
            if (clampedOnly)
                SetEvs(pk, evs);
            return clampedOnly;
        }

        var scaled = new int[evs.Length];
        var fractions = new List<(int Index, double Fraction)>(evs.Length);
        for (var i = 0; i < evs.Length; i++)
        {
            var exact = evs[i] * (double)MaxTotal / total;
            scaled[i] = (int)Math.Floor(exact);
            fractions.Add((i, exact - scaled[i]));
        }

        var remaining = MaxTotal - scaled.Sum();
        foreach (var item in fractions.OrderByDescending(x => x.Fraction))
        {
            if (remaining <= 0)
                break;
            scaled[item.Index]++;
            remaining--;
        }

        SetEvs(pk, scaled);
        return true;
    }

    private static bool ShouldUseGbOriginEvCap(PKM source, JsonElement? preSaveReview)
    {
        if (source.Format <= 2)
            return true;

        var sourceOrigin = ReadInt(preSaveReview, "source_origin_game");
        return sourceOrigin is >= 35 and <= 41;
    }

    private static int? ReadInt(JsonElement? obj, string property)
    {
        if (obj is not { ValueKind: JsonValueKind.Object } value)
            return null;
        if (!value.TryGetProperty(property, out var el) || el.ValueKind != JsonValueKind.Number)
            return null;
        return el.GetInt32();
    }

    private static bool NormalizeGbOriginEvTotal(PKM pk)
    {
        const int MaxEv = 100;
        const int MaxTotal = 510;
        var evs = new[]
        {
            Math.Clamp(pk.EV_HP, 0, MaxEv),
            Math.Clamp(pk.EV_ATK, 0, MaxEv),
            Math.Clamp(pk.EV_DEF, 0, MaxEv),
            Math.Clamp(pk.EV_SPA, 0, MaxEv),
            Math.Clamp(pk.EV_SPD, 0, MaxEv),
            Math.Clamp(pk.EV_SPE, 0, MaxEv),
        };
        var total = evs.Sum();
        if (total <= MaxTotal)
        {
            var clampedOnly =
                evs[0] != pk.EV_HP ||
                evs[1] != pk.EV_ATK ||
                evs[2] != pk.EV_DEF ||
                evs[3] != pk.EV_SPA ||
                evs[4] != pk.EV_SPD ||
                evs[5] != pk.EV_SPE;
            if (clampedOnly)
                SetEvs(pk, evs);
            return clampedOnly;
        }

        var scaled = new int[evs.Length];
        var fractions = new List<(int Index, double Fraction)>(evs.Length);
        for (var i = 0; i < evs.Length; i++)
        {
            var exact = evs[i] * (double)MaxTotal / total;
            scaled[i] = (int)Math.Floor(exact);
            fractions.Add((i, exact - scaled[i]));
        }

        var remaining = MaxTotal - scaled.Sum();
        foreach (var item in fractions.OrderByDescending(x => x.Fraction))
        {
            if (remaining <= 0)
                break;
            scaled[item.Index]++;
            remaining--;
        }

        SetEvs(pk, scaled);
        return true;
    }

    private static void SetEvs(PKM pk, IReadOnlyList<int> evs)
    {
        pk.EV_HP = evs[0];
        pk.EV_ATK = evs[1];
        pk.EV_DEF = evs[2];
        pk.EV_SPA = evs[3];
        pk.EV_SPD = evs[4];
        pk.EV_SPE = evs[5];
    }
}
