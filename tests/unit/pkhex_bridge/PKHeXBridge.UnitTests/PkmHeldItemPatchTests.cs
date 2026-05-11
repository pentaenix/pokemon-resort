using PKHeX.Core;
using PKHeXBridge;
using Xunit;

namespace PKHeXBridge.UnitTests;

public class PkmHeldItemPatchTests
{
    [Fact]
    public void ApplyFromPayload_Succeeds_AndUpdatesHeldItem_ForPk3()
    {
        var pk = new PK3
        {
            Species = 1,
            HeldItem = 0,
        };

        var raw = PkmEncryptedExport.GetStored(pk);
        Assert.NotEmpty(raw);

        var b64 = Convert.ToBase64String(raw);
        const int newItem = 13;
        var result = PkmHeldItemPatch.ApplyFromPayload(b64, newItem);

        Assert.True(result.Success);
        Assert.False(string.IsNullOrEmpty(result.RawPayloadBase64));
        Assert.False(string.IsNullOrEmpty(result.RawHashSha256));

        var decoded = Convert.FromBase64String(result.RawPayloadBase64!);
        var pk2 = EntityFormat.GetFromBytes(decoded);
        Assert.NotNull(pk2);
        Assert.Equal(newItem, pk2.HeldItem);
    }

    [Fact]
    public void ApplyFromPayload_Fails_OnBadBase64()
    {
        var result = PkmHeldItemPatch.ApplyFromPayload("%%%not-base64@@@", 1);
        Assert.False(result.Success);
        Assert.Equal("bad_base64", result.Error);
    }
}
