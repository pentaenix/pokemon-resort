using PKHeX.Core;

namespace PKHeXBridge;

internal static class BridgeProjectPastProjection
{
    internal static bool ShouldUseManualPastProjection(PKM source, Type destType)
    {
        var blank = EntityBlank.GetBlank(destType);
        return blank.Format < source.Format;
    }

    internal static PKM ProjectToPastGeneration(
        PKM source,
        Type destType,
        int targetGame,
        IList<string> notes,
        ICollection<string> lostCategories)
    {
        var projected = EntityBlank.GetBlank(destType);
        var targetFormat = projected.Format;

        projected.Species = ClampSpeciesForFormat(source.Species, targetFormat, notes, lostCategories);
        projected.Form = targetFormat >= 4 ? source.Form : (byte)0;
        projected.Version = targetGame > 0
            ? (GameVersion)targetGame
            : source.Version != GameVersion.Any
                ? source.Version
                : projected.Version;
        projected.EXP = source.EXP;
        projected.Gender = source.Gender;
        projected.Language = source.Language;
        CopyTrainerIdentity(source, projected);
        CopyPersonalityAndStats(source, projected);
        CopyNickname(source, projected);
        projected.HeldItem = IsItemRepresentable(source.HeldItem, targetFormat) ? source.HeldItem : (ushort)0;
        projected.Stat_HPCurrent = source.Stat_HPCurrent;
        projected.Stat_HPMax = source.Stat_HPMax;
        projected.Status_Condition = source.Status_Condition;
        projected.OriginalTrainerFriendship = source.OriginalTrainerFriendship;
        projected.HandlingTrainerFriendship = source.HandlingTrainerFriendship;

        if (source.Format <= 2 && targetFormat is 3 or 4)
            NormalizeGbOriginEvTotal(projected);

        projected.MetLocation = DefaultPastMetLocation(targetFormat);
        projected.MetLevel = ClampMetLevel(source.MetLevel, source.CurrentLevel);
        projected.Ball = DefaultBallForFormat(targetFormat);
        TrySetBoolProperty(projected, "FatefulEncounter", false);

        CopyRepresentableMoves(source, projected, notes, lostCategories);
        PreserveShinyFromSource(source, projected, notes);
        projected.RefreshChecksum();
        // PKHeX checksum refresh can disturb Gen III personality XOR; re-assert shiny after checksum.
        PreserveShinyFromSource(source, projected, notes);

        lostCategories.Add("manual_past_projection");
        notes.Add(
            $"[past_projection] manually built {projected.GetType().Name} from {source.GetType().Name}; " +
            $"met_location={projected.MetLocation}, met_level={projected.MetLevel}.");
        return projected;
    }

    private static void CopyTrainerIdentity(PKM source, PKM projected)
    {
        var otName = string.IsNullOrWhiteSpace(source.OriginalTrainerName)
            ? "RESORT"
            : source.OriginalTrainerName;
        SetTrainerName(source, projected, otName);
        projected.TID16 = source.TID16;
        projected.SID16 = source.SID16;
        if (source.SID16 == 0 && source.ID32 != 0)
            projected.ID32 = source.ID32;
    }

    private static void SetTrainerName(PKM source, PKM projected, string otName)
    {
        otName = NormalizeTrainerName(projected, otName);
        if (projected.Format == 3)
        {
            try
            {
                StringConverter3.SetString(
                    projected.OriginalTrainerTrash,
                    otName,
                    projected.MaxStringLengthTrainer,
                    source.Language == (int)LanguageID.Japanese,
                    StringConverterOption.ClearFF);
                return;
            }
            catch
            {
            }
        }

        try
        {
            var method = projected.GetType().GetMethod("SetTrainerName", [typeof(string)])
                ?? typeof(PKM).GetMethod("SetTrainerName", [typeof(string)]);
            if (method is not null)
            {
                method.Invoke(projected, [otName]);
                return;
            }
        }
        catch
        {
        }

        projected.OriginalTrainerName = otName;
    }

