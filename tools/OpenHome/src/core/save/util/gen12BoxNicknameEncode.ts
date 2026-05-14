import { Language, Lookup } from '@pkm-rs/pkg'

/**
 * Gen I–II box / party nickname blocks are **only** an 11-byte 0x50-terminated Game Boy character string.
 * There is **no nicknamed flag** in the save — tools like PKHeX infer "nicknamed" by comparing those bytes
 * to the canonical **default species name** for that game/language (INT English uses **ALL CAPS**).
 *
 * Our pipeline often keeps UTF-16 display names in **title case** (`Slowbro`). Encoding that literally with
 * {@link utf16StringToGen12} uses **lowercase** GB letter codes (0xA0+) instead of **uppercase** (0x80+),
 * which no longer matches the cartridge default — **every** default-named Pokémon then appears nicknamed.
 *
 * Before writing Gen12 bytes, map "species-default" spellings (case/trim-insensitive) to **uppercase ASCII**
 * so the stored bytes match the original games and PKHeX legality.
 */
export function gen12BoxNicknameForEncode(
  nationalDex: number,
  language: Language,
  nickname: string | undefined
): string {
  const nick = nickname?.trim() ?? ''
  if (!nick) return ''
  const lang = language === Language.None ? Language.English : language
  const species = Lookup.speciesName(nationalDex, lang)?.trim() ?? ''
  if (!species) return nickname ?? ''
  if (nick.toLowerCase() === species.toLowerCase()) {
    return species.toUpperCase()
  }
  return nickname ?? ''
}
