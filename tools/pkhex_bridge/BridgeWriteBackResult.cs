namespace PKHeXBridge;

public sealed class BridgeWriteBackResult
{
    public bool Success { get; init; }
    public string? Status { get; init; }
    public string? Error { get; init; }
    public string? Details { get; init; }
    /// <summary>True if this run created the immutable first snapshot (.initbak).</summary>
    public bool InitBackupCreated { get; init; }
    public string? InitBackupPath { get; init; }
    /// <summary>Overwritable pre-write copy (.bak), updated on every successful write attempt.</summary>
    public string? RollingBackupPath { get; init; }
    /// <summary>
    /// If true, a failed write re-copied the rolling backup onto the live save path so the external file
    /// was not left in a partial or unknown state.
    /// </summary>
    public bool RestoredFromRollingBackup { get; init; }
}