    private static string NormalizeTrainerName(PKM pk, string otName)
    {
        var maxLength = pk.MaxStringLengthTrainer;
        if (maxLength <= 0 || otName.Length <= maxLength)
            return otName;

        return otName[..maxLength];
    }

    /// <summary>
    /// Re-align projected PID so <paramref name="projected"/> is shiny when the decoded source is shiny,
    /// or when <paramref name="canonicalOverlayRequestedShiny"/> is true (Resort <c>hot_mutable_overlay.shiny</c>),
    /// after edits that can break Gen III–V XOR (nature, TID/SID, checksum). Uses PID stride (+25, preserves
    /// nature%25) then CommonEdits.SetIsShiny as fallback.
    /// </summary>
    internal static void PreserveShinyFromSource(
        PKM source,
        PKM projected,
        IList<string> notes,
        bool canonicalOverlayRequestedShiny = false)
    {
        if (projected.IsShiny)
            return;
        if (!source.IsShiny && !canonicalOverlayRequestedShiny)
            return;

        if (projected.Format <= 2)
        {
            if (CommonEdits.SetIsShiny(projected, true))
                notes.Add("[past_projection] forced shiny on legacy GB payload.");
            return;
        }

        var nature = projected.Nature;
        const int maxStrideSteps = 200_000;
        for (var step = 0; step < maxStrideSteps && !projected.IsShiny; step++)
            projected.PID += 25;

        if (projected.IsShiny)
        {
            notes.Add($"[past_projection] preserved shiny via PID stride (nature={nature}).");
            return;
        }

        if (CommonEdits.SetIsShiny(projected, true))
            notes.Add("[past_projection] warn: SetIsShiny fallback; nature may differ from stride-aligned value.");
    }

    private static void CopyPersonalityAndStats(PKM source, PKM projected)
    {
        projected.PID = source.PID;
        projected.EncryptionConstant = source.EncryptionConstant;
        CommonEdits.SetNature(projected, source.Nature);
        if (projected.Format >= 5)
            projected.StatNature = source.StatNature;

        projected.IV_HP = source.IV_HP;
        projected.IV_ATK = source.IV_ATK;
        projected.IV_DEF = source.IV_DEF;
        projected.IV_SPA = source.IV_SPA;
        projected.IV_SPD = source.IV_SPD;
        projected.IV_SPE = source.IV_SPE;

        projected.EV_HP = ClampByte(source.EV_HP);
        projected.EV_ATK = ClampByte(source.EV_ATK);
        projected.EV_DEF = ClampByte(source.EV_DEF);
        projected.EV_SPA = ClampByte(source.EV_SPA);
        projected.EV_SPD = ClampByte(source.EV_SPD);
        projected.EV_SPE = ClampByte(source.EV_SPE);
    }

    private static void NormalizeGbOriginEvTotal(PKM pk)
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
            SetEvs(pk, evs);
            return;
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

