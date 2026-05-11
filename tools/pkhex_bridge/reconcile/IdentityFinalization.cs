using System.Collections.Generic;
using System.Globalization;
using System.Reflection;
using System.Text.Json;
using PKHeX.Core;

namespace PKHeXBridge;

internal static partial class BridgeProjectReconcile
{
    internal static void FinalizeProjectedIdentity(
        PKM source,
        PKM pk,
        JsonElement? preSaveReview,
        JsonElement? hotMutableOverlay,
        IList<string> notes)
    {
        if (!ShouldFinalizePidIdentity(source, pk, preSaveReview))
            return;

        var desiredNature = ReadNature(preSaveReview) ?? pk.Nature;
        var desiredShiny = ReadBool(hotMutableOverlay, "shiny") ?? source.IsShiny;
        var desiredGender = ReadByte(hotMutableOverlay, "gender") ?? pk.Gender;
        var desiredForm = ReadByte(hotMutableOverlay, "form_id") ?? pk.Form;
        var desiredAbilityIndex = ReadAbilityIndex(hotMutableOverlay);

        pk.Form = desiredForm;
        pk.Gender = desiredGender;
        pk.Nature = desiredNature;

        if (desiredAbilityIndex is { } abilityIndex)
            CommonEdits.SetAbilityIndex(pk, abilityIndex);

        var seed = unchecked((int)(pk.PID ^ ((uint)pk.Species << 16) ^ ((uint)desiredNature << 8) ^ desiredForm));
        var rng = new Random(seed);
        var abilitySeed = desiredAbilityIndex is { } ai ? (uint)(Math.Clamp(ai, 0, 1) * 0x10001) : pk.PID;
        const int maxAttempts = 1_000_000;
        var solved = false;

        for (var attempt = 0; attempt < maxAttempts; attempt++)
        {
            pk.PID = EntityPID.GetRandomPID(
                rng,
                pk.Species,
                desiredGender,
                pk.Version,
                desiredNature,
                desiredForm,
                abilitySeed);
            pk.Nature = desiredNature;

            if (MatchesFinalIdentity(pk, desiredNature, desiredShiny, desiredGender, desiredForm, desiredAbilityIndex))
            {
                solved = true;
                break;
            }
        }

        if (!solved)
        {
            throw new InvalidOperationException(
                $"finalize_identity could not solve PID-derived constraints for format={pk.Format} species={pk.Species} " +
                $"form={desiredForm} nature={desiredNature} gender={desiredGender} shiny={desiredShiny} " +
                $"ability_index={(desiredAbilityIndex?.ToString(CultureInfo.InvariantCulture) ?? "unspecified")}.");
        }

        var finalPid = pk.PID;
        CommonEdits.SetAbilityIndex(pk, pk.PIDAbility);
        TrySetGbOriginEncounterAbility(pk);
        pk.PID = finalPid;
        pk.Nature = desiredNature;
        pk.Gender = desiredGender;
        pk.Form = desiredForm;
        if (ShouldRepairMethod1PidIvCorrelation(source, pk, preSaveReview))
        {
                if (HasPidCorrelationIssue(pk))
                {
                var beforePid = pk.PID;
                var beforeAbility = pk.Ability;
                var beforeAbilityNumber = pk.AbilityNumber;
                var beforeIvs = new[]
                {
                    pk.IV_HP,
                    pk.IV_ATK,
                    pk.IV_DEF,
                    pk.IV_SPE,
                    pk.IV_SPA,
                    pk.IV_SPD,
                };

                if (!TrySetMethod1TransferPidIv(pk, desiredNature, desiredShiny, desiredGender, desiredForm, desiredAbilityIndex, notes) || HasPidCorrelationIssue(pk))
                {
                    pk.PID = beforePid;
                    pk.Nature = desiredNature;
                    pk.Gender = desiredGender;
                    pk.Form = desiredForm;
                    pk.IV_HP = beforeIvs[0];
                    pk.IV_ATK = beforeIvs[1];
                    pk.IV_DEF = beforeIvs[2];
                    pk.IV_SPE = beforeIvs[3];
                    pk.IV_SPA = beforeIvs[4];
                    pk.IV_SPD = beforeIvs[5];
                    pk.Ability = beforeAbility;
                    pk.AbilityNumber = beforeAbilityNumber;
                    TrySetAbilityIndex(pk, pk.PIDAbility);
                    TrySetGbOriginEncounterAbility(pk);
                }
            }
        }
        if (pk.Format == 6 && ShouldUseGbOriginEvCap(source, preSaveReview))
            pk.EncryptionConstant = pk.PID;
        AddFinalIdentityNote(pk, notes, "finalize_identity");
    }

