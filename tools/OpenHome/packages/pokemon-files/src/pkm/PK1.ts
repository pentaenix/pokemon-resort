import {
  ConvertStrategy,
  Generation,
  Language,
  Lookup,
  MetadataSummaryLookup,
  MetadataSource,
  OriginGame,
  OriginGames,
  SpeciesLookup,
  Tag,
  type PkmType,
} from '@pkm-rs/pkg'

import { OHPKM } from '../../../../src/core/pkm/OHPKM'
import * as conversion from '../conversion'
import { PkmConverter } from '../conversion/converter'
import * as byteLogic from '../util/byteLogic'
import { gen1RbyCatchRateForNationalDex } from '../util/gen1RbyCatchRate'
import { gen12TrainerNameForEncode } from '../util/gen12TrainerNameEncode'
import { FourMoves } from '../util/pkmInterface'
import { getLevelGen12, getStats } from '../util/statCalc'
import * as stringLogic from '../util/stringConversion'
import * as types from '../util/types'
import { MoveFilter } from '../util/util'
import { PkmConstructorOptions } from './PKM'

/** Personal table for Gen 1 saves: Yellow differs from Red/Blue for some species (e.g. Pikachu line). */
export function gen1PersonalMetadataSourceForOrigin(origin: number): MetadataSource {
  return origin === OriginGame.Yellow ? MetadataSource.Yellow : MetadataSource.RedBlue
}

function gen1PersonalSourcesInFallbackOrder(preferred: MetadataSource): MetadataSource[] {
  return preferred === MetadataSource.Yellow
    ? [MetadataSource.Yellow, MetadataSource.RedBlue]
    : [MetadataSource.RedBlue, MetadataSource.Yellow]
}

/**
 * Inverse of Rust `PkmType::from_byte_gen12` (`pkm_rs_types`): bytes @0x05–0x06 in deposited PK1 data
 * use Generation I/II **storage** numbering (PKHeX `personal_rb`), not a compact 0..=14 menu encoding.
 * PKHeX legality matches these against personal data — e.g. Water=21, Grass=22, Fire=20; mono-types
 * duplicate the same byte in both slots (e.g. Poliwhirl 21/21).
 */
function pkmTypeToGen12StoredStructByte(t: PkmType): number {
  switch (t) {
    case 'Normal':
      return 0
    case 'Fighting':
      return 1
    case 'Flying':
      return 2
    case 'Poison':
      return 3
    case 'Ground':
      return 4
    case 'Rock':
      return 5
    case 'Bug':
      return 7
    case 'Ghost':
      return 8
    case 'Fire':
      return 20
    case 'Water':
      return 21
    case 'Grass':
      return 22
    case 'Electric':
      return 23
    case 'Psychic':
      return 24
    case 'Ice':
      return 25
    case 'Dragon':
      return 26
    default:
      return 0
  }
}

/**
 * Maps species types from PKHeX personal data into R/B/Y deposited struct type bytes @0x05–0x06.
 */
function gen1TypeBytesFromSpeciesMetadata(
  dexNum: number,
  formNum: number,
  fallback: { type1: number; type2: number },
  preferredPersonalSource: MetadataSource
): { type1: number; type2: number } {
  if (dexNum < 1) {
    return fallback
  }
  const meta = MetadataSummaryLookup(dexNum, formNum)
  if (!meta) {
    return fallback
  }
  try {
    for (const src of gen1PersonalSourcesInFallbackOrder(preferredPersonalSource)) {
      if (!meta.hasDataForSource(src)) {
        continue
      }
      const t1 = meta.type1WithSource(src) ?? meta.type1
      const t2Raw = meta.type2WithSource(src) ?? meta.type2
      const type1 = pkmTypeToGen12StoredStructByte(t1)
      const type2 = t2Raw !== undefined ? pkmTypeToGen12StoredStructByte(t2Raw) : type1
      return { type1, type2 }
    }
    return fallback
  } finally {
    meta.free()
  }
}

function tryOriginalPk1PartyByte7(ohpkm: OHPKM): number | undefined {
  const od = ohpkm.originalData
  if (!od) return undefined
  try {
    if (od.tag !== Tag.Pk1) return undefined
    const d = od.data
    return d.length > 7 ? d[7] : undefined
  } finally {
    od.free()
  }
}

export default class PK1 {
  static getFormat() {
    return 'PK1' as const
  }
  format: 'PK1' = 'PK1'
  static getBoxSize() {
    return 33
  }
  gameOfOrigin: number
  language: Language
  dexNum: number
  currentHP: number
  level: number
  statusCondition: number
  type1: number
  type2: number
  /**
   * Gen 1 party struct byte @0x07. Native Gen 1 uses catch rate; Gen 2-origin projections reuse the
   * Gen 2 held-item byte because PKHeX legality treats that field as the origin signal there.
   */
  gen1PartyStructByte7: number
  moves: FourMoves
  trainerID: number
  exp: number
  evsG12: types.StatsPreSplit
  dvs: types.StatsPreSplit
  movePP: FourMoves
  movePPUps: FourMoves
  trainerName: string
  nickname: string
  originalBytes?: ArrayBuffer

