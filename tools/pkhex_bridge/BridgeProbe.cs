using PKHeX.Core;
using System.Reflection;

namespace PKHeXBridge;

public sealed class BridgeProbeResult
{
    public bool Success { get; init; }
    public string? SaveType { get; init; }
    public string? Game { get; init; }
    public string? TrainerName { get; init; }
    public string? GameId { get; init; }
    public string? PlayerName { get; init; }
    public IReadOnlyList<string>? Party { get; init; }
    public IReadOnlyList<string>? Box1 { get; init; }
    public string? PlayTime { get; init; }
    public int? PokedexCount { get; init; }
    public int? Badges { get; init; }
    public SaveTrainerData? Trainer { get; init; }
    public SavePokedexData? Pokedex { get; init; }
    public IReadOnlyList<SavePokemonSummary>? AllPokemon { get; init; }
    public IReadOnlyList<SaveBoxData>? Boxes { get; init; }
    public SaveBagData? Bag { get; init; }
    public string? Status { get; init; }
    public string? Error { get; init; }
    public string? Details { get; init; }
}

public sealed record SaveTrainerData(
    string Name,
    string GameId,
    string Game,
    string SaveType,
    int Generation,
    string Context,
    string PlayTime,
    int Gender,
    int Language,
    uint Id32,
    ushort Tid16,
    ushort Sid16,
    uint DisplayTid,
    uint DisplaySid,
    uint Money,
    int Coins,
    int Badges,
    bool ChecksumsValid,
    string ChecksumInfo);

public sealed record SavePokedexData(
    bool Supported,
    int MaxSpeciesId,
    int SeenCount,
    int CaughtCount,
    decimal PercentSeen,
    decimal PercentCaught,
    IReadOnlyList<SavePokedexEntry> Entries);

public sealed record SavePokedexEntry(
    int SpeciesId,
    string SpeciesName,
    string SpeciesSlug,
    bool Seen,
    bool Caught);

public sealed record SavePokemonLocation(
    string Area,
    int Box,
    int Slot,
    int GlobalIndex);

public sealed record SavePokemonMove(
    int Slot,
    int MoveId,
    string Name,
    int CurrentPp,
    int PpUps);

public sealed record SavePokemonSummary(
    bool Present,
    SavePokemonLocation Location,
    string Format,
    int SpeciesId,
    string SpeciesName,
    string SpeciesSlug,
    string Nickname,
    bool IsNicknamed,
    int Form,
    string FormKey,
    int Gender,
    int Level,
    bool IsEgg,
    bool IsShiny,
    int BallId,
    string OtName,
    int Tid16,
    int Sid16,
    string OriginGame,
    int MetLocationId,
    string MetLocationName,
    int HeldItemId,
    string HeldItemName,
    string Nature,
    int AbilityId,
    string AbilityName,
    string PrimaryType,
    string SecondaryType,
    string TeraType,
    string MarkIcon,
    string PokerusStatus,
    bool IsAlpha,
    bool IsGigantamax,
    int Markings,
    int Dv16,
    IReadOnlyList<SavePokemonMove> Moves,
    bool ChecksumValid);

public sealed record SaveBoxSlotData(
    int Slot,
    int GlobalIndex,
    bool Locked,
    bool OverwriteProtected,
    SavePokemonSummary? Pokemon);

public sealed record SaveBoxData(
    int Index,
    string Name,
    int SlotCount,
    IReadOnlyList<SaveBoxSlotData> Slots);

public sealed record SaveBagData(
    bool Supported,
    IReadOnlyList<SaveBagPocket> Pockets);

public sealed record SaveBagPocket(
    int Index,
    string Type,
    string Name,
    int Capacity,
    int MaxItemCount,
    IReadOnlyList<SaveBagItem> Items);

public sealed record SaveBagItem(
    int Slot,
    int ItemId,
    string Name,
    string Slug,
    int Count);

