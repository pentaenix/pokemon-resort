using System.Text.Json;
using PKHeX.Core;
using Xunit;

namespace PKHeXBridge.UnitTests;

/// <summary>
/// Gen I–II have no nickname bit — PKHeX derives legality from encoded default species spelling vs overlay text.
/// </summary>
public class BridgeGen12NicknameFinalizeTests
{
    [Fact]
    public void FinalizeProjectedNickname_RedBlue_StaleNicknamedFlagWithSpeciesSpelling_ClearsNickname()
    {
        var pk1 = EntityBlank.GetBlank(typeof(PK1));
        pk1.Species = (ushort)Species.Poliwhirl;
        pk1.Version = GameVersion.RD;
        pk1.Language = (int)LanguageID.English;
        pk1.RefreshChecksum();

        using var overlay = JsonDocument.Parse(
            """
            {"nickname":"Poliwhirl","is_nicknamed":true}
            """);

        var notes = new List<string>();
        BridgeProjectReconcile.FinalizeProjectedNickname(pk1, null, overlay.RootElement, notes);

        Assert.False(pk1.IsNicknamed);
        Assert.Equal("POLIWHIRL", pk1.Nickname);
        Assert.Contains("species-name nickname", notes[0]);
    }

    [Fact]
    public void FinalizeProjectedNickname_NoNicknameOverlay_NormalizesTitleCaseSpecies()
    {
        var pk1 = EntityBlank.GetBlank(typeof(PK1));
        pk1.Species = (ushort)Species.Poliwhirl;
        pk1.Version = GameVersion.RD;
        pk1.Language = (int)LanguageID.English;
        pk1.Nickname = "Poliwhirl";
        pk1.IsNicknamed = true;

        var notes = new List<string>();
        BridgeProjectReconcile.FinalizeProjectedNickname(pk1, null, null, notes);

        Assert.False(pk1.IsNicknamed);
        Assert.Equal("POLIWHIRL", pk1.Nickname);
        Assert.Single(notes);
        Assert.Contains("overlay omitted `is_nicknamed`", notes[0]);
    }

    [Fact]
    public void FinalizeProjectedNickname_RedBlue_TrulyCustomNickname_KeepsFlag()
    {
        var pk1 = EntityBlank.GetBlank(typeof(PK1));
        pk1.Species = (ushort)Species.Poliwhirl;
        pk1.Version = GameVersion.RD;
        pk1.Language = (int)LanguageID.English;

        using var overlay = JsonDocument.Parse(
            """
            {"nickname":"SPLASH","is_nicknamed":true}
            """);

        var notes = new List<string>();
        BridgeProjectReconcile.FinalizeProjectedNickname(pk1, null, overlay.RootElement, notes);

        Assert.True(pk1.IsNicknamed);
        Assert.Equal("SPLASH", pk1.Nickname);
        Assert.Contains("applied canonical nickname", Assert.Single(notes));
    }

    [Fact]
    public void PastProjection_Pk9SourceNicknamedTrueButSpeciesText_ForcesGen1GbDefaultNickname()
    {
        var pk9 = EntityBlank.GetBlank(typeof(PK9));
        pk9.Species = (ushort)Species.Poliwhirl;
        pk9.Version = GameVersion.SL;
        pk9.Language = (int)LanguageID.English;
        pk9.Nickname = "Poliwhirl";
        pk9.IsNicknamed = true;
        pk9.OriginalTrainerName = "RED";
        pk9.TID16 = 4321;
        pk9.RefreshChecksum();

        var notes = new List<string>();
        var lost = new HashSet<string>();
        var pk1 = BridgeProjectPastProjection.ProjectToPastGeneration(pk9, typeof(PK1), (int)GameVersion.RD, notes, lost);

        Assert.False(pk1.IsNicknamed);
        Assert.Equal("POLIWHIRL", pk1.Nickname);
    }

    [Fact]
    public void PastProjection_Pk9SourceNicknamedTrueButSpeciesText_ForcesGen2GbDefaultNickname()
    {
        var pk9 = EntityBlank.GetBlank(typeof(PK9));
        pk9.Species = (ushort)Species.Poliwhirl;
        pk9.Version = GameVersion.SL;
        pk9.Language = (int)LanguageID.English;
        pk9.Nickname = "Poliwhirl";
        pk9.IsNicknamed = true;
        pk9.OriginalTrainerName = "RED";
        pk9.TID16 = 4321;
        pk9.RefreshChecksum();

        var notes = new List<string>();
        var lost = new HashSet<string>();
        var pk2 =
            BridgeProjectPastProjection.ProjectToPastGeneration(pk9, typeof(PK2), (int)GameVersion.C, notes, lost);

        Assert.False(pk2.IsNicknamed);
        Assert.Equal("POLIWHIRL", pk2.Nickname);
    }
}
