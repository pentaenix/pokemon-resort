using PKHeX.Core;

namespace PKHeXBridge;

internal static class BridgeProjectFutureProjection
{
    internal static bool ShouldUseManualFutureProjection(PKM source, Type destType)
    {
        var blank = EntityBlank.GetBlank(destType);
        return blank.Format is 5 or 6 && source.Format < blank.Format;
    }

    internal static PKM ProjectToFutureGeneration(PKM source, Type destType, int targetGame)
    {
        var blank = EntityBlank.GetBlank(destType);
        return blank.Format switch
        {
            5 => ProjectToGeneration5(source, destType, targetGame),
            6 => ProjectToGeneration6(source, destType, targetGame),
            _ => throw new NotSupportedException($"Manual future projection does not support format {blank.Format}.")
        };
    }

    internal static PKM ProjectToGeneration5(PKM source, Type destType, int targetGame)
    {
        var projected = EntityBlank.GetBlank(destType);
        projected.Species = source.Species;
        projected.Form = source.Form;
        projected.EXP = source.EXP;
        projected.Language = NormalizeLanguage(source.Language);
        projected.Version = targetGame > 0 ? (GameVersion)targetGame : GameVersion.B;
        projected.TID16 = source.TID16;
        projected.SID16 = source.SID16;
        projected.OriginalTrainerName = string.IsNullOrWhiteSpace(source.OriginalTrainerName)
            ? "RESORT"
            : source.OriginalTrainerName;
        projected.PID = source.PID == 0 ? GenerateDeterministicPid(source) : source.PID;
        projected.Nature = source.Nature;
        projected.Gender = source.Gender;
        projected.IV_HP = source.IV_HP;
        projected.IV_ATK = source.IV_ATK;
        projected.IV_DEF = source.IV_DEF;
        projected.IV_SPA = source.IV_SPA;
        projected.IV_SPD = source.IV_SPD;
        projected.IV_SPE = source.IV_SPE;
        projected.EV_HP = source.EV_HP;
        projected.EV_ATK = source.EV_ATK;
        projected.EV_DEF = source.EV_DEF;
        projected.EV_SPA = source.EV_SPA;
        projected.EV_SPD = source.EV_SPD;
        projected.EV_SPE = source.EV_SPE;
        projected.Move1 = source.Move1;
        projected.Move2 = source.Move2;
        projected.Move3 = source.Move3;
        projected.Move4 = source.Move4;
        projected.Move1_PP = source.Move1_PP;
        projected.Move2_PP = source.Move2_PP;
        projected.Move3_PP = source.Move3_PP;
        projected.Move4_PP = source.Move4_PP;
        projected.Move1_PPUps = source.Move1_PPUps;
        projected.Move2_PPUps = source.Move2_PPUps;
        projected.Move3_PPUps = source.Move3_PPUps;
        projected.Move4_PPUps = source.Move4_PPUps;
        projected.Ball = source.Ball == 0 ? (byte)4 : source.Ball;
        projected.MetLocation = 30001;
        projected.MetLevel = (byte)Math.Clamp((int)source.CurrentLevel, 1, 100);
        projected.MetDate = DateOnly.FromDateTime(DateTime.UtcNow);
        projected.IsEgg = false;
        projected.EggLocation = 0;
        projected.RefreshChecksum();
        return projected;
    }

    private static PKM ProjectToGeneration6(PKM source, Type destType, int targetGame)
    {
        var projected = ProjectToGeneration5(source, destType, targetGame);
        projected.EncryptionConstant = projected.PID;
        projected.RefreshChecksum();
        return projected;
    }

    private static int NormalizeLanguage(int language)
    {
        return language is >= (int)LanguageID.Japanese and <= (int)LanguageID.Spanish
            ? language
            : (int)LanguageID.English;
    }

    private static uint GenerateDeterministicPid(PKM source)
    {
        var seed = unchecked((uint)(((source.Species & 0xffff) << 16) ^ source.TID16 ^ ((int)source.Nature << 24)));
        seed = unchecked((seed * 0x41C64E6Du) + 0x6073u);
        return seed == 0 ? 1u : seed;
    }

}