    internal static void ValidateFinalProjectedIdentity(
        PKM source,
        PKM pk,
        JsonElement? preSaveReview,
        JsonElement? hotMutableOverlay,
        IList<string> notes)
    {
        if (!ShouldFinalizePidIdentity(source, pk, preSaveReview))
            return;

        var desiredNature = ReadNature(preSaveReview) ?? pk.Nature;
        var desiredShiny = ReadBool(hotMutableOverlay, "shiny") ?? source.IsShiny;
        var desiredGender = ReadByte(hotMutableOverlay, "gender") ?? pk.Gender;
        var desiredForm = ReadByte(hotMutableOverlay, "form_id") ?? pk.Form;
        var desiredAbilityIndex = ReadAbilityIndex(hotMutableOverlay);
        if (!MatchesFinalIdentity(pk, desiredNature, desiredShiny, desiredGender, desiredForm, desiredAbilityIndex))
        {
            throw new InvalidOperationException(
                $"finalize_identity validation failed for format={pk.Format} species={pk.Species} " +
                $"form={pk.Form}/{desiredForm} nature={pk.Nature}/{desiredNature} gender={pk.Gender}/{desiredGender} " +
                $"shiny={pk.IsShiny}/{desiredShiny} pid={pk.PID} pid_mod_25={pk.PID % 25}.");
        }

        AddFinalIdentityNote(pk, notes, "finalize_identity_validate");
    }

    private static bool ShouldFinalizePidIdentity(PKM source, PKM pk, JsonElement? preSaveReview)
    {
        if (pk.Format is 3 or 4)
            return true;
        return pk.Format is 5 or 6 && ShouldUseGbOriginEvCap(source, preSaveReview);
    }

    private static bool ShouldRepairMethod1PidIvCorrelation(PKM source, PKM pk, JsonElement? preSaveReview)
    {
        if (pk.Format == 4 && pk.MetLocation == 55)
            return true;
        return pk.Format is 5 or 6 && ShouldUseGbOriginEvCap(source, preSaveReview);
    }

    private static bool MatchesFinalIdentity(
        PKM pk,
        Nature desiredNature,
        bool desiredShiny,
        byte desiredGender,
        byte desiredForm,
        int? desiredAbilityIndex)
    {
        if (pk.Nature != desiredNature)
            return false;
        if (pk.PID % 25 != (uint)desiredNature)
            return false;
        if (pk.IsShiny != desiredShiny)
            return false;
        if (pk.Gender != desiredGender)
            return false;
        if (pk.Form != desiredForm)
            return false;
        if (desiredAbilityIndex is { } abilityIndex && Math.Clamp(abilityIndex, 0, 1) != pk.PIDAbility)
            return false;
        return true;
    }

    private static void AddFinalIdentityNote(PKM pk, IList<string> notes, string tag)
    {
        notes.Add(
            $"[{tag}] format={pk.Format} species={pk.Species} form={pk.Form} nature={pk.Nature} " +
            $"gender={pk.Gender} shiny={pk.IsShiny.ToString().ToLowerInvariant()} pid={pk.PID} " +
            $"pid_mod_25={pk.PID % 25} ability_index={pk.PIDAbility}.");
    }

    private static bool TrySetMethod1TransferPidIv(
        PKM pk,
        Nature desiredNature,
        bool desiredShiny,
        byte desiredGender,
        byte desiredForm,
        int? desiredAbilityIndex,
        IList<string> notes)
    {
        if (TrySetGeneratedGen3EncounterPidIv(pk, desiredNature, desiredShiny, desiredGender, desiredForm, desiredAbilityIndex, notes))
            return true;
        if (TrySetEncounterSlot3TransferPidIv(pk, desiredNature, desiredShiny, desiredGender, desiredForm, desiredAbilityIndex, notes))
            return true;

        var seed = unchecked((uint)(pk.PID ^ ((uint)pk.Species << 16) ^ ((uint)pk.TID16 << 8) ^ pk.SID16));
        const int MaxAttempts = 4_000_000;
        var pidTypes = new[] { PIDType.Method_1, PIDType.Method_2, PIDType.Method_4 };

        for (var attempt = 0; attempt < MaxAttempts; attempt++, seed += 0x9E3779B9u)
        {
            foreach (var pidType in pidTypes)
            {
                PIDGenerator.SetValuesFromSeed(pk, pidType, seed);
                pk.Gender = GenderFromPid(pk, pk.PID);
                pk.Nature = desiredNature;
                pk.Form = desiredForm;
                if (pk.Format == 6)
                    pk.EncryptionConstant = pk.PID;
                if (!MatchesFinalIdentity(pk, desiredNature, desiredShiny, desiredGender, desiredForm, desiredAbilityIndex))
                    continue;

                CommonEdits.SetAbilityIndex(pk, pk.PIDAbility);
                TrySetGbOriginEncounterAbility(pk);
                pk.Nature = desiredNature;
                pk.Gender = desiredGender;
                pk.Form = desiredForm;
                if (pk.Format == 6)
                    pk.EncryptionConstant = pk.PID;
                if (HasPidCorrelationIssue(pk))
                    continue;

                notes.Add($"[transfer_normalize] repaired transfer PID/IV pair with {pidType} LCRNG data.");
                return true;
            }
        }

        notes.Add("[transfer_normalize] could not find a Method 1/2/4 transfer PID/IV pair within search budget.");
        return false;
    }

