using System.Linq;
using System.Reflection;
using System.Text.Json;
using PKHeX.Core;
using Xunit;

namespace PKHeXBridge.IntegrationTests;

/// <summary>Shared helpers for <see cref="BridgeWriteBackIntegrationTests"/>.</summary>
internal static class WriteBackIntegrationHelpers
{
    /// <summary>
    /// 8-byte GBA English box name (no spaces/underscores); some games truncate or ignore other characters.
    /// </summary>
    public const string ExpectedBoxRenameMarker = "WBOK1234";

    public static string GetRepositoryRoot()
    {
        var current = new DirectoryInfo(AppContext.BaseDirectory);
        while (current is not null)
        {
            if (File.Exists(Path.Combine(current.FullName, "CMakeLists.txt")) &&
                Directory.Exists(Path.Combine(current.FullName, "tests", "test-data", "saves")))
            {
                return current.FullName;
            }

            current = current.Parent;
        }

        throw new InvalidOperationException("Could not locate repository root for integration tests.");
    }

    public static string WriteBackFixturePath =>
        Path.Combine(GetRepositoryRoot(), "tests", "test-data", "pkhex_bridge", "writeback_fixture.sav");

    /// <summary>
    /// Committed binary saves under <c>tests/test-data/saves/</c>. Tests must not depend on the mutable top-level <c>saves/</c> folder.
    /// </summary>
    public static string CommittedSaveFixture(string fileName) =>
        Path.Combine(GetRepositoryRoot(), "tests", "test-data", "saves", fileName);

    /// <summary>
    /// Canonical Gen 1–9 saves under <c>tests/test-data/saves/</c> (bridge write-back + probe integration).
    /// </summary>
    public static readonly string[] NumberedGenerationFixtureFileNames =
    [
        "1_Blue.sav",
        "2_cry.sav",
        "3_E.sav",
        "4_heartgold.sav",
        "5_Blk.sav",
        "6_X",
        "7_UM",
        "8_Sw",
        "9_Vi",
    ];

    public static IReadOnlyList<string> ResolveNumberedGenerationFixturePaths()
    {
        var root = GetRepositoryRoot();
        return NumberedGenerationFixtureFileNames
            .Select(name => Path.Combine(root, "tests", "test-data", "saves", name))
            .ToArray();
    }

    /// <summary>
    /// Binary saves committed for automated tests (excludes README and other non-save files).
    /// </summary>
    public static IReadOnlyList<string> EnumerateCommittedTestDataSavePaths()
    {
        var dir = Path.Combine(GetRepositoryRoot(), "tests", "test-data", "saves");
        if (!Directory.Exists(dir))
        {
            return [];
        }

        return Directory.EnumerateFiles(dir)
            .Where(static path =>
            {
                var name = Path.GetFileName(path);
                if (name.StartsWith('.'))
                {
                    return false;
                }

                if (name.EndsWith(".md", StringComparison.OrdinalIgnoreCase))
                {
                    return false;
                }

                return true;
            })
            .OrderBy(static p => p, StringComparer.Ordinal)
            .ToArray();
    }

    /// <summary>
    /// Uses <c>PKHEX_WRITEBACK_FIXTURE_PATH</c> when set to an existing file; otherwise the repo test-data path.
    /// </summary>
    public static string ResolveFixturePath()
    {
        var env = Environment.GetEnvironmentVariable("PKHEX_WRITEBACK_FIXTURE_PATH");
        if (!string.IsNullOrWhiteSpace(env) && File.Exists(env))
        {
            return env;
        }

        return WriteBackFixturePath;
    }

    public static string RequireFixturePath()
    {
        var path = ResolveFixturePath();
        Assert.True(File.Exists(path),
            $"Missing write-back fixture. Copy any PKHeX-supported .sav to:\n{WriteBackFixturePath}\nOr set PKHEX_WRITEBACK_FIXTURE_PATH.\nSee tests/test-data/pkhex_bridge/README.md");
        return path;
    }

    /// <summary>
    /// Saves used for schema-2 PC round-trip coverage. Uses only committed <c>tests/test-data/saves/</c>
    /// (plus optional <c>tests/test-data/pkhex_bridge/writeback_fixture.sav</c>), never the mutable top-level <c>saves/</c> folder.
    /// Override with <c>PKHEX_WRITEBACK_COMPAT_FIXTURES</c> (<see cref="Path.PathSeparator"/>-separated paths).
    /// </summary>
    public static IReadOnlyList<string> ResolveCompatibilityFixturePaths()
    {
        var env = Environment.GetEnvironmentVariable("PKHEX_WRITEBACK_COMPAT_FIXTURES");
        if (!string.IsNullOrWhiteSpace(env))
        {
            return env
                .Split(Path.PathSeparator, StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries)
                .Where(File.Exists)
                .Distinct()
                .ToArray();
        }

        var list = new List<string>();
        var primary = ResolveFixturePath();
        if (File.Exists(primary))
        {
            list.Add(primary);
        }

        foreach (var path in EnumerateCommittedTestDataSavePaths())
        {
            list.Add(path);
        }

        return list.Distinct(StringComparer.Ordinal).ToArray();
    }

    public static string GetBoxName(SaveFile sav, int box)
    {
        var method = sav.GetType().GetMethods(BindingFlags.Instance | BindingFlags.Public)
            .FirstOrDefault(candidate =>
                candidate.Name == "GetBoxName" &&
                candidate.GetParameters().Length == 1 &&
                candidate.GetParameters()[0].ParameterType == typeof(int));
        if (method?.Invoke(sav, new object[] { box }) is string name && !string.IsNullOrWhiteSpace(name))
        {
            return name;
        }

        return $"Box {box + 1}";
    }

