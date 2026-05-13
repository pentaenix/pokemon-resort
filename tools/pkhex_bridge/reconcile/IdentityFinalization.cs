using System.Collections.Generic;
using System.Globalization;
using System.Reflection;
using System.Text.Json;
using PKHeX.Core;

namespace PKHeXBridge;

internal static partial class BridgeProjectReconcile
{
    private enum IdentityProjectionPolicy
    {
        None,
        Gen3Encounter,
        Gen4PalPark,
        Gen56GbTransfer,
        Gen56Gen3Transfer,
    }

    internal static void FinalizeProjectedIdentity(
        PKM source,
        PKM pk,
        JsonElement? preSaveReview,
        JsonElement? hotMutableOverlay,
        IList<string> notes)
    {
        var policy = GetIdentityProjectionPolicy(source, pk, preSaveReview);
        if (policy == IdentityProjectionPolicy.None)
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
        SyncAbilityFromPid(pk);
        pk.PID = finalPid;
        pk.Nature = desiredNature;
        pk.Gender = desiredGender;
        pk.Form = desiredForm;
        if (ShouldRepairPidIvCorrelation(policy))
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
                    SyncAbilityFromPid(pk);
                }
            }
        }
        if (pk.Format == 6 && policy is IdentityProjectionPolicy.Gen56GbTransfer or IdentityProjectionPolicy.Gen56Gen3Transfer)
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
        if (GetIdentityProjectionPolicy(source, pk, preSaveReview) == IdentityProjectionPolicy.None)
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

    private static IdentityProjectionPolicy GetIdentityProjectionPolicy(PKM source, PKM pk, JsonElement? preSaveReview)
    {
        if (pk.Format is 3 or 4)
        {
            if (pk.Format == 3)
                return IdentityProjectionPolicy.Gen3Encounter;
            return pk.MetLocation == 55
                ? IdentityProjectionPolicy.Gen4PalPark
                : IdentityProjectionPolicy.Gen3Encounter;
        }

        if (pk.Format is 5 or 6 && ShouldUseGbOriginEvCap(source, preSaveReview))
            return IdentityProjectionPolicy.Gen56GbTransfer;
        if (pk.Format is 5 or 6 && source.Format == 3)
            return IdentityProjectionPolicy.Gen56Gen3Transfer;

        return IdentityProjectionPolicy.None;
    }

    private static bool ShouldRepairPidIvCorrelation(IdentityProjectionPolicy policy)
    {
        return policy is
            IdentityProjectionPolicy.Gen3Encounter or
            IdentityProjectionPolicy.Gen4PalPark or
            IdentityProjectionPolicy.Gen56GbTransfer or
            IdentityProjectionPolicy.Gen56Gen3Transfer;
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
        if (pk.Format == 3)
        {
            notes.Add("[transfer_normalize] skipped exhaustive Gen III PID/IV brute-force fallback; preserved legacy moves can make PKHeX choose an incompatible encounter path.");
            return false;
        }

        var seed = unchecked((uint)(pk.PID ^ ((uint)pk.Species << 16) ^ ((uint)pk.TID16 << 8) ^ pk.SID16));
        const int MaxAttempts = 4_000_000;
        var pidTypes = new[] { PIDType.Method_1, PIDType.Method_2, PIDType.Method_4 };

        for (var attempt = 0; attempt < MaxAttempts; attempt++, seed += 0x9E3779B9u)
        {
            foreach (var pidType in pidTypes)
            {
                TransferLcrngPidSeed.Apply(pk, pidType, seed);
                pk.Gender = GenderFromPid(pk, pk.PID);
                pk.Nature = desiredNature;
                pk.Form = desiredForm;
                if (pk.Format == 6)
                    pk.EncryptionConstant = pk.PID;
                if (!MatchesFinalIdentity(pk, desiredNature, desiredShiny, desiredGender, desiredForm, desiredAbilityIndex))
                    continue;

                CommonEdits.SetAbilityIndex(pk, pk.PIDAbility);
                TrySetGbOriginEncounterAbility(pk);
                SyncAbilityFromPid(pk);
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

        if (match is not IEncounterConvertible conv)
            return false;

        var encounterGen = match is IGeneration g ? g.Generation : pk.Generation;
        var trainer = new SimpleTrainerInfo(pk.Version)
        {
            OT = pk.OriginalTrainerName,
            TID16 = pk.TID16,
            SID16 = pk.SID16,
            Gender = pk.OriginalTrainerGender,
            Language = pk.Language,
            Generation = encounterGen,
            Context = EncounterGenerationToContext(encounterGen, pk),
        };
        var levelMin = TryReadEncounterProperty<byte>(match, "LevelMin");
        var levelMax = TryReadEncounterProperty<byte>(match, "LevelMax");
        var criteria = new EncounterCriteria
        {
            Nature = desiredNature,
            Gender = (Gender)Math.Clamp((int)desiredGender, 0, 2),
            Ability = desiredAbilityIndex is null
                ? AbilityPermission.Any12
                : desiredAbilityIndex == 0 ? AbilityPermission.OnlyFirst : AbilityPermission.OnlySecond,
            Shiny = desiredShiny ? Shiny.Always : Shiny.Never,
            Form = (sbyte)desiredForm,
            LevelMin = levelMin ?? 0,
            LevelMax = levelMax ?? 0,
        };

        PKM generated;
        try
        {
            generated = conv.ConvertToPKM(trainer, criteria);
        }
        catch
        {
            return false;
        }
        if (pk.Format == 3 && generated.Species != pk.Species)
            return false;

        ApplyGeneratedPidIv(pk, generated, desiredNature, desiredGender, desiredForm);
        if (pk.Format != 3)
        {
            CommonEdits.SetAbilityIndex(pk, pk.PIDAbility);
            TrySetGbOriginEncounterAbility(pk);
            SyncAbilityFromPid(pk);
        }
        if (pk.Format == 3 && !HasPidCorrelationIssue(pk))
        {
            notes.Add($"[transfer_normalize] repaired Gen III PID/IV pair from PKHeX {match.GetType().Name} generator.");
            return true;
        }
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
        if (match is not IEncounterSlot3)
            return false;

        try
        {
            var matchType = match.GetType();
            var version = ReadEncounterProperty<GameVersion>(match, "Version");
            var location = ReadEncounterProperty<ushort>(match, "Location");
            var levelMin = ReadEncounterProperty<byte>(match, "LevelMin");
            var levelMax = ReadEncounterProperty<byte>(match, "LevelMax");
            var slotNumber = ReadEncounterProperty<byte>(match, "SlotNumber");

            var temp = (PK3)EntityBlank.GetBlank(typeof(PK3));
            temp.Species = pk.Species;
            temp.Form = pk.Form;
            temp.Version = version;
            temp.Language = pk.Language;
            temp.TID16 = pk.TID16;
            temp.SID16 = pk.SID16;
            temp.EXP = pk.EXP;
            temp.MetLocation = location;
            temp.MetLevel = Math.Max(levelMin, pk.MetLevel);
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
                LevelMin = levelMin,
                LevelMax = levelMax,
            };

            var info = GetPersonalInfo3(version, pk.Species, pk.Form);
            var seed = unchecked(pk.PID ^ ((uint)location << 16) ^ ((uint)slotNumber << 8) ^ (uint)desiredNature);
            var method = typeof(GenerateMethodH)
                .GetMethods(BindingFlags.Public | BindingFlags.Static)
                .Single(m => m.Name == nameof(GenerateMethodH.SetRandom) && m.IsGenericMethodDefinition);
            method.MakeGenericMethod(matchType).Invoke(null, [match, temp, info, criteria, seed]);

            ApplyGeneratedPidIv(pk, temp, desiredNature, desiredGender, desiredForm);
            CommonEdits.SetAbilityIndex(pk, pk.PIDAbility);
            TrySetGbOriginEncounterAbility(pk);
            SyncAbilityFromPid(pk);
            if (pk.Format == 3 && !HasPidCorrelationIssue(pk))
            {
                notes.Add("[transfer_normalize] repaired Gen III PID/IV pair with PKHeX Method H slot data.");
                return true;
            }

            if (!MatchesFinalIdentity(pk, desiredNature, desiredShiny, desiredGender, desiredForm, desiredAbilityIndex) ||
                HasPidCorrelationIssue(pk))
            {
                return false;
            }

            notes.Add("[transfer_normalize] repaired transfer PID/IV pair with PKHeX Method H slot data.");
            return true;
        }
        catch (Exception ex)
        {
            notes.Add($"[transfer_normalize] skipped Method H slot repair for {match.GetType().Name}: {ex.GetBaseException().Message}");
            return false;
        }
    }

}
