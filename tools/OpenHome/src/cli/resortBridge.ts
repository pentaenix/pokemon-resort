import { BW2SAV } from '@openhome-core/save/BW2SAV'
import { BWSAV } from '@openhome-core/save/BWSAV'
import { DPSAV } from '@openhome-core/save/DPSAV'
import { G1SAV } from '@openhome-core/save/G1SAV'
import { G2SAV } from '@openhome-core/save/G2SAV'
import { G3SAV } from '@openhome-core/save/G3SAV'
import { BDSPSAV } from '@openhome-core/save/Gen89/BDSPSAV'
import { LASAV } from '@openhome-core/save/Gen89/LASAV'
import { SVSAV } from '@openhome-core/save/Gen89/SVSAV'
import { SwShSAV } from '@openhome-core/save/Gen89/SwShSAV'
import { ZASAV } from '@openhome-core/save/Gen89/ZASAV'
import { Gen7AlolaSave } from '@openhome-core/save/Gen7AlolaSave'
import { HGSSSAV } from '@openhome-core/save/HGSSSAV'
import { LGPESAV } from '@openhome-core/save/LGPESAV'
import { ORASSAV } from '@openhome-core/save/ORASSAV'
import { PtSAV } from '@openhome-core/save/PtSAV'
import { G3RRSAV } from '@openhome-core/save/radicalred/G3RRSAV'
import { G3UBSAV } from '@openhome-core/save/unbound/G3UBSAV'
import { buildSaveFile, getPossibleSaveTypes } from '@openhome-core/save/util/load'
import { SAV, SaveWriter } from '@openhome-core/save/interfaces'
import { SAVClass } from '@openhome-core/save/util'
import { PathData } from '@openhome-core/save/util/path'
import { OHPKM } from '@openhome-core/pkm/OHPKM'
import { PKMInterface } from '@openhome-core/pkm/interfaces'
import {
  getMonFileIdentifier,
  getMonGen12Identifier,
  getMonGen345Identifier,
  shouldReuseTrackedGen345Mon,
} from '@openhome-core/pkm/Lookup'
import { G8LumiSAV } from '@openhome-core/save/luminescentplatinum/G8LUMISAV'
import { ConvertStrategy, initSync as initPkmRsWasm, SpeciesAndForm } from '@pkm-rs/pkg'
import dayjs from 'dayjs'
import fs from 'node:fs/promises'
import path from 'node:path'
import { randomUUID } from 'node:crypto'
import { fileURLToPath } from 'node:url'

type JsonObject = Record<string, unknown>
type LookupMap = Record<string, string>
type OhpkmStore = Record<string, OHPKM>

type OpenHomeBox = {
  id: string
  name: string | null
  index: number
  identifiers: Record<string, string>
}

type OpenHomeBank = {
  id: string
  name: string | null
  index: number
  boxes: OpenHomeBox[]
  current_box: number
}

type OpenHomeBanks = {
  banks: OpenHomeBank[]
  current_bank: number
}

type BridgeContext = {
  storageRoot: string
  store: OhpkmStore
  gen12: LookupMap
  gen345: LookupMap
  banks: OpenHomeBanks
}

const RESORT_CONVERT_STRATEGY: ConvertStrategy = {
  'nickname.capitalization': 'GameDefault',
  'metData.originAndLocation': 'MaximizeLegality',
}

console.log = (...items: unknown[]) => process.stderr.write(items.map(String).join(' ') + '\n')
console.warn = (...items: unknown[]) => process.stderr.write(items.map(String).join(' ') + '\n')
console.error = (...items: unknown[]) => process.stderr.write(items.map(String).join(' ') + '\n')

async function initWasm() {
  const bridgeDir = path.dirname(fileURLToPath(import.meta.url))
  const wasmPath = path.resolve(bridgeDir, '..', '..', 'pkm_rs', 'pkg', 'pkm_rs_bg.wasm')
  const wasmBytes = await fs.readFile(wasmPath)
  initPkmRsWasm({ module: wasmBytes })
}

const SAVE_TYPES: SAVClass[] = [
  G1SAV,
  G2SAV,
  G3SAV,
  DPSAV,
  PtSAV,
  HGSSSAV,
  BWSAV,
  BW2SAV,
  ORASSAV,
  XYSAV,
  Gen7AlolaSave,
  LGPESAV,
  SwShSAV,
  BDSPSAV,
  LASAV,
  SVSAV,
  ZASAV,
  G3RRSAV,
  G3UBSAV,
  G8LumiSAV,
]

