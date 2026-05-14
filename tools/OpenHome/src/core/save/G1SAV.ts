import { bytesToUint16BigEndian, get8BitChecksum } from '@openhome-core/save/util/byteLogic'
import { gen12StringToUTF, utf16StringToGen12 } from '@openhome-core/save/util/Strings'
import {
  rememberedGen12BoxStringBytesForWrite,
  rememberGen12BoxStringBytes,
} from '@openhome-core/save/util/gen12BoxStringBytes'
import { gen12BoxNicknameForEncode } from '@openhome-core/save/util/gen12BoxNicknameEncode'
import { Option, range, unique } from '@openhome-core/util/functional'
import {
  ConvertStrategy,
  ExtraFormIndex,
  Gender,
  ItemGen1,
  Language,
  OriginGame,
} from '@pkm-rs/pkg'
import * as conversion from '@pokemon-files/conversion'
import { PK1 } from '@pokemon-files/pkm'
import { NationalDex } from '@pokemon-resources/consts/NationalDex'
import { GEN1_TRANSFER_RESTRICTIONS } from '@pokemon-resources/consts/TransferRestrictions'
import { OHPKM } from '../pkm/OHPKM'
import { Box, BoxAndSlot, OfficialSAV } from './interfaces'
import { LookupType } from './util'
import { PathData } from './util/path'

const SAVE_SIZE_BYTES = 0x8000

export class G1SAV extends OfficialSAV<PK1> {
  static pkmType = PK1

  static transferRestrictions = GEN1_TRANSFER_RESTRICTIONS
  static lookupType: LookupType = 'gen12'

  NUM_BOXES = 14

  CURRENT_BOX_NUM_OFFSET = 0x284c

  CURRENT_BOX_DATA_OFFSET = 0x30c0

  BOX_SIZE = 0x462

  BOX_PKM_OFFSET = 0x16

  BOX_PKM_SIZE = 0x21

  BOX_OT_OFFSET = 0x2aa

  BOX_NICKNAME_OFFSET = 0x386

  origin: OriginGame = OriginGame.Red
  isPlugin: false = false

  boxRows = 4
  boxColumns = 5

  filePath: PathData
  fileCreated?: Date

  money: number = 0 // TODO: set money for gen 1 saves
  name: string
  tid: number
  displayID: string
  language = Language.None

  currentPCBox: number
  boxes: Array<Box<PK1>>

  bytes: Uint8Array

  invalid: boolean = false
  tooEarlyToOpen: boolean = false

  updatedBoxSlots: BoxAndSlot[] = []

  constructor(path: PathData, bytes: Uint8Array) {
    super()
    this.bytes = bytes
    this.filePath = path
    this.tid = bytesToUint16BigEndian(this.bytes, 0x2605)
    this.displayID = this.tid.toString().padStart(5, '0')
    this.name = gen12StringToUTF(this.bytes, 0x2598, 11)

    this.currentPCBox = this.bytes[this.CURRENT_BOX_NUM_OFFSET] & 0x7f
    this.boxes = new Array(this.NUM_BOXES)

    if (this.currentPCBox > this.NUM_BOXES) {
      this.invalid = true
      return
    }
    let currenBoxByteOffset

    if (this.currentPCBox < 6) {
      currenBoxByteOffset = 0x4000 + this.currentPCBox * this.BOX_SIZE
    } else {
      currenBoxByteOffset = 0x6000 + (this.currentPCBox - 6) * this.BOX_SIZE
    }
    this.bytes.set(
      this.bytes.slice(this.CURRENT_BOX_DATA_OFFSET, this.CURRENT_BOX_DATA_OFFSET + this.BOX_SIZE),
      currenBoxByteOffset
    )

    this.origin = OriginGame.Red
    if (this.bytes[0x271c] > 0 || path.name.toLowerCase().includes('yellow')) {
      // pikachu friendship
      this.origin = OriginGame.Yellow
    } else if (path.name.toLowerCase().includes('blue')) {
      this.origin = OriginGame.BlueGreen
    } else {
      this.origin = OriginGame.Red
    }
    const pokemonPerBox = this.boxRows * this.boxColumns

    range(this.NUM_BOXES).forEach((boxNumber) => {
      this.boxes[boxNumber] = new Box(`Box ${boxNumber + 1}`, pokemonPerBox)
      this.decodeG1Box(boxNumber)
    })
  }
  sid?: number | undefined

