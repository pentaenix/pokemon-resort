using System.Security.Cryptography;
using System.Text.Json;
using PKHeX.Core;

namespace PKHeXBridge;

public sealed class BridgeProjectResult
{
    public bool Success { get; init; }
    /// <summary>PID inside the projected PKM (<c>converted.PID</c>) for Resort mirror transport tracking.</summary>
    public uint? TargetPid { get; init; }
    public string? TargetFormatName { get; init; }
    public string? TargetRawPayloadBase64 { get; init; }
    public string? TargetRawHashSha256 { get; init; }
    public BridgeProjectLegality? Legality { get; init; }
    public BridgeProjectLossManifest? LossManifest { get; init; }
    public BridgeProjectBeacon? Beacon { get; init; }
    public string? Status { get; init; }
    public string? Error { get; init; }
    public string? Details { get; init; }
}

public sealed record BridgeProjectLegality(bool Valid, IReadOnlyList<string> Warnings);

public sealed record BridgeProjectLossManifest(
    bool Lossy,
    IReadOnlyList<string> LostCategories,
    IReadOnlyList<string> ProjectedCategories,
    IReadOnlyList<string> Notes);

public sealed record BridgeProjectBeacon(ushort? Tid16, string? OtName, int LineageRootSpecies);

public static class BridgeProject
{
    public static BridgeProjectResult ProjectFromJsonFile(string requestPath)
    {
        if (!File.Exists(requestPath))
        {
            return new BridgeProjectResult
            {
                Success = false,
                Status = "error",
                Error = "missing_file",
                Details = requestPath
            };
        }

        try
        {
            var requestText = File.ReadAllText(requestPath);
            using var document = JsonDocument.Parse(requestText);
            var root = document.RootElement;
            if (!root.TryGetProperty("bridge_project_schema", out var schema) ||
                schema.ValueKind != JsonValueKind.Number ||
                schema.GetInt32() != 1)
            {
                return new BridgeProjectResult
                {
                    Success = false,
                    Status = "error",
                    Error = "unsupported_schema",
                    Details = "Expected bridge_project_schema=1."
                };
            }

            var missing = RequiredMissing(root);
            if (missing is not null)
            {
                return new BridgeProjectResult
                {
                    Success = false,
                    Status = "error",
                    Error = "missing_field",
                    Details = missing
                };
            }

            var allowLossy = ReadAllowLossy(root);

            var sourceB64 = root.GetProperty("source_raw_payload_base64").GetString()!;
            var expectedHash = root.GetProperty("source_raw_hash_sha256").GetString()!;
            var sourceFmtDeclared = root.GetProperty("source_format_name").GetString()!;
            var targetFmtDeclared = root.GetProperty("target_format_name").GetString()!;
            var targetGame = root.GetProperty("target_game").GetInt32();

            byte[] sourceBytes;
            try
            {
                sourceBytes = Convert.FromBase64String(sourceB64);
            }
            catch (FormatException ex)
            {
                return new BridgeProjectResult
                {
                    Success = false,
                    Status = "error",
                    Error = "invalid_base64",
                    Details = ex.Message
                };
            }

            var actualHash = Convert.ToHexString(SHA256.HashData(sourceBytes)).ToLowerInvariant();
            if (!string.Equals(actualHash, expectedHash, StringComparison.OrdinalIgnoreCase))
            {
                return new BridgeProjectResult
                {
                    Success = false,
                    Status = "error",
                    Error = "hash_mismatch",
                    Details = "source_raw_hash_sha256 does not match decoded source_raw_payload_base64."
                };
            }

            var sourceContext = ResolveEntityContext(sourceFmtDeclared);
            var pk = sourceContext == EntityContext.None
                ? EntityFormat.GetFromBytes(sourceBytes)
                : EntityFormat.GetFromBytes(sourceBytes, sourceContext);
            if (pk is null)
            {
                return new BridgeProjectResult
                {
                    Success = false,
                    Status = "error",
                    Error = "decode_failed",
                    Details = "EntityFormat could not decode source_raw_payload_base64."
                };
            }

            if (!string.IsNullOrWhiteSpace(sourceFmtDeclared) &&
                !string.Equals(sourceFmtDeclared, pk.GetType().Name, StringComparison.OrdinalIgnoreCase))
            {
                // Non-fatal: request metadata may be wrong; we still use the decoded entity type.
            }

            var destType = ResolvePkmType(targetFmtDeclared);
            if (destType is null)
            {
                return new BridgeProjectResult
                {
                    Success = false,
                    Status = "error",
                    Error = "unknown_target_format",
                    Details = targetFmtDeclared
                };
            }

            var previousCompat = EntityConverter.AllowIncompatibleConversion;
            try
            {
                if (allowLossy)
                {
                    EntityConverter.AllowIncompatibleConversion = EntityCompatibilitySetting.AllowIncompatibleSane;
                }

                var extraNotes = new List<string>();
                var extraLost = new List<string>();
                var usedManualPastProjection = BridgeProjectPastProjection.ShouldUseManualPastProjection(pk, destType);
                var convResult = EntityConverterResult.None;
                PKM? converted;
                if (usedManualPastProjection)
                {
                    converted = BridgeProjectPastProjection.ProjectToPastGeneration(
                        pk,
                        destType,
                        targetGame,
                        extraNotes,
                        extraLost);
                }
                else
                {
                    converted = EntityConverter.ConvertToType(pk, destType, out convResult);
                }

                if (converted is null)
                {
                    return new BridgeProjectResult
                    {
                        Success = false,
                        Status = "error",
                        Error = "conversion_failed",
                        Details = $"{convResult} target_game={targetGame}"
                    };
                }

                if (root.TryGetProperty("move_reconciliation", out var moveRecon))
                {
                    BridgeProjectReconcile.ApplyMoveReconciliation(converted, moveRecon, extraNotes, extraLost);
                }

                JsonElement? preSaveForFinalize = null;
                JsonElement? hotOverlayForFinalize = null;
                if (root.TryGetProperty("pre_save_review", out var preSave))
                {
                    preSaveForFinalize = preSave;
                    BridgeProjectReconcile.ApplyPreSaveReview(converted, preSave, extraNotes);
                }

                if (root.TryGetProperty("hot_mutable_overlay", out var hotOverlay))
                {
                    hotOverlayForFinalize = hotOverlay;
                    BridgeProjectReconcile.ApplyHotMutableOverlay(converted, hotOverlay, extraNotes);
                }

                BridgeProjectReconcile.FinalizeProjectedIdentity(
                    pk,
                    converted,
                    preSaveForFinalize,
                    hotOverlayForFinalize,
                    extraNotes);
                BridgeProjectReconcile.FinalizeProjectedNickname(
                    converted,
                    preSaveForFinalize,
                    hotOverlayForFinalize,
                    extraNotes);
                converted.RefreshChecksum();
                BridgeProjectReconcile.ValidateFinalProjectedIdentity(
                    pk,
                    converted,
                    preSaveForFinalize,
                    hotOverlayForFinalize,
                    extraNotes);

                var outBytes = converted.EncryptedBoxData;
                var outHash = Convert.ToHexString(SHA256.HashData(outBytes)).ToLowerInvariant();

                var warnings = new List<string>();
                if (!converted.ChecksumValid)
                {
                    warnings.Add("target_checksum_invalid");
                }

                var conversionLossy = usedManualPastProjection || IsLossyConversion(pk, converted, convResult);
                var reconcileLossy = extraLost.Count > 0;
                var lossy = conversionLossy || reconcileLossy;
                var lost = BuildLostCategories(conversionLossy, pk, converted, convResult, extraLost);
                var projected = BuildProjectedCategories();
                var notes = BuildLossNotes(lossy, pk, converted, convResult);
                foreach (var n in extraNotes)
                {
                    notes.Add(n);
                }

                var manifest = new BridgeProjectLossManifest(lossy, lost, projected, notes);

                var beacon = new BridgeProjectBeacon(
                    converted.TID16,
                    string.IsNullOrEmpty(converted.OriginalTrainerName) ? null : converted.OriginalTrainerName,
                    converted.Species);

                return new BridgeProjectResult
                {
                    Success = true,
                    Status = "ok",
                    TargetPid = converted.PID,
                    TargetFormatName = converted.GetType().Name,
                    TargetRawPayloadBase64 = Convert.ToBase64String(outBytes),
                    TargetRawHashSha256 = outHash,
                    Legality = new BridgeProjectLegality(converted.ChecksumValid, warnings),
                    LossManifest = manifest,
                    Beacon = beacon
                };
            }
            finally
            {
                EntityConverter.AllowIncompatibleConversion = previousCompat;
            }
        }
        catch (JsonException ex)
        {
            return new BridgeProjectResult
            {
                Success = false,
                Status = "error",
                Error = "invalid_json",
                Details = ex.Message
            };
        }
        catch (Exception ex)
        {
            return new BridgeProjectResult
            {
                Success = false,
                Status = "error",
                Error = "exception",
                Details = ex.Message
            };
        }
    }

