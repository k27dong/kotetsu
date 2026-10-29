import type { Greeting, PinFields, PinKind, SpecialDayFields } from "./api_types";
import { lunarOf } from "./lunar";
import { daysBetween, parseDate, warsawInstant, warsawMinuteOfDay, type Slot } from "./time";

export type Pin = { id: number; phraseId: number } & PinFields;
export type PhraseRow = {
  id: number;
  text: string;
  lastShownAt: string | null;
  problem: string | null;
};
export type SpecialDay = { id: number } & SpecialDayFields;

type PinField = "date" | "month" | "day" | "weekday";
type Candidate = { pin: Pin; shown: number };

const COUNTER_DAY_ZERO = "2021-09-01";
const MILESTONE_RANK = 40;
const MILESTONES = new Map([
  [2000, "两千天"],
  [3000, "三千天"],
  [4000, "四千天"],
  [5000, "五千天"],
  [6000, "六千天"],
  [7000, "七千天"],
  [8000, "八千天"],
  [9000, "九千天"],
  [10000, "一万天"],
]);
const KIND_PRECEDENCE: Record<PinKind, number> = {
  date: 0,
  lunar_yearly: 1,
  yearly: 2,
  monthly: 3,
  weekly: 4,
};
const PIN_FIELDS: Record<PinKind, PinField[]> = {
  date: ["date"],
  yearly: ["month", "day"],
  lunar_yearly: ["month", "day"],
  monthly: ["day"],
  weekly: ["weekday"],
};

const isoWeekday = (date: string): number => {
  const { year, month, day } = parseDate(date);
  return ((new Date(Date.UTC(year, month - 1, day)).getUTCDay() + 6) % 7) + 1;
};

const lunarMatches = (month: number, day: number, date: string): boolean => {
  const lunar = lunarOf(date);
  if (lunar.leap || lunar.month !== month) return false;
  return lunar.day === day || (day === 30 && lunar.day === lunar.monthLength);
};

export const pinMatchesDate = (pin: Pin, date: string): boolean => {
  const missing = PIN_FIELDS[pin.kind].find((field) => pin[field] === null);
  if (missing !== undefined) {
    console.error(`Pin ${pin.id} (${pin.kind}) has no ${missing}; skipped`);
    return false;
  }
  const { month, day } = parseDate(date);
  switch (pin.kind) {
    case "date":
      return pin.date === date;
    case "yearly":
      return pin.month === month && pin.day === day;
    case "lunar_yearly":
      return pin.month !== null && pin.day !== null && lunarMatches(pin.month, pin.day, date);
    case "monthly":
      return pin.day === day;
    case "weekly":
      return pin.weekday === isoWeekday(date);
  }
};

const lastShownMs = (phrase: PhraseRow): number | null => {
  if (phrase.lastShownAt === null) return -Infinity;
  const time = Date.parse(phrase.lastShownAt);
  if (Number.isNaN(time)) {
    console.error(`Phrase ${phrase.id} has an unreadable last_shown_at; skipped`);
    return null;
  }
  return time;
};

const compareNumbers = (a: number, b: number): number => (a < b ? -1 : a > b ? 1 : 0);

const withoutTheMorningsPick = (candidates: Candidate[], evening: Slot): Candidate[] => {
  // the evening ends where the next morning starts, so that wall time is the first phrase time
  const morningStart = warsawInstant(evening.date, warsawMinuteOfDay(evening.end)).getTime();
  return candidates.filter(
    ({ pin, shown }) =>
      !(pin.slot === "all_day" && shown >= morningStart && shown < evening.start.getTime()),
  );
};

export const pinnedPhraseFor = (slot: Slot, pins: Pin[], phrases: PhraseRow[]): number | null => {
  const byId = new Map(phrases.map((phrase) => [phrase.id, phrase]));
  const candidates: Candidate[] = [];
  for (const pin of pins) {
    const phrase = byId.get(pin.phraseId);
    if (phrase === undefined) {
      console.error(
        `Pin ${pin.id} points at phrase ${pin.phraseId}, which isn't in the list; skipped`,
      );
      continue;
    }
    if (phrase.problem !== null) continue;
    const shown = lastShownMs(phrase);
    if (shown === null) continue;
    if ((pin.slot === "all_day" || pin.slot === slot.kind) && pinMatchesDate(pin, slot.date)) {
      candidates.push({ pin, shown });
    }
  }
  // the morning's pick is visible only through last_shown_at
  const waiting = slot.kind === "evening" ? withoutTheMorningsPick(candidates, slot) : candidates;
  const ranked = (waiting.length > 0 ? waiting : candidates).toSorted(
    (a, b) =>
      Number(a.pin.slot === "all_day") - Number(b.pin.slot === "all_day") ||
      KIND_PRECEDENCE[a.pin.kind] - KIND_PRECEDENCE[b.pin.kind] ||
      compareNumbers(a.shown, b.shown) ||
      a.pin.id - b.pin.id,
  );
  return ranked[0]?.pin.phraseId ?? null;
};

export const rotationPick = (
  phrases: PhraseRow[],
  pinnedIds: Set<number>,
  random: () => number,
): number | null => {
  const eligible: { id: number; shown: number }[] = [];
  for (const phrase of phrases) {
    if (pinnedIds.has(phrase.id) || phrase.problem !== null) continue;
    const shown = lastShownMs(phrase);
    if (shown !== null) eligible.push({ id: phrase.id, shown });
  }
  const oldest = Math.min(...eligible.map(({ shown }) => shown));
  const ties = eligible.filter(({ shown }) => shown === oldest);
  if (ties.length === 0) return null;
  const pick = ties[Math.floor(random() * ties.length)];
  if (pick === undefined) {
    throw new Error("random() must give a number from 0 up to, but not including, 1");
  }
  return pick.id;
};

export const counterDays = (date: string): number => daysBetween(COUNTER_DAY_ZERO, date);

export const milestoneText = (days: number): string | null => MILESTONES.get(days) ?? null;

const fallsOn = (special: SpecialDay, date: string): boolean => {
  if (special.kind === "lunar_yearly") return lunarMatches(special.month, special.day, date);
  const { month, day } = parseDate(date);
  return special.month === month && special.day === day;
};

const specialDayGreeting = (special: SpecialDay, year: number): Greeting | null => {
  if (!special.greeting.includes("{n}")) return { icon: special.icon, text: special.greeting };
  if (special.baseYear === null) {
    console.error(`Special day ${special.id} says {n} but has no base year; skipped`);
    return null;
  }
  const n = year - special.baseYear;
  return n < 1 ? null : { icon: special.icon, text: special.greeting.replaceAll("{n}", String(n)) };
};

export const greetingFor = (date: string, specialDays: SpecialDay[]): Greeting | null => {
  const { year } = parseDate(date);
  const milestone = milestoneText(counterDays(date));
  const todays = specialDays
    .filter((special) => fallsOn(special, date))
    .toSorted((a, b) => a.rank - b.rank || a.id - b.id);
  for (const special of todays) {
    if (milestone !== null && special.rank > MILESTONE_RANK) break;
    const greeting = specialDayGreeting(special, year);
    if (greeting !== null) return greeting;
  }
  return milestone === null ? null : { icon: "star", text: milestone };
};