// XYSAV imports G6SAV internally, but keeping it below the list avoids a long grouped import.
import { XYSAV } from '@openhome-core/save/XYSAV'

function jsonOk(payload: JsonObject) {
  process.stdout.write(JSON.stringify({ success: true, ...payload }) + '\n')
}

function jsonErr(message: string, payload: JsonObject = {}) {
  process.stdout.write(JSON.stringify({ success: false, error: message, ...payload }) + '\n')
  process.exitCode = 1
}

function arg(name: string, fallback?: string): string {
  const args = process.argv.slice(2).filter((item) => item !== '--')
  const prefix = `--${name}=`
  const inline = args.find((item) => item.startsWith(prefix))
  if (inline) return inline.slice(prefix.length)
  const index = args.indexOf(`--${name}`)
  if (index >= 0 && args[index + 1]) return args[index + 1]
  if (fallback !== undefined) return fallback
  throw new Error(`Missing --${name}`)
}

function intArg(name: string, fallback?: number): number {
  const raw = arg(name, fallback === undefined ? undefined : String(fallback))
  const value = Number(raw)
  if (!Number.isInteger(value) || value < 0) {
    throw new Error(`--${name} must be a non-negative integer`)
  }
  return value
}

function boolArg(name: string, fallback: boolean): boolean {
  const raw = arg(name, String(fallback))
  return raw === '1' || raw === 'true' || raw === 'yes'
}

function optionalInt(value: unknown): number | undefined {
  if (value === undefined || value === null) return undefined
  const parsed = Number(value)
  return Number.isInteger(parsed) && parsed >= 0 ? parsed : undefined
}

function pathData(raw: string): PathData {
  return {
    raw,
    dir: path.dirname(raw),
    name: path.basename(raw),
    ext: path.extname(raw),
    separator: path.sep,
  }
}

function bytesFromArrayBuffer(buffer: ArrayBuffer): Uint8Array {
  return new Uint8Array(buffer)
}

function base64(bytes: Uint8Array): string {
  return Buffer.from(bytes).toString('base64')
}

async function readJson<T>(filePath: string, fallback: T): Promise<T> {
  try {
    return JSON.parse(await fs.readFile(filePath, 'utf8')) as T
  } catch (error) {
    if ((error as NodeJS.ErrnoException).code === 'ENOENT') return fallback
    throw error
  }
}

async function writeJson(filePath: string, value: unknown) {
  await fs.mkdir(path.dirname(filePath), { recursive: true })
  await fs.writeFile(filePath, JSON.stringify(value, null, 2), 'utf8')
}

function defaultBanks(): OpenHomeBanks {
  return {
    banks: [
      {
        id: randomUUID(),
        name: null,
        index: 0,
        current_box: 0,
        boxes: Array.from({ length: 30 }, (_, index) => ({
          id: randomUUID(),
          name: null,
          index,
          identifiers: {},
        })),
      },
    ],
    current_bank: 0,
  }
}

function ensureHomeSlot(banks: OpenHomeBanks, bankIndex: number, boxIndex: number): OpenHomeBox {
  while (banks.banks.length <= bankIndex) {
    banks.banks.push({ ...defaultBanks().banks[0], index: banks.banks.length })
  }
  const bank = banks.banks[bankIndex]
  while (bank.boxes.length <= boxIndex) {
    bank.boxes.push({ id: randomUUID(), name: null, index: bank.boxes.length, identifiers: {} })
  }
  return bank.boxes[boxIndex]
}

function findHomeLocation(banks: OpenHomeBanks, openhomeId: string) {
  for (const [bankIndex, bank] of banks.banks.entries()) {
    for (const [boxIndex, box] of bank.boxes.entries()) {
      for (const [slot, id] of Object.entries(box.identifiers)) {
        if (id === openhomeId) return { bank: bankIndex, box: boxIndex, slot: Number(slot) }
      }
    }
  }
  return undefined
}

function removeHomePlacement(banks: OpenHomeBanks, openhomeId: string) {
  for (const bank of banks.banks) {
    for (const box of bank.boxes) {
      for (const [slot, id] of Object.entries(box.identifiers)) {
        if (id === openhomeId) delete box.identifiers[slot]
      }
    }
  }
}

function placeHome(banks: OpenHomeBanks, openhomeId: string, bank: number, box: number, slot: number) {
  removeHomePlacement(banks, openhomeId)
  ensureHomeSlot(banks, bank, box).identifiers[String(slot)] = openhomeId
}

