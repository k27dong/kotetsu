export type PhraseTimes = { first: string; second: string };
export type Slot = { kind: "morning" | "evening"; date: string; start: Date; end: Date };
export type CalendarDate = { year: number; month: number; day: number };

const MINUTE_MS = 60 * 1000;
const DAY_MS = 24 * 60 * MINUTE_MS;
const CALENDAR_DATE = /^\d{4}-\d{2}-\d{2}$/;
const PHRASE_TIME = /^\d{2}:\d{2}$/;
const ISO_TIME_WITH_OFFSET =
  /^(\d{4})-(\d{2})-(\d{2})T(\d{2}):\d{2}(?::\d{2}(?:\.\d{3})?)?(?:Z|[+-]\d{2}:\d{2})$/;

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

const isoDate = (instant: Date): string => instant.toISOString().slice(0, 10);

const utcMidnight = ({ year, month, day }: CalendarDate): number => Date.UTC(year, month - 1, day);

export const parseDate = (text: string): CalendarDate => {
  const date = {
    year: Number(text.slice(0, 4)),
    month: Number(text.slice(5, 7)),
    day: Number(text.slice(8, 10)),
  };
  const real = CALENDAR_DATE.test(text) && isoDate(new Date(utcMidnight(date))) === text;
  if (!real) throw new Error(`Expected a real YYYY-MM-DD date, not "${text}"`);
  return date;
};

export const daysBetween = (from: string, to: string): number =>
  (utcMidnight(parseDate(to)) - utcMidnight(parseDate(from))) / DAY_MS;

export const shiftDate = (date: string, days: number): string =>
  isoDate(new Date(utcMidnight(parseDate(date)) + days * DAY_MS));

export const isIsoTimeWithOffset = (text: string): boolean => {
  const match = ISO_TIME_WITH_OFFSET.exec(text);
  if (match === null) return false;
  const year = Number(match[1]);
  const month = Number(match[2]);
  const day = Number(match[3]);
  const calendar = new Date(Date.UTC(year, month - 1, day)); // Date rolls 02-29 over to 03-01
  const realDay =
    calendar.getUTCFullYear() === year &&
    calendar.getUTCMonth() === month - 1 &&
    calendar.getUTCDate() === day;
  if (!realDay || Number(match[4]) >= 24) return false; // Date reads T24:00 as the next day
  return !Number.isNaN(Date.parse(text));
};

const warsawWallClock = (instant: Date): number => {
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
};

const warsawOffsetMs = (instant: Date): number =>
  warsawWallClock(instant) - Math.floor(instant.getTime() / 1000) * 1000;

export const warsawDate = (instant: Date): string => isoDate(new Date(warsawWallClock(instant)));

export const warsawMinuteOfDay = (instant: Date): number =>
  Math.floor((warsawWallClock(instant) % DAY_MS) / MINUTE_MS);

export const warsawInstant = (date: string, minuteOfDay: number): Date => {
  const wall = utcMidnight(parseDate(date)) + minuteOfDay * MINUTE_MS;
  const before = warsawOffsetMs(new Date(wall - DAY_MS));
  const after = warsawOffsetMs(new Date(wall + DAY_MS));
  const real = [wall - before, wall - after].filter(
    (instant) => warsawOffsetMs(new Date(instant)) === wall - instant,
  );
  // a wall time the clock change repeats takes its first pass; one it skips keeps the old offset
  return new Date(real.length > 0 ? Math.min(...real) : wall - before);
};

export const isPhraseTime = (text: string): boolean =>
  PHRASE_TIME.test(text) && Number(text.slice(0, 2)) < 24 && Number(text.slice(3, 5)) < 60;

const phraseMinute = (text: string): number => {
  if (!isPhraseTime(text)) throw new Error(`A phrase time is HH:MM, not "${text}"`);
  return Number(text.slice(0, 2)) * 60 + Number(text.slice(3, 5));
};

const phraseMinutes = (times: PhraseTimes): { first: number; second: number } => {
  const first = phraseMinute(times.first);
  const second = phraseMinute(times.second);
  if (first >= second) {
    throw new Error(`Phrase times must go up, not ${times.first} then ${times.second}`);
  }
  return { first, second };
};

export const slotAt = (instant: Date, times: PhraseTimes): Slot => {
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
};

export const cleanupMinute = (times: PhraseTimes): number => {
  const { first, second } = phraseMinutes(times);
  return Math.floor((first + second) / 2);
};