public static class BridgeProbe
{
    public static BridgeProbeResult Probe(string savePath)
    {
        if (!File.Exists(savePath))
        {
            return new BridgeProbeResult
            {
                Success = false,
                Status = "error",
                Error = "missing_file",
                Details = savePath
            };
        }

        try
        {
            var sav = SaveUtil.GetSaveFile(savePath);
            if (sav is null)
            {
                return new BridgeProbeResult
                {
                    Success = false,
                    Status = "unsupported",
                    Error = "unsupported_save",
                    Details = savePath
                };
            }

            return SaveReader.Read(sav, savePath);
        }
        catch (Exception ex)
        {
            return new BridgeProbeResult
            {
                Success = false,
                Status = "error",
                Error = "exception",
                Details = ex.Message
            };
        }
    }
}

internal static class SaveReader
{
    public static BridgeProbeResult Read(SaveFile sav, string savePath)
    {
        var warnings = new List<string>();
        var game = sav.Version.ToString();
        var gameId = MapGameId(game, savePath);
        var playTime = GetPlayTimeString(sav, warnings);
        var badges = GetBadgeCount(sav, warnings);
        var trainer = ReadTrainer(sav, gameId, playTime, badges);
        var pokedex = ReadPokedex(sav, warnings);
        var partyPokemon = ReadPartyPokemon(sav, warnings);
        var boxes = ReadBoxes(sav, warnings);
        var allPokemon = partyPokemon
            .Concat(boxes.SelectMany(box => box.Slots)
                .Select(slot => slot.Pokemon)
                .Where(static pokemon => pokemon is not null)
                .Cast<SavePokemonSummary>())
            .ToList();
        var bag = ReadBag(sav, warnings);
        var box1 = boxes.Count > 0
            ? boxes[0].Slots.Select(static slot => slot.Pokemon?.SpeciesSlug ?? "").ToList()
            : Enumerable.Repeat("", Math.Max(0, sav.BoxSlotCount)).ToList();

        return new BridgeProbeResult
        {
            Success = true,
            SaveType = sav.GetType().Name,
            Game = game,
            TrainerName = sav.OT,
            GameId = gameId,
            PlayerName = sav.OT,
            Party = partyPokemon.Select(static pokemon => pokemon.SpeciesSlug).ToList(),
            Box1 = box1,
            PlayTime = playTime,
            PokedexCount = pokedex.CaughtCount,
            Badges = badges,
            Trainer = trainer,
            Pokedex = pokedex,
            AllPokemon = allPokemon,
            Boxes = boxes,
            Bag = bag,
            Status = warnings.Count == 0 ? "ok" : "partial",
            Details = warnings.Count == 0 ? null : string.Join("; ", warnings.Distinct())
        };
    }

    private static SaveTrainerData ReadTrainer(SaveFile sav, string gameId, string playTime, int badges) => new(
        Name: sav.OT,
        GameId: gameId,
        Game: sav.Version.ToString(),
        SaveType: sav.GetType().Name,
        Generation: sav.Generation,
        Context: sav.Context.ToString(),
        PlayTime: playTime,
        Gender: sav.Gender,
        Language: sav.Language,
        Id32: sav.ID32,
        Tid16: sav.TID16,
        Sid16: sav.SID16,
        DisplayTid: sav.DisplayTID,
        DisplaySid: sav.DisplaySID,
        Money: sav.Money,
        Coins: TryGetIntProperty(sav, "Coin") ?? TryGetIntProperty(sav, "Coins") ?? 0,
        Badges: badges,
        ChecksumsValid: sav.ChecksumsValid,
        ChecksumInfo: sav.ChecksumInfo);