async function loadContext(storageRoot: string): Promise<BridgeContext> {
  const store: OhpkmStore = {}
  const monsDir = path.join(storageRoot, 'mons_v2')
  await fs.mkdir(monsDir, { recursive: true })
  for (const entry of await fs.readdir(monsDir, { withFileTypes: true })) {
    if (!entry.isFile() || !entry.name.endsWith('.ohpkm')) continue
    const bytes = await fs.readFile(path.join(monsDir, entry.name))
    const ohpkm = OHPKM.fromBytes(bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength))
    store[ohpkm.openhomeId] = ohpkm
  }
  return {
    storageRoot,
    store,
    gen12: await readJson(path.join(storageRoot, 'gen12_lookup.json'), {}),
    gen345: await readJson(path.join(storageRoot, 'gen345_lookup.json'), {}),
    banks: await readJson(path.join(storageRoot, 'banks.json'), defaultBanks()),
  }
}

async function writeContext(ctx: BridgeContext) {
  const monsDir = path.join(ctx.storageRoot, 'mons_v2')
  await fs.mkdir(monsDir, { recursive: true })
  for (const ohpkm of Object.values(ctx.store)) {
    await fs.writeFile(path.join(monsDir, `${ohpkm.openhomeId}.ohpkm`), bytesFromArrayBuffer(ohpkm.toBytes()))
  }
  await writeJson(path.join(ctx.storageRoot, 'gen12_lookup.json'), ctx.gen12)
  await writeJson(path.join(ctx.storageRoot, 'gen345_lookup.json'), ctx.gen345)
  await writeJson(path.join(ctx.storageRoot, 'banks.json'), ctx.banks)
}

async function loadSave(savePath: string, explicitSaveType?: string): Promise<SAV> {
  const bytes = new Uint8Array(await fs.readFile(savePath))
  const candidates = explicitSaveType
    ? SAVE_TYPES.filter((saveType) => saveType.saveTypeID === explicitSaveType || saveType.saveTypeName === explicitSaveType)
    : getPossibleSaveTypes(bytes, SAVE_TYPES)
  if (candidates.length === 0) throw new Error(`Could not detect save type for ${savePath}`)
  if (candidates.length > 1) {
    throw new Error(`Ambiguous save type: ${candidates.map((saveType) => saveType.saveTypeID).join(', ')}`)
  }
  const result = buildSaveFile(pathData(savePath), bytes, candidates[0])
  if (result._tag === 'Err') throw new Error(result.err)
  if (!result.value) throw new Error(`Could not load save ${savePath}`)
  return result.value
}

async function writeSave(writer: SaveWriter) {
  await fs.writeFile(writer.filepath, writer.bytes)
}

function handleLookupsUpdate(ctx: BridgeContext, ohpkm: OHPKM, save: SAV) {
  const lookupType = (save.constructor as SAVClass).lookupType
  if (lookupType === 'gen12') {
    const identifier = getMonGen12Identifier(ohpkm)
    if (!identifier) throw new Error(`Could not build Gen 1/2 lookup for ${ohpkm.openhomeId}`)
    ctx.gen12[identifier] = ohpkm.openhomeId
  } else if (lookupType === 'gen345') {
    const identifier = getMonGen345Identifier(ohpkm)
    if (!identifier) throw new Error(`Could not build Gen 3/4/5 lookup for ${ohpkm.openhomeId}`)
    ctx.gen345[identifier] = ohpkm.openhomeId
  }
}

function loadIfTracked(ctx: BridgeContext, mon: PKMInterface): OHPKM | undefined {
  switch (mon.format) {
    case 'PK1':
    case 'PK2': {
      const key = getMonGen12Identifier(mon)
      return key ? ctx.store[ctx.gen12[key]] : undefined
    }
    case 'PK3':
    case 'COLOPKM':
    case 'XDPKM':
    case 'PK3RR':
    case 'PK3UB':
    case 'PK4':
    case 'PK5': {
      if (!shouldReuseTrackedGen345Mon(mon)) {
        return undefined
      }

      const key = getMonGen345Identifier(mon)
      return key ? ctx.store[ctx.gen345[key]] : undefined
    }
    case 'PK6':
    case 'PK7':
    case 'PB7':
    case 'PK8':
    case 'PA8':
    case 'PB8':
    case 'PB8LUMI':
    case 'PK9':
    case 'PA9': {
      const identifier = getMonFileIdentifier(mon)
      if (!identifier) return undefined
      const withoutOrigin = identifier.split('-').slice(0, -1).join('-')
      return Object.entries(ctx.store).find(([id]) => id.startsWith(withoutOrigin))?.[1]
    }
    default:
      throw new Error(`Unsupported Pokemon format: ${mon.format}`)
  }
}

