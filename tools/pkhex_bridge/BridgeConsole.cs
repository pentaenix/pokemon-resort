using System.Text.Json;

namespace PKHeXBridge;

public static class BridgeConsole
{
    private static readonly JsonSerializerOptions ImportJsonOptions = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.SnakeCaseLower
    };

    public static int Run(string[] args, TextWriter stdout)
    {
        if (args.Length < 1)
        {
            return WriteResult(
                stdout,
                new BridgeProbeResult
                {
                    Success = false,
                    Error = "missing_argument",
                    Details = "Expected a save file path."
                });
        }

        if (args[0] == "import")
        {
            if (args.Length < 2)
            {
                return WriteImportResult(
                    stdout,
                    new BridgeImportResult
                    {
                        Success = false,
                        Error = "missing_argument",
                        Details = "Expected a save file path after import."
                    });
            }

            var importResult = BridgeImport.Import(args[1]);
            return WriteImportResult(stdout, importResult);
        }

        if (args[0] == "write-projection")
        {
            if (args.Length < 3)
            {
                return WriteWriteBackResult(
                    stdout,
                    new BridgeWriteBackResult
                    {
                        Success = false,
                        Status = "error",
                        Error = "missing_argument",
                        Details = "Expected write-projection <save-path> <projection-json-path>."
                    });
            }

            var writeResult = BridgeWriteBack.WriteProjection(args[1], args[2]);
            return WriteWriteBackResult(stdout, writeResult);
        }

        if (args[0] == "pkm-inspect")
        {
            if (args.Length < 2)
            {
                return WritePkmInspectResult(
                    stdout,
                    new BridgePkmInspectResult
                    {
                        Success = false,
                        Status = "error",
                        Error = "missing_argument",
                        Details = "Expected pkm-inspect <pkm-path> [source-game]."
                    });
            }

            var sourceGame = 0;
            if (args.Length >= 3)
                _ = int.TryParse(args[2], out sourceGame);
            var inspectResult = BridgePkmInspect.Inspect(args[1], sourceGame);
            return WritePkmInspectResult(stdout, inspectResult);
        }

        if (args[0] == "pkm-patch-held-item")
        {
            if (args.Length < 2)
            {
                return WriteHeldItemPatchResult(
                    stdout,
                    new PkmHeldItemPatchResult
                    {
                        Success = false,
                        Error = "missing_argument",
                        Details = "Expected pkm-patch-held-item <input-json-path>."
                    });
            }

            var patchResult = PkmHeldItemPatch.ApplyFromJsonFile(args[1]);
            return WriteHeldItemPatchResult(stdout, patchResult);
        }

        if (args[0] == "project")
        {
            if (args.Length < 2)
            {
                return WriteProjectResult(
                    stdout,
                    new BridgeProjectResult
                    {
                        Success = false,
                        Status = "error",
                        Error = "missing_argument",
                        Details = "Expected project <input-json-path>."
                    });
            }

            var projectResult = BridgeProject.ProjectFromJsonFile(args[1]);
            return WriteProjectResult(stdout, projectResult);
        }

        var result = BridgeProbe.Probe(args[0]);
        return WriteResult(stdout, result);
    }

    private static int WriteResult(TextWriter stdout, BridgeProbeResult result)
    {
        stdout.WriteLine(JsonSerializer.Serialize(
            new
            {
                // Bumped when probe JSON shape changes; native code rejects unknown/stale bridges for transfer UI.
                bridge_probe_schema = 5,
                success = result.Success,
                game_id = result.GameId,
                player_name = result.PlayerName,
                party = result.Party,
                box_1 = result.Box1,
                play_time = result.PlayTime,
                pokedex_count = result.PokedexCount,
                badges = result.Badges,
                trainer = result.Trainer,
                pokedex = result.Pokedex,
                all_pokemon = result.AllPokemon,
                boxes = result.Boxes,
                bag = result.Bag,
                status = result.Status,
                saveType = result.SaveType,
                game = result.Game,
                trainerName = result.TrainerName,
                error = result.Error,
                details = result.Details
            }));
        return result.Success ? 0 : 1;
    }

    private static int WriteImportResult(TextWriter stdout, BridgeImportResult result)
    {
        stdout.WriteLine(JsonSerializer.Serialize(
            new
            {
                bridge_import_schema = 1,
                success = result.Success,
                pokemon = result.Pokemon,
                status = result.Status,
                error = result.Error,
                details = result.Details
            },
            ImportJsonOptions));
        return result.Success ? 0 : 1;
    }

    private static int WriteWriteBackResult(TextWriter stdout, BridgeWriteBackResult result)
    {
        stdout.WriteLine(JsonSerializer.Serialize(
            new
            {
                bridge_write_schema = 1,
                success = result.Success,
                status = result.Status,
                error = result.Error,
                details = result.Details,
                init_backup_created = result.InitBackupCreated,
                init_backup_path = result.InitBackupPath,
                rolling_backup_path = result.RollingBackupPath,
                restored_from_rolling_backup = result.RestoredFromRollingBackup
            },
            ImportJsonOptions));
        return result.Success ? 0 : 1;
    }

    private static int WritePkmInspectResult(TextWriter stdout, BridgePkmInspectResult result)
    {
        stdout.WriteLine(JsonSerializer.Serialize(
            new
            {
                bridge_pkm_inspect_schema = 1,
                bridge_import_schema = 1,
                success = result.Success,
                pokemon = result.Pokemon is null ? null : new[] { result.Pokemon },
                status = result.Status,
                error = result.Error,
                details = result.Details
            },
            ImportJsonOptions));
        return result.Success ? 0 : 1;
    }

    private static int WriteHeldItemPatchResult(TextWriter stdout, PkmHeldItemPatchResult result)
    {
        stdout.WriteLine(JsonSerializer.Serialize(
            new
            {
                bridge_held_item_patch_schema = 1,
                success = result.Success,
                raw_payload_base64 = result.RawPayloadBase64,
                raw_hash_sha256 = result.RawHashSha256,
                error = result.Error,
                details = result.Details
            },
            ImportJsonOptions));
        return result.Success ? 0 : 1;
    }

    private static int WriteProjectResult(TextWriter stdout, BridgeProjectResult result)
    {
        stdout.WriteLine(JsonSerializer.Serialize(
            new
            {
                bridge_project_schema = 1,
                success = result.Success,
                target_pid = result.TargetPid,
                target_format_name = result.TargetFormatName,
                target_raw_payload_base64 = result.TargetRawPayloadBase64,
                target_raw_hash_sha256 = result.TargetRawHashSha256,
                legality = result.Legality,
                loss_manifest = result.LossManifest,
                beacon = result.Beacon,
                status = result.Status,
                error = result.Error,
                details = result.Details
            },
            ImportJsonOptions));
        return result.Success ? 0 : 1;
    }
}
