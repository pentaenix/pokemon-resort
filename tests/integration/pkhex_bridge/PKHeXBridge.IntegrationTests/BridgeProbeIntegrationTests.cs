using PKHeXBridge;
using Xunit;

namespace PKHeXBridge.IntegrationTests;

/// <summary>
/// Bridge probe smoke tests using only committed files under <c>tests/test-data/saves/</c>.
/// </summary>
public class BridgeProbeIntegrationTests
{
    public static TheoryData<string> NumberedGenerationProbeCases()
    {
        var data = new TheoryData<string>();
        foreach (var name in WriteBackIntegrationHelpers.NumberedGenerationFixtureFileNames)
        {
            data.Add(name);
        }

        return data;
    }

    /// <summary>
    /// Gen 1 Blue through Gen 9 Violet — each fixture must exist (committed) and probe successfully.
    /// </summary>
    [Theory]
    [MemberData(nameof(NumberedGenerationProbeCases))]
    public void Probe_NumberedGenerationFixture_Loads(string fileName)
    {
        var savePath = WriteBackIntegrationHelpers.CommittedSaveFixture(fileName);

        Assert.True(File.Exists(savePath),
            $"Missing numbered generation fixture. Expected committed file:\n{savePath}");

        var result = BridgeProbe.Probe(savePath);

        Assert.True(result.Success, $"{fileName}: {result.Error} {result.Details}");
        Assert.False(string.IsNullOrWhiteSpace(result.SaveType));
        Assert.NotNull(result.Boxes);
        Assert.NotEmpty(result.Boxes!);
    }

    [Theory]
    [InlineData("Pokemonheartgold.sav", "SAV4HGSS", "HG", "pokemon_heartgold", "Ethan")]
    public void Probe_LoadsKnownSaves(string fileName, string expectedType, string expectedGame, string expectedGameId, string expectedTrainer)
    {
        var savePath = WriteBackIntegrationHelpers.CommittedSaveFixture(fileName);

        var result = BridgeProbe.Probe(savePath);

        Assert.True(result.Success);
        Assert.Equal(expectedType, result.SaveType);
        Assert.Equal(expectedGame, result.Game);
        Assert.Equal(expectedTrainer, result.TrainerName);
        Assert.Equal(expectedGameId, result.GameId);
        Assert.Equal(expectedTrainer, result.PlayerName);
        Assert.NotNull(result.Party);
        Assert.NotEmpty(result.Party!);
        Assert.NotNull(result.Box1);
        Assert.NotNull(result.Trainer);
        Assert.Equal(expectedTrainer, result.Trainer!.Name);
        Assert.NotNull(result.Pokedex);
        Assert.NotNull(result.Boxes);
        Assert.NotEmpty(result.Boxes!);
        Assert.Equal(result.Boxes![0].SlotCount, result.Box1!.Count);
        Assert.NotNull(result.AllPokemon);
        Assert.NotEmpty(result.AllPokemon!);
        Assert.NotNull(result.Bag);
        Assert.NotNull(result.PlayTime);
        Assert.True(result.PokedexCount >= 0);
        Assert.True(result.Badges >= 0);
        Assert.NotNull(result.Status);
    }

    [Fact]
    public void Probe_BlueSaveWithAmbiguousPkhexVersion_UsesPokemonBlueGameId()
    {
        var savePath = WriteBackIntegrationHelpers.CommittedSaveFixture("pokemon blue - ASH.sav");

        var result = BridgeProbe.Probe(savePath);

        Assert.True(result.Success);
        Assert.Equal("SAV1", result.SaveType);
        Assert.Equal("GN", result.Game);
        Assert.Equal("pokemon_blue", result.GameId);
        Assert.Equal("ASH", result.PlayerName);
        Assert.NotNull(result.Party);
        Assert.NotNull(result.Box1);
        Assert.NotNull(result.Trainer);
        Assert.Equal("pokemon_blue", result.Trainer!.GameId);
    }

    [Fact]
    public void Probe_HeartGoldSave_UsesHeartGoldGameId()
    {
        var savePath = WriteBackIntegrationHelpers.CommittedSaveFixture("Pokemonheartgold.sav");

        var result = BridgeProbe.Probe(savePath);

        Assert.True(result.Success);
        Assert.Equal("pokemon_heartgold", result.GameId);
    }

    [Fact]
    public void Probe_TransferSave_ReadsPackedMarkingStates()
    {
        var savePath = WriteBackIntegrationHelpers.CommittedSaveFixture("A_pkm_US_Trade");

        var result = BridgeProbe.Probe(savePath);

        Assert.True(result.Success);
        Assert.NotNull(result.AllPokemon);

        var markings = result.AllPokemon!
            .Select(static pokemon => pokemon.Markings)
            .Where(static markings => markings != 0)
            .ToArray();

        Assert.NotEmpty(markings);
        Assert.Contains(1365, markings);
        Assert.Contains(601, markings);
    }
}
