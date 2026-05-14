import { bytesToPKM } from '@openhome-core/pkm/FileImport'
import { OHPKM } from '@openhome-core/pkm/OHPKM'
import { R } from '@openhome-core/util/functional'
import { ConvertStrategies, ConvertStrategy } from '@pkm-rs/pkg'
import { PK2 } from '@pokemon-files/pkm'
import fs from 'fs'
import path from 'path'
import { beforeAll, expect, test } from 'vitest'
import { G2SAV } from '../G2SAV'
import { buildUnknownSaveFile } from '../util/load'
import { emptyPathData } from '../util/path'
import { initializeWasm } from './init'

beforeAll(initializeWasm)
let crystalSaveFile: G2SAV
var slowbroOH: OHPKM

const g2BoxStringBytes = (save: G2SAV, box: number, kind: 'ot' | 'nickname', slot: number) => {
  const pokemonPerBox = save.boxRows * save.boxColumns
  const boxOffset = save.boxOffsets[box]
  const otOffset = boxOffset + 1 + pokemonPerBox + 1 + pokemonPerBox * 0x20
  const nicknameOffset = otOffset + pokemonPerBox * 11
  const offset = kind === 'ot' ? otOffset : nicknameOffset
  return Array.from(save.bytes.slice(offset + slot * 11, offset + (slot + 1) * 11))
}

beforeAll(async () => {
  await initializeWasm()
  const result = buildUnknownSaveFile(
    emptyPathData,
    new Uint8Array(fs.readFileSync(path.join(__dirname, 'save-files', 'crystal.sav'))),

    [G2SAV]
  )

  if (R.isErr(result)) {
    throw result.err
  }

  crystalSaveFile = result.value as G2SAV

  const slowpokeBytes = fs.readFileSync(
    path.join('src/core/pkm/__test__/PKMFiles/OhpkmV2', 'slowbro.ohpkm')
  )
  slowbroOH = bytesToPKM(new Uint8Array(slowpokeBytes), 'OHPKM') as OHPKM
})

test('pc box decoded correctly', () => {
  expect(crystalSaveFile.boxes[9].boxSlots[0]?.nickname).toEqual('AMPHAROS')
  expect(crystalSaveFile.boxes[9].boxSlots[1]?.nickname).toEqual('BELLOSSOM')
  expect(crystalSaveFile.boxes[9].boxSlots[18]?.nickname).toEqual('SLOWKING')
  expect(crystalSaveFile.boxes[9].boxSlots[19]?.nickname).toEqual('MISDREAVUS')
})

test('removing mon shifts others in box', () => {
  const result1 = buildUnknownSaveFile(emptyPathData, new Uint8Array(crystalSaveFile.bytes), [
    G2SAV,
  ])

  if (R.isErr(result1)) {
    throw Error(result1.err)
  }

  const modifiedSaveFile1 = result1.value as G2SAV

  modifiedSaveFile1.boxes[9].boxSlots[0] = undefined
  modifiedSaveFile1.updatedBoxSlots.push({ box: 9, boxSlot: 0 })
  modifiedSaveFile1.prepareForSaving()

  const result2 = buildUnknownSaveFile(emptyPathData, new Uint8Array(modifiedSaveFile1.bytes), [
    G2SAV,
  ])

  if (R.isErr(result2)) {
    throw Error(result2.err)
  }

  const modifiedSaveFile2 = result2.value as G2SAV

  expect(modifiedSaveFile2.boxes[9].boxSlots[0]?.nickname).toEqual('BELLOSSOM')
  expect(modifiedSaveFile2.boxes[9].boxSlots[18]?.nickname).toEqual('MISDREAVUS')
  expect(modifiedSaveFile2.boxes[9].boxSlots[19]).toEqual(undefined)
  expect(g2BoxStringBytes(modifiedSaveFile2, 9, 'nickname', 0)).toEqual(
    g2BoxStringBytes(crystalSaveFile, 9, 'nickname', 1)
  )
})

