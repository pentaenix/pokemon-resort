import { bytesToPKM } from '@openhome-core/pkm/FileImport'
import { OHPKM } from '@openhome-core/pkm/OHPKM'
import { R } from '@openhome-core/util/functional'
import { ConvertStrategies, ConvertStrategy, ItemGen2, OriginGame } from '@pkm-rs/pkg'
import { PK1, PK2 } from '@pokemon-files/pkm'
import assert, { fail } from 'assert'
import fs from 'fs'
import path from 'path'
import { beforeAll, expect, test } from 'vitest'
import { G1SAV } from '../G1SAV'
import { buildUnknownSaveFile } from '../util/load'
import { emptyPathData } from '../util/path'
import { initializeWasm } from './init'

let blueSaveFile: G1SAV
var slowbroOH: OHPKM

const boxOffset = (save: G1SAV, box: number) =>
  box < 6 ? 0x4000 + box * save.BOX_SIZE : 0x6000 + (box - 6) * save.BOX_SIZE

const boxStringBytes = (save: G1SAV, box: number, offset: number, slot: number) =>
  Array.from(save.bytes.slice(boxOffset(save, box) + offset + slot * 11, boxOffset(save, box) + offset + (slot + 1) * 11))

beforeAll(initializeWasm)
beforeAll(() => {
  const result = buildUnknownSaveFile(
    emptyPathData,
    new Uint8Array(fs.readFileSync(path.join(__dirname, 'save-files', 'blue.sav'))),
    [G1SAV]
  )

  assert(R.isOk(result))

  blueSaveFile = result.value as G1SAV

  slowbroOH = bytesToPKM(
    new Uint8Array(
      fs.readFileSync(path.join('src/core/pkm/__test__/PKMFiles/OhpkmV2', 'slowbro.ohpkm'))
    ),
    'OHPKM'
  ) as OHPKM
})

test('pc box decoded correctly', () => {
  expect(blueSaveFile.boxes[7].boxSlots[0]?.nickname).toEqual('KABUTOPS')
  expect(blueSaveFile.boxes[7].boxSlots[1]?.nickname).toEqual('AERODACTYL')
  expect(blueSaveFile.boxes[7].boxSlots[9]?.nickname).toEqual('MEWTWO')
  expect(blueSaveFile.boxes[7].boxSlots[10]?.nickname).toEqual('MEW')
})

test('removing leading mon packs remaining toward slot 0 on save write', () => {
  const result1 = buildUnknownSaveFile(emptyPathData, new Uint8Array(blueSaveFile.bytes), [G1SAV])

  if (R.isErr(result1)) {
    fail(result1.err)
  }

  const modifiedSaveFile1 = result1.value as G1SAV

  modifiedSaveFile1.boxes[7].boxSlots[0] = undefined
  modifiedSaveFile1.updatedBoxSlots.push({ box: 7, boxSlot: 0 })
  modifiedSaveFile1.prepareForSaving()

  const result2 = buildUnknownSaveFile(emptyPathData, new Uint8Array(modifiedSaveFile1.bytes), [
    G1SAV,
  ])

  if (R.isErr(result2)) {
    fail(result2.err)
  }

  const modifiedSaveFile2 = result2.value as G1SAV

  expect(modifiedSaveFile2.boxes[7].boxSlots[0]?.nickname).toEqual('AERODACTYL')
  expect(modifiedSaveFile2.boxes[7].boxSlots[8]?.nickname).toEqual('MEWTWO')
  expect(modifiedSaveFile2.boxes[7].boxSlots[9]?.nickname).toEqual('MEW')
  expect(boxStringBytes(modifiedSaveFile2, 7, modifiedSaveFile2.BOX_NICKNAME_OFFSET, 0)).toEqual(
    boxStringBytes(blueSaveFile, 7, blueSaveFile.BOX_NICKNAME_OFFSET, 1)
  )
})

test('rewriting a Gen 1 box preserves raw nickname padding and trade OT bytes', () => {
  const result1 = buildUnknownSaveFile(emptyPathData, new Uint8Array(blueSaveFile.bytes), [G1SAV])

  if (R.isErr(result1)) {
    fail(result1.err)
  }

  const modifiedSaveFile1 = result1.value as G1SAV
  const beforeRhyhornNickname = boxStringBytes(
    modifiedSaveFile1,
    0,
    modifiedSaveFile1.BOX_NICKNAME_OFFSET,
    1
  )
  const beforeTradeOt = boxStringBytes(modifiedSaveFile1, 8, modifiedSaveFile1.BOX_OT_OFFSET, 0)

  modifiedSaveFile1.updatedBoxSlots.push({ box: 0, boxSlot: 0 })
  modifiedSaveFile1.updatedBoxSlots.push({ box: 8, boxSlot: 0 })
  modifiedSaveFile1.prepareForSaving()

  expect(boxStringBytes(modifiedSaveFile1, 0, modifiedSaveFile1.BOX_NICKNAME_OFFSET, 1)).toEqual(
    beforeRhyhornNickname
  )
  expect(boxStringBytes(modifiedSaveFile1, 8, modifiedSaveFile1.BOX_OT_OFFSET, 0)).toEqual(
    beforeTradeOt
  )
  expect(modifiedSaveFile1.boxes[8].boxSlots[0]?.trainerName).toEqual('*')
})