function syncOpenSave(ctx: BridgeContext, save: SAV): string[] {
  const synced: string[] = []
  for (const mon of save.getAllMons()) {
    const tracked = loadIfTracked(ctx, mon)
    if (!tracked) continue
    tracked.syncWithGameData(mon, save)
    ctx.store[tracked.openhomeId] = tracked
    synced.push(tracked.openhomeId)
  }
  return synced
}

type BoxSlotKey = `${number}:${number}`

function slotKey(box: number, boxSlot: number): BoxSlotKey {
  return `${box}:${boxSlot}`
}

/** Partial PC sync for batch / targeted paths — O(|slots|), not O(entire PC). */
function syncOpenSaveSlots(ctx: BridgeContext, save: SAV, slots: Array<{ box: number; boxSlot: number }>): string[] {
  const synced: string[] = []
  const seen = new Set<BoxSlotKey>()
  for (const { box, boxSlot } of slots) {
    const key = slotKey(box, boxSlot)
    if (seen.has(key)) continue
    seen.add(key)
    const mon = save.getMonAt(box, boxSlot)
    if (!mon) continue
    const tracked = loadIfTracked(ctx, mon)
    if (!tracked) continue
    tracked.syncWithGameData(mon, save)
    ctx.store[tracked.openhomeId] = tracked
    synced.push(tracked.openhomeId)
  }
  return synced
}

type SaveIoCounters = { save_loads: number; save_writes: number }

let activeSaveIoCounters: SaveIoCounters = { save_loads: 0, save_writes: 0 }

function resetSaveIoCounters() {
  activeSaveIoCounters = { save_loads: 0, save_writes: 0 }
}

async function trackedLoadSave(savePath: string, explicitSaveType?: string): Promise<SAV> {
  activeSaveIoCounters.save_loads += 1
  return loadSave(savePath, explicitSaveType)
}

async function trackedWriteSave(writer: SaveWriter) {
  activeSaveIoCounters.save_writes += 1
  await writeSave(writer)
}

function traceOpenHomePerfEnabled(): boolean {
  return process.env.PDSM_TRACE_OPENHOME_PERF === '1'
}

function logOpenHomePerf(phase: string, ms: number, extra?: string) {
  if (!traceOpenHomePerfEnabled()) return
  process.stderr.write(`[openhome-perf] ${phase} ${ms.toFixed(2)}ms${extra ? ` ${extra}` : ''}\n`)
}

function warnFakeBatchIfNeeded(operation: string) {
  if (activeSaveIoCounters.save_loads > 1 || activeSaveIoCounters.save_writes > 1) {
    process.stderr.write(
      `[openhome-perf-warning] fake batch detected operation=${operation} save_loads=${activeSaveIoCounters.save_loads} save_writes=${activeSaveIoCounters.save_writes}\n`
    )
  }
}

function convertOhpkmWithFormRetry(save: SAV, tracked: OHPKM): PKMInterface {
  try {
    return save.convertOhpkm(tracked, RESORT_CONVERT_STRATEGY)
  } catch (first) {
    const msg = first instanceof Error ? first.message : String(first)
    if (!/form|Form|species|Species|invalid|Invalid|support|Support/.test(msg)) {
      throw first
    }
    const buf = bytesFromArrayBuffer(tracked.toBytes())
    const clone = OHPKM.fromBytes(buf.buffer.slice(buf.byteOffset, buf.byteOffset + buf.byteLength))
    clone.SpeciesAndForm = new SpeciesAndForm(tracked.dexNum, 0)
    return save.convertOhpkm(clone, RESORT_CONVERT_STRATEGY)
  }
}

function startTrackingNewMon(ctx: BridgeContext, mon: PKMInterface, sourceSave: SAV, destSave?: SAV): OHPKM {
  const ohpkm = OHPKM.fromMonInSave(mon, sourceSave)
  ohpkm.startedTrackingTimestamp = dayjs()
  if (destSave) handleLookupsUpdate(ctx, ohpkm, destSave)
  ctx.store[ohpkm.openhomeId] = ohpkm
  return ohpkm
}

function convertForSave(ctx: BridgeContext, ohpkm: OHPKM, save: SAV): PKMInterface {
  handleLookupsUpdate(ctx, ohpkm, save)
  ctx.store[ohpkm.openhomeId] = ohpkm
  return convertOhpkmWithFormRetry(save, ohpkm)
}

