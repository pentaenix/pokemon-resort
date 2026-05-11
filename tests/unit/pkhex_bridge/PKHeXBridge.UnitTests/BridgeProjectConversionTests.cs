using System.Reflection;
using System.Security.Cryptography;
using System.Text.Json;
using PKHeX.Core;
using PKHeXBridge;
using Xunit;

namespace PKHeXBridge.UnitTests;

public class BridgeProjectConversionTests
{
    [Fact]
    public void Project_ConvertsPk3BulbasaurToPk4_WithValidRequestFile()
    {
        var pk3 = EntityBlank.GetBlank(typeof(PK3));
        pk3.Species = (int)Species.Bulbasaur;
        pk3.EXP = 135;
        pk3.Version = GameVersion.FR;
        pk3.Language = (int)LanguageID.English;
        pk3.OriginalTrainerName = "RESORT";
        pk3.TID16 = 12345;
        pk3.SID16 = 54321;
        pk3.Ball = 4;
        pk3.Move1 = (ushort)Move.Tackle;
        pk3.Move1_PP = 35;
        pk3.EV_HP = 120;
        pk3.EV_ATK = 120;
        pk3.EV_DEF = 120;
        pk3.EV_SPA = 120;
        pk3.EV_SPD = 120;
        pk3.EV_SPE = 120;
        pk3.RefreshChecksum();

        var raw = pk3.EncryptedBoxData;
        var hash = Convert.ToHexString(SHA256.HashData(raw)).ToLowerInvariant();

        var requestPath = Path.GetTempFileName();
        try
        {
            File.WriteAllText(requestPath, $$"""
                {
                  "bridge_project_schema": 1,
                  "source_format_name": "{{pk3.GetType().Name}}",
                  "source_raw_payload_base64": "{{Convert.ToBase64String(raw)}}",
                  "source_raw_hash_sha256": "{{hash}}",
                  "target_game": 10,
                  "target_format_name": "PK4",
                  "projection_policy": { "allow_lossy_projection": true }
                }
                """);

            var result = BridgeProject.ProjectFromJsonFile(requestPath);
            Assert.True(result.Success, result.Details ?? result.Error);
            Assert.Equal("PK4", result.TargetFormatName);
            Assert.False(string.IsNullOrEmpty(result.TargetRawPayloadBase64));
            Assert.False(string.IsNullOrEmpty(result.TargetRawHashSha256));

            var decoded = Convert.FromBase64String(result.TargetRawPayloadBase64!);
            var pk4 = EntityFormat.GetFromBytes(decoded);
            Assert.NotNull(pk4);
            Assert.Equal(typeof(PK4), pk4!.GetType());
            Assert.Equal((int)Species.Bulbasaur, pk4.Species);
            Assert.Equal(55, pk4.MetLocation);
            Assert.Equal(pk4.PIDAbility, pk4.AbilityNumber >> 1);
            Assert.True(pk4.EVTotal <= 510);
            Assert.InRange(pk4.Language, (int)LanguageID.Japanese, (int)LanguageID.Spanish);
            Assert.True(pk4.MetDate is { Year: >= 2000 and <= 2099 });
            var legality = new LegalityAnalysis(pk4);
            Assert.True(
                legality.Valid,
                $"{legality.Report()}\nversion={pk4.Version} met={pk4.MetLocation} egg={pk4.EggLocation} metLevel={pk4.MetLevel} level={pk4.CurrentLevel} pid={pk4.PID} ability={pk4.AbilityNumber} gender={pk4.Gender} language={pk4.Language} date={pk4.MetDate}");
        }
        finally
        {
            File.Delete(requestPath);
        }
    }

    [Fact]
    public void Project_NormalizesGen2OriginEvs_WhenProjectingToPk4()
    {
        var pk2 = EntityBlank.GetBlank(typeof(PK2));
        pk2.Species = (int)Species.Bulbasaur;
        pk2.EXP = 125;
        pk2.Version = GameVersion.GS;
        pk2.Language = (int)LanguageID.English;
        pk2.OriginalTrainerName = "RESORT";
        pk2.TID16 = 12345;
        pk2.EV_HP = 120;
        pk2.EV_ATK = 120;
        pk2.EV_DEF = 120;
        pk2.EV_SPA = 120;
        pk2.EV_SPD = 120;
        pk2.EV_SPE = 120;
        pk2.RefreshChecksum();

        var raw = pk2.EncryptedBoxData;
        var hash = Convert.ToHexString(SHA256.HashData(raw)).ToLowerInvariant();

        var requestPath = Path.GetTempFileName();
        try
        {
            File.WriteAllText(requestPath, $$"""
                {
                  "bridge_project_schema": 1,
                  "source_format_name": "{{pk2.GetType().Name}}",
                  "source_raw_payload_base64": "{{Convert.ToBase64String(raw)}}",
                  "source_raw_hash_sha256": "{{hash}}",
                  "target_game": 10,
                  "target_format_name": "PK4",
                  "projection_policy": { "allow_lossy_projection": true }
                }
                """);

            var result = BridgeProject.ProjectFromJsonFile(requestPath);
            Assert.True(result.Success, result.Details ?? result.Error);

            var decoded = Convert.FromBase64String(result.TargetRawPayloadBase64!);
            var pk4 = EntityFormat.GetFromBytes(decoded);
            Assert.NotNull(pk4);
            Assert.Equal(typeof(PK4), pk4!.GetType());
            Assert.True(pk4.EV_HP <= 100);
            Assert.True(pk4.EV_ATK <= 100);
            Assert.True(pk4.EV_DEF <= 100);
            Assert.True(pk4.EV_SPA <= 100);
            Assert.True(pk4.EV_SPD <= 100);
            Assert.True(pk4.EV_SPE <= 100);
            Assert.True(pk4.EVTotal <= 510);
        }
        finally
        {
            File.Delete(requestPath);
        }
    }

    [Fact]
    public void Project_BumpsMinimumExpByOne_WhenGen2DirectTransferAtLevelFloor_ToPk4()
    {
        var pk2 = EntityBlank.GetBlank(typeof(PK2));
        pk2.Species = (int)Species.Bulbasaur;
        pk2.Version = GameVersion.GS;
        pk2.Language = (int)LanguageID.English;
        pk2.OriginalTrainerName = "RESORT";
        pk2.TID16 = 12345;
        pk2.CurrentLevel = 14;
        var g2Growth = pk2.PersonalInfo.EXPGrowth;
        pk2.EXP = Experience.GetEXP((byte)pk2.CurrentLevel, g2Growth);
        pk2.RefreshChecksum();

        var raw = pk2.EncryptedBoxData;
        var hash = Convert.ToHexString(SHA256.HashData(raw)).ToLowerInvariant();

        var requestPath = Path.GetTempFileName();
        try
        {
            File.WriteAllText(requestPath, $$"""
                {
                  "bridge_project_schema": 1,
                  "source_format_name": "{{pk2.GetType().Name}}",
                  "source_raw_payload_base64": "{{Convert.ToBase64String(raw)}}",
                  "source_raw_hash_sha256": "{{hash}}",
                  "target_game": 10,
                  "target_format_name": "PK4",
                  "projection_policy": { "allow_lossy_projection": true }
                }
                """);

            var result = BridgeProject.ProjectFromJsonFile(requestPath);
            Assert.True(result.Success, result.Details ?? result.Error);

            var decoded = Convert.FromBase64String(result.TargetRawPayloadBase64!);
            var pk4 = EntityFormat.GetFromBytes(decoded);
            Assert.NotNull(pk4);
            Assert.Equal(typeof(PK4), pk4!.GetType());

            var g4Growth = pk4.PersonalInfo.EXPGrowth;
            var floor = Experience.GetEXP((byte)pk4.CurrentLevel, g4Growth);
            Assert.Equal(floor + 1, pk4.EXP);
            Assert.Contains(
                "gb_origin: bumped EXP",
                string.Join('\n', result.LossManifest?.Notes ?? []),
                StringComparison.Ordinal);
        }
        finally
        {
            File.Delete(requestPath);
        }
    }

    [Fact]
    public void Project_NormalizesCanonicalGbOriginEvs_WhenSourceSnapshotIsAlreadyPk4()
    {
        var pk4Source = EntityBlank.GetBlank(typeof(PK4));
        pk4Source.Species = (int)Species.Jigglypuff;
        pk4Source.EXP = 1000;
        pk4Source.Version = GameVersion.FR;
        pk4Source.Language = (int)LanguageID.English;
        pk4Source.OriginalTrainerName = "RESORT";
        pk4Source.TID16 = 12345;
        pk4Source.MetLocation = 55;
        pk4Source.MetLevel = 10;
        pk4Source.EV_HP = 120;
        pk4Source.EV_ATK = 120;
        pk4Source.EV_DEF = 120;
        pk4Source.EV_SPA = 120;
        pk4Source.EV_SPD = 120;
        pk4Source.EV_SPE = 120;
        pk4Source.RefreshChecksum();

        var raw = pk4Source.EncryptedBoxData;
        var hash = Convert.ToHexString(SHA256.HashData(raw)).ToLowerInvariant();

        var requestPath = Path.GetTempFileName();
        try
        {
            File.WriteAllText(requestPath, $$"""
                {
                  "bridge_project_schema": 1,
                  "source_format_name": "{{pk4Source.GetType().Name}}",
                  "source_raw_payload_base64": "{{Convert.ToBase64String(raw)}}",
                  "source_raw_hash_sha256": "{{hash}}",
                  "target_game": 10,
                  "target_format_name": "PK4",
                  "projection_policy": { "allow_lossy_projection": true },
                  "pre_save_review": {
                    "enabled": true,
                    "source_origin_game": 35
                  }
                }
                """);

            var result = BridgeProject.ProjectFromJsonFile(requestPath);
            Assert.True(result.Success, result.Details ?? result.Error);

            var decoded = Convert.FromBase64String(result.TargetRawPayloadBase64!);
            var pk4 = EntityFormat.GetFromBytes(decoded);
            Assert.NotNull(pk4);
            Assert.Equal(typeof(PK4), pk4!.GetType());
            Assert.True(pk4.EV_HP <= 100);
            Assert.True(pk4.EV_ATK <= 100);
            Assert.True(pk4.EV_DEF <= 100);
            Assert.True(pk4.EV_SPA <= 100);
            Assert.True(pk4.EV_SPD <= 100);
            Assert.True(pk4.EV_SPE <= 100);
            Assert.True(pk4.EVTotal <= 510);
        }
        finally
        {
            File.Delete(requestPath);
        }
    }

