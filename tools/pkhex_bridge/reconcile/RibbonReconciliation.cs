using System.Collections.Generic;
using System.Globalization;
using System.Reflection;
using System.Text.Json;
using PKHeX.Core;

namespace PKHeXBridge;

internal static partial class BridgeProjectReconcile
{
/// <summary>
/// Optional catalog keys that are not identical to PKHeX property names. Values are tried in order
/// after the JSON key itself (e.g. warm JSON may use <c>RibbonChampion</c> for Hoenn Champion).
/// </summary>
private static readonly IReadOnlyDictionary<string, string[]> RibbonCatalogToPkHeXPropertyAliases =
    new Dictionary<string, string[]>(StringComparer.Ordinal)
    {
        ["RibbonChampion"] = new[] { "RibbonChampionG3" },
    };

    internal static int ApplyCanonicalRibbonCatalog(PKM pk, JsonElement ribbonCatalog, IList<string> notes)
    {
        var t = pk.GetType();
        var applied = 0;
        foreach (var prop in ribbonCatalog.EnumerateObject())
        {
            if (prop.Value.ValueKind == JsonValueKind.True)
            {
                if (TrySetRibbonBoolProperty(t, pk, prop.Name))
                {
                    applied++;
                    continue;
                }

                if (RibbonCatalogToPkHeXPropertyAliases.TryGetValue(prop.Name, out var aliases))
                {
                    foreach (var alt in aliases)
                    {
                        if (TrySetRibbonBoolProperty(t, pk, alt))
                        {
                            applied++;
                            break;
                        }
                    }
                }

                continue;
            }

            if (prop.Value.ValueKind == JsonValueKind.Number &&
                TryApplyRibbonIntegralMax(t, pk, prop.Name, prop.Value.GetInt32()))
            {
                applied++;
            }
        }

        if (applied > 0)
        {
            notes.Add(
                $"[ribbon_catalog] replayed {applied.ToString(CultureInfo.InvariantCulture)} canonical ribbon catalog field(s) onto {t.Name}.");
        }

        return applied;
    }

    private static bool TryApplyRibbonIntegralMax(Type t, PKM pk, string propertyName, int incoming)
    {
        var pi = t.GetProperty(propertyName, BindingFlags.Public | BindingFlags.Instance);
        if (pi?.CanWrite != true)
            return false;

        var pt = pi.PropertyType;
        if (pt != typeof(byte) && pt != typeof(sbyte) && pt != typeof(short) && pt != typeof(ushort) &&
            pt != typeof(int) && pt != typeof(uint))
            return false;

        try
        {
            var cur = 0;
            if (pi.CanRead)
            {
                var raw = pi.GetValue(pk);
                if (raw is not null)
                    cur = Convert.ToInt32(raw, CultureInfo.InvariantCulture);
            }

            var v = Math.Max(cur, incoming);
            object converted = pt switch
            {
                _ when pt == typeof(byte) => (byte)Math.Clamp(v, byte.MinValue, byte.MaxValue),
                _ when pt == typeof(sbyte) => (sbyte)Math.Clamp(v, sbyte.MinValue, sbyte.MaxValue),
                _ when pt == typeof(short) => (short)Math.Clamp(v, short.MinValue, short.MaxValue),
                _ when pt == typeof(ushort) => (ushort)Math.Clamp(v, ushort.MinValue, ushort.MaxValue),
                _ when pt == typeof(uint) => (uint)Math.Clamp((long)v, uint.MinValue, uint.MaxValue),
                _ => Math.Clamp(v, int.MinValue, int.MaxValue)
            };
            pi.SetValue(pk, converted);
            return true;
        }
        catch
        {
            return false;
        }
    }

    private static bool TrySetRibbonBoolProperty(Type t, PKM pk, string propertyName)
    {
        var pi = t.GetProperty(propertyName, BindingFlags.Public | BindingFlags.Instance);
        if (pi?.PropertyType != typeof(bool) || !pi.CanWrite)
            return false;

        try
        {
            pi.SetValue(pk, true);
            return true;
        }
        catch
        {
            // Ribbon accessors may throw on illegal combinations for this format; skip.
            return false;
        }
    }
}