    private static void CopyNickname(PKM source, PKM projected)
    {
        // Gen I–II: PKHeX treats "unset" nickname as precise Game Boy default spelling (typically ALL CAPS).
        // Modern formats often keep `IsNicknamed=true` together with canonical species capitalization ("Poliwhirl");
        // blindly copying looks like an illegal nickname in Red/Blue or Gold/Silver byte-for-byte comparisons.
        if (projected.Format <= 2 && !string.IsNullOrWhiteSpace(source.Nickname))
        {
            var speciesName = GetSpeciesName(projected.Species);
            if (!string.IsNullOrWhiteSpace(speciesName) &&
                string.Equals(
                    source.Nickname.Trim(),
                    speciesName.Trim(),
                    StringComparison.OrdinalIgnoreCase))
            {
                try
                {
                    projected.SetDefaultNickname();
                    projected.IsNicknamed = false;
                    return;
                }
                catch
                {
                    try
                    {
                        CommonEdits.ClearNickname(projected);
                        TrySetBoolProperty(projected, "IsNicknamed", false);
                        return;
                    }
                    catch
                    {
                        projected.Nickname = speciesName;
                        projected.IsNicknamed = false;
                        return;
                    }
                }
            }
        }

        if (source.IsNicknamed && !string.IsNullOrWhiteSpace(source.Nickname))
        {
            projected.Nickname = source.Nickname;
            projected.IsNicknamed = true;
            return;
        }

        try
        {
            projected.SetDefaultNickname();
            projected.IsNicknamed = false;
            if (!projected.IsNicknamed || projected.Format != 3)
                return;
            StringConverter3.SetString(
                projected.NicknameTrash,
                GetSpeciesName(projected.Species).ToUpperInvariant(),
                projected.MaxStringLengthNickname,
                source.Language == (int)LanguageID.Japanese,
                StringConverterOption.ClearFF);
            projected.IsNicknamed = false;
        }
        catch
        {
            projected.Nickname = GetSpeciesName(projected.Species);
            projected.IsNicknamed = false;
        }
    }

    private static ushort ClampSpeciesForFormat(
        int species,
        int format,
        IList<string> notes,
        ICollection<string> lostCategories)
    {
        var max = MaxSpeciesForFormat(format);
        if (species > 0 && species <= max)
            return (ushort)species;

        lostCategories.Add("species_not_in_target_generation");
        notes.Add($"[past_projection] species {species} is not representable in generation {format}; using Bulbasaur.");
        return (ushort)Species.Bulbasaur;
    }

    private static int MaxSpeciesForFormat(int format) => format switch
    {
        <= 1 => 151,
        2 => 251,
        3 => 386,
        4 => 493,
        5 => 649,
        6 => 721,
        7 => 807,
        8 => 905,
        _ => 1025
    };

    private static int MaxMoveForFormat(int format) => format switch
    {
        <= 1 => 165,
        2 => 251,
        3 => 354,
        4 => 467,
        5 => 559,
        6 => 621,
        7 => 719,
        8 => 826,
        _ => 919
    };

    private static bool IsItemRepresentable(int item, int format)
    {
        if (item <= 0)
            return true;
        return format switch
        {
            <= 1 => false,
            2 => item <= 255,
            3 => item <= 377,
            4 => item <= 536,
            5 => item <= 638,
            6 => item <= 775,
            _ => true
        };
    }

    private static ushort DefaultPastMetLocation(int targetFormat) => targetFormat switch
    {
        <= 2 => 1,     // Pallet Town (display fallback; Gen I/II have limited encounter metadata).
        3 => 201,      // Faraway Island.
        4 => 3000,     // Lovely place.
        5 => 40001,    // Lovely place.
        _ => 30001     // a lovely place.
    };

    private static byte ClampMetLevel(int metLevel, int currentLevel)
    {
        var value = metLevel > 0 ? metLevel : currentLevel;
        return (byte)Math.Clamp(value, 1, 100);
    }

    private static byte DefaultBallForFormat(int targetFormat) => targetFormat <= 1 ? (byte)0 : (byte)4;