    [Fact]
    public void Project_NormalizesCanonicalGbOriginEvsAndLanguage_WhenProjectingToPk3()
    {
        var pk3Source = EntityBlank.GetBlank(typeof(PK3));
        pk3Source.Species = (int)Species.Jigglypuff;
        pk3Source.EXP = 1000;
        pk3Source.Version = GameVersion.FR;
        pk3Source.Language = 0;
        pk3Source.EV_HP = 120;
        pk3Source.EV_ATK = 120;
        pk3Source.EV_DEF = 120;
        pk3Source.EV_SPA = 120;
        pk3Source.EV_SPD = 120;
        pk3Source.EV_SPE = 120;
        pk3Source.RefreshChecksum();

        var raw = pk3Source.EncryptedBoxData;
        var hash = Convert.ToHexString(SHA256.HashData(raw)).ToLowerInvariant();
        var requestPath = Path.GetTempFileName();
        try
        {
            File.WriteAllText(requestPath, $$"""
                {
                  "bridge_project_schema": 1,
                  "source_format_name": "{{pk3Source.GetType().Name}}",
                  "source_raw_payload_base64": "{{Convert.ToBase64String(raw)}}",
                  "source_raw_hash_sha256": "{{hash}}",
                  "target_game": 4,
                  "target_format_name": "PK3",
                  "projection_policy": { "allow_lossy_projection": true },
                  "pre_save_review": {
                    "enabled": true,
                    "source_origin_game": 35
                  }
                }
                """);

            var result = BridgeProject.ProjectFromJsonFile(requestPath);
            Assert.True(result.Success, result.Details ?? result.Error);
            var pk3 = EntityFormat.GetFromBytes(Convert.FromBase64String(result.TargetRawPayloadBase64!));
            Assert.NotNull(pk3);
            Assert.Equal(typeof(PK3), pk3!.GetType());
            Assert.InRange(pk3.Language, (int)LanguageID.Japanese, (int)LanguageID.Spanish);
            Assert.True(pk3.EV_HP <= 100);
            Assert.True(pk3.EV_ATK <= 100);
            Assert.True(pk3.EV_DEF <= 100);
            Assert.True(pk3.EV_SPA <= 100);
            Assert.True(pk3.EV_SPD <= 100);
            Assert.True(pk3.EV_SPE <= 100);
            Assert.True(pk3.EVTotal <= 510);
        }
        finally
        {
            File.Delete(requestPath);
        }
    }

    [Fact]
    public void Project_UsesPokeTransferFields_WhenProjectingGbOriginToPk5()
    {
        var pk4Source = EntityBlank.GetBlank(typeof(PK4));
        pk4Source.Species = (int)Species.Jigglypuff;
        pk4Source.EXP = 1000;
        pk4Source.Version = GameVersion.FR;
        pk4Source.Language = 0;
        pk4Source.MetLocation = 55;
        pk4Source.MetLevel = 0;
        pk4Source.PID = 0;
        pk4Source.TID16 = 12345;
        pk4Source.SID16 = 54321;
        pk4Source.EV_HP = 120;
        pk4Source.EV_ATK = 120;
        pk4Source.EV_DEF = 120;
        pk4Source.EV_SPA = 120;
        pk4Source.EV_SPD = 120;
        pk4Source.EV_SPE = 120;
        pk4Source.RefreshChecksum();

        var raw = pk4Source.EncryptedBoxData;
        var hash = Convert.ToHexString(SHA256.HashData(raw)).ToLowerInvariant();
        var requestPath = Path.GetTempFileName();
        try
        {
            File.WriteAllText(requestPath, $$"""
                {
                  "bridge_project_schema": 1,
                  "source_format_name": "{{pk4Source.GetType().Name}}",
                  "source_raw_payload_base64": "{{Convert.ToBase64String(raw)}}",
                  "source_raw_hash_sha256": "{{hash}}",
                  "target_game": 20,
                  "target_format_name": "PK5",
                  "projection_policy": { "allow_lossy_projection": true },
                  "pre_save_review": {
                    "enabled": true,
                    "source_origin_game": 35,
                    "origin_game": 4,
                    "met_location_id": 30001,
                    "met_level": 10,
                    "language": 0,
                    "apply_static_fields": true
                  }
                }
                """);

            var result = BridgeProject.ProjectFromJsonFile(requestPath);
            Assert.True(result.Success, result.Details ?? result.Error);
            Assert.Equal("PK5", result.TargetFormatName);
            Assert.False(string.IsNullOrEmpty(result.TargetRawPayloadBase64));
            var pk5 = new PK5(Convert.FromBase64String(result.TargetRawPayloadBase64!));
            Assert.Equal(30001, pk5.MetLocation);
            Assert.NotEqual(55, pk5.EggLocation);
            Assert.False(pk5.IsEgg);
            Assert.True(pk5.MetDate is { Year: >= 2000 and <= 2099 });
            Assert.Equal(10, pk5.MetLevel);
            Assert.InRange(pk5.Language, (int)LanguageID.Japanese, (int)LanguageID.Spanish);
            Assert.Equal(pk5.Nature, (Nature)(pk5.PID % 25));
            Assert.Equal(pk5.PIDAbility, pk5.AbilityNumber >> 1);
            Assert.Contains(result.LossManifest?.Notes ?? [], note => note.Contains("[transfer_normalize]"));
            Assert.Contains(result.LossManifest?.Notes ?? [], note => note.Contains("[finalize_identity]"));
        }
        finally
        {
            File.Delete(requestPath);
        }
    }

    [Fact]
    public void Project_UsesBankTransferFields_WhenProjectingGbOriginToPk6()
    {
        var pk4Source = EntityBlank.GetBlank(typeof(PK4));
        pk4Source.Species = (int)Species.Exeggcute;
        pk4Source.EXP = 1000;
        pk4Source.Version = GameVersion.FR;
        pk4Source.Language = 0;
        pk4Source.MetLocation = 55;
        pk4Source.MetLevel = 0;
        pk4Source.PID = 0;
        pk4Source.EncryptionConstant = 0;
        pk4Source.RefreshChecksum();

        var projected = ProjectAndDecode(pk4Source, 26, "PK6", $$"""
          "pre_save_review": {
            "enabled": true,
            "source_origin_game": 35,
            "origin_game": 4,
            "met_location_id": 30001,
            "met_level": 10,
            "language": 0,
            "apply_static_fields": true
          }
        """);

        var pk6 = Assert.IsType<PK6>(projected);
        Assert.Equal(GameVersion.FR, pk6.Version);
        Assert.Equal((ushort)30001, pk6.MetLocation);
        Assert.False(pk6.IsEgg);
        Assert.Equal(0, pk6.EggLocation);
        Assert.True(pk6.MetDate is { Year: >= 2000 and <= 2099 });
        Assert.Equal(10, pk6.MetLevel);
        Assert.InRange(pk6.Language, (int)LanguageID.Japanese, (int)LanguageID.Spanish);
        Assert.NotEqual(0u, pk6.PID);
        Assert.NotEqual(0u, pk6.EncryptionConstant);
        Assert.Equal(pk6.PID, pk6.EncryptionConstant);
        Assert.Equal(pk6.Nature, (Nature)(pk6.PID % 25));
        Assert.Equal(49, pk6.Country);
        Assert.Equal(1, pk6.ConsoleRegion);

        var report = new LegalityAnalysis(pk6).Report();
        Assert.DoesNotContain("Unable to match an encounter from origin game.", report, StringComparison.Ordinal);
        Assert.DoesNotContain("PID should be equal to EC.", report, StringComparison.Ordinal);
        Assert.DoesNotContain("PID-Nature mismatch.", report, StringComparison.Ordinal);
        Assert.DoesNotContain("Ability is not valid for species/form.", report, StringComparison.Ordinal);
        Assert.DoesNotContain("Ability mismatch for encounter.", report, StringComparison.Ordinal);
        Assert.DoesNotContain("Geolocation: Country is not in 3DS region.", report, StringComparison.Ordinal);
        Assert.DoesNotContain("Original Trainer Memory: Can't obtain Memory on Original Trainer Version.", report, StringComparison.Ordinal);
        Assert.DoesNotContain("Memory: Original Trainer Memory missing.", report, StringComparison.Ordinal);
        Assert.DoesNotContain("PID is not set.", report, StringComparison.Ordinal);
        Assert.DoesNotContain("Encryption Constant is not set.", report, StringComparison.Ordinal);
    }

