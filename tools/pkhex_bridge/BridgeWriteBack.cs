using System.Text.Json;
using PKHeX.Core;
using PKHeXBridge.WriteBack;

namespace PKHeXBridge;

/// <summary>
/// Orchestrates validated write-back from JSON projections. Per-domain apply logic lives under
/// <see cref="WriteBack"/> (e.g. <see cref="PcBoxProjectionApplier"/>, future item/bag appliers).
/// </summary>
public static class BridgeWriteBack
{
    public static BridgeWriteBackResult WriteProjection(string savePath, string projectionPath)
    {
        if (string.IsNullOrWhiteSpace(savePath) || WriteBackInputValidation.HasEmbeddedNull(savePath))
        {
            return new BridgeWriteBackResult
            {
                Success = false,
                Status = "error",
                Error = "invalid_save_path",
                Details = "save path is empty or invalid"
            };
        }

        if (string.IsNullOrWhiteSpace(projectionPath) || WriteBackInputValidation.HasEmbeddedNull(projectionPath))
        {
            return new BridgeWriteBackResult
            {
                Success = false,
                Status = "error",
                Error = "invalid_projection_path",
                Details = "projection path is empty or invalid"
            };
        }

        if (!File.Exists(savePath))
        {
            return new BridgeWriteBackResult
            {
                Success = false,
                Status = "error",
                Error = "missing_save",
                Details = savePath
            };
        }

        if (!File.Exists(projectionPath))
        {
            return new BridgeWriteBackResult
            {
                Success = false,
                Status = "error",
                Error = "missing_projection",
                Details = projectionPath
            };
        }

        if (!WriteBackInputValidation.TryValidateProjectionFileSize(projectionPath, out var sizeError))
        {
            return new BridgeWriteBackResult
            {
                Success = false,
                Status = "error",
                Error = "projection_too_large",
                Details = sizeError
            };
        }

        try
        {
            var sav = SaveUtil.GetVariantSAV(savePath);
            if (sav is null)
            {
                return new BridgeWriteBackResult
                {
                    Success = false,
                    Status = "unsupported",
                    Error = "unsupported_save",
                    Details = savePath
                };
            }

            using var document = JsonDocument.Parse(File.ReadAllText(projectionPath));
            if (!document.RootElement.TryGetProperty("projection_schema", out var schemaEl) ||
                schemaEl.ValueKind != JsonValueKind.Number)
            {
                return new BridgeWriteBackResult
                {
                    Success = false,
                    Status = "error",
                    Error = "unsupported_projection_schema",
                    Details = projectionPath
                };
            }

            var schema = schemaEl.GetInt32();
            if (schema is < 1 or > 2)
            {
                return new BridgeWriteBackResult
                {
                    Success = false,
                    Status = "error",
                    Error = "unsupported_projection_schema",
                    Details = $"schema={schema}"
                };
            }

            string backupDir;
            try
            {
                backupDir = TransferWriteBackupPaths.BackupDirectoryForProjection(projectionPath);
                Directory.CreateDirectory(backupDir);
            }
            catch (Exception ex)
            {
                return new BridgeWriteBackResult
                {
                    Success = false,
                    Status = "error",
                    Error = "backup_dir_failed",
                    Details = ex.Message
                };
            }

            var saveKey = TransferWriteBackupPaths.StableSaveKey(savePath);
            var (initBackupPath, rollingBackupPath) = TransferWriteBackupPaths.InitAndRollingPaths(backupDir, saveKey);

            var initBackupCreated = false;
            try
            {
                if (!File.Exists(initBackupPath))
                {
                    File.Copy(savePath, initBackupPath, overwrite: false);
                    initBackupCreated = true;
                }

                File.Copy(savePath, rollingBackupPath, overwrite: true);
            }
            catch (Exception ex)
            {
                return new BridgeWriteBackResult
                {
                    Success = false,
                    Status = "error",
                    Error = "backup_failed",
                    Details = ex.Message,
                    InitBackupCreated = false,
                    InitBackupPath = initBackupPath,
                    RollingBackupPath = rollingBackupPath
                };
            }

            try
            {
                if (schema >= 2 &&
                    document.RootElement.TryGetProperty("pc_boxes", out var pcBoxes) &&
                    pcBoxes.ValueKind == JsonValueKind.Array)
                {
                    PcBoxProjectionApplier.Apply(sav, pcBoxes);
                }

                if (document.RootElement.TryGetProperty("box_names", out var boxNames) &&
                    boxNames.ValueKind == JsonValueKind.Array)
                {
                    BoxNameProjectionApplier.Apply(sav, boxNames);
                }
            }
            catch (Exception ex)
            {
                return new BridgeWriteBackResult
                {
                    Success = false,
                    Status = "error",
                    Error = "projection_apply_failed",
                    Details = ex.Message,
                    InitBackupCreated = initBackupCreated,
                    InitBackupPath = initBackupPath,
                    RollingBackupPath = rollingBackupPath
                };
            }

            if (!SaveFileAtomicWriter.TryWrite(sav, savePath, rollingBackupPath, out var writeError,
                    out var restoredFromRolling))
            {
                return new BridgeWriteBackResult
                {
                    Success = false,
                    Status = "error",
                    Error = "save_write_failed",
                    Details = writeError,
                    InitBackupCreated = initBackupCreated,
                    InitBackupPath = initBackupPath,
                    RollingBackupPath = rollingBackupPath,
                    RestoredFromRollingBackup = restoredFromRolling
                };
            }

            return new BridgeWriteBackResult
            {
                Success = true,
                Status = "ok",
                InitBackupCreated = initBackupCreated,
                InitBackupPath = initBackupPath,
                RollingBackupPath = rollingBackupPath
            };
        }
        catch (Exception ex)
        {
            return new BridgeWriteBackResult
            {
                Success = false,
                Status = "error",
                Error = "exception",
                Details = ex.Message
            };
        }
    }
}