test('rewriting a Gen 2 box preserves raw nickname padding bytes', () => {
  const result1 = buildUnknownSaveFile(emptyPathData, new Uint8Array(crystalSaveFile.bytes), [
    G2SAV,
  ])

  if (R.isErr(result1)) {
    throw Error(result1.err)
  }

  const modifiedSaveFile1 = result1.value as G2SAV
  const beforeAmpharosNickname = g2BoxStringBytes(modifiedSaveFile1, 9, 'nickname', 0)

  modifiedSaveFile1.updatedBoxSlots.push({ box: 9, boxSlot: 0 })
  modifiedSaveFile1.prepareForSaving()

  expect(g2BoxStringBytes(modifiedSaveFile1, 9, 'nickname', 0)).toEqual(beforeAmpharosNickname)
})

test('inserting mon works', () => {
  const result1 = buildUnknownSaveFile(emptyPathData, new Uint8Array(crystalSaveFile.bytes), [
    G2SAV,
  ])

  if (R.isErr(result1)) {
    throw Error(result1.err)
  }

  const modifiedSaveFile1 = result1.value as G2SAV

  modifiedSaveFile1.boxes[13].boxSlots[17] = PK2.fromOhpkm(
    slowbroOH,
    ConvertStrategies.getDefault()
  )
  modifiedSaveFile1.updatedBoxSlots.push({ box: 13, boxSlot: 0 })
  modifiedSaveFile1.prepareForSaving()

  const result2 = buildUnknownSaveFile(emptyPathData, new Uint8Array(modifiedSaveFile1.bytes), [
    G2SAV,
  ])

  if (R.isErr(result2)) {
    throw Error(result2.err)
  }

  const modifiedSaveFile2 = result2.value as G2SAV

  expect(modifiedSaveFile2.boxes[13].boxSlots[0]?.nickname).toEqual('UNOWN')
  expect(modifiedSaveFile2.boxes[13].boxSlots[16]?.nickname).toEqual('WIGGLYTUFF')
  expect(modifiedSaveFile2.boxes[13].boxSlots[17]?.nickname).toEqual('SLOWBRO')
  expect(modifiedSaveFile2.boxes[13].boxSlots[17]?.trainerName).toEqual(slowbroOH.trainerName)
})

test('inserting mon with game capitalization gives correct nickname', () => {
  const result1 = buildUnknownSaveFile(emptyPathData, new Uint8Array(crystalSaveFile.bytes), [
    G2SAV,
  ])

  if (R.isErr(result1)) {
    throw Error(result1.err)
  }

  const modifiedSaveFile1 = result1.value as G2SAV

  const modernStrategy: ConvertStrategy = {
    ...ConvertStrategies.getDefault(),
    'nickname.capitalization': 'Modern',
  }
  modifiedSaveFile1.boxes[13].boxSlots[17] = PK2.fromOhpkm(slowbroOH, modernStrategy)
  modifiedSaveFile1.updatedBoxSlots.push({ box: 13, boxSlot: 0 })
  modifiedSaveFile1.prepareForSaving()

  const result2 = buildUnknownSaveFile(emptyPathData, new Uint8Array(modifiedSaveFile1.bytes), [
    G2SAV,
  ])

  if (R.isErr(result2)) {
    throw Error(result2.err)
  }

  const modifiedSaveFile2 = result2.value as G2SAV

  expect(modifiedSaveFile2.boxes[13].boxSlots[17]?.nickname).toEqual('SLOWBRO')
  expect(modifiedSaveFile2.boxes[13].boxSlots[17]?.trainerName).toEqual(slowbroOH.trainerName)
  expect(modifiedSaveFile2.boxes[13].boxSlots[17]?.trainerName).not.toEqual('')
  expect(g2BoxStringBytes(modifiedSaveFile2, 13, 'nickname', 17)).toEqual([
    0x92, 0x8b, 0x8e, 0x96, 0x81, 0x91, 0x8e, 0x50, 0x50, 0x50, 0x50,
  ])
})