function prepareTrackedMonsForSave(ctx: BridgeContext, save: SAV) {
  if (save.updatedBoxSlots.length > 0) {
    const seen = new Set<BoxSlotKey>()
    for (const { box, boxSlot } of save.updatedBoxSlots) {
      const key = slotKey(box, boxSlot)
      if (seen.has(key)) continue
      seen.add(key)
      const mon = save.getMonAt(box, boxSlot)
      if (!mon) continue
      const tracked = loadIfTracked(ctx, mon)
      if (!tracked) continue
      tracked.tradeToSave(save)
      save.setMonAt(box, boxSlot, convertOhpkmWithFormRetry(save, tracked))
      ctx.store[tracked.openhomeId] = tracked
    }
    return
  }
  for (let box = 0; box < save.getBoxCount(); box++) {
    for (let slot = 0; slot < save.boxSlotCount; slot++) {
      const mon = save.getMonAt(box, slot)
      if (!mon) continue
      const tracked = loadIfTracked(ctx, mon)
      if (!tracked) continue
      tracked.tradeToSave(save)
      save.setMonAt(box, slot, convertOhpkmWithFormRetry(save, tracked))
      ctx.store[tracked.openhomeId] = tracked
    }
  }
}

async function commandBatchPullToHome() {
  resetSaveIoCounters()
  const t0 = Date.now()
  const timings: Record<string, number> = {}
  const mark = () => Date.now()
  let last = mark()

  const storageRoot = arg('storage-root')
  const savePath = arg('save')
  const opsPath = arg('ops-json')
  const write = boolArg('write-save', true)
  const raw = await fs.readFile(opsPath, 'utf8')
  const parsed = JSON.parse(raw) as unknown
  if (!Array.isArray(parsed) || parsed.length === 0) {
    throw new Error('ops-json must be a non-empty JSON array')
  }

  const ctx = await loadContext(storageRoot)
  timings.ms_load_context = mark() - last
  last = mark()

  const save = await trackedLoadSave(savePath, arg('save-type', ''))
  timings.ms_load_save = mark() - last
  last = mark()

  const syncSlots: Array<{ box: number; boxSlot: number }> = []
  const ops: Array<{
    box: number
    slot: number
    homeBank: number
    homeBox: number
    homeSlot: number
  }> = []

  for (const item of parsed) {
    if (!item || typeof item !== 'object') throw new Error('ops-json entries must be objects')
    const o = item as Record<string, unknown>
    const box = Number(o.box ?? o.sourceBox ?? o.source_box)
    const slot = Number(o.slot ?? o.sourceSlot ?? o.source_slot)
    const homeBank = Number(o.home_bank ?? o.homeBank ?? 0)
    const homeBox = Number(o.home_box ?? o.homeBox ?? 0)
    const homeSlot = Number(o.home_slot ?? o.homeSlot)
    if (!Number.isInteger(box) || !Number.isInteger(slot) || !Number.isInteger(homeSlot)) {
      throw new Error('ops-json each entry needs integer box, slot, home_slot (and optional home_bank, home_box)')
    }
    ops.push({ box, slot, homeBank, homeBox, homeSlot })
    syncSlots.push({ box, boxSlot: slot })
  }

  const synced = syncOpenSaveSlots(ctx, save, syncSlots)
  timings.ms_sync_slots = mark() - last
  last = mark()

  const pulls: JsonObject[] = []
  for (const op of ops) {
    const { box, slot, homeBank, homeBox, homeSlot } = op
    const mon = save.getMonAt(box, slot)
    if (!mon) throw new Error(`No Pokemon at save box ${box}, slot ${slot}`)

    const displacedHomeId = ensureHomeSlot(ctx.banks, homeBank, homeBox).identifiers[String(homeSlot)]
    const ohpkm = loadIfTracked(ctx, mon) ?? startTrackingNewMon(ctx, mon, save)
    placeHome(ctx.banks, ohpkm.openhomeId, homeBank, homeBox, homeSlot)

    if (displacedHomeId) {
      const displaced = ctx.store[displacedHomeId]
      if (!displaced) throw new Error(`Home slot referenced missing OHPKM ${displacedHomeId}`)
      save.setMonAt(box, slot, convertForSave(ctx, displaced, save))
    } else {
      save.setMonAt(box, slot, undefined)
    }
    save.updatedBoxSlots.push({ box, boxSlot: slot })

    pulls.push({
      openhomeId: ohpkm.openhomeId,
      ohpkmBase64: base64(bytesFromArrayBuffer(ohpkm.toBytes())),
      displacedHomeOpenhomeId: displacedHomeId ?? null,
      sourceBox: box,
      sourceSlot: slot,
      homeLocation: { bank: homeBank, box: homeBox, slot: homeSlot },
    })
  }
  timings.ms_apply_ops = mark() - last
  last = mark()

  if (write) {
    prepareTrackedMonsForSave(ctx, save)
    timings.ms_prepare_tracked = mark() - last
    last = mark()
    await trackedWriteSave(save.prepareWriter())
    timings.ms_write_save_file = mark() - last
    last = mark()
  } else {
    timings.ms_prepare_tracked = 0
    timings.ms_write_save_file = 0
  }

  await writeContext(ctx)
  timings.ms_write_context = mark() - last

  warnFakeBatchIfNeeded('batch-pull-to-home')
  const totalMs = Date.now() - t0
  const perf = {
    save_loads: activeSaveIoCounters.save_loads,
    save_writes: activeSaveIoCounters.save_writes,
    count: ops.length,
    total_ms: totalMs,
    ...timings,
  }
  logOpenHomePerf('batch-pull-to-home', totalMs, `count=${ops.length} loads=${perf.save_loads} writes=${perf.save_writes}`)
  jsonOk({
    operation: 'batch-pull-to-home',
    pulls,
    syncedOpenhomeIds: synced,
    sourceSaveWritten: write,
    perf,
  })
}

