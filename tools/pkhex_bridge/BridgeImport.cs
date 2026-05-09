using PKHeX.Core;
using System.Security.Cryptography;
using System.Text.Json;

namespace PKHeXBridge;

public sealed class BridgeImportResult
{
    public bool Success { get; init; }
    public IReadOnlyList<BridgeImportPokemon>? Pokemon { get; init; }
    public string? Status { get; init; }
    public string? Error { get; init; }
    public string? Details { get; init; }
}

public sealed record BridgeImportLocation(
    string Area,
    int Box,
    int Slot,
    int GlobalIndex);

public sealed record BridgeImportMove(
    int MoveId,
    int Pp,
    int PpUps);

public sealed record BridgeImportHot(
    int SpeciesId,
    int FormId,
    string Nickname,
    bool IsNicknamed,
    int Level,
    uint Exp,
    int Gender,
    bool Shiny,
    int AbilityId,
    int AbilitySlot,
    int HeldItemId,
    IReadOnlyList<BridgeImportMove> Moves,
    int HpCurrent,
    int HpMax,
    int StatusFlags,
    string OtName,
    ushort Tid16,
    ushort Sid16,
    uint Tid32,
    int OriginGame,
    int Language,
    int MetLocationId,
    int MetLevel,
    long? MetDateUnix,
    int BallId,
    uint Pid,
    uint EncryptionConstant,
    string? HomeTracker,
    int Dv16,
    int LineageRootSpecies,
    int IdentityStrength);

public sealed record BridgeImportPokemon(
    int SourceGame,
    string FormatName,
    BridgeImportLocation SourceLocation,
    string RawPayloadBase64,
    string RawHashSha256,
    BridgeImportHot Hot,
    string WarmJson,
    string SuspendedJson);

public static class BridgeImport
{
    public static BridgeImportResult Import(string savePath)
    {
        if (!File.Exists(savePath))
        {
            return new BridgeImportResult
            {
                Success = false,
                Status = "error",
                Error = "missing_file",
                Details = savePath
            };
        }

        try
        {
            var sav = SaveUtil.GetVariantSAV(savePath);
            if (sav is null)
            {
                return new BridgeImportResult
                {
                    Success = false,
                    Status = "unsupported",
                    Error = "unsupported_save",
                    Details = savePath
                };
            }

            return BridgeImportReader.Read(sav, savePath);
        }
        catch (Exception ex)
        {
            return new BridgeImportResult
            {
                Success = false,
                Status = "error",
                Error = "exception",
                Details = ex.Message
            };
        }
    }
}

internal static class BridgeImportReader
{
    public static BridgeImportResult Read(SaveFile sav, string savePath)
    {
        var pokemon = new List<BridgeImportPokemon>();
        var warnings = new List<string>();
        var sourceGame = Convert.ToInt32(sav.Version);

        if (sav.HasParty)
        {
            try
            {
                var party = sav.PartyData;
                for (var i = 0; i < party.Count; i++)
                {
                    var pkm = party[i];
                    if (IsPresent(pkm))
                    {
                        pokemon.Add(ReadPokemon(
                            pkm,
                            sav,
                            sourceGame,
                            new BridgeImportLocation("party", -1, i, i),
                            usePartyPayload: true));
                    }
                }
            }
            catch
            {
                warnings.Add("party_unavailable");
            }
        }

        if (sav.HasBox)
        {
            try
            {
                for (var box = 0; box < sav.BoxCount; box++)
                {
                    for (var slot = 0; slot < sav.BoxSlotCount; slot++)
                    {
                        PKM? pkm = null;
                        try
                        {
                            pkm = sav.GetBoxSlotAtIndex(box, slot);
                        }
                        catch
                        {
                            warnings.Add("box_slot_read_error");
                        }

                        if (IsPresent(pkm))
                        {
                            pokemon.Add(ReadPokemon(
                                pkm!,
                                sav,
                                sourceGame,
                                new BridgeImportLocation(
                                    "box",
                                    box,
                                    slot,
                                    (box * sav.BoxSlotCount) + slot),
                                usePartyPayload: false));
                        }
                    }
                }
            }
            catch
            {
                warnings.Add("boxes_unavailable");
            }
        }

        return new BridgeImportResult
        {
            Success = true,
            Pokemon = pokemon,
            Status = warnings.Count == 0 ? "ok" : "partial",
            Details = warnings.Count == 0 ? null : string.Join("; ", warnings.Distinct())
        };
    }

