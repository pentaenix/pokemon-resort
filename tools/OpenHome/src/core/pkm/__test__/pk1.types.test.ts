import * as conversion from '@pokemon-files/conversion'
import PK1 from '@pokemon-files/pkm/PK1'
import { OriginGame } from '@pkm-rs/pkg'
import { NationalDex } from '@pokemon-resources/consts/NationalDex'
import { beforeAll, expect, test } from 'vitest'
import { initializeWasm } from './init'

beforeAll(initializeWasm)

test('PK1 Poliwhirl encodes mono Water as PKHeX personal_rb bytes 21/21', () => {
  const buf = new ArrayBuffer(33)
  const dv = new DataView(buf)
  dv.setUint8(0, conversion.toGen1PokemonIndex(NationalDex.Poliwhirl))
  dv.setUint8(5, 9)
  dv.setUint8(6, 9)

  const pk1 = PK1.fromBytes(buf)
  pk1.gameOfOrigin = OriginGame.Red

  const out = new DataView(pk1.toBytes())
  expect(out.getUint8(5)).toBe(21)
  expect(out.getUint8(6)).toBe(21)
})

test('PK1 Bulbasaur encodes Grass/Poison as 22/3 per personal_rb', () => {
  const buf = new ArrayBuffer(33)
  const dv = new DataView(buf)
  dv.setUint8(0, conversion.toGen1PokemonIndex(NationalDex.Bulbasaur))
  dv.setUint8(5, 0)
  dv.setUint8(6, 0)

  const pk1 = PK1.fromBytes(buf)
  pk1.gameOfOrigin = OriginGame.Red

  const out = new DataView(pk1.toBytes())
  expect(out.getUint8(5)).toBe(22)
  expect(out.getUint8(6)).toBe(3)
})
