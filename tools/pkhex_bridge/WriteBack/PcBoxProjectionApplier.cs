using System.Security.Cryptography;
using System.Text.Json;
using System.Reflection;
using PKHeX.Core;

namespace PKHeXBridge.WriteBack;

/// <summary>
/// Applies <c>pc_boxes</c> projection (schema 2): validated payloads only, locked slots rejected.
/// </summary>
public static class PcBoxProjectionApplier
{
    public static void Apply(SaveFile sav, JsonElement pcBoxes)
    {
        if (!sav.HasBox)
            throw new InvalidOperationException("save_has_no_boxes");

        var expectedBoxes = sav.BoxCount;
        var slotCount = sav.BoxSlotCount;

        if (pcBoxes.GetArrayLength() != expectedBoxes)
            throw new InvalidOperationException($"pc_boxes_len={pcBoxes.GetArrayLength()} expected_BoxCount={expectedBoxes}");

        var bi = 0;
        foreach (var boxEl in pcBoxes.EnumerateArray())
        {
            if (bi >= expectedBoxes)
                break;

            if (boxEl.ValueKind != JsonValueKind.Object)
                throw new InvalidOperationException($"pc_boxes[{bi}] must be object");

            if (!boxEl.TryGetProperty("slots", out var slots) || slots.ValueKind != JsonValueKind.Array)
                throw new InvalidOperationException($"pc_boxes[{bi}].slots missing");

            if (slots.GetArrayLength() != slotCount)
                throw new InvalidOperationException($"pc_boxes[{bi}].slots_len={slots.GetArrayLength()} expected={slotCount}");

            var si = 0;
            foreach (var slotEl in slots.EnumerateArray())
            {
                if (si >= slotCount)
                    break;

                if (SafeBool(() => sav.IsBoxSlotLocked(bi, si)))
                    throw new InvalidOperationException($"locked slot box={bi} slot={si}");

                if (SafeBool(() => sav.IsBoxSlotOverwriteProtected(bi, si)))
                    throw new InvalidOperationException($"overwrite_protected slot box={bi} slot={si}");

                if (slotEl.ValueKind == JsonValueKind.Null)
                {
                    SetSlotBlank(sav, bi, si);
                }
                else if (slotEl.ValueKind == JsonValueKind.Object)
                {
                    var b64 = RequireString(slotEl, "raw_payload_base64");
                    var hexHash = RequireString(slotEl, "raw_hash_sha256");
                    var raw = Convert.FromBase64String(b64);
                    var actualHash = Convert.ToHexString(SHA256.HashData(raw)).ToLowerInvariant();
                    if (!string.Equals(actualHash, hexHash, StringComparison.OrdinalIgnoreCase))
                        throw new InvalidOperationException($"payload_hash_mismatch box={bi} slot={si}");

                    var pkm = DecodeForTargetSave(sav, raw);
                    ApplyGen12DvFromProjectedPayload(pkm, raw, sav.Context);

                    sav.SetBoxSlotAtIndex(pkm, bi, si);
                }
                else
                {
                    throw new InvalidOperationException($"bad_slot_json box={bi} slot={si}");
                }

                si++;
            }

            bi++;
        }
    }

    private static PKM DecodeForTargetSave(SaveFile sav, byte[] raw)
    {
        try
        {
            return sav.GetDecryptedPKM(raw);
        }
        catch
        {
            var pkm = EntityFormat.GetFromBytes(raw, sav.Context);
            if (pkm is null)
                throw new InvalidOperationException("pkm_decode_failed");
            return pkm;
        }
    }

    private static void ApplyGen12DvFromProjectedPayload(PKM pkm, byte[] raw, EntityContext context)
    {
        if (pkm.Format > 2)
            return;

        var dv16 = ReadGen12Dv16FromProjectedPayload(raw, pkm, context);
        if (dv16 is null or 0)
            return;

        var prop = pkm.GetType().GetProperty("DV16", BindingFlags.Instance | BindingFlags.Public);
        if (prop is null || !prop.CanWrite)
            return;

        prop.SetValue(pkm, Convert.ChangeType(dv16.Value, prop.PropertyType));
    }

    private static ushort? ReadGen12Dv16FromProjectedPayload(byte[] raw, PKM pkm, EntityContext context)
    {
        var offset = pkm switch
        {
            PK1 => 0x1B,
            PK2 => 0x15,
            _ => pkm.GetType().Name.ToLowerInvariant() switch
            {
                "pk1" => 0x1B,
                "pk2" => 0x15,
                _ => -1,
            },
        };

        if (offset < 0 || raw.Length < offset + 2)
            return null;

        // Prefer the projected bytes because native intentionally patches this exact payload before write-back.
        var projected = (ushort)((raw[offset] << 8) | raw[offset + 1]);
        if (projected != 0)
            return projected;

        try
        {
            var entity = EntityFormat.GetFromBytes(raw, context);
            if (entity is not null && entity.Format <= 2)
            {
                var prop = entity.GetType().GetProperty("DV16", BindingFlags.Instance | BindingFlags.Public);
                if (prop?.GetValue(entity) is { } value)
                    return Convert.ToUInt16(value);
            }
        }
        catch
        {
            // Decode fallback is best-effort; zero/no DV simply means no explicit Gen 1/2 DV repair here.
        }

        return null;
    }

    private static void SetSlotBlank(SaveFile sav, int box, int slot)
    {
        var blank = sav.BlankPKM;
        var pk = blank.Clone();
        sav.SetBoxSlotAtIndex(pk, box, slot);
    }

    private static string RequireString(JsonElement obj, string name)
    {
        if (!obj.TryGetProperty(name, out var el) || el.ValueKind != JsonValueKind.String)
            throw new InvalidOperationException($"missing_{name}");
        var s = el.GetString();
        return string.IsNullOrEmpty(s) ? throw new InvalidOperationException($"empty_{name}") : s;
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
}