    private static BridgeImportPokemon ReadPokemon(
        PKM pokemon,
        SaveFile sav,
        int sourceGame,
        BridgeImportLocation location,
        bool usePartyPayload)
    {
        ArgumentNullException.ThrowIfNull(sav);
        var raw = usePartyPayload
            ? pokemon.EncryptedPartyData
            : pokemon.EncryptedBoxData;
        var hash = SHA256.HashData(raw);
        var formatName = pokemon.GetType().Name.ToLowerInvariant();

        var warm = JsonSerializer.Serialize(new
        {
            schema_version = 1,
            bridge_import_schema = 2,
            source_location = location,
            format = formatName,
            checksum_valid = pokemon.ChecksumValid,
            nature = pokemon.Nature.ToString(),
            stat_nature = pokemon.StatNature.ToString(),
            original_trainer_friendship = pokemon.OriginalTrainerFriendship,
            handling_trainer_friendship = pokemon.HandlingTrainerFriendship,
            current_friendship = pokemon.CurrentFriendship,
            resort_catalog = BuildResortCatalog(pokemon)
        });

        var suspended = JsonSerializer.Serialize(new
        {
            schema_version = 1,
            parser = "pkhex_bridge_import",
            source_location = location,
            pkm_type = pokemon.GetType().FullName,
            party_payload = usePartyPayload
        });

        return new BridgeImportPokemon(
            SourceGame: sourceGame,
            FormatName: formatName,
            SourceLocation: location,
            RawPayloadBase64: Convert.ToBase64String(raw),
            RawHashSha256: Convert.ToHexString(hash).ToLowerInvariant(),
            Hot: new BridgeImportHot(
                SpeciesId: pokemon.Species,
                FormId: pokemon.Form,
                Nickname: pokemon.Nickname,
                IsNicknamed: pokemon.IsNicknamed,
                Level: pokemon.CurrentLevel,
                Exp: pokemon.EXP,
                Gender: pokemon.Gender,
                Shiny: pokemon.IsShiny,
                AbilityId: pokemon.Ability,
                AbilitySlot: pokemon.AbilityNumber,
                HeldItemId: pokemon.HeldItem,
                Moves: ReadMoves(pokemon),
                HpCurrent: pokemon.Stat_HPCurrent,
                HpMax: pokemon.Stat_HPMax,
                StatusFlags: pokemon.Status_Condition,
                OtName: pokemon.OriginalTrainerName,
                Tid16: pokemon.TID16,
                Sid16: pokemon.SID16,
                Tid32: pokemon.ID32,
                OriginGame: Convert.ToInt32(pokemon.Version),
                Language: pokemon.Language,
                MetLocationId: pokemon.MetLocation,
                MetLevel: pokemon.MetLevel,
                MetDateUnix: pokemon.MetDate is { } date
                    ? new DateTimeOffset(date.ToDateTime(TimeOnly.MinValue), TimeSpan.Zero).ToUnixTimeSeconds()
                    : null,
                BallId: pokemon.Ball,
                Pid: pokemon.PID,
                EncryptionConstant: pokemon.EncryptionConstant,
                HomeTracker: null,
                Dv16: GetPokemonDv16(pokemon),
                LineageRootSpecies: pokemon.Species,
                IdentityStrength: 1),
            WarmJson: warm,
            SuspendedJson: suspended);
    }

    private static IReadOnlyList<BridgeImportMove> ReadMoves(PKM pokemon)
    {
        var moves = new List<BridgeImportMove>(4);
        for (var i = 0; i < 4; i++)
        {
            var move = pokemon.GetMove(i);
            if (move == 0)
                continue;

            moves.Add(new BridgeImportMove(
                move,
                i switch
                {
                    0 => pokemon.Move1_PP,
                    1 => pokemon.Move2_PP,
                    2 => pokemon.Move3_PP,
                    _ => pokemon.Move4_PP
                },
                i switch
                {
                    0 => pokemon.Move1_PPUps,
                    1 => pokemon.Move2_PPUps,
                    2 => pokemon.Move3_PPUps,
                    _ => pokemon.Move4_PPUps
                }));
        }
        return moves;
    }

    private static bool IsPresent(PKM? pokemon) => pokemon is not null && pokemon.Species > 0;

    private static int GetPokemonDv16(PKM pokemon)
    {
        if (pokemon.Format > 2)
            return -1;
        return TryGetIntProperty(pokemon, "DV16") ?? -1;
    }

