import { PKMInterface } from '@pokemon-files/util'
import { gen12StringToUTF } from './Strings'

type Gen12BoxStringBytes = {
  trainerName: Uint8Array
  nickname: Uint8Array
}

const rememberedBoxStrings = new WeakMap<PKMInterface, Gen12BoxStringBytes>()

export function rememberGen12BoxStringBytes(
  mon: PKMInterface,
  trainerName: Uint8Array,
  nickname: Uint8Array
) {
  rememberedBoxStrings.set(mon, {
    trainerName: new Uint8Array(trainerName),
    nickname: new Uint8Array(nickname),
  })
}

export function rememberedGen12BoxStringBytesForWrite(
  mon: PKMInterface,
  field: keyof Gen12BoxStringBytes,
  currentText: string,
  length: number
): Uint8Array | undefined {
  const remembered = rememberedBoxStrings.get(mon)?.[field]
  if (!remembered || remembered.length !== length) return undefined
  return gen12StringToUTF(remembered, 0, length) === currentText
    ? new Uint8Array(remembered)
    : undefined
}
