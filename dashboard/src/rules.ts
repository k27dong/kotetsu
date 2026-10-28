import { lunarOf } from "./lunar";
import { daysBetween, parseDate, warsawDate, type Slot } from "./time";

export type PinKind = "date" | "yearly" | "lunar_yearly" | "monthly" | "weekly";
export type PinSlot = "morning" | "evening" | "all_day";
export type Pin = {
  id: number;
  phraseId: number;
  kind: PinKind;
  date: string | null;
  month: number | null;
  day: number | null;
  weekday: number | null;
  slot: PinSlot;
};
export type PhraseRow = {
  id: number;
  text: string;
  lastShownAt: string | null;
  problem: string | null;
};

export type Icon = "heart" | "rings" | "cake" | "cat" | "hearts" | "star";
export type SpecialDay = {
  id: number;
  kind: "yearly" | "lunar_yearly";
  month: number;
  day: number;
  greeting: string;
  baseYear: number | null;
  icon: Icon;
  rank: number;
};
export type Greeting = { icon: Icon; text: string };

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

function pinField<Field extends "date" | "month" | "day" | "weekday">(
  pin: Pin,
  field: Field,
): NonNullable<Pin[Field]> {
  const value = pin[field];
  if (value === null) throw new Error(`Pin ${pin.id} (${pin.kind}) has no ${field}`);
  return value;
}

function isoWeekday(date: string): number {
  const { year, month, day } = parseDate(date);
  return ((new Date(Date.UTC(year, month - 1, day)).getUTCDay() + 6) % 7) + 1;
}

function lunarMatches(month: number, day: number, date: string): boolean {
  const lunar = lunarOf(date);
  if (lunar.leap || lunar.month !== month) return false;
  return lunar.day === day || (day === 30 && lunar.day === lunar.monthLength);
}

export function pinMatchesDate(pin: Pin, date: string): boolean {
  const { month, day } = parseDate(date);
  switch (pin.kind) {
    case "date":
      return pinField(pin, "date") === date;
    case "yearly":
      return pinField(pin, "month") === month && pinField(pin, "day") === day;
    case "lunar_yearly":
      return lunarMatches(pinField(pin, "month"), pinField(pin, "day"), date);
    case "monthly":
      return pinField(pin, "day") === day;
    case "weekly":
      return pinField(pin, "weekday") === isoWeekday(date);
  }
}

function shownAtMs(phrase: PhraseRow): number {
  if (phrase.lastShownAt === null) return -Infinity;
  const time = Date.parse(phrase.lastShownAt);
  if (Number.isNaN(time)) throw new Error(`Phrase ${phrase.id} has an unreadable last_shown_at`);
  return time;
}

function compareShown(a: PhraseRow, b: PhraseRow): number {
  const [x, y] = [shownAtMs(a), shownAtMs(b)];
  return x < y ? -1 : x > y ? 1 : 0;
}

export function pinnedPhraseFor(slot: Slot, pins: Pin[], phrases: PhraseRow[]): number | null {
  const byId = new Map(phrases.map((phrase) => [phrase.id, phrase]));
  const phraseOf = (pin: Pin): PhraseRow => {
    const phrase = byId.get(pin.phraseId);
    if (phrase === undefined) {
      throw new Error(`Pin ${pin.id} points at phrase ${pin.phraseId}, which isn't in the list`);
    }
    return phrase;
  };
  const candidates = pins.filter(
    (pin) =>
      (pin.slot === "all_day" || pin.slot === slot.kind) &&
      pinMatchesDate(pin, slot.date) &&
      phraseOf(pin).problem === null,
  );
  const hadTheMorning = (pin: Pin): boolean => {
    const shown = shownAtMs(phraseOf(pin));
    return (
      pin.slot === "all_day" &&
      Number.isFinite(shown) &&
      shown < slot.start.getTime() &&
      warsawDate(new Date(shown)) === slot.date
    );
  };
  // an all-day pin that took the morning leaves the evening to the next one
  const waiting =
    slot.kind === "evening" ? candidates.filter((pin) => !hadTheMorning(pin)) : candidates;
  const ranked = (waiting.length > 0 ? waiting : candidates).toSorted(
    (a, b) =>
      Number(a.slot === "all_day") - Number(b.slot === "all_day") ||
      KIND_PRECEDENCE[a.kind] - KIND_PRECEDENCE[b.kind] ||
      compareShown(phraseOf(a), phraseOf(b)) ||
      a.id - b.id,
  );
  return ranked[0]?.phraseId ?? null;
}

export function rotationPick(
  phrases: PhraseRow[],
  pinnedIds: Set<number>,
  random: () => number,
): number | null {
  const eligible = phrases.filter((phrase) => !pinnedIds.has(phrase.id) && phrase.problem === null);
  const oldest = Math.min(...eligible.map(shownAtMs));
  const ties = eligible.filter((phrase) => shownAtMs(phrase) === oldest);
  if (ties.length === 0) return null;
  const pick = ties[Math.floor(random() * ties.length)];
  if (pick === undefined) {
    throw new Error("random() must give a number from 0 up to, but not including, 1");
  }
  return pick.id;
}

export function counterDays(date: string): number {
  return daysBetween(COUNTER_DAY_ZERO, date);
}

export function milestoneText(days: number): string | null {
  return MILESTONES.get(days) ?? null;
}

function fallsOn(special: SpecialDay, date: string): boolean {
  if (special.kind === "lunar_yearly") return lunarMatches(special.month, special.day, date);
  const { month, day } = parseDate(date);
  return special.month === month && special.day === day;
}

function specialDayGreeting(special: SpecialDay, year: number): Greeting | null {
  if (!special.greeting.includes("{n}")) return { icon: special.icon, text: special.greeting };
  if (special.baseYear === null) {
    throw new Error(`Special day ${special.id} says {n} but has no base year`);
  }
  const n = year - special.baseYear;
  return n < 1 ? null : { icon: special.icon, text: special.greeting.replaceAll("{n}", String(n)) };
}

export function greetingFor(date: string, specialDays: SpecialDay[]): Greeting | null {
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
}
