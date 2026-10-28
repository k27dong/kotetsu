import type { Icon, Pin, PinKind, PinSlot, SpecialDay } from "./rules";
import { isIsoTimeWithOffset, isPhraseTime, parseDate, type PhraseTimes } from "./time";

export class InputError extends Error {}

export type PinFields = Omit<Pin, "id" | "phraseId">;
export type PhraseInput = { text: string; pins: PinFields[] };
export type SpecialDayFields = Omit<SpecialDay, "id">;
export type PhraseFilter = "all" | "pinned" | "english";
export type ScreenReport = {
  refresh: "full" | "partial";
  at: string;
  phraseId: number | null;
  weatherAt: string | null;
  tooLong: number[];
  missingGlyphs: number[];
  wifiSignal: number | null;
  piTemperatureC: number | null;
  lastError: string | null;
};

const PHRASE_MAX_CHARACTERS = 500;
const LAST_ERROR_MAX_CHARACTERS = 1000;
const ANY_LEAP_YEAR = 2000;
const GREETING_MAX_BYTES = 63; // the Pi keeps a greeting in 64 bytes, with its terminating NUL
const WIDEST_YEAR_COUNT = "9999"; // {n} counts years since a four-digit baseYear
const PIN_KINDS: readonly PinKind[] = ["date", "yearly", "lunar_yearly", "monthly", "weekly"];
const PIN_SLOTS: readonly PinSlot[] = ["morning", "evening", "all_day"];
const PIN_DATE_FIELDS = ["date", "month", "day", "weekday"] as const;
const SPECIAL_DAY_KINDS: readonly SpecialDay["kind"][] = ["yearly", "lunar_yearly"];
const ICONS: readonly Icon[] = ["heart", "rings", "cake", "cat", "hearts", "star"];
const REFRESHES: readonly ScreenReport["refresh"][] = ["full", "partial"];
const FILTERS: readonly PhraseFilter[] = ["all", "pinned", "english"];
const CONTROL = /\p{Cc}/u;
const CONTROL_BUT_LINE_FEED = /(?!\n)\p{Cc}/u;

const utf8 = new TextDecoder("utf-8", { fatal: true, ignoreBOM: false });
const encoder = new TextEncoder();

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function fieldsOf(value: unknown, what: string): Record<string, unknown> {
  if (!isRecord(value)) throw new InputError(`${what} must be an object`);
  return value;
}

function listOf(value: unknown, what: string): unknown[] {
  if (!Array.isArray(value)) throw new InputError(`${what} must be a list`);
  return value;
}

function oneOf<Choice extends string>(
  value: unknown,
  choices: readonly Choice[],
  what: string,
): Choice {
  const choice = choices.find((candidate) => candidate === value);
  if (choice === undefined) throw new InputError(`${what} must be one of ${choices.join(", ")}`);
  return choice;
}

function wholeNumber(value: unknown, what: string, min: number, max: number): number {
  if (typeof value !== "number" || !Number.isInteger(value) || value < min || value > max) {
    throw new InputError(`${what} must be a whole number from ${min} to ${max}`);
  }
  return value;
}

function phraseId(value: unknown, what: string): number {
  if (typeof value !== "number" || !Number.isSafeInteger(value) || value < 1) {
    throw new InputError(`${what} must be a phrase id`);
  }
  return value;
}

function cleanText(value: unknown, what: string): string {
  if (typeof value !== "string") throw new InputError(`${what} must be text`);
  if (!value.isWellFormed()) throw new InputError(`${what} has a lone surrogate`);
  return value.normalize("NFC").replaceAll("\r\n", "\n").trim(); // trim() also strips U+3000
}

function calendarDate(value: unknown, what: string): string {
  if (typeof value !== "string") throw new InputError(`${what} must be a YYYY-MM-DD date`);
  try {
    parseDate(value);
  } catch {
    throw new InputError(`${what} must be a real YYYY-MM-DD date`);
  }
  return value;
}

function monthAndDay(
  fields: Record<string, unknown>,
  kind: SpecialDay["kind"],
  what: string,
): { month: number; day: number } {
  const month = wholeNumber(fields.month, `${what}'s month`, 1, 12);
  const daysInMonth = new Date(Date.UTC(ANY_LEAP_YEAR, month, 0)).getUTCDate();
  const lastDay = kind === "lunar_yearly" ? 30 : daysInMonth;
  return { month, day: wholeNumber(fields.day, `${what}'s day`, 1, lastDay) };
}

function isoTime(value: unknown, what: string): string {
  if (typeof value !== "string" || !isIsoTimeWithOffset(value)) {
    throw new InputError(`${what} must be a real ISO 8601 time with an offset`);
  }
  return new Date(value).toISOString();
}

function finiteNumber(value: unknown, what: string): number {
  if (typeof value !== "number" || !Number.isFinite(value)) {
    throw new InputError(`${what} must be a number`);
  }
  return value;
}

export async function jsonBody(request: Request): Promise<unknown> {
  const bytes = await request.arrayBuffer();
  try {
    return JSON.parse(utf8.decode(bytes));
  } catch {
    throw new InputError("The body must be JSON in UTF-8");
  }
}

function phraseText(value: unknown): string {
  const text = cleanText(value, "A phrase");
  if (CONTROL_BUT_LINE_FEED.test(text)) {
    throw new InputError("A phrase can't hold control characters other than line feeds");
  }
  const length = [...text].length;
  if (length === 0 || length > PHRASE_MAX_CHARACTERS) {
    throw new InputError(`A phrase must have 1 to ${PHRASE_MAX_CHARACTERS} characters`);
  }
  return text;
}

