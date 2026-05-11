using PKHeX.Core;

namespace PKHeXBridge;

internal static class PkmEncryptedExport
{
    internal static byte[] GetStored(PKM pkm)
    {
        var buf = new byte[pkm.SIZE_STORED];
        pkm.WriteEncryptedDataStored(buf);
        return buf;
    }

    internal static byte[] GetParty(PKM pkm)
    {
        var buf = new byte[pkm.SIZE_PARTY];
        pkm.WriteEncryptedDataParty(buf);
        return buf;
    }
}