  constructor(arg: ArrayBuffer | OHPKM, options: PkmConstructorOptions) {
    if (arg instanceof ArrayBuffer) {
      const u8 = new Uint8Array(arg)
      // Gen 1 box deposit is exactly 33 bytes; only strip a 3-byte party wrapper on longer buffers.
      const buffer = u8.byteLength !== PK1.getBoxSize() && u8[2] === 0xff ? arg.slice(3) : arg
      this.originalBytes = buffer
      const dataView = new DataView(buffer)
      this.gameOfOrigin = 0
      this.language = 0
      this.dexNum = conversion.fromGen1PokemonIndex(dataView.getUint8(0x0))
      this.currentHP = dataView.getUint16(0x1, false)
      this.level = dataView.getUint8(0x3)
      this.statusCondition = dataView.getUint8(0x4)
      const rawType1 = dataView.getUint8(0x5)
      const rawType2 = dataView.getUint8(0x6)
      // `gameOfOrigin` is set later by G1SAV; assume Red/Blue until `recomputeStructTypesFromPersonalTable()`.
      const speciesTypes = gen1TypeBytesFromSpeciesMetadata(this.dexNum, 0, {
        type1: rawType1,
        type2: rawType2,
      }, MetadataSource.RedBlue)
      this.type1 = speciesTypes.type1
      this.type2 = speciesTypes.type2
      this.gen1PartyStructByte7 = dataView.getUint8(0x7)
      this.moves = [
        dataView.getUint8(0x8),
        dataView.getUint8(0x9),
        dataView.getUint8(0xa),
        dataView.getUint8(0xb),
      ]
      this.trainerID = dataView.getUint16(0xc, false)
      this.exp = (dataView.getUint32(0xe, false) >> 8) & 0xffffff
      this.evsG12 = {
        hp: dataView.getUint16(0x11, false),
        atk: dataView.getUint16(0x13, false),
        def: dataView.getUint16(0x15, false),
        spe: dataView.getUint16(0x17, false),
        spc: dataView.getUint16(0x19, false),
      }
      this.dvs = types.readDVsFromBytes(dataView, 0x1b)
      this.movePP = [
        byteLogic.uIntFromBufferBits(dataView, 0x1d, 0, 6, false),
        byteLogic.uIntFromBufferBits(dataView, 0x1e, 0, 6, false),
        byteLogic.uIntFromBufferBits(dataView, 0x1f, 0, 6, false),
        byteLogic.uIntFromBufferBits(dataView, 0x20, 0, 6, false),
      ]
      this.movePPUps = [
        byteLogic.uIntFromBufferBits(dataView, 0x1d, 6, 2, false),
        byteLogic.uIntFromBufferBits(dataView, 0x1e, 6, 2, false),
        byteLogic.uIntFromBufferBits(dataView, 0x1f, 6, 2, false),
        byteLogic.uIntFromBufferBits(dataView, 0x20, 6, 2, false),
      ]
      if (dataView.byteLength >= 66) {
        this.trainerName = stringLogic.readGameBoyStringFromBytes(dataView, 0x2c, 8)
      } else {
        this.trainerName = 'TRAINER'
      }

      if (dataView.byteLength >= 66) {
        this.nickname = stringLogic.readGameBoyStringFromBytes(dataView, 0x37, 11)
      } else {
        this.nickname = Lookup.speciesName(this.dexNum, this.language)
      }
    } else {
      const converter = new PkmConverter(this.format, options.strategy)
      const other = arg
      this.gameOfOrigin = other.gameOfOrigin
      this.language = other.language
      this.dexNum = other.dexNum
      this.currentHP = other.currentHP ?? 0
      this.level = 0
      this.statusCondition = 0

      const { type1, type2 } = gen1TypeBytesFromSpeciesMetadata(
        this.dexNum,
        other.formNum ?? 0,
        { type1: 0, type2: 0 },
        gen1PersonalMetadataSourceForOrigin(other.gameOfOrigin)
      )
      this.type1 = type1
      this.type2 = type2

      this.gen1PartyStructByte7 =
        OriginGames.generation(other.gameOfOrigin) === Generation.G2
          ? other.heldItemIndex
          : tryOriginalPk1PartyByte7(other) ?? gen1RbyCatchRateForNationalDex(this.dexNum)

      const moveFilter = MoveFilter.fromPkmClass(PK1)
      this.moves = moveFilter.moves(other)
      this.movePP = moveFilter.movePp(other, this.format)
      this.movePPUps = moveFilter.movePpUps(other)

      if (
        !(
          OriginGames.generation(other.gameOfOrigin) === Generation.G1 ||
          OriginGames.generation(other.gameOfOrigin) === Generation.G2
        ) &&
        other.personalityValue !== undefined
      ) {
        this.trainerID = other.personalityValue % 0x10000
      } else {
        this.trainerID = other.trainerID
      }
      this.exp = other.exp
      this.evsG12 = other.evsG12 ?? {
        hp: 0,
        atk: 0,
        def: 0,
        spe: 0,
        spc: 0,
      }
      this.dvs = other.dvs
      this.trainerName = gen12TrainerNameForEncode(other.trainerName)
      this.nickname = converter.nickname(other)
    }

    this.level = getLevelGen12(this.dexNum, this.exp)
  }