    private static object BuildResortCatalog(PKM pokemon)
    {
        return new
        {
            schema = 1,
            moves = ReadMoveSlotsDetailed(pokemon),
            friendship = new
            {
                original_trainer = pokemon.OriginalTrainerFriendship,
                handling_trainer = pokemon.HandlingTrainerFriendship,
                current = pokemon.CurrentFriendship
            },
            pokerus = ReadPokerusDetail(pokemon),
            static_fields = ReadStaticFieldDetail(pokemon),
            ribbons = ReadRibbonCatalogEntries(pokemon),
            ribbon_flags = ReadRibbonBoolProperties(pokemon),
            memory_fields = ReadIntegralPropertiesMatching(pokemon, static n =>
                n.Contains("Memory", StringComparison.Ordinal) ||
                n.Contains("Feeling", StringComparison.Ordinal)),
            contest_fields = ReadIntegralPropertiesMatching(pokemon, static n =>
                n.Contains("Contest", StringComparison.Ordinal) ||
                string.Equals(n, "Sheen", StringComparison.Ordinal) ||
                string.Equals(n, "Performance", StringComparison.Ordinal)),
            markings = TryGetIntProperty(pokemon, "MarkingValue")
                ?? TryGetIntProperty(pokemon, "MarkValue")
                ?? TryGetIntProperty(pokemon, "Markings")
                ?? 0
        };
    }

    private static object ReadStaticFieldDetail(PKM pokemon)
    {
        return new
        {
            met_location_id = pokemon.MetLocation,
            met_level = pokemon.MetLevel,
            ball_id = pokemon.Ball,
            origin_game = Convert.ToInt32(pokemon.Version),
            language = pokemon.Language,
            ot_name = pokemon.OriginalTrainerName ?? "",
            tid16 = pokemon.TID16,
            sid16 = pokemon.SID16,
            tid32 = pokemon.ID32,
            pid = pokemon.PID,
            encryption_constant = pokemon.EncryptionConstant,
            fateful_encounter = TryGetBoolProperty(pokemon, "FatefulEncounter") ?? false
        };
    }

    private static IReadOnlyList<object> ReadMoveSlotsDetailed(PKM pokemon)
    {
        var moves = new List<object>(4);
        for (var i = 0; i < 4; i++)
        {
            var move = pokemon.GetMove(i);
            moves.Add(new
            {
                slot_index = i,
                move_id = move,
                move_name = GetMoveName(move),
                current_pp = i switch
                {
                    0 => pokemon.Move1_PP,
                    1 => pokemon.Move2_PP,
                    2 => pokemon.Move3_PP,
                    _ => pokemon.Move4_PP
                },
                pp_ups = i switch
                {
                    0 => pokemon.Move1_PPUps,
                    1 => pokemon.Move2_PPUps,
                    2 => pokemon.Move3_PPUps,
                    _ => pokemon.Move4_PPUps
                }
            });
        }
        return moves;
    }

    private static string GetMoveName(int move) =>
        move <= 0 ? "" : GetGameString("Move", move, "Moves");

    private static string GetGameString(string tableName, int value, string fallbackLabel)
    {
        try
        {
            var table = GameInfo.Strings.GetType().GetProperty(tableName)?.GetValue(GameInfo.Strings) as
                IReadOnlyList<string>;
            if (table is not null && value >= 0 && value < table.Count)
                return table[value] ?? "";
        }
        catch
        {
        }

        return value > 0 ? $"{fallbackLabel} {value}" : "";
    }

    private static object ReadPokerusDetail(PKM pokemon)
    {
        var state = TryGetIntProperty(pokemon, "PokerusStrain")
            ?? TryGetIntProperty(pokemon, "PKRS_Strain")
            ?? TryGetIntProperty(pokemon, "PokerusState")
            ?? 0;
        var days = TryGetIntProperty(pokemon, "PKRS_Days")
            ?? TryGetIntProperty(pokemon, "PokerusDays")
            ?? 0;
        return new
        {
            strain_or_state = state,
            days,
            status = GetPokerusStatusLabel(state, days)
        };
    }

    private static string GetPokerusStatusLabel(int state, int days)
    {
        if (state > 0)
            return "infected";
        if (days > 0)
            return "cured";
        return "";
    }