    public static BridgeProjectResult EchoForTests(
        string targetFormatName,
        byte[] targetRawPayload,
        bool lossy)
    {
        var hash = SHA256.HashData(targetRawPayload);
        return new BridgeProjectResult
        {
            Success = true,
            TargetPid = null,
            TargetFormatName = targetFormatName,
            TargetRawPayloadBase64 = Convert.ToBase64String(targetRawPayload),
            TargetRawHashSha256 = Convert.ToHexString(hash).ToLowerInvariant(),
            Legality = new BridgeProjectLegality(true, []),
            LossManifest = new BridgeProjectLossManifest(
                lossy,
                lossy ? ["memories"] : [],
                ["species", "level", "moves"],
                lossy ? ["Target format cannot represent all source fields."] : []),
            Beacon = new BridgeProjectBeacon(null, null, 0),
            Status = "ok"
        };
    }

    private static bool ReadAllowLossy(JsonElement root)
    {
        if (!root.TryGetProperty("projection_policy", out var pol) || pol.ValueKind != JsonValueKind.Object)
        {
            return true;
        }

        if (pol.TryGetProperty("allow_lossy_projection", out var v) && v.ValueKind == JsonValueKind.False)
        {
            return false;
        }

        return true;
    }

    private static string? RequiredMissing(JsonElement root)
    {
        foreach (var property in new[]
                 {
                     "source_format_name",
                     "source_raw_payload_base64",
                     "source_raw_hash_sha256",
                     "target_game",
                     "target_format_name"
                 })
        {
            if (!root.TryGetProperty(property, out var value) ||
                value.ValueKind is JsonValueKind.Null or JsonValueKind.Undefined)
            {
                return property;
            }
        }

        return null;
    }

