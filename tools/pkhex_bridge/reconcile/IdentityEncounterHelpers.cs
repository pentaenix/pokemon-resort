using System.Globalization;
using System.Reflection;
using PKHeX.Core;

namespace PKHeXBridge;

internal static partial class BridgeProjectReconcile
{
    private static T ReadEncounterProperty<T>(object encounter, string property)
    {
        var value = encounter.GetType().GetProperty(property, BindingFlags.Public | BindingFlags.Instance)?.GetValue(encounter);
        if (value is T typed)
            return typed;
        if (value is IConvertible)
            return (T)Convert.ChangeType(value, typeof(T), CultureInfo.InvariantCulture);
        throw new InvalidOperationException($"Encounter missing {property}.");
    }

    private static T? TryReadEncounterProperty<T>(object encounter, string property) where T : struct
    {
        try
        {
            return ReadEncounterProperty<T>(encounter, property);
        }
        catch
        {
            return null;
        }
    }

    private static PersonalInfo3 GetPersonalInfo3(GameVersion version, int species, byte form)
    {
        var table = version switch
        {
            GameVersion.E => PersonalTable.E,
            GameVersion.R or GameVersion.S => PersonalTable.RS,
            GameVersion.LG => PersonalTable.LG,
            _ => PersonalTable.FR,
        };
        return (PersonalInfo3)table.GetFormEntry((ushort)species, form);
    }

    private static void ApplyGeneratedPidIv(
        PKM pk,
        PKM generated,
        Nature desiredNature,
        byte desiredGender,
        byte desiredForm)
    {
        pk.PID = generated.PID;
        pk.IV_HP = generated.IV_HP;
        pk.IV_ATK = generated.IV_ATK;
        pk.IV_DEF = generated.IV_DEF;
        pk.IV_SPE = generated.IV_SPE;
        pk.IV_SPA = generated.IV_SPA;
        pk.IV_SPD = generated.IV_SPD;
        if (pk.Format == 3)
        {
            pk.Version = generated.Version;
            pk.MetLocation = generated.MetLocation;
            pk.MetLevel = generated.MetLevel;
            pk.Ball = generated.Ball;
            TrySetBoolProperty(pk, "FatefulEncounter", generated.FatefulEncounter);
            pk.Ability = generated.Ability;
            pk.AbilityNumber = generated.AbilityNumber;
            pk.Nature = generated.Nature;
            pk.Gender = generated.Gender;
            pk.Form = generated.Form;
            pk.RefreshChecksum();
            return;
        }
        pk.Nature = desiredNature;
        pk.Gender = desiredGender;
        pk.Form = desiredForm;
        if (pk.Format == 6)
            pk.EncryptionConstant = pk.PID;
        pk.RefreshChecksum();
    }

    private static bool HasPidCorrelationIssue(PKM pk)
    {
        try
        {
            var report = new LegalityAnalysis(pk).Report();
            return report.Contains(
                "PID+ correlation does not match what was expected for the Encounter's type.",
                StringComparison.Ordinal);
        }
        catch
        {
            return false;
        }
    }

    private static EntityContext EncounterGenerationToContext(byte generation, PKM pk) => generation switch
    {
        1 => EntityContext.Gen1,
        2 => EntityContext.Gen2,
        3 => EntityContext.Gen3,
        4 => EntityContext.Gen4,
        5 => EntityContext.Gen5,
        6 => EntityContext.Gen6,
        7 => EntityContext.Gen7,
        8 => EntityContext.Gen8,
        9 => EntityContext.Gen9,
        _ => pk.Context,
    };

    private static void SyncAbilityFromPid(PKM pk)
    {
        // PK6 bank transfers use TrySetGbOriginEncounterAbility (FR personal table). RefreshAbility here
        // overwrites that and triggers "Ability mismatch for encounter." on Gen VI.
        if (pk.Format is not (3 or 4 or 5))
            return;
        try
        {
            pk.RefreshAbility((int)pk.PIDAbility);
        }
        catch
        {
        }
    }

    private static byte GenderFromPid(PKM pk, uint pid)
    {
        var ratio = pk.PersonalInfo.Gender;
        if (ratio == 255)
            return 2;
        if (ratio == 254)
            return 1;
        if (ratio == 0)
            return 0;
        if (ratio is > 0 and < 254)
            return (byte)(((pid & 0xff) < ratio) ? 1 : 0);

        return pk.Gender;
    }
}
