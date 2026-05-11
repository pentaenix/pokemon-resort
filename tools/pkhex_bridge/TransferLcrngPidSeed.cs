using PKHeX.Core;
using static PKHeX.Core.PIDType;

namespace PKHeXBridge;

/// <summary>
/// Gen III/IV LCRNG PID+IV application for transfer repair. PKHeX 26 removed <c>PIDGenerator.SetValuesFromSeed</c>;
/// this mirrors the former <c>SetValuesFromSeedLCRNG</c> path for Method 1/2/3/4 (incl. Unown and roamer).
/// </summary>
internal static class TransferLcrngPidSeed
{
    internal static void Apply(PKM pk, PIDType type, uint seed)
    {
        var a16 = LCRNG.Next16(ref seed);
        var b16 = LCRNG.Next16(ref seed);
        if (type is Method_3 or Method_3_Unown)
            b16 = LCRNG.Next16(ref seed);

        var unown = type is Method_1_Unown or Method_2_Unown or Method_3_Unown or Method_4_Unown;
        pk.PID = unown ? (a16 << 16) | b16 : (b16 << 16) | a16;

        var c16 = LCRNG.Next15(ref seed);
        if (type is Method_2 or Method_2_Unown)
            c16 = LCRNG.Next15(ref seed);

        var d16 = LCRNG.Next15(ref seed);
        if (type is Method_4 or Method_4_Unown)
            d16 = LCRNG.Next15(ref seed);

        var iv32 = (d16 << 15) | c16;
        if (type == Method_1_Roamer)
            iv32 &= 0xFF;

        pk.SetIVs(iv32);
    }
}