function kindFields(
  kind: PinKind,
  fields: Record<string, unknown>,
  what: string,
): Partial<PinFields> {
  switch (kind) {
    case "date":
      return { date: calendarDate(fields.date, `${what}'s date`) };
    case "yearly":
    case "lunar_yearly":
      return monthAndDay(fields, kind, what);
    case "monthly":
      return { day: wholeNumber(fields.day, `${what}'s day`, 1, 31) };
    case "weekly":
      return { weekday: wholeNumber(fields.weekday, `${what}'s weekday`, 1, 7) };
  }
}

function pinFields(value: unknown, what: string): PinFields {
  const fields = fieldsOf(value, what);
  const kind = oneOf(fields.kind, PIN_KINDS, `${what}'s kind`);
  const slot = oneOf(fields.slot, PIN_SLOTS, `${what}'s slot`);
  const pin: PinFields = {
    kind,
    date: null,
    month: null,
    day: null,
    weekday: null,
    slot,
    ...kindFields(kind, fields, what),
  };
  for (const name of PIN_DATE_FIELDS) {
    if (pin[name] === null && (fields[name] ?? null) !== null) {
      throw new InputError(`${what} is ${kind}, so its ${name} must be null`);
    }
  }
  return pin;
}

export function phraseInput(value: unknown): PhraseInput {
  const fields = fieldsOf(value, "The phrase");
  return {
    text: phraseText(fields.text),
    pins: listOf(fields.pins, "pins").map((pin, index) => pinFields(pin, `Pin ${index + 1}`)),
  };
}

export function phraseFilter(value: string | null): PhraseFilter {
  return value === null ? "all" : oneOf(value, FILTERS, "filter");
}

function phraseTime(value: unknown, what: string): string {
  if (typeof value !== "string" || !isPhraseTime(value)) {
    throw new InputError(`${what} must be HH:MM, from 00:00 to 23:59`);
  }
  return value;
}

export function phraseTimesInput(value: unknown): PhraseTimes {
  const fields = fieldsOf(value, "The settings");
  const one = phraseTime(fields.firstPhraseTime, "firstPhraseTime");
  const other = phraseTime(fields.secondPhraseTime, "secondPhraseTime");
  if (one === other) throw new InputError("The two phrase times must differ");
  return one < other ? { first: one, second: other } : { first: other, second: one };
}

function greetingText(value: unknown, what: string): string {
  const text = cleanText(value, what);
  if (CONTROL.test(text)) {
    throw new InputError(`${what} must be one line without control characters`);
  }
  const widest = text.replaceAll("{n}", WIDEST_YEAR_COUNT);
  if (text === "" || encoder.encode(widest).length > GREETING_MAX_BYTES) {
    throw new InputError(
      `${what} must have 1 to ${GREETING_MAX_BYTES} bytes of UTF-8, with {n} as four digits`,
    );
  }
  return text;
}

function specialDayFields(value: unknown, what: string): SpecialDayFields {
  const fields = fieldsOf(value, what);
  const kind = oneOf(fields.kind, SPECIAL_DAY_KINDS, `${what}'s kind`);
  const greeting = greetingText(fields.greeting, `${what}'s greeting`);
  const baseYear =
    fields.baseYear === null ? null : wholeNumber(fields.baseYear, `${what}'s baseYear`, 1, 9999);
  if (baseYear === null && greeting.includes("{n}")) {
    throw new InputError(`${what} counts years with {n}, so it needs a baseYear`);
  }
  return {
    kind,
    ...monthAndDay(fields, kind, what),
    greeting,
    baseYear,
    icon: oneOf(fields.icon, ICONS, `${what}'s icon`),
    rank: wholeNumber(fields.rank, `${what}'s rank`, 0, 1000),
  };
}

export function specialDaysInput(value: unknown): SpecialDayFields[] {
  return listOf(value, "The special days").map((day, index) =>
    specialDayFields(day, `Special day ${index + 1}`),
  );
}

export function screenReport(text: string): ScreenReport {
  let value: unknown;
  try {
    value = JSON.parse(text);
  } catch {
    throw new InputError("The report must be JSON");
  }
  const fields = fieldsOf(value, "The report");
  const ids = (key: "tooLong" | "missingGlyphs") =>
    listOf(fields[key], key).map((id) => phraseId(id, `Each of ${key}`));
  const { lastError } = fields;
  if (lastError !== null && typeof lastError !== "string") {
    throw new InputError("lastError must be text or null");
  }
  if (lastError !== null && [...lastError].length > LAST_ERROR_MAX_CHARACTERS) {
    throw new InputError(`lastError must have at most ${LAST_ERROR_MAX_CHARACTERS} characters`);
  }
  return {
    refresh: oneOf(fields.refresh, REFRESHES, "refresh"),
    at: isoTime(fields.at, "at"),
    phraseId: fields.phraseId === null ? null : phraseId(fields.phraseId, "phraseId"),
    weatherAt: fields.weatherAt === null ? null : isoTime(fields.weatherAt, "weatherAt"),
    tooLong: ids("tooLong"),
    missingGlyphs: ids("missingGlyphs"),
    wifiSignal: fields.wifiSignal === null ? null : finiteNumber(fields.wifiSignal, "wifiSignal"),
    piTemperatureC:
      fields.piTemperatureC === null ? null : finiteNumber(fields.piTemperatureC, "piTemperatureC"),
    lastError,
  };
}