    private static SavePokedexData ReadPokedex(SaveFile sav, List<string> warnings)
    {
        if (!sav.HasPokeDex)
            return new SavePokedexData(false, sav.MaxSpeciesID, 0, 0, 0, 0, Array.Empty<SavePokedexEntry>());

        var entries = new List<SavePokedexEntry>();
        try
        {
            for (ushort species = 1; species <= sav.MaxSpeciesID; species++)
            {
                var seen = sav.GetSeen(species);
                var caught = sav.GetCaught(species);
                if (!seen && !caught)
                    continue;

                var name = GetSpeciesName(species);
                entries.Add(new SavePokedexEntry(
                    species,
                    name,
                    ToSpeciesSlug(species),
                    seen,
                    caught));
            }

            return new SavePokedexData(
                true,
                sav.MaxSpeciesID,
                entries.Count(static entry => entry.Seen),
                entries.Count(static entry => entry.Caught),
                sav.MaxSpeciesID == 0 ? 0 : (decimal)entries.Count(static entry => entry.Seen) / sav.MaxSpeciesID,
                sav.MaxSpeciesID == 0 ? 0 : (decimal)entries.Count(static entry => entry.Caught) / sav.MaxSpeciesID,
                entries);
        }
        catch
        {
            warnings.Add("pokedex_unavailable");
            return new SavePokedexData(false, sav.MaxSpeciesID, 0, 0, 0, 0, Array.Empty<SavePokedexEntry>());
        }
    }

    private static IReadOnlyList<SavePokemonSummary> ReadPartyPokemon(SaveFile sav, List<string> warnings)
    {
        if (!sav.HasParty)
            return Array.Empty<SavePokemonSummary>();

        try
        {
            var result = new List<SavePokemonSummary>();
            var party = sav.PartyData;
            for (var i = 0; i < party.Count; i++)
            {
                var pokemon = party[i];
                if (!IsPresent(pokemon))
                    continue;

                result.Add(ReadPokemon(
                    pokemon,
                    new SavePokemonLocation("party", -1, i, i)));
            }
            return result;
        }
        catch
        {
            warnings.Add("party_unavailable");
            return Array.Empty<SavePokemonSummary>();
        }
    }

    private static IReadOnlyList<SaveBoxData> ReadBoxes(SaveFile sav, List<string> warnings)
    {
        if (!sav.HasBox)
            return Array.Empty<SaveBoxData>();

        var boxes = new List<SaveBoxData>();
        try
        {
            for (var box = 0; box < sav.BoxCount; box++)
            {
                var slots = new List<SaveBoxSlotData>();
                for (var slot = 0; slot < sav.BoxSlotCount; slot++)
                {
                    var globalIndex = (box * sav.BoxSlotCount) + slot;
                    PKM? pokemon = null;
                    SavePokemonSummary? summary = null;
                    try
                    {
                        pokemon = sav.GetBoxSlotAtIndex(box, slot);
                    }
                    catch
                    {
                        warnings.Add("box_slot_read_error");
                    }

                    if (pokemon is not null && IsPresent(pokemon))
                    {
                        summary = ReadPokemon(
                            pokemon,
                            new SavePokemonLocation("box", box, slot, globalIndex));
                    }

                    slots.Add(new SaveBoxSlotData(
                        slot,
                        globalIndex,
                        SafeBool(() => sav.IsBoxSlotLocked(box, slot)),
                        SafeBool(() => sav.IsBoxSlotOverwriteProtected(box, slot)),
                        summary));
                }

                boxes.Add(new SaveBoxData(
                    box,
                    GetBoxName(sav, box),
                    sav.BoxSlotCount,
                    slots));
            }
        }
        catch
        {
            warnings.Add("boxes_unavailable");
        }

        return boxes;
    }

