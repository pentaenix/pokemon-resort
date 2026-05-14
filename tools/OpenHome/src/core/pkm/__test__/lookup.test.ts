import { ConvertStrategies, FormMetadata, MetadataSummaryLookup } from '@pkm-rs/pkg'
import { PK2, PK3 } from '@pokemon-files/pkm'
import { NationalDex } from '@pokemon-resources/consts/NationalDex'
import fs from 'fs'
import path from 'path'
import { assert, beforeAll, describe, expect, test } from 'vitest'
import { getMonGen345Identifier, shouldReuseTrackedGen345Mon } from '../Lookup'
import { OHPKM } from '../OHPKM'
import { initializeWasm } from './init'

var MimeJr: FormMetadata
var MrMimeKanto: FormMetadata
var MrMimeGalar: FormMetadata
var MrRime: FormMetadata

var Vaporeon: FormMetadata
var Sylveon: FormMetadata

var Applin: FormMetadata
var Hydrapple: FormMetadata

beforeAll(initializeWasm)
beforeAll(() => {
  function mustLookupForm(nationalDex: number, formIndex: number) {
    const metadata = MetadataSummaryLookup(nationalDex, formIndex)
    assert(metadata !== undefined)
    return metadata
  }

  function mustLookupBaseForm(nationalDex: number) {
    return mustLookupForm(nationalDex, 0)
  }

  MimeJr = mustLookupBaseForm(NationalDex.MimeJr)
  MrMimeKanto = mustLookupBaseForm(NationalDex.MrMime)
  MrMimeGalar = mustLookupForm(NationalDex.MrMime, 1)
  MrRime = mustLookupBaseForm(NationalDex.MrRime)

  Vaporeon = mustLookupBaseForm(NationalDex.Vaporeon)
  Sylveon = mustLookupBaseForm(NationalDex.Sylveon)

  Applin = mustLookupBaseForm(NationalDex.Applin)
  Hydrapple = mustLookupBaseForm(NationalDex.Hydrapple)
})

describe('getMonGen345Identifier', () => {
  test('matches between PK3 and OHPKM built from that PK3 (roundtrip / Resort lookup)', () => {
    const gen3Dir = path.join(__dirname, 'PKMFiles', 'Gen3')
    const sample = fs.readdirSync(gen3Dir).find((f) => f.endsWith('.pkm'))
    assert(sample !== undefined)
    const bytes = new Uint8Array(fs.readFileSync(path.join(gen3Dir, sample)))
    const pk3 = PK3.fromBytes(bytes.buffer)
    const ohpkm = new OHPKM(pk3)
    expect(getMonGen345Identifier(pk3)).toEqual(getMonGen345Identifier(ohpkm))
  })

  test('gb-origin gen 3 projections do not reuse tracked identities', () => {
    const hoohBytes = new Uint8Array(
      fs.readFileSync(path.join(__dirname, 'PKMFiles', 'Gen2', 'hooh.pk2'))
    )
    const ohpkm = new OHPKM(PK2.fromBytes(hoohBytes.buffer))
    const pk3 = PK3.fromOhpkm(ohpkm, ConvertStrategies.getDefault())
    expect(shouldReuseTrackedGen345Mon(pk3)).toBe(false)
  })
})

describe('validate expected evolution relationships', () => {
  test('mr rime is evo of mime jr', () => {
    expect(MrRime.isEvolutionOf(MimeJr))
  })

  test('mr rime is evo of galarian mr mime', () => {
    expect(MrRime.isEvolutionOf(MrMimeGalar))
  })

  test('mr rime is NOT evo of kantonian mr mime', () => {
    expect(!MrRime.isEvolutionOf(MrMimeKanto))
  })

  test('mr rime is NOT evo of mr rime', () => {
    expect(!MrRime.isEvolutionOf(MrRime))
  })

  test('vaporeon is NOT evo of sylveon', () => {
    expect(!Vaporeon.isEvolutionOf(Sylveon))
  })

  test('hydrapple is evo of applin', () => {
    expect(!Hydrapple.isEvolutionOf(Applin))
  })
})