    private static bool TrySetGeneratedGen3EncounterPidIv(
        PKM pk,
        Nature desiredNature,
        bool desiredShiny,
        byte desiredGender,
        byte desiredForm,
        int? desiredAbilityIndex,
        IList<string> notes)
    {
        var match = new LegalityAnalysis(pk).EncounterMatch;
        var trainer = new SimpleTrainerInfo(pk.Version)
        {
            OT = pk.OriginalTrainerName,
            TID16 = pk.TID16,
            SID16 = pk.SID16,
            Gender = pk.OriginalTrainerGender,
            Language = pk.Language,
            Generation = 3,
            Context = EntityContext.Gen3,
        };
        var criteria = new EncounterCriteria
        {
            Nature = desiredNature,
            Gender = (Gender)Math.Clamp((int)desiredGender, 0, 2),
            Ability = desiredAbilityIndex is null
                ? AbilityPermission.Any12
                : desiredAbilityIndex == 0 ? AbilityPermission.OnlyFirst : AbilityPermission.OnlySecond,
            Shiny = desiredShiny ? Shiny.Always : Shiny.Never,
            Form = (sbyte)desiredForm,
        };

        PKM? generated = match switch
        {
            EncounterGift3 gift => gift.ConvertToPKM(trainer, criteria),
            EncounterEgg egg => egg.ConvertToPKM(trainer, criteria),
            _ => null,
        };
        if (generated is null)
            return false;

        ApplyGeneratedPidIv(pk, generated, desiredNature, desiredGender, desiredForm);
        CommonEdits.SetAbilityIndex(pk, pk.PIDAbility);
        TrySetGbOriginEncounterAbility(pk);
        if (!MatchesFinalIdentity(pk, desiredNature, desiredShiny, desiredGender, desiredForm, desiredAbilityIndex) ||
            HasPidCorrelationIssue(pk))
        {
            return false;
        }

        notes.Add($"[transfer_normalize] repaired transfer PID/IV pair from PKHeX {match.GetType().Name} generator.");
        return true;
    }

    private static bool TrySetEncounterSlot3TransferPidIv(
        PKM pk,
        Nature desiredNature,
        bool desiredShiny,
        byte desiredGender,
        byte desiredForm,
        int? desiredAbilityIndex,
        IList<string> notes)
    {
        var match = new LegalityAnalysis(pk).EncounterMatch;
        if (match is not EncounterSlot3 slot)
            return false;

        try
        {
            var temp = (PK3)EntityBlank.GetBlank(typeof(PK3));
            temp.Species = pk.Species;
            temp.Form = pk.Form;
            temp.Version = slot.Version;
            temp.Language = pk.Language;
            temp.TID16 = pk.TID16;
            temp.SID16 = pk.SID16;
            temp.EXP = pk.EXP;
            temp.MetLocation = slot.Location;
            temp.MetLevel = Math.Max(slot.LevelMin, pk.MetLevel);
            temp.Ball = (byte)Ball.Poke;

            var criteria = new EncounterCriteria
            {
                Nature = desiredNature,
                Gender = (Gender)Math.Clamp((int)desiredGender, 0, 2),
                Ability = desiredAbilityIndex is null
                    ? AbilityPermission.Any12
                    : desiredAbilityIndex == 0 ? AbilityPermission.OnlyFirst : AbilityPermission.OnlySecond,
                Shiny = desiredShiny ? Shiny.Always : Shiny.Never,
                Form = (sbyte)desiredForm,
                LevelMin = slot.LevelMin,
                LevelMax = slot.LevelMax,
            };

            var info = (PersonalInfo3)PersonalTable.FR.GetFormEntry(pk.Species, pk.Form);
            var seed = unchecked(pk.PID ^ ((uint)slot.Location << 16) ^ ((uint)slot.SlotNumber << 8) ^ (uint)desiredNature);
            GenerateMethodH.SetRandom(slot, temp, info, criteria, seed);

            ApplyGeneratedPidIv(pk, temp, desiredNature, desiredGender, desiredForm);
            CommonEdits.SetAbilityIndex(pk, pk.PIDAbility);
            TrySetGbOriginEncounterAbility(pk);

            if (!MatchesFinalIdentity(pk, desiredNature, desiredShiny, desiredGender, desiredForm, desiredAbilityIndex) ||
                HasPidCorrelationIssue(pk))
            {
                return false;
            }

            notes.Add("[transfer_normalize] repaired transfer PID/IV pair with PKHeX Method H slot data.");
            return true;
        }
        catch
        {
            return false;
        }
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
        pk.Nature = desiredNature;
        pk.Gender = desiredGender;
        pk.Form = desiredForm;
        if (pk.Format == 6)
            pk.EncryptionConstant = pk.PID;
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