    private static Type? ResolvePkmType(string name)
    {
        var asm = typeof(PKM).Assembly;
        var key = name.Trim();
        if (string.IsNullOrEmpty(key))
        {
            return null;
        }

        foreach (var candidate in CandidateTypeNames(key))
        {
            var t = asm.GetType($"PKHeX.Core.{candidate}");
            if (t is not null && typeof(PKM).IsAssignableFrom(t))
            {
                return t;
            }
        }

        return null;
    }

    private static EntityContext ResolveEntityContext(string name)
    {
        return name.Trim().ToUpperInvariant() switch
        {
            "PK1" => EntityContext.Gen1,
            "PK2" => EntityContext.Gen2,
            "PK3" => EntityContext.Gen3,
            "PK4" => EntityContext.Gen4,
            "PK5" => EntityContext.Gen5,
            "PK6" => EntityContext.Gen6,
            "PK7" => EntityContext.Gen7,
            "PB7" => EntityContext.Gen7b,
            "PK8" => EntityContext.Gen8,
            "PA8" => EntityContext.Gen8a,
            "PB8" => EntityContext.Gen8b,
            "PK9" => EntityContext.Gen9,
            _ => EntityContext.None
        };
    }

    private static IEnumerable<string> CandidateTypeNames(string key)
    {
        yield return key;
        if (key.Length >= 1)
        {
            yield return char.ToUpperInvariant(key[0]) + key[1..];
        }

        if (key.Length >= 2 && char.ToUpperInvariant(key[0]) == 'P' && char.ToUpperInvariant(key[1]) == 'K')
        {
            var rest = key.Length > 2 ? key[2..] : "";
            yield return "PK" + rest.ToUpperInvariant();
        }

        yield return key.ToUpperInvariant();
    }

    private static bool IsLossyConversion(PKM src, PKM dest, EntityConverterResult convResult)
    {
        if (convResult is EntityConverterResult.SuccessIncompatibleReflection or EntityConverterResult.SuccessIncompatibleManual)
        {
            return true;
        }

        return dest.Format < src.Format;
    }

    private static List<string> BuildLostCategories(
        bool conversionLossy,
        PKM src,
        PKM dest,
        EntityConverterResult convResult,
        IReadOnlyList<string>? reconcileLost = null)
    {
        var list = new List<string>();
        if (reconcileLost is not null)
        {
            foreach (var e in reconcileLost)
            {
                if (!string.IsNullOrEmpty(e))
                    list.Add(e);
            }
        }

        if (!conversionLossy)
        {
            return list;
        }

        list.Add("downgrade_or_incompatible_route");
        if (dest.Format < src.Format)
        {
            list.Add("later_generation_fields");
        }

        if (convResult == EntityConverterResult.SuccessIncompatibleReflection)
        {
            list.Add("reflection_sanitized_fields");
        }

        _ = src;
        return list;
    }

    private static List<string> BuildProjectedCategories()
    {
        return ["species", "level", "exp", "moves", "ability", "held_item", "evs", "ivs", "pid", "ot"];
    }

    private static List<string> BuildLossNotes(bool lossy, PKM src, PKM dest, EntityConverterResult convResult)
    {
        var notes = new List<string>();
        if (lossy)
        {
            notes.Add($"Conversion may omit fields not representable in {dest.GetType().Name} (EntityConverterResult={convResult}).");
        }

        if (dest.Format < src.Format)
        {
            notes.Add($"Downgrade from generation {src.Format} to {dest.Format}: Resort retains full-fidelity history separately.");
        }

        return notes;
    }
}