async function commandBatchPushToGame() {
  resetSaveIoCounters()
  const t0 = Date.now()
  const timings: Record<string, number> = {}
  const mark = () => Date.now()
  let last = mark()

  const storageRoot = arg('storage-root')
  const savePath = arg('save')
  const opsPath = arg('ops-json')
  const write = boolArg('write-save', true)
  const raw = await fs.readFile(opsPath, 'utf8')
  const parsed = JSON.parse(raw) as unknown
  if (!Array.isArray(parsed) || parsed.length === 0) {
    throw new Error('ops-json must be a non-empty JSON array')
  }

  const ctx = await loadContext(storageRoot)
  timings.ms_load_context = mark() - last
  last = mark()

  const save = await trackedLoadSave(savePath, arg('save-type', ''))
  timings.ms_load_save = mark() - last
  last = mark()

  const syncSlots: Array<{ box: number; boxSlot: number }> = []
  const ops: Array<{ openhomeId: string; box: number; slot: number }> = []
  for (const item of parsed) {
    if (!item || typeof item !== 'object') throw new Error('ops-json entries must be objects')
    const o = item as Record<string, unknown>
    const openhomeId = String(o.openhome_id ?? o.openhomeId ?? '')
    const box = Number(o.box ?? o.destBox ?? o.dest_box)
    const slot = Number(o.slot ?? o.destSlot ?? o.dest_slot)
    if (!openhomeId || !Number.isInteger(box) || !Number.isInteger(slot)) {
      throw new Error('ops-json each entry needs openhome_id, box, slot')
    }
    ops.push({ openhomeId, box, slot })
    syncSlots.push({ box, boxSlot: slot })
  }

  const synced = syncOpenSaveSlots(ctx, save, syncSlots)
  timings.ms_sync_slots = mark() - last
  last = mark()

  const pushes: JsonObject[] = []
  for (const op of ops) {
    const { openhomeId, box, slot } = op
    const ohpkm = ctx.store[openhomeId]
    if (!ohpkm) throw new Error(`Unknown OpenHome ID ${openhomeId}`)

    const sourceHomeLocation = findHomeLocation(ctx.banks, openhomeId)
    const displaced = save.getMonAt(box, slot)
    ohpkm.tradeToSave(save)
    save.setMonAt(box, slot, convertForSave(ctx, ohpkm, save))
    save.updatedBoxSlots.push({ box, boxSlot: slot })

    if (sourceHomeLocation) {
      if (displaced) {
        const displacedOhpkm = loadIfTracked(ctx, displaced) ?? startTrackingNewMon(ctx, displaced, save)
        placeHome(
          ctx.banks,
          displacedOhpkm.openhomeId,
          sourceHomeLocation.bank,
          sourceHomeLocation.box,
          sourceHomeLocation.slot
        )
      } else {
        removeHomePlacement(ctx.banks, openhomeId)
      }
    }

    pushes.push({
      openhomeId,
      ohpkmBase64: base64(bytesFromArrayBuffer(ohpkm.toBytes())),
      sourceHomeLocation: sourceHomeLocation ?? null,
      targetLocation: { box, slot },
    })
  }
  timings.ms_apply_ops = mark() - last
  last = mark()

  if (write) {
    prepareTrackedMonsForSave(ctx, save)
    timings.ms_prepare_tracked = mark() - last
    last = mark()
    await trackedWriteSave(save.prepareWriter())
    timings.ms_write_save_file = mark() - last
    last = mark()
  } else {
    timings.ms_prepare_tracked = 0
    timings.ms_write_save_file = 0
  }

  await writeContext(ctx)
  timings.ms_write_context = mark() - last

  warnFakeBatchIfNeeded('batch-push-to-game')
  const totalMs = Date.now() - t0
  const perf = {
    save_loads: activeSaveIoCounters.save_loads,
    save_writes: activeSaveIoCounters.save_writes,
    count: ops.length,
    total_ms: totalMs,
    ...timings,
  }
  logOpenHomePerf('batch-push-to-game', totalMs, `count=${ops.length} loads=${perf.save_loads} writes=${perf.save_writes}`)
  jsonOk({
    operation: 'batch-push-to-game',
    pushes,
    syncedOpenhomeIds: synced,
    targetSaveWritten: write,
    perf,
  })
}