    [Fact]
    public void Project_PreservesRequestedGender_WhenGen6Method1RepairCannotSatisfyEncounter()
    {
        var pk4Source = EntityBlank.GetBlank(typeof(PK4));
        pk4Source.Species = (int)Species.Jigglypuff;
        pk4Source.EXP = 1000;
        pk4Source.Version = GameVersion.FR;
        pk4Source.Language = (int)LanguageID.English;
        pk4Source.MetLocation = 55;
        pk4Source.MetLevel = 10;
        pk4Source.PID = 0;
        pk4Source.RefreshChecksum();

        var projected = ProjectAndDecode(pk4Source, 26, "PK6", $$"""
          "pre_save_review": {
            "enabled": true,
            "source_origin_game": 35,
            "origin_game": 4,
            "met_location_id": 30001,
            "met_level": 10,
            "language": {{(int)LanguageID.English}},
            "apply_static_fields": true
          },
          "hot_mutable_overlay": {
            "enabled": true,
            "species_id": {{(int)Species.Jigglypuff}},
            "form_id": 0,
            "gender": 1,
            "shiny": false
          }
        """);

        var pk6 = Assert.IsType<PK6>(projected);
        Assert.Equal(1, pk6.Gender);
        Assert.Equal(pk6.PID, pk6.EncryptionConstant);
        Assert.Equal(GameVersion.FR, pk6.Version);
        var report = new LegalityAnalysis(pk6).Report();
        Assert.DoesNotContain("Ability mismatch for encounter.", report, StringComparison.Ordinal);
        Assert.DoesNotContain("PID+ correlation does not match what was expected for the Encounter's type.", report, StringComparison.Ordinal);
    }

    [Fact]
    public void Project_RejectsHashMismatch()
    {
        var pk3 = EntityBlank.GetBlank(typeof(PK3));
        pk3.Species = (int)Species.Bulbasaur;
        pk3.Version = GameVersion.FR;
        pk3.RefreshChecksum();
        var raw = pk3.EncryptedBoxData;

        var requestPath = Path.GetTempFileName();
        try
        {
            File.WriteAllText(requestPath, $$"""
                {
                  "bridge_project_schema": 1,
                  "source_format_name": "PK3",
                  "source_raw_payload_base64": "{{Convert.ToBase64String(raw)}}",
                  "source_raw_hash_sha256": "0000000000000000000000000000000000000000000000000000000000000000",
                  "target_game": 10,
                  "target_format_name": "PK4"
                }
                """);

            var result = BridgeProject.ProjectFromJsonFile(requestPath);
            Assert.False(result.Success);
            Assert.Equal("hash_mismatch", result.Error);
        }
        finally
        {
            File.Delete(requestPath);
        }
    }

    [Fact]
    public void Project_AppliesMoveReconciliation_FromMoveNames()
    {
        var pk3 = EntityBlank.GetBlank(typeof(PK3));
        pk3.Species = (int)Species.Bulbasaur;
        pk3.EXP = 125;
        pk3.Version = GameVersion.FR;
        pk3.Language = (int)LanguageID.English;
        pk3.Move1 = (ushort)Move.Tackle;
        pk3.Move2 = (ushort)Move.Growl;
        pk3.RefreshChecksum();

        var raw = pk3.EncryptedBoxData;
        var hash = Convert.ToHexString(SHA256.HashData(raw)).ToLowerInvariant();

        var requestPath = Path.GetTempFileName();
        try
        {
            File.WriteAllText(requestPath, $$"""
                {
                  "bridge_project_schema": 1,
                  "source_format_name": "{{pk3.GetType().Name}}",
                  "source_raw_payload_base64": "{{Convert.ToBase64String(raw)}}",
                  "source_raw_hash_sha256": "{{hash}}",
                  "target_game": 10,
                  "target_format_name": "PK4",
                  "projection_policy": { "allow_lossy_projection": true },
                  "move_reconciliation": {
                    "enabled": true,
                    "moves": [
                      { "slot_index": 0, "move_name": "Scratch" },
                      { "slot_index": 1, "move_name": "Growl" }
                    ]
                  }
                }
                """);

            var result = BridgeProject.ProjectFromJsonFile(requestPath);
            Assert.True(result.Success, result.Details ?? result.Error);

            var decoded = Convert.FromBase64String(result.TargetRawPayloadBase64!);
            var pk4 = EntityFormat.GetFromBytes(decoded);
            Assert.NotNull(pk4);
            Assert.Equal(typeof(PK4), pk4!.GetType());
            Assert.Equal((ushort)Move.Scratch, pk4.Move1);
            Assert.Equal((ushort)Move.Growl, pk4.Move2);
        }
        finally
        {
            File.Delete(requestPath);
        }
    }

    [Fact]
    public void Project_PreSaveReview_OverwritesNicknameAndFriendship()
    {
        var pk3 = EntityBlank.GetBlank(typeof(PK3));
        pk3.Species = (int)Species.Bulbasaur;
        pk3.Version = GameVersion.FR;
        pk3.Language = (int)LanguageID.English;
        pk3.Nickname = "Old";
        pk3.IsNicknamed = true;
        pk3.OriginalTrainerFriendship = 10;
        pk3.RefreshChecksum();

        var raw = pk3.EncryptedBoxData;
        var hash = Convert.ToHexString(SHA256.HashData(raw)).ToLowerInvariant();

        var requestPath = Path.GetTempFileName();
        try
        {
            File.WriteAllText(requestPath, $$"""
                {
                  "bridge_project_schema": 1,
                  "source_format_name": "{{pk3.GetType().Name}}",
                  "source_raw_payload_base64": "{{Convert.ToBase64String(raw)}}",
                  "source_raw_hash_sha256": "{{hash}}",
                  "target_game": 10,
                  "target_format_name": "PK4",
                  "projection_policy": { "allow_lossy_projection": true },
                  "pre_save_review": {
                    "enabled": true,
                    "nickname": "Bulby",
                    "is_nicknamed": true,
                    "nature": "Sassy",
                    "friendship_ot": 200,
                    "friendship_handling": 199,
                    "friendship_current": 198
                  }
                }
                """);

            var result = BridgeProject.ProjectFromJsonFile(requestPath);
            Assert.True(result.Success, result.Details ?? result.Error);

            var decoded = Convert.FromBase64String(result.TargetRawPayloadBase64!);
            var pk4 = EntityFormat.GetFromBytes(decoded);
            Assert.NotNull(pk4);
            Assert.Equal("Bulby", pk4!.Nickname);
            Assert.True(pk4.IsNicknamed);
            // Friendship triplets are format-dependent after EntityConverter; current + nickname must apply.
            Assert.Equal(198, pk4.CurrentFriendship);
        }
        finally
        {
            File.Delete(requestPath);
        }
    }

    [Fact]
    public void Project_PreSaveReview_ClearsDefaultNicknameEvenWhenSkipFlagPresent()
    {
        var pk3 = EntityBlank.GetBlank(typeof(PK3));
        pk3.Species = (int)Species.Pikachu;
        pk3.Version = GameVersion.E;
        pk3.Language = (int)LanguageID.English;
        pk3.Nickname = "PIKA";
        pk3.IsNicknamed = true;
        pk3.RefreshChecksum();

        var raw = pk3.EncryptedBoxData;
        var hash = Convert.ToHexString(SHA256.HashData(raw)).ToLowerInvariant();

        var requestPath = Path.GetTempFileName();
        try
        {
            File.WriteAllText(requestPath, $$"""
                {
                  "bridge_project_schema": 1,
                  "source_format_name": "{{pk3.GetType().Name}}",
                  "source_raw_payload_base64": "{{Convert.ToBase64String(raw)}}",
                  "source_raw_hash_sha256": "{{hash}}",
                  "target_game": 3,
                  "target_format_name": "PK3",
                  "projection_policy": { "allow_lossy_projection": true },
                  "pre_save_review": {
                    "enabled": true,
                    "skip_default_nickname": true,
                    "nickname": "Pikachu",
                    "is_nicknamed": false
                  }
                }
                """);

            var result = BridgeProject.ProjectFromJsonFile(requestPath);
            Assert.True(result.Success, result.Details ?? result.Error);

            var decoded = Convert.FromBase64String(result.TargetRawPayloadBase64!);
            var projected = EntityFormat.GetFromBytes(decoded);
            Assert.NotNull(projected);
            Assert.Equal(typeof(PK3), projected!.GetType());
            Assert.Equal("PIKACHU", projected.Nickname);
            Assert.False(projected.IsNicknamed);
        }
        finally
        {
            File.Delete(requestPath);
        }
    }

