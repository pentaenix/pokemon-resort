using System.Collections.Generic;
using System.Globalization;
using System.Reflection;
using System.Text.Json;
using PKHeX.Core;

namespace PKHeXBridge;

internal static partial class BridgeProjectReconcile
{
    private static void ApplyTrainerName(PKM pk, string otName)
    {
        if (string.IsNullOrWhiteSpace(otName))
            return;
        otName = NormalizeTrainerName(pk, otName);
        if (pk.Format == 3)
        {
            try
            {
                StringConverter3.SetString(
                    pk.OriginalTrainerTrash,
                    otName,
                    pk.MaxStringLengthTrainer,
                    pk.Language == (int)LanguageID.Japanese,
                    StringConverterOption.ClearFF);
                return;
            }
            catch
            {
            }
        }
        pk.OriginalTrainerName = otName;
    }

    private static string NormalizeTrainerName(PKM pk, string otName)
    {
        if (pk.Format >= 3 && otName.Length < 2)
            return "GB";

        var maxLength = pk.MaxStringLengthTrainer;
        if (maxLength <= 0 || otName.Length <= maxLength)
            return otName;

        return otName[..maxLength];
    }

    private static byte ClampByte(int v) => (byte)Math.Clamp(v, 0, 255);

    private static byte BallForTargetFormat(PKM pk, int requested)
    {
        const byte pokeBall = 4;
        if (requested <= 0)
            return pokeBall;

        var max = pk.Format switch
        {
            <= 2 => 0,
            3 => 12,
            4 => 16,
            5 => 16,
            _ => 255
        };
        if (max <= 0)
            return 0;
        return requested <= max ? ClampByte(requested) : pokeBall;
    }

    private static void ApplyNature(PKM pk, Nature nature)
    {
        CommonEdits.SetNature(pk, nature);
        if (pk.Format is >= 5 and < 8)
            pk.Nature = nature;
    }

    private static Nature? ReadNature(JsonElement? obj)
    {
        if (obj is not { ValueKind: JsonValueKind.Object } value)
            return null;
        if (!value.TryGetProperty("nature", out var natureEl) || natureEl.ValueKind != JsonValueKind.String)
            return null;
        return Enum.TryParse<Nature>(natureEl.GetString(), ignoreCase: true, out var nature) ? nature : null;
    }

    private static bool? ReadBool(JsonElement? obj, string property)
    {
        if (obj is not { ValueKind: JsonValueKind.Object } value)
            return null;
        if (!value.TryGetProperty(property, out var el))
            return null;
        return el.ValueKind switch
        {
            JsonValueKind.True => true,
            JsonValueKind.False => false,
            _ => null
        };
    }

    private static byte? ReadByte(JsonElement? obj, string property)
    {
        if (obj is not { ValueKind: JsonValueKind.Object } value)
            return null;
        if (!value.TryGetProperty(property, out var el) || el.ValueKind != JsonValueKind.Number)
            return null;
        return ClampByte(el.GetInt32());
    }

    private static string? ReadString(JsonElement? obj, string property)
    {
        if (obj is not { ValueKind: JsonValueKind.Object } value)
            return null;
        if (!value.TryGetProperty(property, out var el) || el.ValueKind != JsonValueKind.String)
            return null;
        return el.GetString();
    }

    private static int? ReadAbilityIndex(JsonElement? obj)
    {
        if (obj is not { ValueKind: JsonValueKind.Object } value)
            return null;

        foreach (var property in new[] { "ability_index", "ability_slot", "pid_ability" })
        {
            if (!value.TryGetProperty(property, out var el) || el.ValueKind != JsonValueKind.Number)
                continue;
            return Math.Clamp(el.GetInt32(), 0, 1);
        }

        if (value.TryGetProperty("ability_number", out var abilityNumber) && abilityNumber.ValueKind == JsonValueKind.Number)
            return Math.Clamp(abilityNumber.GetInt32() - 1, 0, 1);

        return null;
    }

    private static string GetSpeciesName(int species)
    {
        try
        {
            var table = GameInfo.Strings.GetType().GetProperty("Species")?.GetValue(GameInfo.Strings) as IReadOnlyList<string>;
            if (table is not null && species > 0 && species < table.Count && !string.IsNullOrWhiteSpace(table[species]))
                return table[species]!;
        }
        catch
        {
        }

        return species > 0 ? ((Species)species).ToString() : "";
    }

    private static void TrySetIntProperty(object instance, string name, int value)
    {
        try
        {
            var prop = instance.GetType().GetProperty(name);
            if (prop is null || !prop.CanWrite)
                return;
            var pt = prop.PropertyType;
            object boxed = pt.Name switch
            {
                nameof(Byte) => (byte)value,
                nameof(SByte) => (sbyte)value,
                nameof(Int16) => (short)value,
                nameof(UInt16) => (ushort)value,
                nameof(Int32) => value,
                nameof(UInt32) => (uint)value,
                _ => null!
            };
            if (boxed is null)
                return;
            prop.SetValue(instance, boxed);
        }
        catch
        {
        }
    }

    private static void TrySetBoolProperty(object instance, string name, bool value)
    {
        try
        {
            var prop = instance.GetType().GetProperty(name);
            if (prop is null || !prop.CanWrite || prop.PropertyType != typeof(bool))
                return;
            prop.SetValue(instance, value);
        }
        catch
        {
        }
    }
}