    public static bool IsPresent(PKM? pkm) => pkm is not null && pkm.Species > 0;

    /// <summary>Encrypted box payload per slot; <see langword="null"/> means empty slot in projection JSON.</summary>
    public static byte[]?[,] CaptureBoxPayloadGrid(SaveFile sav)
    {
        var grid = new byte[]?[sav.BoxCount, sav.BoxSlotCount];
        for (var b = 0; b < sav.BoxCount; b++)
        {
            for (var s = 0; s < sav.BoxSlotCount; s++)
            {
                PKM? pkm = null;
                try
                {
                    pkm = sav.GetBoxSlotAtIndex(b, s);
                }
                catch
                {
                    // leave null
                }

                grid[b, s] = IsPresent(pkm) ? pkm!.EncryptedBoxData : null;
            }
        }

        return grid;
    }

    public static void WriteProjectionSchema1(string path, IReadOnlyList<string> boxNames)
    {
        using var stream = File.Create(path);
        using var writer = new Utf8JsonWriter(stream, new JsonWriterOptions { Indented = false });
        writer.WriteStartObject();
        writer.WriteNumber("projection_schema", 1);
        writer.WriteStartArray("box_names");
        foreach (var n in boxNames)
        {
            writer.WriteStringValue(n);
        }

        writer.WriteEndArray();
        writer.WriteEndObject();
    }

    public static void WriteProjectionSchema2(string path, IReadOnlyList<string> boxNames, byte[]?[,] payloadGrid)
    {
        var boxes = payloadGrid.GetLength(0);
        var slots = payloadGrid.GetLength(1);
        if (boxNames.Count != boxes)
        {
            throw new ArgumentException("box_names length must match payload grid box count");
        }

        using var stream = File.Create(path);
        using var writer = new Utf8JsonWriter(stream, new JsonWriterOptions { Indented = false });
        writer.WriteStartObject();
        writer.WriteNumber("projection_schema", 2);
        writer.WriteStartArray("box_names");
        foreach (var n in boxNames)
        {
            writer.WriteStringValue(n);
        }

        writer.WriteEndArray();
        writer.WriteStartArray("pc_boxes");
        for (var b = 0; b < boxes; b++)
        {
            writer.WriteStartObject();
            writer.WriteStartArray("slots");
            for (var s = 0; s < slots; s++)
            {
                var raw = payloadGrid[b, s];
                if (raw is null || raw.Length == 0)
                {
                    writer.WriteNullValue();
                }
                else
                {
                    writer.WriteStartObject();
                    writer.WriteString("raw_payload_base64", Convert.ToBase64String(raw));
                    writer.WriteString("raw_hash_sha256",
                        Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(raw)).ToLowerInvariant());
                    writer.WriteEndObject();
                }
            }

            writer.WriteEndArray();
            writer.WriteEndObject();
        }

        writer.WriteEndArray();
        writer.WriteEndObject();
    }

    public static void WritePcBoxesOnlyProjectionSchema2(string path, byte[]?[,] payloadGrid)
    {
        var boxes = payloadGrid.GetLength(0);
        var slots = payloadGrid.GetLength(1);
        using var stream = File.Create(path);
        using var writer = new Utf8JsonWriter(stream, new JsonWriterOptions { Indented = false });
        writer.WriteStartObject();
        writer.WriteNumber("projection_schema", 2);
        writer.WriteStartArray("pc_boxes");
        for (var b = 0; b < boxes; b++)
        {
            writer.WriteStartObject();
            writer.WriteStartArray("slots");
            for (var s = 0; s < slots; s++)
            {
                var raw = payloadGrid[b, s];
                if (raw is null || raw.Length == 0)
                {
                    writer.WriteNullValue();
                }
                else
                {
                    writer.WriteStartObject();
                    writer.WriteString("raw_payload_base64", Convert.ToBase64String(raw));
                    writer.WriteString("raw_hash_sha256",
                        Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(raw)).ToLowerInvariant());
                    writer.WriteEndObject();
                }
            }

            writer.WriteEndArray();
            writer.WriteEndObject();
        }

        writer.WriteEndArray();
        writer.WriteEndObject();
    }

    /// <summary>Find two occupied slots to swap; returns false if fewer than two Pokémon in PC.</summary>
    public static bool TryFindTwoOccupiedSlots(
        SaveFile sav,
        out int boxA,
        out int slotA,
        out int boxB,
        out int slotB)
    {
        boxA = slotA = boxB = slotB = 0;
        var first = (-1, -1);
        for (var b = 0; b < sav.BoxCount; b++)
        {
            for (var s = 0; s < sav.BoxSlotCount; s++)
            {
                PKM? pkm = null;
                try
                {
                    pkm = sav.GetBoxSlotAtIndex(b, s);
                }
                catch
                {
                    continue;
                }

                if (!IsPresent(pkm))
                {
                    continue;
                }

                if (first.Item1 < 0)
                {
                    first = (b, s);
                }
                else
                {
                    boxA = first.Item1;
                    slotA = first.Item2;
                    boxB = b;
                    slotB = s;
                    return true;
                }
            }
        }

        return false;
    }

    public static void Swap(ref byte[]? a, ref byte[]? b)
    {
        var t = a;
        a = b;
        b = t;
    }
}
