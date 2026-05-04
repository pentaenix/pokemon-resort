using System.Security.Cryptography;
using System.Text;

namespace PKHeXBridge.WriteBack;

/// <summary>
/// Resolves persistent-storage backup folder and per-save filenames (hash of canonical save path).
/// </summary>
public static class TransferWriteBackupPaths
{
    public const string BackupSubfolderName = "transfer_write_backups";

    public static string BackupDirectoryForProjection(string projectionPath)
    {
        var projFull = Path.GetFullPath(projectionPath);
        var projDir = Path.GetDirectoryName(projFull);
        if (string.IsNullOrEmpty(projDir))
            throw new InvalidOperationException("projection_directory_missing");

        return Path.Combine(projDir, BackupSubfolderName);
    }

    /// <summary>Stable filename stem for backup files (SHA-256 hex of full normalized save path).</summary>
    public static string StableSaveKey(string savePath)
    {
        var full = Path.GetFullPath(savePath);
        var hash = SHA256.HashData(Encoding.UTF8.GetBytes(full));
        return Convert.ToHexString(hash).ToLowerInvariant();
    }

    public static (string InitPath, string RollingPath) InitAndRollingPaths(string backupDir, string saveKey)
    {
        var init = Path.Combine(backupDir, saveKey + ".initbak");
        var rolling = Path.Combine(backupDir, saveKey + ".bak");
        return (init, rolling);
    }
}
