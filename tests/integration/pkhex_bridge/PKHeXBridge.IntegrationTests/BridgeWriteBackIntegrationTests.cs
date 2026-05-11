using System.Linq;
using PKHeX.Core;
using PKHeXBridge;
using Xunit;

namespace PKHeXBridge.IntegrationTests;

/// <summary>
/// End-to-end write-back tests using <c>tests/test-data/pkhex_bridge/writeback_fixture.sav</c>
/// (copy any PKHeX-supported save there, or set <c>PKHEX_WRITEBACK_FIXTURE_PATH</c>).
/// </summary>
public class BridgeWriteBackIntegrationTests
{
    [Fact]
    public void WriteBack_RoundTrip_MinimalChange_PkhexStillLoadsSave()
    {
        var fixture = WriteBackIntegrationHelpers.RequireFixturePath();
        var tempDir = Path.Combine(Path.GetTempPath(), "pkr_wb_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(tempDir);
        try
        {
            var savePath = Path.Combine(tempDir, "roundtrip.sav");
            File.Copy(fixture, savePath);

            var sav = SaveUtil.GetSaveFile(savePath);
            Assert.NotNull(sav);

            var names = Enumerable.Range(0, sav.BoxCount)
                .Select(i => WriteBackIntegrationHelpers.GetBoxName(sav, i))
                .ToArray();
            names[0] += "_WB_RT";

            var projPath = Path.Combine(tempDir, "projection.json");
            WriteBackIntegrationHelpers.WriteProjectionSchema1(projPath, names);

            var result = BridgeWriteBack.WriteProjection(savePath, projPath);
            Assert.True(result.Success);

            var sav2 = SaveUtil.GetSaveFile(savePath);
            Assert.NotNull(sav2);
            Assert.True(sav2.ChecksumsValid);
        }
        finally
        {
            TryDeleteDirectory(tempDir);
        }
    }

    [Fact]
    public void WriteBack_SwapTwoPcSlots_VerifySpeciesSwapped()
    {
        var fixture = WriteBackIntegrationHelpers.RequireFixturePath();
        var tempDir = Path.Combine(Path.GetTempPath(), "pkr_wb_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(tempDir);
        try
        {
            var savePath = Path.Combine(tempDir, "swap.sav");
            File.Copy(fixture, savePath);

            var sav = SaveUtil.GetSaveFile(savePath);
            Assert.NotNull(sav);

            if (!WriteBackIntegrationHelpers.TryFindTwoOccupiedSlots(sav, out var ba, out var sa, out var bb, out var sb))
            {
                Assert.Fail(
                    "Fixture needs at least two Pokémon in PC boxes for swap test. Add PC Pokémon or use a different save.");
            }

            var pkA = sav.GetBoxSlotAtIndex(ba, sa);
            var pkB = sav.GetBoxSlotAtIndex(bb, sb);
            Assert.True(WriteBackIntegrationHelpers.IsPresent(pkA));
            Assert.True(WriteBackIntegrationHelpers.IsPresent(pkB));
            var speciesA = pkA!.Species;
            var speciesB = pkB!.Species;

            var grid = WriteBackIntegrationHelpers.CaptureBoxPayloadGrid(sav);
            WriteBackIntegrationHelpers.Swap(ref grid[ba, sa], ref grid[bb, sb]);

            var names = Enumerable.Range(0, sav.BoxCount)
                .Select(i => WriteBackIntegrationHelpers.GetBoxName(sav, i))
                .ToArray();

            var projPath = Path.Combine(tempDir, "projection.json");
            WriteBackIntegrationHelpers.WriteProjectionSchema2(projPath, names, grid);

            var result = BridgeWriteBack.WriteProjection(savePath, projPath);
            Assert.True(result.Success);

            var sav2 = SaveUtil.GetSaveFile(savePath);
            Assert.NotNull(sav2);

            var afterA = sav2.GetBoxSlotAtIndex(ba, sa);
            var afterB = sav2.GetBoxSlotAtIndex(bb, sb);
            Assert.True(WriteBackIntegrationHelpers.IsPresent(afterA));
            Assert.True(WriteBackIntegrationHelpers.IsPresent(afterB));
            Assert.Equal(speciesB, afterA!.Species);
            Assert.Equal(speciesA, afterB!.Species);
        }
        finally
        {
            TryDeleteDirectory(tempDir);
        }
    }

    [Fact]
    public void WriteBack_PreserveBoxSlot_KeepsOriginalSpeciesWhileOtherSlotsStillApply()
    {
        var fixture = WriteBackIntegrationHelpers.RequireFixturePath();
        var tempDir = Path.Combine(Path.GetTempPath(), "pkr_wb_preserve_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(tempDir);
        try
        {
            var savePath = Path.Combine(tempDir, "preserve.sav");
            File.Copy(fixture, savePath);

            var sav = SaveUtil.GetSaveFile(savePath);
            Assert.NotNull(sav);

            if (!WriteBackIntegrationHelpers.TryFindTwoOccupiedSlots(sav, out var ba, out var sa, out var bb, out var sb))
            {
                Assert.Fail(
                    "Fixture needs at least two Pokémon in PC boxes. Add PC Pokémon or use a different save.");
            }

            var pkA = sav.GetBoxSlotAtIndex(ba, sa);
            var pkB = sav.GetBoxSlotAtIndex(bb, sb);
            Assert.True(WriteBackIntegrationHelpers.IsPresent(pkA));
            Assert.True(WriteBackIntegrationHelpers.IsPresent(pkB));
            var speciesA = pkA!.Species;
            var speciesB = pkB!.Species;
            if (speciesA == speciesB)
            {
                Assert.Fail("Fixture needs two PC Pokémon with different species for preserve regression test.");
            }

            var grid = WriteBackIntegrationHelpers.CaptureBoxPayloadGrid(sav);
            var names = Enumerable.Range(0, sav.BoxCount)
                .Select(i => WriteBackIntegrationHelpers.GetBoxName(sav, i))
                .ToArray();

            var projPath = Path.Combine(tempDir, "projection.json");
            WriteBackIntegrationHelpers.WriteProjectionSchema2PreserveSlotAndCloneInto(
                projPath,
                names,
                grid,
                ba,
                sa,
                bb,
                sb);

            var result = BridgeWriteBack.WriteProjection(savePath, projPath);
            Assert.True(result.Success, $"{result.Error} {result.Details}");

            var sav2 = SaveUtil.GetSaveFile(savePath);
            Assert.NotNull(sav2);

            var afterA = sav2.GetBoxSlotAtIndex(ba, sa);
            var afterB = sav2.GetBoxSlotAtIndex(bb, sb);
            Assert.True(WriteBackIntegrationHelpers.IsPresent(afterA));
            Assert.True(WriteBackIntegrationHelpers.IsPresent(afterB));
            Assert.Equal(speciesA, afterA!.Species);
            Assert.Equal(speciesA, afterB!.Species);
        }
        finally
        {
            TryDeleteDirectory(tempDir);
        }
    }

    [Fact]
    public void WriteBack_Schema2_RoundTripsAllCompatibilityFixtures()
    {
        var fixtures = WriteBackIntegrationHelpers.ResolveCompatibilityFixturePaths();
        Assert.NotEmpty(fixtures);

        foreach (var fixture in fixtures)
        {
            var tempDir = Path.Combine(Path.GetTempPath(), "pkr_wb_compat_" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(tempDir);
            try
            {
                var savePath = Path.Combine(tempDir, Path.GetFileName(fixture));
                File.Copy(fixture, savePath);

                var sav = SaveUtil.GetSaveFile(savePath);
                Assert.NotNull(sav);
                if (!sav.HasBox)
                    continue;

                var grid = WriteBackIntegrationHelpers.CaptureBoxPayloadGrid(sav);
                var projPath = Path.Combine(tempDir, "projection.json");
                WriteBackIntegrationHelpers.WritePcBoxesOnlyProjectionSchema2(projPath, grid);

                var result = BridgeWriteBack.WriteProjection(savePath, projPath);
                Assert.True(result.Success, $"{Path.GetFileName(fixture)} failed: {result.Error} {result.Details}");

                var sav2 = SaveUtil.GetSaveFile(savePath);
                Assert.NotNull(sav2);
                Assert.True(sav2.ChecksumsValid, $"{Path.GetFileName(fixture)} checksums invalid after write-back");
            }
            finally
            {
                TryDeleteDirectory(tempDir);
            }
        }
    }

    /// <summary>
    /// Committed Gen 1 (Blue) through Gen 9 (Violet) saves: PC payload round-trip only; temp working copy, no profile DB.
    /// </summary>
    [Fact]
    public void WriteBack_Schema2_RoundTrips_NumberedGen1Through9Fixtures()
    {
        var fixtures = WriteBackIntegrationHelpers.ResolveNumberedGenerationFixturePaths();
        Assert.Equal(9, fixtures.Count);

        foreach (var fixture in fixtures)
        {
            Assert.True(File.Exists(fixture), $"Add missing save to tests/test-data/saves: {Path.GetFileName(fixture)}");
        }

        foreach (var fixture in fixtures)
        {
            var tempDir = Path.Combine(Path.GetTempPath(), "pkr_wb_gen19_" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(tempDir);
            try
            {
                var savePath = Path.Combine(tempDir, Path.GetFileName(fixture));
                File.Copy(fixture, savePath);

                var sav = SaveUtil.GetSaveFile(savePath);
                Assert.NotNull(sav);
                if (!sav.HasBox)
                {
                    Assert.Fail($"{Path.GetFileName(fixture)}: save has no PC box data for write-back test");
                }

                var grid = WriteBackIntegrationHelpers.CaptureBoxPayloadGrid(sav);
                var projPath = Path.Combine(tempDir, "projection.json");
                WriteBackIntegrationHelpers.WritePcBoxesOnlyProjectionSchema2(projPath, grid);

                var result = BridgeWriteBack.WriteProjection(savePath, projPath);
                var label = Path.GetFileName(fixture);
                Assert.True(result.Success, $"{label} failed: {result.Error} {result.Details}");

                var sav2 = SaveUtil.GetSaveFile(savePath);
                Assert.NotNull(sav2);
                Assert.True(sav2.ChecksumsValid, $"{label} checksums invalid after write-back");
            }
            finally
            {
                TryDeleteDirectory(tempDir);
            }
        }
    }

    [Fact]
    public void WriteBack_Gen2ProjectedDv16_PersistsToSavedPokemon()
    {
        var fixture = WriteBackIntegrationHelpers.CommittedSaveFixture("2_cry.sav");
        Assert.True(File.Exists(fixture), $"Missing Gen 2 fixture: {fixture}");
        var tempDir = Path.Combine(Path.GetTempPath(), "pkr_wb_gen2_dv_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(tempDir);
        try
        {
            var savePath = Path.Combine(tempDir, "2_cry.sav");
            File.Copy(fixture, savePath);

            var sav = SaveUtil.GetSaveFile(savePath);
            Assert.NotNull(sav);

            var grid = WriteBackIntegrationHelpers.CaptureBoxPayloadGrid(sav);
            var target = FindFirstOccupiedGen12Payload(grid);
            Assert.True(target.HasValue, "Gen 2 fixture needs at least one occupied PC slot with PK2 payload bytes");

            const ushort dv16 = 0x1234;
            var (box, slot) = target.Value;
            var raw = (byte[])grid[box, slot]!.Clone();
            Assert.True(raw.Length >= 0x17, "PK2 projected payload too short for DV16 offset");
            raw[0x15] = (byte)(dv16 >> 8);
            raw[0x16] = (byte)(dv16 & 0xFF);
            grid[box, slot] = raw;

            var projPath = Path.Combine(tempDir, "projection.json");
            WriteBackIntegrationHelpers.WritePcBoxesOnlyProjectionSchema2(projPath, grid);

            var result = BridgeWriteBack.WriteProjection(savePath, projPath);
            Assert.True(result.Success, $"{result.Error} {result.Details}");

            var sav2 = SaveUtil.GetSaveFile(savePath);
            Assert.NotNull(sav2);
            var written = sav2.GetBoxSlotAtIndex(box, slot);
            Assert.NotNull(written);
            Assert.Equal(dv16, GetDv16(written!));
        }
        finally
        {
            TryDeleteDirectory(tempDir);
        }
    }

    [Fact]
    public void WriteBack_RenameBox_ReloadShowsExpectedName()
    {
        var fixture = WriteBackIntegrationHelpers.RequireFixturePath();
        var tempDir = Path.Combine(Path.GetTempPath(), "pkr_wb_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(tempDir);
        try
        {
            var savePath = Path.Combine(tempDir, "rename.sav");
            File.Copy(fixture, savePath);

            var sav = SaveUtil.GetSaveFile(savePath);
            Assert.NotNull(sav);

            var names = Enumerable.Range(0, sav.BoxCount)
                .Select(i => WriteBackIntegrationHelpers.GetBoxName(sav, i))
                .ToArray();
            const int renameBoxIndex = 0;
            names[renameBoxIndex] = WriteBackIntegrationHelpers.ExpectedBoxRenameMarker;

            var projPath = Path.Combine(tempDir, "projection.json");
            WriteBackIntegrationHelpers.WriteProjectionSchema1(projPath, names);

            var result = BridgeWriteBack.WriteProjection(savePath, projPath);
            Assert.True(result.Success);

            var sav2 = SaveUtil.GetSaveFile(savePath);
            Assert.NotNull(sav2);
            Assert.Equal(
                WriteBackIntegrationHelpers.ExpectedBoxRenameMarker,
                WriteBackIntegrationHelpers.GetBoxName(sav2, renameBoxIndex));
        }
        finally
        {
            TryDeleteDirectory(tempDir);
        }
    }

    private static void TryDeleteDirectory(string path)
    {
        try
        {
            if (Directory.Exists(path))
            {
                Directory.Delete(path, recursive: true);
            }
        }
        catch
        {
            // temp cleanup best-effort
        }
    }

    private static (int Box, int Slot)? FindFirstOccupiedGen12Payload(byte[]?[,] grid)
    {
        for (var b = 0; b < grid.GetLength(0); b++)
        {
            for (var s = 0; s < grid.GetLength(1); s++)
            {
                var raw = grid[b, s];
                if (raw is { Length: >= 0x17 })
                    return (b, s);
            }
        }

        return null;
    }

    private static ushort GetDv16(PKM pokemon)
    {
        var prop = pokemon.GetType().GetProperty("DV16");
        Assert.NotNull(prop);
        return Convert.ToUInt16(prop.GetValue(pokemon));
    }
}