  static fromBytes(buffer: ArrayBuffer): PK1 {
    return new PK1(buffer, { encrypted: false })
  }

  static fromOhpkm(ohpkm: OHPKM, strategy: ConvertStrategy): PK1 {
    return new PK1(ohpkm, { strategy })
  }

  toBytes(options?: types.ToBytesOptions): ArrayBuffer {
    const buffer = new ArrayBuffer(options?.includeExtraFields ? 66 : 33)
    const dataView = new DataView(buffer)

    dataView.setUint8(0x0, conversion.toGen1PokemonIndex(this.dexNum))
    dataView.setUint16(0x1, this.currentHP, false)
    dataView.setUint8(0x3, this.level)
    dataView.setUint8(0x4, this.statusCondition)
    dataView.setUint8(0x5, this.type1)
    dataView.setUint8(0x6, this.type2)
    dataView.setUint8(0x7, this.gen1PartyStructByte7)
    for (let i = 0; i < 4; i++) {
      dataView.setUint8(0x8 + i, this.moves[i])
    }
    dataView.setUint16(0xc, this.trainerID, false)
    new Uint8Array(buffer).set(byteLogic.uint24ToBytesBigEndian(this.exp), 0xe)
    dataView.setUint16(0x11, this.evsG12.hp, false)
    dataView.setUint16(0x13, this.evsG12.atk, false)
    dataView.setUint16(0x15, this.evsG12.def, false)
    dataView.setUint16(0x17, this.evsG12.spe, false)
    dataView.setUint16(0x19, this.evsG12.spc, false)

    types.writeDVsToBytes(this.dvs, dataView, 0x1b)
    for (let i = 0; i < 4; i++) {
      byteLogic.uIntToBufferBits(dataView, this.movePP[i], 0x1d + i, 0, 6, false)
    }

    for (let i = 0; i < 4; i++) {
      byteLogic.uIntToBufferBits(dataView, this.movePPUps[i], 0x1d + i, 6, 2, false)
    }

    if (options?.includeExtraFields) {
      stringLogic.writeGameBoyStringToBytes(dataView, this.trainerName, 0x2c, 8, true)
    }

    if (options?.includeExtraFields) {
      stringLogic.writeGameBoyStringToBytes(dataView, this.nickname, 0x37, 11, true)
    }
    return buffer
  }

  public getStats() {
    return getStats(this)
  }

  public get gender() {
    return this.metadata?.genderFromAtkDv(this.dvs.atk)
  }

  public get heldItemIndex() {
    return 0
  }

  public get heldItemName() {
    return 'None'
  }

  public get trainerGender() {
    return false
  }

  public get secretID() {
    return 0
  }

  public get formNum() {
    return 0
  }

  public getLevel() {
    return getLevelGen12(this.dexNum, this.exp)
  }

  isShiny() {
    return (
      this.dvs.spe === 10 &&
      this.dvs.def === 10 &&
      this.dvs.spc === 10 &&
      [2, 3, 6, 7, 10, 11, 14, 15].includes(this.dvs.atk)
    )
  }

  isSquareShiny() {
    return false
  }

  public get metadata() {
    return MetadataSummaryLookup(this.dexNum, this.formNum)
  }

  public get speciesMetadata() {
    return SpeciesLookup(this.dexNum)
  }

  static maxValidMove() {
    return 165
  }

  static maxValidBall() {
    return 0
  }

  /**
   * After `gameOfOrigin` is set (e.g. Yellow vs Red/Blue), re-encode @0x05–0x06 from the matching
   * Gen 1 personal table (Gen12 **storage** type bytes per PKHeX `personal_rb`, not legacy 0..14).
   */
  recomputeStructTypesFromPersonalTable(): void {
    let fallbackType1 = this.type1
    let fallbackType2 = this.type2
    if (this.originalBytes) {
      const dv = new DataView(this.originalBytes)
      if (dv.byteLength > 0x6) {
        fallbackType1 = dv.getUint8(0x5)
        fallbackType2 = dv.getUint8(0x6)
      }
    }
    const t = gen1TypeBytesFromSpeciesMetadata(
      this.dexNum,
      0,
      { type1: fallbackType1, type2: fallbackType2 },
      gen1PersonalMetadataSourceForOrigin(this.gameOfOrigin)
    )
    this.type1 = t.type1
    this.type2 = t.type2
  }
}