    [Fact]
    public void Project_PreSaveReview_ClearsGen3DefaultNickname()
    {
        var pk3 = EntityBlank.GetBlank(typeof(PK3));
        pk3.Species = (int)Species.Bulbasaur;
        pk3.Version = GameVersion.E;
        pk3.Language = (int)LanguageID.English;
        pk3.Nickname = "Bulbasaur";
        pk3.IsNicknamed = true;
        pk3.RefreshChecksum();

        var raw = pk3.EncryptedBoxData;
        var hash = Convert.ToHexString(SHA256.HashData(raw)).ToLowerInvariant();

        var requestPath = Path.GetTempFileName();
        try
        {
            File.WriteAllText(requestPath, $$"""
                {
                  "bridge_project_schema": 1,
                  "source_format_name": "{{pk3.GetType().Name}}",
                  "source_raw_payload_base64": "{{Convert.ToBase64String(raw)}}",
                  "source_raw_hash_sha256": "{{hash}}",
                  "target_game": 3,
                  "target_format_name": "PK3",
                  "projection_policy": { "allow_lossy_projection": true },
                  "pre_save_review": {
                    "enabled": true,
                    "nickname": "Bulbasaur",
                    "is_nicknamed": false
                  }
                }
                """);

            var result = BridgeProject.ProjectFromJsonFile(requestPath);
            Assert.True(result.Success, result.Details ?? result.Error);

            var decoded = Convert.FromBase64String(result.TargetRawPayloadBase64!);
            var projected = EntityFormat.GetFromBytes(decoded);
            Assert.NotNull(projected);
            Assert.Equal(typeof(PK3), projected!.GetType());
            Assert.Equal("BULBASAUR", projected.Nickname);
            Assert.False(projected.IsNicknamed);
        }
        finally
        {
            File.Delete(requestPath);
        }
    }

    [Fact]
    public void Project_PreSaveReview_DoesNotOverwriteStaticEncounterFieldsByDefault()
    {
        var pk3 = EntityBlank.GetBlank(typeof(PK3));
        pk3.Species = (int)Species.Bulbasaur;
        pk3.Version = GameVersion.FR;
        pk3.Language = (int)LanguageID.English;
        pk3.MetLocation = 10;
        pk3.MetLevel = 5;
        pk3.Ball = 4;
        pk3.RefreshChecksum();

        var raw = pk3.EncryptedBoxData;
        var hash = Convert.ToHexString(SHA256.HashData(raw)).ToLowerInvariant();

        var requestPath = Path.GetTempFileName();
        try
        {
            File.WriteAllText(requestPath, $$"""
                {
                  "bridge_project_schema": 1,
                  "source_format_name": "{{pk3.GetType().Name}}",
                  "source_raw_payload_base64": "{{Convert.ToBase64String(raw)}}",
                  "source_raw_hash_sha256": "{{hash}}",
                  "target_game": 10,
                  "target_format_name": "PK4",
                  "projection_policy": { "allow_lossy_projection": true },
                  "pre_save_review": {
                    "enabled": true,
                    "met_location_id": 88,
                    "met_level": 7,
                    "ball_id": 1,
                    "fateful_encounter": false
                  }
                }
                """);

            var result = BridgeProject.ProjectFromJsonFile(requestPath);
            Assert.True(result.Success, result.Details ?? result.Error);

            var decoded = Convert.FromBase64String(result.TargetRawPayloadBase64!);
            var pk4 = EntityFormat.GetFromBytes(decoded);
            Assert.NotNull(pk4);
            Assert.NotEqual(88, pk4!.MetLocation);
            Assert.NotEqual(7, pk4.MetLevel);
        }
        finally
        {
            File.Delete(requestPath);
        }
    }

    [Fact]
    public void Project_HotMutableOverlay_PatchesSameFormatPayloadWithoutChangingEncounterStatics()
    {
        var pk3 = EntityBlank.GetBlank(typeof(PK3));
        pk3.Species = (int)Species.Bulbasaur;
        pk3.Version = GameVersion.FR;
        pk3.Language = (int)LanguageID.English;
        pk3.MetLocation = 10;
        pk3.MetLevel = 5;
        pk3.Ball = 4;
        pk3.EXP = 125;
        pk3.Nickname = "Base";
        pk3.Move1 = (ushort)Move.Tackle;
        pk3.Move1_PP = 35;
        pk3.RefreshChecksum();

        var raw = pk3.EncryptedBoxData;
        var hash = Convert.ToHexString(SHA256.HashData(raw)).ToLowerInvariant();

        var requestPath = Path.GetTempFileName();
        try
        {
            File.WriteAllText(requestPath, $$"""
                {
                  "bridge_project_schema": 1,
                  "source_format_name": "{{pk3.GetType().Name}}",
                  "source_raw_payload_base64": "{{Convert.ToBase64String(raw)}}",
                  "source_raw_hash_sha256": "{{hash}}",
                  "target_game": 3,
                  "target_format_name": "PK3",
                  "projection_policy": { "allow_lossy_projection": true },
                  "hot_mutable_overlay": {
                    "enabled": true,
                    "species_id": {{(int)Species.Ivysaur}},
                    "form_id": 0,
                    "level": 20,
                    "exp": 8000,
                    "nickname": "Return",
                    "is_nicknamed": true,
                    "gender": 0,
                    "held_item_id": 0,
                    "hp_current": 42,
                    "hp_max": 42,
                    "status_flags": 0,
                    "moves": [
                      { "slot_index": 0, "move_id": {{(int)Move.VineWhip}}, "current_pp": 10, "pp_ups": 0 }
                    ]
                  }
                }
                """);

            var result = BridgeProject.ProjectFromJsonFile(requestPath);
            Assert.True(result.Success, result.Details ?? result.Error);

            var decoded = Convert.FromBase64String(result.TargetRawPayloadBase64!);
            var projected = EntityFormat.GetFromBytes(decoded);
            Assert.NotNull(projected);
            Assert.Equal(typeof(PK3), projected!.GetType());
            Assert.Equal((int)Species.Ivysaur, projected.Species);
            Assert.Equal((uint)8000, projected.EXP);
            Assert.Equal("Return", projected.Nickname);
            Assert.Equal((ushort)Move.VineWhip, projected.Move1);
            Assert.Equal(10, projected.Move1_PP);
            Assert.Equal(10, projected.MetLocation);
            Assert.Equal(5, projected.MetLevel);
            Assert.Equal(4, projected.Ball);
        }
        finally
        {
            File.Delete(requestPath);
        }
    }

    [Fact]
    public void Project_ManualPastProjection_PreservesShinyPk5ToPk3()
    {
        var pk5 = EntityBlank.GetBlank(typeof(PK5));
        pk5.Species = (int)Species.Pikachu;
        pk5.EXP = 8000;
        pk5.Version = GameVersion.B;
        pk5.Language = (int)LanguageID.English;
        pk5.OriginalTrainerName = "RESORT";
        pk5.TID16 = 2222;
        pk5.SID16 = 3333;
        pk5.PID = 0xCCA3CCA5u;
        pk5.Nature = Nature.Hardy;
        CommonEdits.SetIsShiny(pk5, true);
        Assert.True(pk5.IsShiny);

        pk5.Move1 = (ushort)Move.Tackle;
        pk5.RefreshChecksum();

        var raw = pk5.EncryptedBoxData;
        var hash = Convert.ToHexString(SHA256.HashData(raw)).ToLowerInvariant();

        var requestPath = Path.GetTempFileName();
        try
        {
            File.WriteAllText(requestPath, $$"""
                {
                  "bridge_project_schema": 1,
                  "source_format_name": "{{pk5.GetType().Name}}",
                  "source_raw_payload_base64": "{{Convert.ToBase64String(raw)}}",
                  "source_raw_hash_sha256": "{{hash}}",
                  "target_game": 3,
                  "target_format_name": "PK3",
                  "projection_policy": { "allow_lossy_projection": true },
                  "hot_mutable_overlay": {
                    "enabled": true,
                    "skip_default_nickname": true,
                    "ot_name": "RESORT",
                    "tid16": 2222,
                    "sid16": 3333,
                    "tid32": {{pk5.ID32}},
                    "nickname": "Pikachu",
                    "is_nicknamed": false
                  }
                }
                """);

            var result = BridgeProject.ProjectFromJsonFile(requestPath);
            Assert.True(result.Success, result.Details ?? result.Error);

            var decoded = Convert.FromBase64String(result.TargetRawPayloadBase64!);
            var pk3 = EntityFormat.GetFromBytes(decoded);
            Assert.NotNull(pk3);
            Assert.Equal(typeof(PK3), pk3!.GetType());
            Assert.True(pk3.IsShiny, "manual past projection must keep shininess when down-converting to Gen 3");
        }
        finally
        {
            File.Delete(requestPath);
        }
    }