async function commandDetectSave() {
  const savePath = arg('save')
  const save = await loadSave(savePath, arg('save-type', ''))
  jsonOk({
    saveType: (save.constructor as SAVClass).saveTypeID,
    saveTypeName: (save.constructor as SAVClass).saveTypeName,
    trainerName: save.name,
    displayID: save.displayID,
    boxCount: save.getBoxCount(),
    boxSlotCount: save.boxSlotCount,
  })
}

async function commandPullToHome() {
  resetSaveIoCounters()
  const t0 = Date.now()
  const storageRoot = arg('storage-root')
  const savePath = arg('save')
  const box = intArg('box')
  const slot = intArg('slot')
  const homeBank = intArg('home-bank', 0)
  const homeBox = intArg('home-box', 0)
  const homeSlot = intArg('home-slot')
  const write = boolArg('write-save', true)
  const ctx = await loadContext(storageRoot)
  const save = await trackedLoadSave(savePath, arg('save-type', ''))
  const synced = syncOpenSaveSlots(ctx, save, [{ box, boxSlot: slot }])
  const mon = save.getMonAt(box, slot)
  if (!mon) throw new Error(`No Pokemon at save box ${box}, slot ${slot}`)

  const displacedHomeId = ensureHomeSlot(ctx.banks, homeBank, homeBox).identifiers[String(homeSlot)]
  const ohpkm = loadIfTracked(ctx, mon) ?? startTrackingNewMon(ctx, mon, save)
  placeHome(ctx.banks, ohpkm.openhomeId, homeBank, homeBox, homeSlot)

  if (displacedHomeId) {
    const displaced = ctx.store[displacedHomeId]
    if (!displaced) throw new Error(`Home slot referenced missing OHPKM ${displacedHomeId}`)
    save.setMonAt(box, slot, convertForSave(ctx, displaced, save))
  } else {
    save.setMonAt(box, slot, undefined)
  }
  save.updatedBoxSlots.push({ box, boxSlot: slot })
  if (write) {
    prepareTrackedMonsForSave(ctx, save)
    await trackedWriteSave(save.prepareWriter())
  }
  await writeContext(ctx)
  const totalMs = Date.now() - t0
  const perf = {
    save_loads: activeSaveIoCounters.save_loads,
    save_writes: activeSaveIoCounters.save_writes,
    count: 1,
    total_ms: totalMs,
  }
  logOpenHomePerf('pull-to-home', totalMs, `loads=${perf.save_loads} writes=${perf.save_writes}`)
  jsonOk({
    operation: 'pull-to-home',
    openhomeId: ohpkm.openhomeId,
    ohpkmBase64: base64(bytesFromArrayBuffer(ohpkm.toBytes())),
    syncedOpenhomeIds: synced,
    displacedHomeOpenhomeId: displacedHomeId ?? null,
    sourceSaveWritten: write,
    homeLocation: { bank: homeBank, box: homeBox, slot: homeSlot },
    perf,
  })
}

