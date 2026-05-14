import { Language } from '@pkm-rs/pkg'
import { PKMDate, Stats } from './types'

export function validGen4Language(language: Language): boolean {
  return language >= Language.Japanese && language <= Language.SpanishSpain
}

export function validPkmDate(date: PKMDate | undefined): date is PKMDate {
  if (!date) return false
  if (date.year < 2000 || date.year > 2099) return false
  if (date.month < 1 || date.month > 12) return false

  const maxDay = new Date(date.year, date.month, 0).getDate()
  return date.day >= 1 && date.day <= maxDay
}

export function currentPkmDate(): PKMDate {
  const now = new Date()
  return {
    month: now.getMonth() + 1,
    day: now.getDate(),
    year: now.getFullYear(),
  }
}

function normalizeCappedEvs(stats: Stats, perStatMax: number): Stats {
  const capped = {
    hp: Math.min(Math.max(Math.trunc(stats.hp), 0), perStatMax),
    atk: Math.min(Math.max(Math.trunc(stats.atk), 0), perStatMax),
    def: Math.min(Math.max(Math.trunc(stats.def), 0), perStatMax),
    spe: Math.min(Math.max(Math.trunc(stats.spe), 0), perStatMax),
    spa: Math.min(Math.max(Math.trunc(stats.spa), 0), perStatMax),
    spd: Math.min(Math.max(Math.trunc(stats.spd), 0), perStatMax),
  }
  const keys: Array<keyof Stats> = ['hp', 'atk', 'def', 'spe', 'spa', 'spd']
  const total = keys.reduce((sum, key) => sum + capped[key], 0)
  if (total <= 510) return capped
  if (total <= 0) return capped

  const scaled = { ...capped }
  const fractions = keys.map((key) => {
    const exact = (capped[key] * 510) / total
    scaled[key] = Math.floor(exact)
    return { key, fraction: exact - scaled[key] }
  })
  let remaining = 510 - keys.reduce((sum, key) => sum + scaled[key], 0)
  fractions.sort((a, b) => b.fraction - a.fraction)
  for (const { key } of fractions) {
    if (remaining <= 0) break
    scaled[key] += 1
    remaining--
  }
  return scaled
}

export function normalizeGen4Evs(stats: Stats): Stats {
  return normalizeCappedEvs(stats, 255)
}

export function normalizeGbOriginTransferEvs(stats: Stats): Stats {
  return normalizeCappedEvs(stats, 100)
}