    [Fact]
    public void Project_ManuallyBuildsPastGeneration_WithDefaultMetLocation()
    {
        var pk5 = EntityBlank.GetBlank(typeof(PK5));
        pk5.Species = (int)Species.Bulbasaur;
        pk5.EXP = 8000;
        pk5.Version = GameVersion.B;
        pk5.Language = (int)LanguageID.English;
        pk5.OriginalTrainerName = "RESORT";
        pk5.TID16 = 2222;
        pk5.SID16 = 3333;
        pk5.PID = 22; // Sassy for Gen 3 PID-derived nature.
        pk5.Nature = Nature.Sassy;
        pk5.IV_HP = 31;
        pk5.IV_ATK = 30;
        pk5.IV_DEF = 29;
        pk5.IV_SPA = 28;
        pk5.IV_SPD = 27;
        pk5.IV_SPE = 26;
        pk5.IsNicknamed = false;
        pk5.SetDefaultNickname();
        pk5.Move1 = (ushort)Move.DracoMeteor; // Not representable in Gen 3; should be replaced.
        pk5.Move1_PP = 99;
        pk5.Move2 = (ushort)Move.Tackle;
        pk5.Move2_PP = 99;
        pk5.RefreshChecksum();

        var raw = pk5.EncryptedBoxData;
        var hash = Convert.ToHexString(SHA256.HashData(raw)).ToLowerInvariant();

        var requestPath = Path.GetTempFileName();
        try
        {
            File.WriteAllText(requestPath, $$"""
                {
                  "bridge_project_schema": 1,
                  "source_format_name": "{{pk5.GetType().Name}}",
                  "source_raw_payload_base64": "{{Convert.ToBase64String(raw)}}",
                  "source_raw_hash_sha256": "{{hash}}",
                  "target_game": 3,
                  "target_format_name": "PK3",
                  "projection_policy": { "allow_lossy_projection": true },
                  "hot_mutable_overlay": {
                    "enabled": true,
                    "ot_name": "RESORT",
                    "tid16": 2222,
                    "sid16": 3333,
                    "tid32": {{pk5.ID32}},
                    "nickname": "Bulbasaur",
                    "is_nicknamed": false
                  }
                }
                """);

            var result = BridgeProject.ProjectFromJsonFile(requestPath);
            Assert.True(result.Success, result.Details ?? result.Error);
            Assert.True(result.LossManifest!.Lossy);
            Assert.Contains("manual_past_projection", result.LossManifest.LostCategories);

            var decoded = Convert.FromBase64String(result.TargetRawPayloadBase64!);
            var pk3 = EntityFormat.GetFromBytes(decoded);
            Assert.NotNull(pk3);
            Assert.Equal(typeof(PK3), pk3!.GetType());
            Assert.Equal((int)Species.Bulbasaur, pk3.Species);
            Assert.Equal((uint)8000, pk3.EXP);
            Assert.Equal("RESORT", pk3.OriginalTrainerName);
            Assert.Equal(2222, pk3.TID16);
            Assert.Equal(3333, pk3.SID16);
            Assert.NotEqual(0u, pk3.PID);
            Assert.Equal(Nature.Sassy, pk3.Nature);
            Assert.Equal(31, pk3.IV_HP);
            Assert.Equal(30, pk3.IV_ATK);
            Assert.Equal(29, pk3.IV_DEF);
            Assert.Equal(28, pk3.IV_SPA);
            Assert.Equal(27, pk3.IV_SPD);
            Assert.Equal(26, pk3.IV_SPE);
            Assert.False(pk3.IsNicknamed);
            Assert.Equal(201, pk3.MetLocation); // Faraway Island default for Gen 3 past projection.
            Assert.Equal((ushort)Move.Tackle, pk3.Move1);
            Assert.True(pk3.Move1_PP <= MoveInfo.GetPP(pk3.Context, pk3.Move1));
            Assert.NotEqual((ushort)Move.DracoMeteor, pk3.Move1);
        }
        finally
        {
            File.Delete(requestPath);
        }
    }

    [Fact]
    public void Project_AppliesCanonicalStaticIdentityToGen3Projection()
    {
        var pk5 = EntityBlank.GetBlank(typeof(PK5));
        pk5.Species = (int)Species.Pikachu;
        pk5.EXP = 8000;
        pk5.Version = GameVersion.B;
        pk5.Language = (int)LanguageID.Japanese;
        pk5.OriginalTrainerName = "ァグト";
        pk5.TID16 = 1111;
        pk5.SID16 = 0;
        pk5.PID = 3998925365u;
        pk5.Nature = Nature.Hardy;
        pk5.Move1 = (ushort)Move.Tackle;
        pk5.RefreshChecksum();

        var raw = pk5.EncryptedBoxData;
        var hash = Convert.ToHexString(SHA256.HashData(raw)).ToLowerInvariant();

        var requestPath = Path.GetTempFileName();
        try
        {
            File.WriteAllText(requestPath, $$"""
                {
                  "bridge_project_schema": 1,
                  "source_format_name": "{{pk5.GetType().Name}}",
                  "source_raw_payload_base64": "{{Convert.ToBase64String(raw)}}",
                  "source_raw_hash_sha256": "{{hash}}",
                  "target_game": 3,
                  "target_format_name": "PK3",
                  "projection_policy": { "allow_lossy_projection": true },
                  "pre_save_review": {
                    "enabled": true,
                    "apply_static_fields": true,
                    "ot_name": "Ray",
                    "tid16": 2222,
                    "sid16": 3333,
                    "origin_game": 21,
                    "language": {{(int)LanguageID.English}},
                    "met_location_id": 201,
                    "met_level": 5,
                    "ball_id": 4,
                    "pid": 3998925365
                  },
                  "hot_mutable_overlay": {
                    "enabled": true,
                    "species_id": 25,
                    "form_id": 0,
                    "level": 40,
                    "exp": 9000,
                    "nickname": "Pikachu",
                    "is_nicknamed": false,
                    "gender": 1,
                    "shiny": false,
                    "moves": [{"slot_index":0,"move_id":33,"current_pp":35,"pp_ups":0}]
                  }
                }
                """);

            var result = BridgeProject.ProjectFromJsonFile(requestPath);
            Assert.True(result.Success, result.Details ?? result.Error);

            var decoded = Convert.FromBase64String(result.TargetRawPayloadBase64!);
            var pk3 = EntityFormat.GetFromBytes(decoded);
            Assert.NotNull(pk3);
            Assert.Equal(typeof(PK3), pk3!.GetType());
            Assert.Equal("Ray", pk3.OriginalTrainerName);
            Assert.Equal(2222, pk3.TID16);
            Assert.Equal(3333, pk3.SID16);
            Assert.NotEqual(0, pk3.SID16);
        }
        finally
        {
            File.Delete(requestPath);
        }
    }

    [Fact]
    public void Project_PreSaveReview_AppliesNatureAfterCanonicalPidForGen3()
    {
        var pk5 = EntityBlank.GetBlank(typeof(PK5));
        pk5.Species = (int)Species.Pikachu;
        pk5.EXP = 8000;
        pk5.Version = GameVersion.B;
        pk5.Language = (int)LanguageID.English;
        pk5.OriginalTrainerName = "Ray";
        pk5.TID16 = 2222;
        pk5.SID16 = 3333;
        pk5.PID = 3998925365u; // Modest if used directly as Gen 3 PID.
        pk5.Nature = Nature.Sassy;
        pk5.Move1 = (ushort)Move.Tackle;
        pk5.RefreshChecksum();

        var raw = pk5.EncryptedBoxData;
        var hash = Convert.ToHexString(SHA256.HashData(raw)).ToLowerInvariant();

        var requestPath = Path.GetTempFileName();
        try
        {
            File.WriteAllText(requestPath, $$"""
                {
                  "bridge_project_schema": 1,
                  "source_format_name": "{{pk5.GetType().Name}}",
                  "source_raw_payload_base64": "{{Convert.ToBase64String(raw)}}",
                  "source_raw_hash_sha256": "{{hash}}",
                  "target_game": 3,
                  "target_format_name": "PK3",
                  "projection_policy": { "allow_lossy_projection": true },
                  "pre_save_review": {
                    "enabled": true,
                    "apply_static_fields": true,
                    "ot_name": "Ray",
                    "tid16": 2222,
                    "sid16": 3333,
                    "origin_game": 3,
                    "language": {{(int)LanguageID.English}},
                    "pid": 3998925365,
                    "nature": "Sassy"
                  },
                  "hot_mutable_overlay": {
                    "enabled": true,
                    "species_id": 25,
                    "form_id": 0,
                    "level": 40,
                    "exp": 9000,
                    "nickname": "Pikachu",
                    "is_nicknamed": false,
                    "gender": 1,
                    "shiny": false,
                    "moves": [{"slot_index":0,"move_id":33,"current_pp":35,"pp_ups":0}]
                  }
                }
                """);

            var result = BridgeProject.ProjectFromJsonFile(requestPath);
            Assert.True(result.Success, result.Details ?? result.Error);

            var decoded = Convert.FromBase64String(result.TargetRawPayloadBase64!);
            var pk3 = EntityFormat.GetFromBytes(decoded);
            Assert.NotNull(pk3);
            Assert.Equal(typeof(PK3), pk3!.GetType());
            Assert.Equal(Nature.Sassy, pk3.Nature);
            Assert.NotEqual(3998925365u, pk3.PID);
            Assert.False(pk3.IsNicknamed);
        }
        finally
        {
            File.Delete(requestPath);
        }
    }

    [Fact]
    public void Project_PreSaveReview_FallsBackInvalidBallForGen3()
    {
        var pk5 = EntityBlank.GetBlank(typeof(PK5));
        pk5.Species = (int)Species.Pikachu;
        pk5.EXP = 8000;
        pk5.Version = GameVersion.B;
        pk5.Language = (int)LanguageID.English;
        pk5.OriginalTrainerName = "Ray";
        pk5.TID16 = 2222;
        pk5.SID16 = 3333;
        pk5.Ball = 16; // Cherish Ball in modern formats; invalid in Gen 3.
        pk5.Move1 = (ushort)Move.Tackle;
        pk5.RefreshChecksum();

        var raw = pk5.EncryptedBoxData;
        var hash = Convert.ToHexString(SHA256.HashData(raw)).ToLowerInvariant();

        var requestPath = Path.GetTempFileName();
        try
        {
            File.WriteAllText(requestPath, $$"""
                {
                  "bridge_project_schema": 1,
                  "source_format_name": "{{pk5.GetType().Name}}",
                  "source_raw_payload_base64": "{{Convert.ToBase64String(raw)}}",
                  "source_raw_hash_sha256": "{{hash}}",
                  "target_game": 3,
                  "target_format_name": "PK3",
                  "projection_policy": { "allow_lossy_projection": true },
                  "pre_save_review": {
                    "enabled": true,
                    "apply_static_fields": true,
                    "ot_name": "Ray",
                    "tid16": 2222,
                    "sid16": 3333,
                    "origin_game": 3,
                    "language": {{(int)LanguageID.English}},
                    "ball_id": 16
                  }
                }
                """);

            var result = BridgeProject.ProjectFromJsonFile(requestPath);
            Assert.True(result.Success, result.Details ?? result.Error);

            var decoded = Convert.FromBase64String(result.TargetRawPayloadBase64!);
            var pk3 = EntityFormat.GetFromBytes(decoded);
            Assert.NotNull(pk3);
            Assert.Equal(typeof(PK3), pk3!.GetType());
            Assert.Equal(4, pk3.Ball);
        }
        finally
        {
            File.Delete(requestPath);
        }
    }

