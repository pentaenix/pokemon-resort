using PKHeX.Core;
using PKHeXBridge;
using Xunit;

namespace PKHeXBridge.IntegrationTests;

/// <summary>
/// Uses <c>tests/test-data/pkhex_bridge/item_transfer.sav</c>: Box 1 slots 1–2 are Bulbasaur (held Poké Ball)
/// and Charmander (no hold). Asserts <see cref="PkmHeldItemPatch"/> matches PKHeX clone exports (no other fields changed).
/// </summary>
public class HeldItemTransferIntegrationTests
{
    /// <summary>First PC box index.</summary>
    private const int Box = 0;

    /// <summary>User-facing slots 1 and 2 → zero-based indices 0 and 1.</summary>
    private const int BulbasaurSlot = 0;

    private const int CharmanderSlot = 1;

    private static string RequireItemTransferFixturePath()
    {
        var path = Path.Combine(
            WriteBackIntegrationHelpers.GetRepositoryRoot(),
            "tests",
            "test-data",
            "pkhex_bridge",
            "item_transfer.sav");
        Assert.True(
            File.Exists(path),
            $"Missing item transfer fixture.\nExpected:\n{path}\nCopy a prepared save there (Bulbasaur + Poké Ball in box 1 slot 1, Charmander slot 2).");
        return path;
    }

    [Fact]
    public void Fixture_HasBulbasaurWithPokeBall_AndCharmanderWithoutHold_InFirstBoxFirstTwoSlots()
    {
        var path = RequireItemTransferFixturePath();
        var sav = SaveUtil.GetVariantSAV(path);
        Assert.NotNull(sav);

        var bulba = sav.GetBoxSlotAtIndex(Box, BulbasaurSlot);
        var charm = sav.GetBoxSlotAtIndex(Box, CharmanderSlot);
        Assert.True(WriteBackIntegrationHelpers.IsPresent(bulba));
        Assert.True(WriteBackIntegrationHelpers.IsPresent(charm));

        Assert.Equal((ushort)Species.Bulbasaur, bulba!.Species);
        Assert.Equal((ushort)Species.Charmander, charm!.Species);

        const ushort pokeBall = 4; // Gen III item table
        Assert.Equal(pokeBall, bulba.HeldItem);
        Assert.Equal((ushort)0, charm.HeldItem);
    }

    [Fact]
    public void PkmHeldItemPatch_ClearBulbasaurItem_MatchesPkhexClone_OnlyHeldItemChanges()
    {
        var path = RequireItemTransferFixturePath();
        var sav = SaveUtil.GetVariantSAV(path);
        Assert.NotNull(sav);

        var bulba = sav.GetBoxSlotAtIndex(Box, BulbasaurSlot);
        Assert.True(WriteBackIntegrationHelpers.IsPresent(bulba));

        var expected = bulba!.Clone();
        expected.HeldItem = 0;

        var patch = PkmHeldItemPatch.ApplyFromPayload(Convert.ToBase64String(bulba.EncryptedBoxData), 0);
        Assert.True(patch.Success);
        Assert.NotNull(patch.RawPayloadBase64);

        var expectedBytes = expected.EncryptedBoxData;
        var actualBytes = Convert.FromBase64String(patch.RawPayloadBase64!);
        Assert.Equal(expectedBytes, actualBytes);
    }

    [Fact]
    public void PkmHeldItemPatch_GiveCharmanderPokeBall_MatchesPkhexClone()
    {
        var path = RequireItemTransferFixturePath();
        var sav = SaveUtil.GetVariantSAV(path);
        Assert.NotNull(sav);

        var charm = sav.GetBoxSlotAtIndex(Box, CharmanderSlot);
        Assert.True(WriteBackIntegrationHelpers.IsPresent(charm));

        const ushort pokeBall = 4;
        var expected = charm!.Clone();
        expected.HeldItem = pokeBall;

        var patch = PkmHeldItemPatch.ApplyFromPayload(Convert.ToBase64String(charm.EncryptedBoxData), pokeBall);
        Assert.True(patch.Success);
        Assert.NotNull(patch.RawPayloadBase64);

        Assert.Equal(expected.EncryptedBoxData, Convert.FromBase64String(patch.RawPayloadBase64!));
    }
}
