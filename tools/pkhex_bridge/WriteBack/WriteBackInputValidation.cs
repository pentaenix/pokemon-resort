namespace PKHeXBridge.WriteBack;

/// <summary>
/// Bounds and sanity checks for paths and projection payloads (DoS / accidental huge reads).
/// </summary>
public static class WriteBackInputValidation
{
    public const long MaxProjectionFileBytes = 64 * 1024 * 1024;

    public static bool HasEmbeddedNull(string path) => path.AsSpan().Contains('\0');

    public static bool TryValidateProjectionFileSize(string projectionPath, out string error)
    {
        error = "";
        try
        {
            var len = new FileInfo(projectionPath).Length;
            if (len > MaxProjectionFileBytes)
            {
                error = $"projection_file_too_large bytes={len} max={MaxProjectionFileBytes}";
                return false;
            }

            return true;
        }
        catch (Exception ex)
        {
            error = ex.Message;
            return false;
        }
    }
}