    [Fact]
    public void Project_FinalIdentity_UnownToPk3PreservesNatureAndForm()
    {
        var pk5 = CreatePk5(Species.Unown, Nature.Adamant);
        pk5.Form = 7;

        var projected = ProjectAndDecode(pk5, 3, "PK3", $$"""
          "pre_save_review": {
            "enabled": true,
            "apply_static_fields": true,
            "ot_name": "Ray",
            "tid16": 2222,
            "sid16": 3333,
            "origin_game": 3,
            "language": {{(int)LanguageID.English}},
            "nature": "Adamant"
          },
          "hot_mutable_overlay": {
            "enabled": true,
            "species_id": {{(int)Species.Unown}},
            "form_id": 7,
            "level": 20,
            "exp": 8000,
            "nickname": "Unown",
            "is_nicknamed": false,
            "gender": 2,
            "shiny": false,
            "moves": [{"slot_index":0,"move_id":33,"current_pp":35,"pp_ups":0}]
          }
        """);

        Assert.Equal(typeof(PK3), projected.GetType());
        Assert.Equal((int)Species.Unown, projected.Species);
        Assert.Equal(Nature.Adamant, projected.Nature);
        Assert.Equal((uint)Nature.Adamant, projected.PID % 25);
        Assert.Equal(7, projected.Form);
    }

    [Fact]
    public void Project_FinalIdentity_NormalPokemonToPk3PreservesNature()
    {
        var pk5 = CreatePk5(Species.Bulbasaur, Nature.Sassy);

        var projected = ProjectAndDecode(pk5, 3, "PK3", $$"""
          "pre_save_review": {
            "enabled": true,
            "nature": "Sassy"
          },
          "hot_mutable_overlay": {
            "enabled": true,
            "species_id": {{(int)Species.Bulbasaur}},
            "form_id": 0,
            "level": 20,
            "exp": 8000,
            "nickname": "Bulbasaur",
            "is_nicknamed": false,
            "gender": 0,
            "shiny": false,
            "moves": [{"slot_index":0,"move_id":33,"current_pp":35,"pp_ups":0}]
          }
        """);

        Assert.Equal(typeof(PK3), projected.GetType());
        Assert.Equal((int)Species.Bulbasaur, projected.Species);
        Assert.Equal(Nature.Sassy, projected.Nature);
        Assert.Equal((uint)Nature.Sassy, projected.PID % 25);
    }

    [Fact]
    public void Project_FinalIdentity_GenderedSpeciesToPk3PreservesGenderAndNature()
    {
        var pk5 = CreatePk5(Species.Eevee, Nature.Calm);

        var projected = ProjectAndDecode(pk5, 3, "PK3", $$"""
          "pre_save_review": {
            "enabled": true,
            "nature": "Calm"
          },
          "hot_mutable_overlay": {
            "enabled": true,
            "species_id": {{(int)Species.Eevee}},
            "form_id": 0,
            "level": 20,
            "exp": 8000,
            "nickname": "Eevee",
            "is_nicknamed": false,
            "gender": 1,
            "shiny": false,
            "moves": [{"slot_index":0,"move_id":33,"current_pp":35,"pp_ups":0}]
          }
        """);

        Assert.Equal(typeof(PK3), projected.GetType());
        Assert.Equal((int)Species.Eevee, projected.Species);
        Assert.Equal(Nature.Calm, projected.Nature);
        Assert.Equal(1, projected.Gender);
        Assert.Equal((uint)Nature.Calm, projected.PID % 25);
    }

    [Fact]
    public void Project_FinalIdentity_ShinyPokemonToPk3PreservesShinyAndNature()
    {
        var pk5 = CreatePk5(Species.Pikachu, Nature.Adamant);
        CommonEdits.SetIsShiny(pk5, true);
        Assert.True(pk5.IsShiny);

        var projected = ProjectAndDecode(pk5, 3, "PK3", $$"""
          "pre_save_review": {
            "enabled": true,
            "ot_name": "Ray",
            "tid16": 2222,
            "sid16": 3333,
            "nature": "Adamant"
          },
          "hot_mutable_overlay": {
            "enabled": true,
            "species_id": {{(int)Species.Pikachu}},
            "form_id": 0,
            "level": 20,
            "exp": 8000,
            "nickname": "Pikachu",
            "is_nicknamed": false,
            "gender": 0,
            "shiny": true,
            "moves": [{"slot_index":0,"move_id":33,"current_pp":35,"pp_ups":0}]
          }
        """);

        Assert.Equal(typeof(PK3), projected.GetType());
        Assert.Equal((int)Species.Pikachu, projected.Species);
        Assert.Equal(Nature.Adamant, projected.Nature);
        Assert.True(projected.IsShiny);
        Assert.Equal((uint)Nature.Adamant, projected.PID % 25);
    }

    [Fact]
    public void Project_FinalNickname_NonNicknamedPk3DefaultDoesNotBecomePk5Nickname()
    {
        var pk5 = CreatePk5(Species.Pikachu, Nature.Hardy);
        pk5.Nickname = "PIKACHU";
        pk5.IsNicknamed = true;
        pk5.RefreshChecksum();

        var projected = ProjectAndDecode(pk5, 15, "PK5", $$"""
          "pre_save_review": {
            "enabled": true,
            "nickname": "PIKACHU",
            "is_nicknamed": false
          },
          "hot_mutable_overlay": {
            "enabled": true,
            "species_id": {{(int)Species.Pikachu}},
            "form_id": 0,
            "level": 20,
            "exp": 8000,
            "nickname": "PIKACHU",
            "is_nicknamed": false,
            "gender": 0,
            "shiny": false,
            "moves": [{"slot_index":0,"move_id":33,"current_pp":35,"pp_ups":0}]
          }
        """);

        Assert.False(projected.IsNicknamed);
        Assert.Equal("Pikachu", projected.Nickname);
        Assert.NotEqual("PIKACHU", projected.Nickname);
    }

    [Fact]
    public void Project_PreSaveReview_ReplaysCanonicalRibbonFlagsOntoPk3Mirror()
    {
        var pk5 = CreatePk5(Species.Pikachu, Nature.Hardy);
        pk5.RefreshChecksum();

        var pk3 = ProjectAndDecode(pk5, 3, "PK3", $$"""
          "pre_save_review": {
            "enabled": true,
            "apply_static_fields": true,
            "ot_name": "Ray",
            "tid16": 2222,
            "sid16": 3333,
            "origin_game": 3,
            "language": {{(int)LanguageID.English}},
            "ribbon_flags": {
              "RibbonChampion": true
            }
          },
          "hot_mutable_overlay": {
            "enabled": true,
            "species_id": {{(int)Species.Pikachu}},
            "form_id": 0,
            "level": 20,
            "exp": 8000,
            "nickname": "Pikachu",
            "is_nicknamed": false,
            "gender": 0,
            "shiny": false,
            "moves": [{"slot_index":0,"move_id":33,"current_pp":35,"pp_ups":0}]
          }
        """);

        // PKHeX names Hoenn Champion RibbonChampionG3; catalog shorthand RibbonChampion maps via bridge aliases.
        var prop = pk3.GetType().GetProperty("RibbonChampionG3", BindingFlags.Public | BindingFlags.Instance);
        Assert.NotNull(prop);
        Assert.Equal(typeof(bool), prop!.PropertyType);
        Assert.True((bool)prop.GetValue(pk3)!);
    }

    [Fact]
    public void Project_PreSaveReview_ReplaysCanonicalRibbonFlagsOntoPk5Return()
    {
        var pk3 = EntityBlank.GetBlank(typeof(PK3));
        pk3.Species = (ushort)Species.Pikachu;
        pk3.Version = GameVersion.E;
        pk3.Language = (int)LanguageID.English;
        pk3.OriginalTrainerName = "Ray";
        pk3.TID16 = 2222;
        pk3.SID16 = 3333;
        pk3.Move1 = (ushort)Move.Tackle;
        pk3.RefreshChecksum();

        var pk5 = ProjectAndDecode(pk3, 15, "PK5", $$"""
          "pre_save_review": {
            "enabled": true,
            "apply_static_fields": true,
            "ot_name": "Ray",
            "tid16": 2222,
            "sid16": 3333,
            "origin_game": 21,
            "language": {{(int)LanguageID.English}},
            "ribbon_flags": {
              "RibbonChampion": true,
              "RibbonEffort": true
            }
          },
          "hot_mutable_overlay": {
            "enabled": true,
            "species_id": {{(int)Species.Pikachu}},
            "form_id": 0,
            "level": 20,
            "exp": 8000,
            "nickname": "Pikachu",
            "is_nicknamed": false,
            "gender": 0,
            "shiny": false,
            "moves": [{"slot_index":0,"move_id":33,"current_pp":35,"pp_ups":0}]
          }
        """);

        Assert.True((bool)pk5.GetType().GetProperty("RibbonChampionG3")!.GetValue(pk5)!);
        Assert.True((bool)pk5.GetType().GetProperty("RibbonEffort")!.GetValue(pk5)!);
    }

