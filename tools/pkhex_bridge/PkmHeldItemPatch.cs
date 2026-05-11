using System.Security.Cryptography;
using System.Text.Json;
using PKHeX.Core;

namespace PKHeXBridge;

public sealed class PkmHeldItemPatchResult
{
    public bool Success { get; init; }
    public string? Error { get; init; }
    public string? Details { get; init; }
    public string? RawPayloadBase64 { get; init; }
    public string? RawHashSha256 { get; init; }
}

/// <summary>
/// Mutates import-grade PC encrypted PKM bytes by changing <see cref="PKM.HeldItem"/> and re-exporting
/// encrypted stored-format PKM bytes (<see cref="PkmEncryptedExport.GetStored"/>) for write-back consistency.
/// </summary>
public static class PkmHeldItemPatch
{
    public static PkmHeldItemPatchResult ApplyFromJsonFile(string inputJsonPath)
    {
        if (!File.Exists(inputJsonPath))
        {
            return new PkmHeldItemPatchResult
            {
                Success = false,
                Error = "missing_input",
                Details = inputJsonPath
            };
        }

        JsonDocument doc;
        try
        {
            doc = JsonDocument.Parse(File.ReadAllText(inputJsonPath));
        }
        catch (Exception ex)
        {
            return new PkmHeldItemPatchResult
            {
                Success = false,
                Error = "invalid_input_json",
                Details = ex.Message
            };
        }

        using (doc)
        {
            var root = doc.RootElement;
            if (!root.TryGetProperty("raw_payload_base64", out var b64El) || b64El.ValueKind != JsonValueKind.String)
            {
                return new PkmHeldItemPatchResult { Success = false, Error = "missing_raw_payload_base64" };
            }

            if (!root.TryGetProperty("held_item_id", out var itemEl) ||
                itemEl.ValueKind != JsonValueKind.Number)
            {
                return new PkmHeldItemPatchResult { Success = false, Error = "missing_held_item_id" };
            }

            var b64 = b64El.GetString();
            if (string.IsNullOrEmpty(b64))
            {
                return new PkmHeldItemPatchResult { Success = false, Error = "empty_raw_payload_base64" };
            }

            var heldItemId = itemEl.GetInt32();
            return ApplyFromPayload(b64, heldItemId);
        }
    }

    public static PkmHeldItemPatchResult ApplyFromPayload(string rawPayloadBase64, int heldItemId)
    {
        byte[] raw;
        try
        {
            raw = Convert.FromBase64String(rawPayloadBase64);
        }
        catch (Exception ex)
        {
            return new PkmHeldItemPatchResult
            {
                Success = false,
                Error = "bad_base64",
                Details = ex.Message
            };
        }

        PKM? pkm;
        try
        {
            pkm = EntityFormat.GetFromBytes(raw);
        }
        catch (Exception ex)
        {
            return new PkmHeldItemPatchResult
            {
                Success = false,
                Error = "pkm_decode_failed",
                Details = ex.Message
            };
        }

        if (pkm is null)
        {
            return new PkmHeldItemPatchResult { Success = false, Error = "pkm_decode_failed", Details = "null" };
        }

        try
        {
            var clamped = Math.Clamp(heldItemId, 0, ushort.MaxValue);
            pkm.HeldItem = (ushort)clamped;
        }
        catch (Exception ex)
        {
            return new PkmHeldItemPatchResult
            {
                Success = false,
                Error = "held_item_set_failed",
                Details = ex.Message
            };
        }

        byte[] newRaw;
        try
        {
            newRaw = PkmEncryptedExport.GetStored(pkm);
        }
        catch (Exception ex)
        {
            return new PkmHeldItemPatchResult
            {
                Success = false,
                Error = "encrypted_export_failed",
                Details = ex.Message
            };
        }

        if (newRaw.Length == 0)
        {
            return new PkmHeldItemPatchResult { Success = false, Error = "empty_encrypted_export" };
        }

        var hash = Convert.ToHexString(SHA256.HashData(newRaw)).ToLowerInvariant();
        var outB64 = Convert.ToBase64String(newRaw);

        return new PkmHeldItemPatchResult
        {
            Success = true,
            RawPayloadBase64 = outB64,
            RawHashSha256 = hash
        };
    }
}
