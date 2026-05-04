using PKHeX.Core;

namespace PKHeXBridge.WriteBack;

/// <summary>
/// Durably writes serialized save bytes via a temp file, verifies on-disk integrity, then replaces the live path.
/// On replace failure, restores <paramref name="rollingBackupPath"/> onto the live save when possible.
/// </summary>
public static class SaveFileAtomicWriter
{
    /// <summary>Reject absurd exports (corruption or hostile projection).</summary>
    public const long MaxSerializedSaveBytes = 64 * 1024 * 1024;

    public static bool TryWrite(
        SaveFile sav,
        string savePath,
        string rollingBackupPath,
        out string error,
        out bool restoredFromRolling)
    {
        error = "";
        restoredFromRolling = false;

        byte[] bytes;
        try
        {
            var exported = sav.Write();
            bytes = exported.ToArray();
            if (bytes.Length == 0)
            {
                error = "Write() returned empty buffer";
                return false;
            }

            if (bytes.Length > MaxSerializedSaveBytes)
            {
                error = $"serialized_save_too_large bytes={bytes.Length} max={MaxSerializedSaveBytes}";
                return false;
            }
        }
        catch (Exception ex)
        {
            error = ex.Message;
            return false;
        }

        var fullSave = Path.GetFullPath(savePath);
        var dir = Path.GetDirectoryName(fullSave);
        if (string.IsNullOrEmpty(dir))
        {
            error = "save_path_has_no_directory";
            return false;
        }

        DeleteStalePartialWrites(dir, fullSave);

        var fileName = Path.GetFileName(fullSave);
        var tmpPath = Path.Combine(dir, fileName + ".prtmp_" + Guid.NewGuid().ToString("N"));

        try
        {
            WriteTempDurably(tmpPath, bytes);
        }
        catch (Exception ex)
        {
            error = ex.Message;
            TryDeleteFile(tmpPath);
            return false;
        }

        if (!VerifyTempMatchesExpected(tmpPath, bytes, out var verifyError))
        {
            error = verifyError;
            TryDeleteFile(tmpPath);
            return false;
        }

        try
        {
            File.Move(tmpPath, fullSave, overwrite: true);
            return true;
        }
        catch (Exception ex)
        {
            error = ex.Message;
            TryDeleteFile(tmpPath);
            if (TryCopyFile(rollingBackupPath, fullSave, out var restoreEx))
            {
                restoredFromRolling = true;
                error += " (reverted to pre-write copy from rolling backup)";
            }
            else
            {
                error += " | critical: could not restore save from rolling backup: " + restoreEx;
            }

            return false;
        }
    }

    private static void WriteTempDurably(string tmpPath, byte[] bytes)
    {
        // WriteThrough: reduce window where OS buffers loss before rename (especially Windows).
        var options = OperatingSystem.IsWindows() ? FileOptions.WriteThrough : FileOptions.None;
        using (var fs = new FileStream(tmpPath, FileMode.Create, FileAccess.Write, FileShare.None, 4096, options))
        {
            fs.Write(bytes);
            fs.Flush(flushToDisk: true);
        }
    }

    private static bool VerifyTempMatchesExpected(string tmpPath, byte[] expectedBytes, out string verifyError)
    {
        verifyError = "";
        try
        {
            var fi = new FileInfo(tmpPath);
            if (!fi.Exists || fi.Length != expectedBytes.Length)
            {
                verifyError =
                    $"temp_length_mismatch expected={expectedBytes.Length} actual={(fi.Exists ? fi.Length : -1)}";
                return false;
            }

            var diskBytes = File.ReadAllBytes(tmpPath);
            if (!expectedBytes.AsSpan().SequenceEqual(diskBytes))
            {
                verifyError = "temp_byte_mismatch_after_write";
                return false;
            }

            return true;
        }
        catch (Exception ex)
        {
            verifyError = ex.Message;
            return false;
        }
    }

    private static void DeleteStalePartialWrites(string saveDirectory, string fullSavePath)
    {
        var fileName = Path.GetFileName(fullSavePath);
        if (string.IsNullOrEmpty(fileName))
        {
            return;
        }

        try
        {
            foreach (var path in Directory.EnumerateFiles(saveDirectory, fileName + ".prtmp*"))
            {
                TryDeleteFile(path);
            }
        }
        catch
        {
            // best-effort cleanup
        }
    }

    private static void TryDeleteFile(string path)
    {
        try
        {
            if (File.Exists(path))
            {
                File.Delete(path);
            }
        }
        catch
        {
        }
    }

    private static bool TryCopyFile(string sourcePath, string destPath, out string copyError)
    {
        copyError = "";
        try
        {
            File.Copy(sourcePath, destPath, overwrite: true);
            return true;
        }
        catch (Exception ex)
        {
            copyError = ex.Message;
            return false;
        }
    }
}
