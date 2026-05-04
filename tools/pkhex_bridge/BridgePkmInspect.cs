using PKHeX.Core;
using System.Security.Cryptography;
using System.Text.Json;

namespace PKHeXBridge;

public sealed class BridgePkmInspectResult
{
    public bool Success { get; init; }
    public BridgeImportPokemon? Pokemon { get; init; }
    public string? Status { get; init; }
    public string? Error { get; init; }
    public string? Details { get; init; }
}

public static class BridgePkmInspect
{
    public static BridgePkmInspectResult Inspect(string pkmPath, int sourceGame)
    {
        if (!File.Exists(pkmPath))
        {
            return new BridgePkmInspectResult
            {
                Success = false,
                Status = "error",
                Error = "missing_file",
                Details = pkmPath
            };
        }

        try
        {
            var raw = File.ReadAllBytes(pkmPath);
            var pokemon = EntityFormat.GetFromBytes(raw);
            if (pokemon is null || pokemon.Species <= 0)
            {
                return new BridgePkmInspectResult
                {
                    Success = false,
                    Status = "unsupported",
                    Error = "pkm_decode_failed",
                    Details = pkmPath
                };
            }

            return new BridgePkmInspectResult
            {
                Success = true,
                Status = "ok",
                Pokemon = ReadPokemon(pokemon, raw, sourceGame)
            };
        }
        catch (Exception ex)
        {
            return new BridgePkmInspectResult
            {
                Success = false,
                Status = "error",
                Error = "exception",
                Details = ex.Message
            };
        }
    }

    private static BridgeImportPokemon ReadPokemon(PKM pokemon, byte[] raw, int sourceGame)
    {
        var exported = raw.Length > 0 ? raw : pokemon.EncryptedBoxData;
        var hash = SHA256.HashData(exported);
        var formatName = pokemon.GetType().Name.ToLowerInvariant();
        var location = new BridgeImportLocation("snapshot", -1, -1, -1);

        var warm = JsonSerializer.Serialize(new
        {
            schema_version = 1,
            bridge_pkm_inspect_schema = 1,
            format = formatName,
            checksum_valid = pokemon.ChecksumValid,
            species_slug = PokeSpriteMetadata.ResolveSpeciesSlug(pokemon.Species, ""),
            species_name = GetSpeciesName(pokemon.Species),
            form_key = PokeSpriteMetadata.ResolveFormKey(pokemon),
            held_item_name = GetItemName(pokemon.HeldItem),
            nature = pokemon.Nature.ToString(),
            stat_nature = pokemon.StatNature.ToString(),
            ability_name = GetAbilityName(pokemon.Ability),
            primary_type = GetPokemonTypeName(pokemon, "Type1"),
            secondary_type = GetPokemonTypeName(pokemon, "Type2"),
            tera_type = GetTeraTypeName(pokemon),
            mark_icon = GetSelectedMarkIconKey(pokemon),
            pokerus_status = GetPokerusStatus(pokemon),
            static_fields = ReadStaticFieldDetail(pokemon),
            is_alpha = TryGetBoolProperty(pokemon, "IsAlpha") ?? false,
            is_gigantamax = TryGetBoolProperty(pokemon, "CanGigantamax")
                ?? TryGetBoolProperty(pokemon, "Gigantamax")
                ?? TryGetBoolProperty(pokemon, "IsGigantamax")
                ?? false,
            markings = GetPokemonMarkingValue(pokemon),
            moves = ReadMoveDetails(pokemon),
            original_trainer_friendship = pokemon.OriginalTrainerFriendship,
            handling_trainer_friendship = pokemon.HandlingTrainerFriendship,
            current_friendship = pokemon.CurrentFriendship
        });

        var suspended = JsonSerializer.Serialize(new
        {
            schema_version = 1,
            parser = "pkhex_bridge_pkm_inspect",
            pkm_type = pokemon.GetType().FullName
        });

        return new BridgeImportPokemon(
            SourceGame: sourceGame,
            FormatName: formatName,
            SourceLocation: location,
            RawPayloadBase64: Convert.ToBase64String(exported),
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
                OriginGame: sourceGame > 0 ? sourceGame : Convert.ToInt32(pokemon.Version),
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
            if (move <= 0)
                continue;
            moves.Add(new BridgeImportMove(
                MoveId: move,
                Pp: i switch
                {
                    0 => pokemon.Move1_PP,
                    1 => pokemon.Move2_PP,
                    2 => pokemon.Move3_PP,
                    _ => pokemon.Move4_PP
                },
                PpUps: i switch
                {
                    0 => pokemon.Move1_PPUps,
                    1 => pokemon.Move2_PPUps,
                    2 => pokemon.Move3_PPUps,
                    _ => pokemon.Move4_PPUps
                }));
        }
        return moves;
    }