    [Fact]
    public void Project_PreSaveReview_RibbonCatalogNumeric_AppliesRibbonCountOnPk3Mirror()
    {
        var pk5 = CreatePk5(Species.Pikachu, Nature.Hardy);
        pk5.RefreshChecksum();

        var pk3 = ProjectAndDecode(pk5, 3, "PK3", $$"""
          "pre_save_review": {
            "enabled": true,
            "apply_static_fields": true,
            "ot_name": "Ray",
            "tid16": 2222,
            "sid16": 3333,
            "origin_game": 3,
            "language": {{(int)LanguageID.English}},
            "ribbon_flags": {
              "RibbonCountG3Cool": 2
            }
          },
          "hot_mutable_overlay": {
            "enabled": true,
            "species_id": {{(int)Species.Pikachu}},
            "form_id": 0,
            "level": 20,
            "exp": 8000,
            "nickname": "Pikachu",
            "is_nicknamed": false,
            "gender": 0,
            "shiny": false,
            "moves": [{"slot_index":0,"move_id":33,"current_pp":35,"pp_ups":0}]
          }
        """);

        var prop = pk3.GetType().GetProperty("RibbonCountG3Cool");
        Assert.NotNull(prop);
        Assert.Equal(2, Convert.ToInt32(prop!.GetValue(pk3)!, System.Globalization.CultureInfo.InvariantCulture));
    }

    [Fact]
    public void Project_PreSaveReview_AppliesCanonicalPokerusOntoPk3Mirror()
    {
        var pk5 = CreatePk5(Species.Pikachu, Nature.Hardy);
        pk5.RefreshChecksum();

        var pk3 = ProjectAndDecode(pk5, 3, "PK3", $$"""
          "pre_save_review": {
            "enabled": true,
            "pokerus_strain": 7,
            "pokerus_days": 3
          },
          "hot_mutable_overlay": {
            "enabled": true,
            "species_id": {{(int)Species.Pikachu}},
            "form_id": 0,
            "level": 20,
            "exp": 8000,
            "nickname": "Pikachu",
            "is_nicknamed": false,
            "gender": 0,
            "shiny": false,
            "moves": [{"slot_index":0,"move_id":33,"current_pp":35,"pp_ups":0}]
          }
        """);

        Assert.Equal(7, GetIntProperty(pk3, "PokerusStrain", "PKRS_Strain", "PokerusState"));
        Assert.Equal(3, GetIntProperty(pk3, "PKRS_Days", "PokerusDays"));
    }

    [Fact]
    public void Import_ReadPokerusDetail_ReportsNonZeroPk3Pokerus()
    {
        var pk3 = EntityBlank.GetBlank(typeof(PK3));
        pk3.Species = (ushort)Species.Pikachu;
        pk3.Version = GameVersion.E;
        pk3.Language = (int)LanguageID.English;
        pk3.OriginalTrainerName = "Ray";
        pk3.TID16 = 2222;
        pk3.SID16 = 3333;
        SetIntProperty(pk3, 7, "PokerusStrain", "PKRS_Strain", "PokerusState");
        SetIntProperty(pk3, 3, "PKRS_Days", "PokerusDays");

        var reader = typeof(BridgeProject).Assembly.GetType("PKHeXBridge.BridgeImportReader", throwOnError: true)!;
        var method = reader.GetMethod("ReadPokerusDetail", BindingFlags.Static | BindingFlags.NonPublic)!;
        var detail = method.Invoke(null, new object[] { pk3 })!;
        using var json = JsonDocument.Parse(JsonSerializer.Serialize(detail));
        var root = json.RootElement;

        Assert.Equal(7, root.GetProperty("strain_or_state").GetInt32());
        Assert.Equal(3, root.GetProperty("days").GetInt32());
        Assert.Equal("infected", root.GetProperty("status").GetString());
    }

    [Fact]
    public void Project_FinalNickname_RealNicknameIsPreservedInPk5Projection()
    {
        var pk5 = CreatePk5(Species.Pikachu, Nature.Hardy);
        pk5.Nickname = "Sparky";
        pk5.IsNicknamed = true;
        pk5.RefreshChecksum();

        var projected = ProjectAndDecode(pk5, 15, "PK5", $$"""
          "pre_save_review": {
            "enabled": true,
            "nickname": "Sparky",
            "is_nicknamed": true
          },
          "hot_mutable_overlay": {
            "enabled": true,
            "species_id": {{(int)Species.Pikachu}},
            "form_id": 0,
            "level": 20,
            "exp": 8000,
            "nickname": "Sparky",
            "is_nicknamed": true,
            "gender": 0,
            "shiny": false,
            "moves": [{"slot_index":0,"move_id":33,"current_pp":35,"pp_ups":0}]
          }
        """);

        Assert.True(projected.IsNicknamed);
        Assert.Equal("Sparky", projected.Nickname);
    }

    [Fact]
    public void Project_FinalNickname_NonNicknamedPk5ToPk3ReturnsToPk5WithoutFakeNickname()
    {
        var pk5 = CreatePk5(Species.Pikachu, Nature.Hardy);
        CommonEdits.ClearNickname(pk5);
        pk5.RefreshChecksum();

        var pk3 = ProjectAndDecode(pk5, 3, "PK3", $$"""
          "pre_save_review": {
            "enabled": true,
            "language": {{(int)LanguageID.English}},
            "nickname": "Pikachu",
            "is_nicknamed": false
          },
          "hot_mutable_overlay": {
            "enabled": true,
            "species_id": {{(int)Species.Pikachu}},
            "form_id": 0,
            "level": 20,
            "exp": 8000,
            "nickname": "Pikachu",
            "is_nicknamed": false,
            "gender": 0,
            "shiny": false,
            "moves": [{"slot_index":0,"move_id":33,"current_pp":35,"pp_ups":0}]
          }
        """);
        Assert.False(pk3.IsNicknamed);
        Assert.Equal("PIKACHU", pk3.Nickname);

        var returned = ProjectAndDecode(pk3, 15, "PK5", $$"""
          "pre_save_review": {
            "enabled": true,
            "language": {{(int)LanguageID.English}},
            "nickname": "{{pk3.Nickname}}",
            "is_nicknamed": false
          },
          "hot_mutable_overlay": {
            "enabled": true,
            "species_id": {{(int)Species.Pikachu}},
            "form_id": 0,
            "level": 20,
            "exp": 8000,
            "nickname": "{{pk3.Nickname}}",
            "is_nicknamed": false,
            "gender": 0,
            "shiny": false,
            "moves": [{"slot_index":0,"move_id":33,"current_pp":35,"pp_ups":0}]
          }
        """);

        Assert.False(returned.IsNicknamed);
        Assert.Equal("Pikachu", returned.Nickname);
        Assert.NotEqual("PIKACHU", returned.Nickname);
    }

    [Fact]
    public void Project_FinalNickname_NonNicknamedUnownPk5ToPk3ReturnsToPk5WithoutFakeNickname()
    {
        var pk5 = CreatePk5(Species.Unown, Nature.Hardy);
        CommonEdits.ClearNickname(pk5);
        pk5.RefreshChecksum();

        var pk3 = ProjectAndDecode(pk5, 3, "PK3", $$"""
          "pre_save_review": {
            "enabled": true,
            "language": {{(int)LanguageID.English}},
            "nickname": "Unown",
            "is_nicknamed": false
          },
          "hot_mutable_overlay": {
            "enabled": true,
            "species_id": {{(int)Species.Unown}},
            "form_id": 0,
            "level": 20,
            "exp": 8000,
            "nickname": "Unown",
            "is_nicknamed": false,
            "gender": 2,
            "shiny": false,
            "moves": [{"slot_index":0,"move_id":33,"current_pp":35,"pp_ups":0}]
          }
        """);
        Assert.False(pk3.IsNicknamed);
        Assert.Equal("UNOWN", pk3.Nickname);

        var returned = ProjectAndDecode(pk3, 15, "PK5", $$"""
          "pre_save_review": {
            "enabled": true,
            "language": {{(int)LanguageID.English}},
            "nickname": "{{pk3.Nickname}}",
            "is_nicknamed": false
          },
          "hot_mutable_overlay": {
            "enabled": true,
            "species_id": {{(int)Species.Unown}},
            "form_id": 0,
            "level": 20,
            "exp": 8000,
            "nickname": "{{pk3.Nickname}}",
            "is_nicknamed": false,
            "gender": 2,
            "shiny": false,
            "moves": [{"slot_index":0,"move_id":33,"current_pp":35,"pp_ups":0}]
          }
        """);

        Assert.False(returned.IsNicknamed);
        Assert.Equal("Unown", returned.Nickname);
        Assert.NotEqual("UNOWN", returned.Nickname);
    }