    private static void CopyRepresentableMoves(
        PKM source,
        PKM projected,
        IList<string> notes,
        ICollection<string> lostCategories)
    {
        var maxMove = MaxMoveForFormat(projected.Format);
        var entries = new List<(ushort Move, byte Pp, byte PpUps)>(4);
        for (var slot = 0; slot < 4; slot++)
        {
            var move = source.GetMove(slot);
            if (move <= 0)
                continue;
            if (move > maxMove || !MoveHasTargetPp(projected, (ushort)move))
            {
                lostCategories.Add("move_not_in_target_generation");
                notes.Add($"[past_projection] dropped move id {move} from slot {slot}; not in generation {projected.Format}.");
                continue;
            }

            var moveId = (ushort)move;
            var sourcePp = slot switch
            {
                0 => source.Move1_PP,
                1 => source.Move2_PP,
                2 => source.Move3_PP,
                _ => source.Move4_PP
            };
            var ppUp = slot switch
            {
                0 => ClampPpUps(source.Move1_PPUps),
                1 => ClampPpUps(source.Move2_PPUps),
                2 => ClampPpUps(source.Move3_PPUps),
                _ => ClampPpUps(source.Move4_PPUps)
            };
            entries.Add((moveId, ClampMovePp(projected, moveId, sourcePp), ppUp));
        }

        if (entries.Count == 0)
        {
            var tackle = (ushort)33; // Tackle, stable in every generation.
            entries.Add((tackle, SafeMovePp(projected, tackle), 0));
            lostCategories.Add("move_placeholder_fill");
            notes.Add("[past_projection] filled empty moveset with Tackle.");
        }

        var moves = new ushort[4];
        var pp = new byte[4];
        var ppUps = new byte[4];
        for (var i = 0; i < Math.Min(4, entries.Count); i++)
        {
            moves[i] = entries[i].Move;
            pp[i] = entries[i].Pp;
            ppUps[i] = entries[i].PpUps;
        }

        projected.Move1 = moves[0];
        projected.Move2 = moves[1];
        projected.Move3 = moves[2];
        projected.Move4 = moves[3];
        projected.Move1_PP = pp[0] == 0 && moves[0] != 0 ? SafeMovePp(projected, moves[0]) : pp[0];
        projected.Move2_PP = pp[1] == 0 && moves[1] != 0 ? SafeMovePp(projected, moves[1]) : pp[1];
        projected.Move3_PP = pp[2] == 0 && moves[2] != 0 ? SafeMovePp(projected, moves[2]) : pp[2];
        projected.Move4_PP = pp[3] == 0 && moves[3] != 0 ? SafeMovePp(projected, moves[3]) : pp[3];
        projected.Move1_PPUps = ppUps[0];
        projected.Move2_PPUps = ppUps[1];
        projected.Move3_PPUps = ppUps[2];
        projected.Move4_PPUps = ppUps[3];
    }

    private static byte ClampMovePp(PKM pk, ushort move, int requestedPp)
    {
        var max = SafeMovePp(pk, move);
        return (byte)Math.Clamp(requestedPp, 1, max);
    }

    private static bool MoveHasTargetPp(PKM pk, ushort move)
    {
        try
        {
            return MoveInfo.GetPP(pk.Context, move) > 0;
        }
        catch
        {
            return false;
        }
    }

    private static byte ClampPpUps(int ppUps) => (byte)Math.Clamp(ppUps, 0, 3);

    private static byte SafeMovePp(PKM pk, ushort move)
    {
        try
        {
            return (byte)Math.Clamp((int)MoveInfo.GetPP(pk.Context, move), 1, 255);
        }
        catch
        {
            return 35;
        }
    }

    private static byte ClampByte(int value) => (byte)Math.Clamp(value, 0, 255);

    private static string GetSpeciesName(int species)
    {
        try
        {
            var table = GameInfo.Strings.GetType().GetProperty("Species")?.GetValue(GameInfo.Strings) as IReadOnlyList<string>;
            if (table is not null && species > 0 && species < table.Count && !string.IsNullOrWhiteSpace(table[species]))
                return table[species];
        }
        catch
        {
        }

        return species > 0 ? ((Species)species).ToString() : "";
    }

    private static void TrySetBoolProperty(object instance, string name, bool value)
    {
        try
        {
            var prop = instance.GetType().GetProperty(name);
            if (prop is not null && prop.CanWrite && prop.PropertyType == typeof(bool))
                prop.SetValue(instance, value);
        }
        catch
        {
        }
    }
}