    private static SavePokemonSummary ReadPokemon(PKM pokemon, SavePokemonLocation location)
    {
        var species = pokemon.Species;
        var ballId = TryGetIntProperty(pokemon, "Ball") ?? 0;
        var heldItemId = pokemon.HeldItem;
        var abilityId = pokemon.Ability;
        var speciesSlug = PokeSpriteMetadata.ResolveSpeciesSlug(species, ToSpeciesSlug(species));
        var formKey = PokeSpriteMetadata.ResolveFormKey(pokemon);
        return new SavePokemonSummary(
            Present: IsPresent(pokemon),
            Location: location,
            Format: pokemon.GetType().Name,
            SpeciesId: species,
            SpeciesName: GetSpeciesName(species),
            SpeciesSlug: speciesSlug,
            Nickname: pokemon.Nickname,
            IsNicknamed: pokemon.IsNicknamed,
            Form: pokemon.Form,
            FormKey: formKey,
            Gender: pokemon.Gender,
            Level: pokemon.CurrentLevel,
            IsEgg: pokemon.IsEgg,
            IsShiny: pokemon.IsShiny,
            BallId: ballId,
            OtName: pokemon.OriginalTrainerName ?? "",
            Tid16: pokemon.TID16,
            Sid16: pokemon.SID16,
            OriginGame: GetOriginGameName(pokemon),
            MetLocationId: pokemon.MetLocation,
            MetLocationName: GetMetLocationName(pokemon),
            HeldItemId: heldItemId,
            HeldItemName: GetItemName(heldItemId),
            Nature: pokemon.Nature.ToString(),
            AbilityId: abilityId,
            AbilityName: GetAbilityName(abilityId),
            PrimaryType: GetPokemonTypeName(pokemon, "Type1"),
            SecondaryType: GetPokemonTypeName(pokemon, "Type2"),
            TeraType: GetTeraTypeName(pokemon),
            MarkIcon: GetSelectedMarkIconKey(pokemon),
            PokerusStatus: GetPokerusStatus(pokemon),
            IsAlpha: TryGetBoolProperty(pokemon, "IsAlpha") ?? false,
            IsGigantamax: TryGetBoolProperty(pokemon, "CanGigantamax")
                ?? TryGetBoolProperty(pokemon, "Gigantamax")
                ?? TryGetBoolProperty(pokemon, "IsGigantamax")
                ?? false,
            Markings: GetPokemonMarkingValue(pokemon),
            Dv16: GetPokemonDv16(pokemon),
            Moves: ReadMoves(pokemon),
            ChecksumValid: pokemon.ChecksumValid);
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

    private static IReadOnlyList<SavePokemonMove> ReadMoves(PKM pokemon)
    {
        var moves = new List<SavePokemonMove>(4);
        for (var i = 0; i < 4; i++)
        {
            var move = pokemon.GetMove(i);
            if (move == 0)
                continue;

            moves.Add(new SavePokemonMove(
                i,
                move,
                GetMoveName(move),
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

    private static SaveBagData ReadBag(SaveFile sav, List<string> warnings)
    {
        try
        {
            if (sav.Inventory is not System.Collections.IEnumerable inventory)
                return new SaveBagData(false, Array.Empty<SaveBagPocket>());

            var pockets = new List<SaveBagPocket>();
            var pocketIndex = 0;
            foreach (var pocket in inventory)
            {
                if (pocket is null)
                    continue;

                pockets.Add(ReadBagPocket(pocket, pocketIndex++));
            }
            return new SaveBagData(pockets.Count > 0, pockets);
        }
        catch
        {
            warnings.Add("bag_unavailable");
            return new SaveBagData(false, Array.Empty<SaveBagPocket>());
        }
    }

    private static SaveBagPocket ReadBagPocket(object pocket, int pocketIndex)
    {
        var type = GetFieldValue(pocket, "Type")?.ToString() ?? $"Pocket{pocketIndex + 1}";
        var items = GetFieldValue(pocket, "Items") as System.Collections.IEnumerable;
        var bagItems = new List<SaveBagItem>();
        var slot = 0;
        if (items is not null)
        {
            foreach (var item in items)
            {
                var itemId = TryGetIntProperty(item, "Index") ?? 0;
                var count = TryGetIntProperty(item, "Count") ?? 0;
                if (itemId > 0 && count > 0)
                {
                    var name = GetItemName(itemId);
                    var slug = Slugify(name);
                    if (string.IsNullOrWhiteSpace(slug))
                        slug = $"item_{itemId}";
                    bagItems.Add(new SaveBagItem(
                        slot,
                        itemId,
                        name,
                        slug,
                        count));
                }
                slot++;
            }
        }

        return new SaveBagPocket(
            pocketIndex,
            type,
            HumanizeIdentifier(type),
            TryGetIntProperty(pocket, "Count") ?? slot,
            TryGetIntField(pocket, "MaxCount") ?? 0,
            bagItems);
    }

    private static string MapGameId(string version, string savePath)
    {
        var v = (version ?? "").Trim().ToUpperInvariant();
        var mapped = v switch
        {
            "RD" => "pokemon_red",
            "BU" => "pokemon_blue",
            "YW" => "pokemon_yellow",
            "GD" => "pokemon_gold",
            "GS" => GuessGameIdFromFilename(savePath),
            "SI" => "pokemon_silver",
            "C" => "pokemon_crystal",
            "R" => "pokemon_ruby",
            "S" => "pokemon_sapphire",
            "E" => "pokemon_emerald",
            "FR" => "pokemon_firered",
            "LG" => "pokemon_leafgreen",
            "D" => "pokemon_diamond",
            "P" => "pokemon_pearl",
            "PT" => "pokemon_platinum",
            "DP" => "pokemon_diamond",
            "HG" => "pokemon_heartgold",
            "SS" => "pokemon_soulsilver",
            "HGSS" => GuessHgssGameId(savePath),
            "B" => "pokemon_black",
            "W" => "pokemon_white",
            "B2" => "pokemon_black_2",
            "W2" => "pokemon_white_2",
            "X" => "pokemon_x",
            "Y" => "pokemon_y",
            "OR" => "pokemon_omega_ruby",
            "AS" => "pokemon_alpha_sapphire",
            "SN" => "pokemon_sun",
            "MN" => "pokemon_moon",
            "US" => "pokemon_ultra_sun",
            "UM" => "pokemon_ultra_moon",
            "GP" => "pokemon_lets_go_pikachu",
            "GE" => "pokemon_lets_go_eevee",
            // Common PKHeX / internal abbreviations for Switch titles.
            "SW" => "pokemon_sword",
            "SH" => "pokemon_shield",
            "SWSH" => "pokemon_sword",
            "BD" => "pokemon_brilliant_diamond",
            "SP" => "pokemon_shining_pearl",
            "LA" => "pokemon_legends_arceus",
            "SL" => "pokemon_scarlet",
            "VL" => "pokemon_violet",
            "SV" => "pokemon_scarlet",
            // Some older or ambiguous saves come back from PKHeX with generation-style
            // abbreviations instead of a concrete version code. Prefer filename hints in
            // those cases so we keep a stable game id for icons and UI labels.
            "GN" => GuessGameIdFromFilename(savePath),
            _ => ""
        };
        if (!string.IsNullOrWhiteSpace(mapped))
            return mapped;

        // If PKHeX’s Version string is unknown/short (or empty), guess from the filename.
        var guessed = GuessGameIdFromFilename(savePath);
        if (!string.IsNullOrWhiteSpace(guessed))
            return guessed;

        // Final fallback: stable but may not match icons.
        return $"pokemon_{v.ToLowerInvariant()}";
    }

    private static string GuessGameIdFromFilename(string savePath)
    {
        var fileName = Path.GetFileNameWithoutExtension(savePath);
        var lowerName = fileName.ToLowerInvariant();
        var tokens = TokenizeHint(fileName);

        if (tokens.Contains("red") || tokens.Contains("rd"))
            return "pokemon_red";
        if (tokens.Contains("blue") || tokens.Contains("bu"))
            return "pokemon_blue";
        if (tokens.Contains("yellow") || tokens.Contains("yw"))
            return "pokemon_yellow";
        if (tokens.Contains("gold") || tokens.Contains("gd"))
            return "pokemon_gold";
        if (tokens.Contains("silver") || tokens.Contains("si"))
            return "pokemon_silver";
        if (tokens.Contains("crystal"))
            return "pokemon_crystal";

        if (HasSequence(tokens, "omega", "ruby") || tokens.Contains("omegaruby") || tokens.Contains("or"))
            return "pokemon_omega_ruby";
        if (HasSequence(tokens, "alpha", "sapphire") || tokens.Contains("alphasapphire") || tokens.Contains("as"))
            return "pokemon_alpha_sapphire";

        if (lowerName.Contains("heartgold") || HasSequence(tokens, "heart", "gold") || tokens.Contains("heartgold") || tokens.Contains("hg"))
            return "pokemon_heartgold";
        if (lowerName.Contains("soulsilver") || HasSequence(tokens, "soul", "silver") || tokens.Contains("soulsilver") || tokens.Contains("ss"))
            return "pokemon_soulsilver";

        if (tokens.Contains("sword") || tokens.Contains("sw"))
            return "pokemon_sword";
        if (tokens.Contains("shield") || tokens.Contains("sh"))
            return "pokemon_shield";

        if (HasSequence(tokens, "ultra", "sun") || tokens.Contains("ultrasun") || tokens.Contains("us"))
            return "pokemon_ultra_sun";
        if (HasSequence(tokens, "ultra", "moon") || tokens.Contains("ultramoon") || tokens.Contains("um"))
            return "pokemon_ultra_moon";
        if (tokens.Contains("sun") || tokens.Contains("sn"))
            return "pokemon_sun";
        if (tokens.Contains("moon") || tokens.Contains("mn"))
            return "pokemon_moon";

        if (tokens.Contains("scarlet") || tokens.Contains("sc"))
            return "pokemon_scarlet";
        if (tokens.Contains("violet") || tokens.Contains("vi") || tokens.Contains("vl"))
            return "pokemon_violet";

        return "";
    }

    private static string GuessHgssGameId(string savePath)
    {
        var fileName = Path.GetFileNameWithoutExtension(savePath);
        var lowerName = fileName.ToLowerInvariant();
        var tokens = TokenizeHint(fileName);

        if (lowerName.Contains("soulsilver") || HasSequence(tokens, "soul", "silver") || tokens.Contains("soulsilver") || tokens.Contains("ss"))
            return "pokemon_soulsilver";
        if (lowerName.Contains("heartgold") || HasSequence(tokens, "heart", "gold") || tokens.Contains("heartgold") || tokens.Contains("hg"))
            return "pokemon_heartgold";

        return "pokemon_hgss";
    }

    private static HashSet<string> TokenizeHint(string value)
    {
        var tokens = new HashSet<string>();
        var current = new List<char>();
        foreach (var ch in value.ToLowerInvariant())
        {
            if (char.IsLetterOrDigit(ch))
            {
                current.Add(ch);
                continue;
            }

            AddToken(tokens, current);
        }
        AddToken(tokens, current);
        return tokens;
    }

    private static void AddToken(HashSet<string> tokens, List<char> current)
    {
        if (current.Count == 0)
            return;

        tokens.Add(new string(current.ToArray()));
        current.Clear();
    }

    private static bool HasSequence(HashSet<string> tokens, string first, string second) =>
        tokens.Contains(first) && tokens.Contains(second);

    private static string GetPlayTimeString(SaveFile sav, List<string> warnings)
    {
        var hours = TryGetIntProperty(sav, "PlayedHours");
        var minutes = TryGetIntProperty(sav, "PlayedMinutes");
        if (hours.HasValue && minutes.HasValue)
            return $"{hours.Value}:{minutes.Value:00}";

        if (TryGetPropertyValue(sav, "PlayTimeString", out var value) &&
            value is string text &&
            !string.IsNullOrWhiteSpace(text))
            return NormalizePlayTime(text);

        warnings.Add("play_time_unavailable");
        return "";
    }

    private static int GetBadgeCount(SaveFile sav, List<string> warnings)
    {
        if (TryGetIntProperty(sav, "Badges") is int count)
            return count <= 8 ? count : PopCount(count);

        if (TryGetPropertyValue(sav, "Misc", out var misc) &&
            misc is not null &&
            TryGetIntProperty(misc, "Badges") is int miscCount)
            return miscCount <= 8 ? miscCount : PopCount(miscCount);

        warnings.Add("badges_unavailable");
        return 0;
    }

    private static string GetBoxName(SaveFile sav, int box)
    {
        var method = sav.GetType().GetMethods(BindingFlags.Instance | BindingFlags.Public)
            .FirstOrDefault(candidate =>
                candidate.Name == "GetBoxName" &&
                candidate.GetParameters().Length == 1 &&
                candidate.GetParameters()[0].ParameterType == typeof(int));
        if (method?.Invoke(sav, new object[] { box }) is string name && !string.IsNullOrWhiteSpace(name))
            return name;

        return $"Box {box + 1}";
    }

    private static bool IsPresent(PKM? pokemon) => pokemon is not null && pokemon.Species > 0;

    private static string ToSpeciesSlug(int species)
    {
        var speciesName = GetSpeciesName(species);
        return string.IsNullOrWhiteSpace(speciesName) ? $"species_{species}" : Slugify(speciesName);
    }

    private static string GetSpeciesName(int species) => GetGameString("Species", species);

    private static string GetItemName(int item) => item <= 0 ? "" : GetGameString("Item", item, "Items");

    private static string GetMoveName(int move) => move <= 0 ? "" : GetGameString("Move", move, "Moves");

    private static string GetAbilityName(int ability) => ability <= 0 ? "" : GetGameString("Ability", ability, "Abilities");

    private static string GetTypeName(int type) => type < 0 ? "" : GetGameString("Type", type, "Types");

    private static int NormalizePokemonTypeIdForFormat(int typeId, int format)
    {
        if (format > 2)
            return typeId;

        return typeId switch
        {
            7 => 6,   // Bug
            8 => 7,   // Ghost
            20 => 9,  // Fire
            21 => 10, // Water
            22 => 11, // Grass
            23 => 12, // Electric
            24 => 13, // Psychic
            25 => 14, // Ice
            26 => 15, // Dragon
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

    private static string GetOriginGameName(PKM pokemon)
    {
        try
        {
            // Normalize to the same token vocabulary native uses for origin mapping.
            // Example: "HG" -> "heartgold", "D" -> "diamond".
            var id = MapGameId(pokemon.Version.ToString(), "");
            return id.StartsWith("pokemon_", StringComparison.Ordinal) ? id["pokemon_".Length..] : pokemon.Version.ToString();
        }
        catch
        {
            return "";
        }
    }

    private static string GetMetLocationName(PKM pokemon)
    {
        foreach (var propertyName in new[] { "MetLocationName", "Met_Location_Name" })
        {
            if (TryGetPropertyValue(pokemon, propertyName, out var value) &&
                value is string text &&
                !string.IsNullOrWhiteSpace(text))
            {
                return text;
            }
        }

        var metLocation = pokemon.MetLocation;
        return metLocation > 0 ? GetGameString("Location", metLocation, "Locations") : "";
    }

    private static string GetGameString(string singularName, int index, string? pluralName = null)
    {
        try
        {
            var gameInfoType = Type.GetType("PKHeX.Core.GameInfo, PKHeX.Core");
            var stringsProperty = gameInfoType?.GetProperty("Strings", BindingFlags.Public | BindingFlags.Static);
            var stringsObject = stringsProperty?.GetValue(null);
            if (stringsObject is null)
                return "";

            foreach (var propertyName in new[] { singularName, $"{singularName}Strings", pluralName, $"{pluralName}Strings" }
                         .Where(static name => !string.IsNullOrWhiteSpace(name))
                         .Cast<string>())
            {
                var stringsPropertyInfo = stringsObject.GetType().GetProperty(propertyName, BindingFlags.Public | BindingFlags.Instance);
                var strings = stringsPropertyInfo?.GetValue(stringsObject) as System.Collections.IList;
                if (strings is not null && index >= 0 && index < strings.Count)
                    return strings[index]?.ToString() ?? "";
            }
        }
        catch
        {
        }

        return "";
    }

    private static string Slugify(string value)
    {
        var chars = value
            .Trim()
            .ToLowerInvariant()
            .Select(ch => char.IsLetterOrDigit(ch) ? ch : '_')
            .ToArray();
        var collapsed = new string(chars);
        while (collapsed.Contains("__", StringComparison.Ordinal))
            collapsed = collapsed.Replace("__", "_", StringComparison.Ordinal);
        return collapsed.Trim('_');
    }

    private static string HumanizeIdentifier(string value)
    {
        if (string.IsNullOrWhiteSpace(value))
            return "";

        var result = new List<char>();
        for (var i = 0; i < value.Length; i++)
        {
            var ch = value[i];
            if (i > 0 && char.IsUpper(ch) && !char.IsWhiteSpace(value[i - 1]))
                result.Add(' ');
            result.Add(ch == '_' ? ' ' : ch);
        }
        return new string(result.ToArray()).Trim();
    }

    private static string NormalizePlayTime(string value)
    {
        var digits = value.Where(ch => char.IsDigit(ch) || ch == ':').ToArray();
        var cleaned = new string(digits);
        return string.IsNullOrWhiteSpace(cleaned) ? value : cleaned;
    }

    private static int PopCount(int value)
    {
        var count = 0;
        var remaining = (uint)value;
        while (remaining != 0)
        {
            count += (int)(remaining & 1);
            remaining >>= 1;
        }
        return count;
    }

    private static bool SafeBool(Func<bool> getValue)
    {
        try
        {
            return getValue();
        }
        catch
        {
            return false;
        }
    }

    private static int? TryGetIntProperty(object? target, string propertyName)
    {
        if (target is null || !TryGetPropertyValue(target, propertyName, out var value) || value is null)
            return null;

        return TryConvertToInt(value);
    }

    private static bool? TryGetBoolProperty(object? target, string propertyName)
    {
        if (target is null || !TryGetPropertyValue(target, propertyName, out var value) || value is null)
            return null;
        if (value is bool b)
            return b;
        if (TryConvertToInt(value) is int i)
            return i != 0;
        return null;
    }

    private static int? TryGetIntField(object target, string fieldName)
    {
        var value = GetFieldValue(target, fieldName);
        return TryConvertToInt(value);
    }

    private static object? GetFieldValue(object target, string fieldName)
    {
        var field = target.GetType().GetField(fieldName, BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic);
        return field?.GetValue(target);
    }

    private static int? TryConvertToInt(object? value)
    {
        if (value is null)
            return null;

        return value switch
        {
            byte b => b,
            sbyte sb => sb,
            short s => s,
            ushort us => us,
            int i => i,
            uint ui => unchecked((int)ui),
            long l => unchecked((int)l),
            ulong ul => unchecked((int)ul),
            _ when value.GetType().IsEnum => Convert.ToInt32(value),
            _ => null
        };
    }

    private static bool TryGetPropertyValue(object target, string propertyName, out object? value)
    {
        var property = target.GetType()
            .GetProperties(BindingFlags.Instance | BindingFlags.Public)
            .FirstOrDefault(candidate => candidate.Name == propertyName && candidate.GetIndexParameters().Length == 0);
        if (property is null)
        {
            value = null;
            return false;
        }

        value = property.GetValue(target);
        return true;
    }
}