    [Fact]
    public void Project_FinalNickname_CustomNicknameSurvivesPk5ToPk3ToPk5()
    {
        var pk5 = CreatePk5(Species.Pikachu, Nature.Hardy);
        CommonEdits.SetNickname(pk5, "Sparky");
        pk5.RefreshChecksum();

        var pk3 = ProjectAndDecode(pk5, 3, "PK3", $$"""
          "pre_save_review": {
            "enabled": true,
            "language": {{(int)LanguageID.English}},
            "nickname": "Sparky",
            "is_nicknamed": true
          },
          "hot_mutable_overlay": {
            "enabled": true,
            "species_id": {{(int)Species.Pikachu}},
            "form_id": 0,
            "level": 20,
            "exp": 8000,
            "nickname": "Sparky",
            "is_nicknamed": true,
            "gender": 0,
            "shiny": false,
            "moves": [{"slot_index":0,"move_id":33,"current_pp":35,"pp_ups":0}]
          }
        """);
        Assert.True(pk3.IsNicknamed);
        Assert.Equal("Sparky", pk3.Nickname);

        var returned = ProjectAndDecode(pk3, 15, "PK5", $$"""
          "pre_save_review": {
            "enabled": true,
            "language": {{(int)LanguageID.English}},
            "nickname": "Sparky",
            "is_nicknamed": true
          },
          "hot_mutable_overlay": {
            "enabled": true,
            "species_id": {{(int)Species.Pikachu}},
            "form_id": 0,
            "level": 20,
            "exp": 8000,
            "nickname": "Sparky",
            "is_nicknamed": true,
            "gender": 0,
            "shiny": false,
            "moves": [{"slot_index":0,"move_id":33,"current_pp":35,"pp_ups":0}]
          }
        """);

        Assert.True(returned.IsNicknamed);
        Assert.Equal("Sparky", returned.Nickname);
    }

    [Fact]
    public void Project_FinalNickname_SpeciesNameCanRemainExplicitNicknameWhenCanonicalFlagTrue()
    {
        var pk5 = CreatePk5(Species.Pikachu, Nature.Hardy);
        CommonEdits.SetNickname(pk5, "Pikachu");
        pk5.RefreshChecksum();
        Assert.True(pk5.IsNicknamed);

        var pk3 = ProjectAndDecode(pk5, 3, "PK3", $$"""
          "pre_save_review": {
            "enabled": true,
            "language": {{(int)LanguageID.English}},
            "nickname": "Pikachu",
            "is_nicknamed": true
          },
          "hot_mutable_overlay": {
            "enabled": true,
            "species_id": {{(int)Species.Pikachu}},
            "form_id": 0,
            "level": 20,
            "exp": 8000,
            "nickname": "Pikachu",
            "is_nicknamed": true,
            "gender": 0,
            "shiny": false,
            "moves": [{"slot_index":0,"move_id":33,"current_pp":35,"pp_ups":0}]
          }
        """);
        Assert.Equal("Pikachu", pk3.Nickname);

        var returned = ProjectAndDecode(pk3, 15, "PK5", $$"""
          "pre_save_review": {
            "enabled": true,
            "language": {{(int)LanguageID.English}},
            "nickname": "Pikachu",
            "is_nicknamed": true
          },
          "hot_mutable_overlay": {
            "enabled": true,
            "species_id": {{(int)Species.Pikachu}},
            "form_id": 0,
            "level": 20,
            "exp": 8000,
            "nickname": "Pikachu",
            "is_nicknamed": true,
            "gender": 0,
            "shiny": false,
            "moves": [{"slot_index":0,"move_id":33,"current_pp":35,"pp_ups":0}]
          }
        """);

        Assert.True(returned.IsNicknamed);
        Assert.Equal("Pikachu", returned.Nickname);
    }

    [Fact]
    public void Project_FinalNickname_ClearNicknameUsesTargetLanguageSpeciesDefault()
    {
        var pk5 = CreatePk5(Species.Bulbasaur, Nature.Hardy);
        pk5.Language = (int)LanguageID.German;
        CommonEdits.ClearNickname(pk5);
        pk5.RefreshChecksum();

        var pk3 = ProjectAndDecode(pk5, 3, "PK3", $$"""
          "pre_save_review": {
            "enabled": true,
            "language": {{(int)LanguageID.German}},
            "nickname": "{{pk5.Nickname}}",
            "is_nicknamed": false
          },
          "hot_mutable_overlay": {
            "enabled": true,
            "species_id": {{(int)Species.Bulbasaur}},
            "form_id": 0,
            "level": 20,
            "exp": 8000,
            "nickname": "{{pk5.Nickname}}",
            "is_nicknamed": false,
            "gender": 0,
            "shiny": false,
            "moves": [{"slot_index":0,"move_id":33,"current_pp":35,"pp_ups":0}]
          }
        """);
        var expectedPk3 = EntityBlank.GetBlank(typeof(PK3));
        expectedPk3.Species = (ushort)Species.Bulbasaur;
        expectedPk3.Language = (int)LanguageID.German;
        CommonEdits.ClearNickname(expectedPk3);

        Assert.False(pk3.IsNicknamed);
        Assert.Equal(expectedPk3.Nickname, pk3.Nickname);

        var returned = ProjectAndDecode(pk3, 15, "PK5", $$"""
          "pre_save_review": {
            "enabled": true,
            "language": {{(int)LanguageID.German}},
            "nickname": "{{pk3.Nickname}}",
            "is_nicknamed": false
          },
          "hot_mutable_overlay": {
            "enabled": true,
            "species_id": {{(int)Species.Bulbasaur}},
            "form_id": 0,
            "level": 20,
            "exp": 8000,
            "nickname": "{{pk3.Nickname}}",
            "is_nicknamed": false,
            "gender": 0,
            "shiny": false,
            "moves": [{"slot_index":0,"move_id":33,"current_pp":35,"pp_ups":0}]
          }
        """);
        var expectedPk5 = EntityBlank.GetBlank(typeof(PK5));
        expectedPk5.Species = (ushort)Species.Bulbasaur;
        expectedPk5.Language = (int)LanguageID.German;
        CommonEdits.ClearNickname(expectedPk5);

        Assert.False(returned.IsNicknamed);
        Assert.Equal(expectedPk5.Nickname, returned.Nickname);
    }

    private static PKM CreatePk5(Species species, Nature nature)
    {
        var pk5 = EntityBlank.GetBlank(typeof(PK5));
        pk5.Species = (ushort)species;
        pk5.EXP = 8000;
        pk5.Version = GameVersion.B;
        pk5.Language = (int)LanguageID.English;
        pk5.OriginalTrainerName = "Ray";
        pk5.TID16 = 2222;
        pk5.SID16 = 3333;
        pk5.Nature = nature;
        pk5.StatNature = nature;
        pk5.Move1 = (ushort)Move.Tackle;
        pk5.Move1_PP = 35;
        pk5.RefreshChecksum();
        return pk5;
    }

    private static PKM ProjectAndDecode(PKM source, int targetGame, string targetFormatName, string extraJson)
    {
        var raw = source.EncryptedBoxData;
        var hash = Convert.ToHexString(SHA256.HashData(raw)).ToLowerInvariant();
        var requestPath = Path.GetTempFileName();
        try
        {
            File.WriteAllText(requestPath, $$"""
                {
                  "bridge_project_schema": 1,
                  "source_format_name": "{{source.GetType().Name}}",
                  "source_raw_payload_base64": "{{Convert.ToBase64String(raw)}}",
                  "source_raw_hash_sha256": "{{hash}}",
                  "target_game": {{targetGame}},
                  "target_format_name": "{{targetFormatName}}",
                  "projection_policy": { "allow_lossy_projection": true },
                  {{extraJson}}
                }
                """);

            var result = BridgeProject.ProjectFromJsonFile(requestPath);
            Assert.True(result.Success, result.Details ?? result.Error);
            Assert.Equal(targetFormatName, result.TargetFormatName);
            var decoded = Convert.FromBase64String(result.TargetRawPayloadBase64!);
            var projected = DecodeExactTarget(decoded, targetFormatName);
            Assert.NotNull(projected);
            return projected!;
        }
        finally
        {
            File.Delete(requestPath);
        }
    }

    private static PKM? DecodeExactTarget(byte[] decoded, string targetFormatName) => targetFormatName.ToUpperInvariant() switch
    {
        "PK3" => new PK3(decoded),
        "PK4" => new PK4(decoded),
        "PK5" => new PK5(decoded),
        "PK6" => new PK6(decoded),
        _ => EntityFormat.GetFromBytes(decoded)
    };

    private static int GetIntProperty(PKM pk, params string[] names)
    {
        foreach (var name in names)
        {
            var prop = pk.GetType().GetProperty(name, BindingFlags.Public | BindingFlags.Instance);
            if (prop is not null && prop.GetValue(pk) is { } value)
                return Convert.ToInt32(value, System.Globalization.CultureInfo.InvariantCulture);
        }
        throw new InvalidOperationException($"None of these properties exist on {pk.GetType().Name}: {string.Join(", ", names)}");
    }

    private static void SetIntProperty(PKM pk, int value, params string[] names)
    {
        foreach (var name in names)
        {
            var prop = pk.GetType().GetProperty(name, BindingFlags.Public | BindingFlags.Instance);
            if (prop is not null && prop.CanWrite)
            {
                prop.SetValue(pk, Convert.ChangeType(value, prop.PropertyType, System.Globalization.CultureInfo.InvariantCulture));
                return;
            }
        }
        throw new InvalidOperationException($"None of these properties are writable on {pk.GetType().Name}: {string.Join(", ", names)}");
    }
}