  /** Rebuild `boxSlots` for one box from `this.bytes` (fixed 20-slot PC layout). */
  private decodeG1Box(boxNumber: number): void {
    const pokemonPerBox = this.boxRows * this.boxColumns
    const box = this.boxes[boxNumber]
    let boxByteOffset: number

    if (boxNumber < 6) {
      boxByteOffset = 0x4000 + boxNumber * this.BOX_SIZE
    } else {
      boxByteOffset = 0x6000 + (boxNumber - 6) * this.BOX_SIZE
    }

    for (let i = 0; i < pokemonPerBox; i += 1) {
      box.boxSlots[i] = undefined
    }

    for (let monIndex = 0; monIndex < pokemonPerBox; monIndex++) {
      const speciesByte = this.bytes[boxByteOffset + this.BOX_PKM_OFFSET + monIndex * this.BOX_PKM_SIZE]
      // Gen 1 PC padding often uses 0x00 or 0xff; both are invalid deposited species.
      if (speciesByte && speciesByte !== 0xff) {
        try {
          const mon = PK1.fromBytes(
            this.bytes.slice(
              boxByteOffset + this.BOX_PKM_OFFSET + monIndex * this.BOX_PKM_SIZE,
              boxByteOffset + this.BOX_PKM_OFFSET + (monIndex + 1) * this.BOX_PKM_SIZE
            ).buffer
          )

          const trainerNameBytes = this.bytes.slice(
            boxByteOffset + this.BOX_OT_OFFSET + monIndex * 11,
            boxByteOffset + this.BOX_OT_OFFSET + (monIndex + 1) * 11
          )
          const nicknameBytes = this.bytes.slice(
            boxByteOffset + this.BOX_NICKNAME_OFFSET + monIndex * 11,
            boxByteOffset + this.BOX_NICKNAME_OFFSET + (monIndex + 1) * 11
          )
          mon.trainerName = gen12StringToUTF(trainerNameBytes, 0, 11)
          mon.nickname = gen12StringToUTF(nicknameBytes, 0, 11)
          rememberGen12BoxStringBytes(mon, trainerNameBytes, nicknameBytes)
          mon.gameOfOrigin = this.origin
          mon.language = Language.English
          mon.recomputeStructTypesFromPersonalTable()
          box.boxSlots[monIndex] = mon
        } catch (e) {
          console.error(`G1SAV: ${e}`)
        }
      }
    }
  }

  /**
   * Ensures the recorded count byte matches non-empty slots after a write+decode (catches layout bugs).
   */
  private assertG1BoxCountConsistent(boxNumber: number): void {
    let boxByteOffset: number
    if (boxNumber < 6) {
      boxByteOffset = 0x4000 + boxNumber * this.BOX_SIZE
    } else {
      boxByteOffset = 0x6000 + (boxNumber - 6) * this.BOX_SIZE
    }
    const recorded = this.bytes[boxByteOffset]
    const box = this.boxes[boxNumber]
    const actual = box.boxSlots.reduce<number>((n, s) => n + (s ? 1 : 0), 0)
    if (recorded !== actual) {
      console.warn(
        `G1SAV: box ${boxNumber} count mismatch after save (bytes=${recorded}, slots=${actual}); save layout may be wrong.`
      )
    }
  }