    private static IReadOnlyList<object> ReadMoveDetails(PKM pokemon)
    {
        var moves = new List<object>(4);
        for (var i = 0; i < 4; i++)
        {
            var move = pokemon.GetMove(i);
            if (move <= 0)
                continue;
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

    private static int GetPokemonDv16(PKM pokemon)
    {
        if (pokemon.Format > 2)
            return -1;
        return TryGetIntProperty(pokemon, "DV16") ?? -1;
    }

    private static int GetPokemonMarkingValue(PKM pokemon) =>
        TryGetIntProperty(pokemon, "MarkingValue")
        ?? TryGetIntProperty(pokemon, "MarkValue")
        ?? TryGetIntProperty(pokemon, "Markings")
        ?? 0;

    private static string GetSpeciesName(int species) => species <= 0 ? "" : GetGameString("Species", species, "Species");
    private static string GetAbilityName(int ability) => ability <= 0 ? "" : GetGameString("Ability", ability, "Abilities");
    private static string GetItemName(int item) => item <= 0 ? "" : GetGameString("Item", item, "Items");
    private static string GetMoveName(int move) => move <= 0 ? "" : GetGameString("Move", move, "Moves");
    private static string GetTypeName(int type) => type < 0 ? "" : GetGameString("Type", type, "Types");

    private static string GetGameString(string tableName, int value, string fallbackLabel)
    {
        try
        {
            var table = GameInfo.Strings.GetType().GetProperty(tableName)?.GetValue(GameInfo.Strings) as IReadOnlyList<string>;
            if (table is not null && value >= 0 && value < table.Count)
                return table[value] ?? "";
        }
        catch
        {
        }
        return value > 0 ? $"{fallbackLabel} {value}" : "";
    }

    private static int NormalizePokemonTypeIdForFormat(int typeId, int format)
    {
        if (format > 2)
            return typeId;

        return typeId switch
        {
            7 => 6,
            8 => 7,
            20 => 9,
            21 => 10,
            22 => 11,
            23 => 12,
            24 => 13,
            25 => 14,
            26 => 15,
            _ => typeId
        };
    }

    private static string GetPokemonTypeName(PKM pokemon, string propertyName)
    {
        if (!TryGetPropertyValue(pokemon, "PersonalInfo", out var personalInfo) || personalInfo is null)
            return "";

        var typeId = TryGetIntProperty(personalInfo, propertyName);
        return typeId.HasValue ? GetTypeName(NormalizePokemonTypeIdForFormat(typeId.Value, pokemon.Format)) : "";
    }

    private static string GetTeraTypeName(PKM pokemon)
    {
        var typeId = TryGetIntProperty(pokemon, "TeraTypeOriginal")
            ?? TryGetIntProperty(pokemon, "TeraTypeOverride")
            ?? TryGetIntProperty(pokemon, "TeraType");
        return typeId.HasValue ? GetTypeName(typeId.Value) : "";
    }

    private static string GetSelectedMarkIconKey(PKM pokemon)
    {
        foreach (var propertyName in new[] { "RibbonMark", "MarkingTitle", "SelectedMark", "AppliedMark" })
        {
            if (TryGetPropertyValue(pokemon, propertyName, out var value) && value is not null)
            {
                var text = value.ToString() ?? "";
                if (!string.IsNullOrWhiteSpace(text) && text != "0" && text != "None")
                    return HumanizeIdentifier(text);
            }
        }
        return "";
    }

    private static string GetPokerusStatus(PKM pokemon)
    {
        var state = TryGetIntProperty(pokemon, "PokerusState")
            ?? TryGetIntProperty(pokemon, "PKRS_Strain")
            ?? 0;
        var days = TryGetIntProperty(pokemon, "PokerusDays")
            ?? TryGetIntProperty(pokemon, "PKRS_Days")
            ?? 0;

        if (state > 0)
            return "infected";
        if (days > 0)
            return "cured";
        return "";
    }

    private static string HumanizeIdentifier(string value)
    {
        var chars = new List<char>();
        char previous = '\0';
        foreach (var ch in value)
        {
            if (char.IsUpper(ch) && chars.Count > 0 && previous != ' ' && previous != '-')
                chars.Add('-');
            else if (ch == '_' || ch == ' ')
                chars.Add('-');
            chars.Add(char.ToLowerInvariant(ch));
            previous = ch;
        }
        return new string(chars.ToArray()).Trim('-');
    }

    private static int? TryGetIntProperty(object? target, string propertyName)
    {
        if (target is null || !TryGetPropertyValue(target, propertyName, out var value) || value is null)
            return null;
        try { return Convert.ToInt32(value); } catch { return null; }
    }

    private static bool? TryGetBoolProperty(object? target, string propertyName)
    {
        if (target is null || !TryGetPropertyValue(target, propertyName, out var value) || value is null)
            return null;
        try { return Convert.ToBoolean(value); } catch { return null; }
    }

    private static bool TryGetPropertyValue(object target, string propertyName, out object? value)
    {
        var prop = target.GetType()
            .GetProperties()
            .FirstOrDefault(p => p.Name == propertyName);
        if (prop is null)
        {
            value = null;
            return false;
        }
        value = prop.GetValue(target);
        return true;
    }
}