test('inserting mon works', () => {
  const result1 = buildUnknownSaveFile(emptyPathData, new Uint8Array(blueSaveFile.bytes), [G1SAV])

  if (R.isErr(result1)) {
    fail(result1.err)
  }
  const modifiedSaveFile1 = result1.value as G1SAV

  modifiedSaveFile1.boxes[7].boxSlots[11] = PK1.fromOhpkm(slowbroOH, ConvertStrategies.getDefault())
  modifiedSaveFile1.updatedBoxSlots.push({ box: 7, boxSlot: 0 })
  modifiedSaveFile1.prepareForSaving()

  const result2 = buildUnknownSaveFile(emptyPathData, new Uint8Array(modifiedSaveFile1.bytes), [
    G1SAV,
  ])

  if (R.isErr(result2)) {
    fail(result2.err)
  }

  const modifiedSaveFile2 = result2.value as G1SAV

  expect(modifiedSaveFile2.boxes[7].boxSlots[0]?.nickname).toEqual('KABUTOPS')
  expect(modifiedSaveFile2.boxes[7].boxSlots[10]?.nickname).toEqual('MEW')
  expect(modifiedSaveFile2.boxes[7].boxSlots[11]?.nickname).toEqual('SLOWBRO')
  expect(modifiedSaveFile2.boxes[7].boxSlots[11]?.trainerName).toEqual(slowbroOH.trainerName)
})

test('inserting mon with game capitalization gives correct nickname', () => {
  const result1 = buildUnknownSaveFile(emptyPathData, new Uint8Array(blueSaveFile.bytes), [G1SAV])

  if (R.isErr(result1)) {
    fail(result1.err)
  }
  const modifiedSaveFile1 = result1.value as G1SAV

  const modernStrategy: ConvertStrategy = {
    ...ConvertStrategies.getDefault(),
    'nickname.capitalization': 'Modern',
  }
  modifiedSaveFile1.boxes[7].boxSlots[11] = PK1.fromOhpkm(slowbroOH, modernStrategy)
  modifiedSaveFile1.updatedBoxSlots.push({ box: 7, boxSlot: 0 })
  modifiedSaveFile1.prepareForSaving()

  const result2 = buildUnknownSaveFile(emptyPathData, new Uint8Array(modifiedSaveFile1.bytes), [
    G1SAV,
  ])

  if (R.isErr(result2)) {
    fail(result2.err)
  }

  const modifiedSaveFile2 = result2.value as G1SAV

  expect(modifiedSaveFile2.boxes[7].boxSlots[11]?.nickname).toEqual('SLOWBRO')
  expect(modifiedSaveFile2.boxes[7].boxSlots[11]?.trainerName).toEqual(slowbroOH.trainerName)
  expect(modifiedSaveFile2.boxes[7].boxSlots[11]?.trainerName).not.toEqual('')
  expect(boxStringBytes(modifiedSaveFile2, 7, modifiedSaveFile2.BOX_NICKNAME_OFFSET, 11)).toEqual([
    0x92, 0x8b, 0x8e, 0x96, 0x81, 0x91, 0x8e, 0x50, 0x50, 0x50, 0x50,
  ])
})

test('Gen 2-origin Pokemon projected to Gen 1 clamp OT length and keep the held-byte source', () => {
  const sourceOh = OHPKM.defaultWithSpecies(1, 0)
  ;(sourceOh as any).gameOfOrigin = OriginGame.Crystal
  sourceOh.trainerName = 'TOO-LONG'
  const sourcePk2 = PK2.fromOhpkm(sourceOh, ConvertStrategies.getDefault())
  sourcePk2.heldItemIndexGen2 = ItemGen2.fromIndex(1)
  const source = new OHPKM(sourcePk2)

  const projected = PK1.fromOhpkm(source, ConvertStrategies.getDefault())

  expect(projected.trainerName.length).toBeLessThanOrEqual(8)
  expect(projected.gen1PartyStructByte7).toEqual(1)
})