  prepareForSaving() {
    const changedBoxes: number[] = unique(this.updatedBoxSlots.map((coords) => coords.box))

    changedBoxes.forEach((boxNumber) => {
      let boxByteOffset: number

      if (boxNumber < 6) {
        boxByteOffset = 0x4000 + boxNumber * this.BOX_SIZE
      } else {
        boxByteOffset = 0x6000 + (boxNumber - 6) * this.BOX_SIZE
      }
      const box = this.boxes[boxNumber]
      let numMons = 0

      const pokemonPerBox = this.boxRows * this.boxColumns
      const packed = box.boxSlots.filter((m): m is PK1 => m !== undefined && m !== null)
      for (let i = 0; i < pokemonPerBox; i += 1) {
        box.boxSlots[i] = packed[i]
      }

      // Gen 1 boxes are a contiguous list in hardware: Pokémon sit in slots 0..count-1.
      // We pack edited boxes only at save time (not during in-memory UI moves in the transfer screen).
      box.boxSlots.forEach((boxMon, monIndex) => {
        if (boxMon) {
          numMons++
          this.bytes[boxByteOffset + 1 + monIndex] = conversion.toGen1PokemonIndex(boxMon.dexNum)
          this.bytes.set(
            new Uint8Array(boxMon.toBytes()),
            boxByteOffset + this.BOX_PKM_OFFSET + monIndex * this.BOX_PKM_SIZE
          )
          const trainerNameBuffer =
            rememberedGen12BoxStringBytesForWrite(boxMon, 'trainerName', boxMon.trainerName, 11) ??
            utf16StringToGen12(boxMon.trainerName, 11, true)

          this.bytes.set(trainerNameBuffer, boxByteOffset + this.BOX_OT_OFFSET + monIndex * 11)
          const nicknameText = gen12BoxNicknameForEncode(
            boxMon.dexNum,
            boxMon.language,
            boxMon.nickname
          )
          const nicknameBuffer =
            rememberedGen12BoxStringBytesForWrite(boxMon, 'nickname', boxMon.nickname, 11) ??
            utf16StringToGen12(nicknameText, 11, true).fill(0x50, nicknameText.length)

          this.bytes.set(nicknameBuffer, boxByteOffset + this.BOX_NICKNAME_OFFSET + monIndex * 11)
        } else {
          this.bytes[boxByteOffset + 1 + monIndex] = 0xff
          this.bytes.set(
            new Uint8Array(this.BOX_PKM_SIZE).fill(0),
            boxByteOffset + this.BOX_PKM_OFFSET + monIndex * this.BOX_PKM_SIZE
          )
          this.bytes.set(new Uint8Array(11).fill(0), boxByteOffset + this.BOX_OT_OFFSET + monIndex * 11)
          this.bytes.set(
            new Uint8Array(11).fill(0),
            boxByteOffset + this.BOX_NICKNAME_OFFSET + monIndex * 11
          )
        }
      })

      this.bytes[boxByteOffset] = numMons
      let boxChecksumOffset

      if (boxNumber < 6) {
        boxChecksumOffset = 0x5a4d + boxNumber
      } else {
        boxChecksumOffset = 0x7a4d + boxNumber
      }
      const boxChecksum =
        get8BitChecksum(this.bytes, boxByteOffset, boxByteOffset + this.BOX_SIZE) ^ 0xff

      this.bytes[boxChecksumOffset] = boxChecksum
      if (boxNumber === this.currentPCBox) {
        this.bytes.set(
          this.bytes.slice(boxByteOffset, boxByteOffset + this.BOX_SIZE),
          this.CURRENT_BOX_DATA_OFFSET
        )
      }

      this.decodeG1Box(boxNumber)
      this.assertG1BoxCountConsistent(boxNumber)
    })
    const bank2Checksum = get8BitChecksum(this.bytes, 0x4000, 0x5a4c) ^ 0xff

    this.bytes[0x5a4c] = bank2Checksum
    const bank3Checksum = get8BitChecksum(this.bytes, 0x6000, 0x7a4c) ^ 0xff

    this.bytes[0x7a4c] = bank3Checksum
    const wholeSaveChecksum = get8BitChecksum(this.bytes, 0x2598, 0x3521) ^ 0xff

    this.bytes[0x3523] = wholeSaveChecksum
  }

