/**
 * Gen I/II trainer names are fixed-length 8-character fields.
 * Keep the projected OT name legal by trimming anything longer than the cartridge can store.
 */
export function gen12TrainerNameForEncode(name: string | undefined): string {
  const ot = name?.trim() ?? ''
  if (!ot) return ''
  return ot.slice(0, 8)
}
