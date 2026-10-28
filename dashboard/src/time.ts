export type PhraseTimes = { first: string; second: string };
export type Slot = { kind: "morning" | "evening"; date: string; start: Date; end: Date };
export type CalendarDate = { year: number; month: number; day: number };

const MINUTE_MS = 60 * 1000;
const DAY_MS = 24 * 60 * MINUTE_MS;
const CALENDAR_DATE = /^\d{4}-\d{2}-\d{2}$/;
const PHRASE_TIME = /^\d{2}:\d{2}$/;

const warsawClock = new Intl.DateTimeFormat("en-US", {
  timeZone: "Europe/Warsaw",
  hourCycle: "h23",
  year: "numeric",
  month: "2-digit",
  day: "2-digit",
  hour: "2-digit",
  minute: "2-digit",
  second: "2-digit",
});

function isoDate(instant: Date): string {
  return instant.toISOString().slice(0, 10);
}

function utcMidnight({ year, month, day }: CalendarDate): number {
  return Date.UTC(year, month - 1, day);
}

export function parseDate(text: string): CalendarDate {
  const date = {
    year: Number(text.slice(0, 4)),
    month: Number(text.slice(5, 7)),
    day: Number(text.slice(8, 10)),
  };
  const real = CALENDAR_DATE.test(text) && isoDate(new Date(utcMidnight(date))) === text;
  if (!real) throw new Error(`Expected a real YYYY-MM-DD date, not "${text}"`);
  return date;
}

export function daysBetween(from: string, to: string): number {
  return (utcMidnight(parseDate(to)) - utcMidnight(parseDate(from))) / DAY_MS;
}

function shiftDate(date: string, days: number): string {
  return isoDate(new Date(utcMidnight(parseDate(date)) + days * DAY_MS));
}

function warsawWallClock(instant: Date): number {
  const parts = new Map(warsawClock.formatToParts(instant).map((part) => [part.type, part.value]));
  const field = (type: Intl.DateTimeFormatPartTypes): number => {
    const value = parts.get(type);
    if (value === undefined) {
      throw new Error(`No ${type} in the Warsaw time of ${instant.toISOString()}`);
    }
    return Number(value);
  };
  return Date.UTC(
    field("year"),
    field("month") - 1,
    field("day"),
    field("hour"),
    field("minute"),
    field("second"),
  );
}

function warsawOffsetMs(instant: Date): number {
  return warsawWallClock(instant) - Math.floor(instant.getTime() / 1000) * 1000;
}

export function warsawDate(instant: Date): string {
  return isoDate(new Date(warsawWallClock(instant)));
}

export function warsawMinuteOfDay(instant: Date): number {
  return Math.floor((warsawWallClock(instant) % DAY_MS) / MINUTE_MS);
}

export function warsawInstant(date: string, minuteOfDay: number): Date {
  const wall = utcMidnight(parseDate(date)) + minuteOfDay * MINUTE_MS;
  const before = warsawOffsetMs(new Date(wall - DAY_MS));
  const after = warsawOffsetMs(new Date(wall + DAY_MS));
  const real = [wall - before, wall - after].filter(
    (instant) => warsawOffsetMs(new Date(instant)) === wall - instant,
  );
  // a wall time the clock change repeats takes its first pass; one it skips keeps the old offset
  return new Date(real.length > 0 ? Math.min(...real) : wall - before);
}

function phraseMinute(text: string): number {
  const hour = Number(text.slice(0, 2));
  const minute = Number(text.slice(3, 5));
  if (!PHRASE_TIME.test(text) || hour > 23 || minute > 59) {
    throw new Error(`A phrase time is HH:MM, not "${text}"`);
  }
  return hour * 60 + minute;
}

function phraseMinutes(times: PhraseTimes): { first: number; second: number } {
  const first = phraseMinute(times.first);
  const second = phraseMinute(times.second);
  if (first >= second) {
    throw new Error(`Phrase times must go up, not ${times.first} then ${times.second}`);
  }
  return { first, second };
}

export function slotAt(instant: Date, times: PhraseTimes): Slot {
  const { first, second } = phraseMinutes(times);
  const today = warsawDate(instant);
  const yesterday = shiftDate(today, -1);
  const tomorrow = shiftDate(today, 1);
  const starts: Omit<Slot, "end">[] = [
    { kind: "evening", date: yesterday, start: warsawInstant(yesterday, second) },
    { kind: "morning", date: today, start: warsawInstant(today, first) },
    { kind: "evening", date: today, start: warsawInstant(today, second) },
    { kind: "morning", date: tomorrow, start: warsawInstant(tomorrow, first) },
  ];
  const index = starts.findLastIndex(({ start }) => start.getTime() <= instant.getTime());
  const current = starts[index];
  const next = starts[index + 1];
  if (current === undefined || next === undefined) {
    throw new Error(`No slot contains ${instant.toISOString()}`);
  }
  return { ...current, end: next.start };
}

export function cleanupMinute(times: PhraseTimes): number {
  const { first, second } = phraseMinutes(times);
  return Math.floor((first + second) / 2);
}