  convertOhpkm(ohpkm: OHPKM, strategy: ConvertStrategy): PK1 {
    return PK1.fromOhpkm(ohpkm, strategy)
  }

  supportsMon(dexNumber: number, formeNumber: number, extraFormIndex?: ExtraFormIndex): boolean {
    if (extraFormIndex !== undefined) return false
    return dexNumber <= NationalDex.Mew && formeNumber === 0
  }

  supportsItem(itemIndex: number) {
    return ItemGen1.fromModern(itemIndex) !== undefined
  }
  static saveTypeAbbreviation = 'RBY (Int)'
  static saveTypeName = 'Pokémon Red/Blue/Yellow (INT)'
  static saveTypeID = 'G1SAV'

  static fileIsSave(bytes: Uint8Array): boolean {
    // Gen 1 and Gen 2 saves are the same size, so assume it's Gen 2 if the Gen 2 checksums are valid
    if (areCrystalInternationalChecksumsValid(bytes) || areGoldSilverChecksumsValid(bytes)) {
      return false
    }
    const decodedFirst64 = new TextDecoder('utf-8').decode(bytes.slice(0, 64))

    if (decodedFirst64.includes('Metroid') || decodedFirst64.includes('ZeroMission')) {
      // lol
      return false
    }
    return bytes.length === SAVE_SIZE_BYTES
  }

  static includesOrigin(origin: OriginGame) {
    return origin >= OriginGame.Red && origin <= OriginGame.Yellow
  }

  get trainerGender() {
    return Gender.Male
  }

  getMonAt(boxNum: number, boxSlot: number) {
    const box = this.boxes[boxNum]
    if (!box) return undefined
    return box.boxSlots[boxSlot]
  }

  setMonAt(boxNum: number, boxSlot: number, mon: Option<PK1>): void {
    const box = this.boxes[boxNum]
    if (!box) return
    box.boxSlots[boxSlot] = mon
  }
}

function areGoldSilverChecksumsValid(bytes: Uint8Array) {
  const checksum1 = getGoldSilverInternationalChecksum1(bytes)

  if (checksum1 !== bytes[0x2d69]) {
    return false
  }
  const checksum2 = getGoldSilverInternationalChecksum2(bytes)

  return checksum2 === bytes[0x7e6d]
}

function getGoldSilverInternationalChecksum1(bytes: Uint8Array) {
  return get8BitChecksum(bytes, 0x2009, 0x2d68)
}

function getGoldSilverInternationalChecksum2(bytes: Uint8Array) {
  let checksum = 0

  checksum += get8BitChecksum(bytes, 0x15c7, 0x17ec)
  checksum += get8BitChecksum(bytes, 0x3d96, 0x3f3f)
  checksum += get8BitChecksum(bytes, 0x0c6b, 0x10e7)
  checksum += get8BitChecksum(bytes, 0x7e39, 0x7e6c)
  checksum += get8BitChecksum(bytes, 0x10e8, 0x15c6)
  return checksum & 0xff
}

function getCrystalInternationalChecksum1(bytes: Uint8Array) {
  return get8BitChecksum(bytes, 0x2009, 0x2b82)
}

function getCrystalInternationalChecksum2(bytes: Uint8Array) {
  return get8BitChecksum(bytes, 0x1209, 0x1d82)
}

function areCrystalInternationalChecksumsValid(bytes: Uint8Array) {
  const checksum1 = getCrystalInternationalChecksum1(bytes)

  if (checksum1 !== bytes[0x2d0d]) {
    return false
  }
  const checksum2 = getCrystalInternationalChecksum2(bytes)

  return checksum2 === bytes[0x1f0d]
}