    private static object ReadRibbonBoolProperties(PKM pokemon)
    {
        var dict = new SortedDictionary<string, bool>();
        foreach (var prop in pokemon.GetType().GetProperties(
                     System.Reflection.BindingFlags.Instance |
                     System.Reflection.BindingFlags.Public))
        {
            if (prop.PropertyType != typeof(bool))
                continue;
            if (!prop.Name.Contains("Ribbon", StringComparison.Ordinal))
                continue;
            try
            {
                if (prop.GetValue(pokemon) is not true)
                    continue;
                dict[prop.Name] = true;
            }
            catch
            {
                // Skip unreadable ribbon accessors on this entity type.
            }
        }

        return dict;
    }

    /// <summary>
    /// Canonical Resort ribbon map: <c>true</c> ribbon flags plus integral tier/count fields (e.g. Gen III contest levels).
    /// </summary>
    private static SortedDictionary<string, object> ReadRibbonCatalogEntries(PKM pokemon)
    {
        var dict = new SortedDictionary<string, object>();
        foreach (var prop in pokemon.GetType().GetProperties(
                     System.Reflection.BindingFlags.Instance |
                     System.Reflection.BindingFlags.Public))
        {
            if (!prop.Name.Contains("Ribbon", StringComparison.Ordinal))
                continue;

            try
            {
                if (prop.PropertyType == typeof(bool))
                {
                    if (prop.GetValue(pokemon) is true)
                        dict[prop.Name] = true;
                    continue;
                }

                if (prop.PropertyType != typeof(byte) && prop.PropertyType != typeof(sbyte) &&
                    prop.PropertyType != typeof(short) && prop.PropertyType != typeof(ushort) &&
                    prop.PropertyType != typeof(int) && prop.PropertyType != typeof(uint))
                    continue;

                if (!prop.CanRead)
                    continue;
                var raw = prop.GetValue(pokemon);
                if (raw is null)
                    continue;
                var n = Convert.ToInt32(raw, System.Globalization.CultureInfo.InvariantCulture);
                if (n > 0)
                    dict[prop.Name] = n;
            }
            catch
            {
                // Skip unreadable accessors on this entity type.
            }
        }

        return dict;
    }

    private static object ReadIntegralPropertiesMatching(PKM pokemon, Func<string, bool> nameMatch)
    {
        var dict = new SortedDictionary<string, int>();
        foreach (var prop in pokemon.GetType().GetProperties(
                     System.Reflection.BindingFlags.Instance |
                     System.Reflection.BindingFlags.Public))
        {
            if (!nameMatch(prop.Name))
                continue;
            var v = TryIntegralProperty(prop, pokemon);
            if (v.HasValue)
                dict[prop.Name] = v.Value;
        }

        return dict;
    }

    private static int? TryIntegralProperty(System.Reflection.PropertyInfo prop, object instance)
    {
        if (prop.PropertyType != typeof(byte) && prop.PropertyType != typeof(sbyte) &&
            prop.PropertyType != typeof(short) && prop.PropertyType != typeof(ushort) &&
            prop.PropertyType != typeof(int) && prop.PropertyType != typeof(uint) &&
            prop.PropertyType != typeof(long) && prop.PropertyType != typeof(ulong))
            return null;
        try
        {
            var value = prop.GetValue(instance);
            return value switch
            {
                byte b => b,
                sbyte b => b,
                short s => s,
                ushort s => s,
                int i => i,
                uint u when u <= int.MaxValue => (int)u,
                long l when l is >= int.MinValue and <= int.MaxValue => (int)l,
                ulong ul when ul <= int.MaxValue => (int)ul,
                _ => null
            };
        }
        catch
        {
            return null;
        }
    }

    private static bool? TryGetBoolProperty(object instance, string name)
    {
        var property = instance.GetType().GetProperty(name);
        if (property is null || property.PropertyType != typeof(bool))
            return null;

        try
        {
            return property.GetValue(instance) as bool?;
        }
        catch
        {
            return null;
        }
    }

    private static int? TryGetIntProperty(object instance, string name)
    {
        var property = instance.GetType().GetProperty(name);
        if (property is null)
            return null;

        try
        {
            var value = property.GetValue(instance);
            return value switch
            {
                byte b => b,
                sbyte b => b,
                short s => s,
                ushort s => s,
                int i => i,
                uint u when u <= int.MaxValue => (int)u,
                long l when l is >= int.MinValue and <= int.MaxValue => (int)l,
                ulong ul when ul <= int.MaxValue => (int)ul,
                _ => null
            };
        }
        catch
        {
            return null;
        }
    }
}