async function commandPushToGame() {
  resetSaveIoCounters()
  const t0 = Date.now()
  const storageRoot = arg('storage-root')
  const savePath = arg('save')
  const openhomeId = arg('openhome-id')
  const box = intArg('box')
  const slot = intArg('slot')
  const write = boolArg('write-save', true)
  const ctx = await loadContext(storageRoot)
  const save = await trackedLoadSave(savePath, arg('save-type', ''))
  const synced = syncOpenSaveSlots(ctx, save, [{ box, boxSlot: slot }])
  const ohpkm = ctx.store[openhomeId]
  if (!ohpkm) throw new Error(`Unknown OpenHome ID ${openhomeId}`)

  const sourceHomeLocation = findHomeLocation(ctx.banks, openhomeId)
  const displaced = save.getMonAt(box, slot)
  ohpkm.tradeToSave(save)
  save.setMonAt(box, slot, convertForSave(ctx, ohpkm, save))
  save.updatedBoxSlots.push({ box, boxSlot: slot })

  if (sourceHomeLocation) {
    if (displaced) {
      const displacedOhpkm = loadIfTracked(ctx, displaced) ?? startTrackingNewMon(ctx, displaced, save)
      placeHome(
        ctx.banks,
        displacedOhpkm.openhomeId,
        sourceHomeLocation.bank,
        sourceHomeLocation.box,
        sourceHomeLocation.slot
      )
    } else {
      removeHomePlacement(ctx.banks, openhomeId)
    }
  }

  if (write) {
    prepareTrackedMonsForSave(ctx, save)
    await trackedWriteSave(save.prepareWriter())
  }
  await writeContext(ctx)
  const totalMs = Date.now() - t0
  const perf = {
    save_loads: activeSaveIoCounters.save_loads,
    save_writes: activeSaveIoCounters.save_writes,
    count: 1,
    total_ms: totalMs,
  }
  logOpenHomePerf('push-to-game', totalMs, `loads=${perf.save_loads} writes=${perf.save_writes}`)
  jsonOk({
    operation: 'push-to-game',
    openhomeId,
    ohpkmBase64: base64(bytesFromArrayBuffer(ohpkm.toBytes())),
    syncedOpenhomeIds: synced,
    targetSaveWritten: write,
    sourceHomeLocation: sourceHomeLocation ?? null,
    targetLocation: { box, slot },
    perf,
  })
}

async function commandSyncSave() {
  const storageRoot = arg('storage-root')
  const savePath = arg('save')
  const ctx = await loadContext(storageRoot)
  const save = await loadSave(savePath, arg('save-type', ''))
  const synced = syncOpenSave(ctx, save)
  await writeContext(ctx)
  jsonOk({ operation: 'sync-save', syncedOpenhomeIds: synced })
}

async function commandSupportsMons() {
  const savePath = arg('save')
  const save = await loadSave(savePath, arg('save-type', ''))
  const rawMons = JSON.parse(arg('mons-json', '[]')) as unknown
  if (!Array.isArray(rawMons)) throw new Error('--mons-json must be an array')

  const results = rawMons.map((item) => {
    if (!item || typeof item !== 'object') throw new Error('--mons-json entries must be objects')
    const obj = item as Record<string, unknown>
    const dexNumber = optionalInt(obj.dexNumber ?? obj.dex_number)
    const formeNumber = optionalInt(obj.formeNumber ?? obj.form_number ?? obj.form)
    const extraFormIndex = optionalInt(obj.extraFormIndex ?? obj.extra_form_index)
    if (dexNumber === undefined) throw new Error('mon entry missing dexNumber')
    return {
      dexNumber,
      formeNumber: formeNumber ?? 0,
      extraFormIndex: extraFormIndex ?? null,
      supported: save.supportsMon(dexNumber, formeNumber ?? 0, extraFormIndex),
    }
  })

  jsonOk({
    operation: 'supports-mons',
    saveType: (save.constructor as SAVClass).saveTypeID,
    results,
  })
}

async function main() {
  await initWasm()
  const command = process.argv.slice(2).find((item) => item !== '--' && !item.startsWith('--'))
  if (!command) throw new Error('Missing command')
  switch (command) {
    case 'detect-save':
      await commandDetectSave()
      break
    case 'sync-save':
      await commandSyncSave()
      break
    case 'pull-to-home':
      await commandPullToHome()
      break
    case 'push-to-game':
      await commandPushToGame()
      break
    case 'batch-pull-to-home':
      await commandBatchPullToHome()
      break
    case 'batch-push-to-game':
      await commandBatchPushToGame()
      break
    case 'supports-mons':
      await commandSupportsMons()
      break
    default:
      throw new Error(`Unknown command ${command}`)
  }
}

main().catch((error) => jsonErr(error instanceof Error ? error.message : String(error)))
