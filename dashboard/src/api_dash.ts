import { match } from "ts-pattern";
import { activeOverride, rotationPhrase, scheduledPhrase } from "./api_pi";
import type {
  CreatedPhrase,
  ErrorResponse,
  Greeting,
  PhraseSummary,
  PinFields,
  ScreenReport,
  Settings,
  SpecialDayFields,
  StatusResponse,
} from "./api_types";
import {
  clearFromWall,
  readDisplayState,
  readPhrase,
  readPhrases,
  readPhraseTimes,
  readPins,
  readSpecialDays,
} from "./db";
import type { RouteContext } from "./index";
import { solarOf } from "./lunar";
import {
  counterDays,
  greetingFor,
  milestoneText,
  type Pin,
  type PhraseRow,
  type SpecialDay,
} from "./rules";
import { parseDate, shiftDate, slotAt, warsawDate, type PhraseTimes } from "./time";
import {
  InputError,
  jsonBody,
  phraseFilter,
  phraseInput,
  phraseTimesInput,
  specialDaysInput,
} from "./validate";

const HAN = /\p{Script=Han}/u;

const idParam = (params: Record<string, string>): number => {
  const id = Number(params.id);
  if (!Number.isSafeInteger(id)) throw new InputError("A phrase id must be a whole number");
  return id;
};

const noContent = (): Response => new Response(null, { status: 204 });

const notFound = (): Response => new Response(null, { status: 404 });

const yearlyDate = (year: number, month: number, day: number): string | null => {
  const date = new Date(Date.UTC(year, month - 1, day));
  return date.getUTCMonth() === month - 1 ? date.toISOString().slice(0, 10) : null;
};

const lunarDate = (lunarYear: number, month: number, day: number): string | null => {
  const date = solarOf(lunarYear, month, day, false);
  // a day-30 special day takes the last day of a 29-day month
  return date ?? (day === 30 ? solarOf(lunarYear, month, 29, false) : null);
};

const nextSpecialDay = (
  today: string,
  specialDays: SpecialDay[],
): (Greeting & { date: string }) | null => {
  const { year } = parseDate(today);
  const dates = new Set<string>();
  // the lunar year that began last winter may still be running
  for (const candidateYear of [year - 1, year, year + 1]) {
    for (const special of specialDays) {
      const date =
        special.kind === "yearly"
          ? yearlyDate(candidateYear, special.month, special.day)
          : lunarDate(candidateYear, special.month, special.day);
      if (date !== null && date >= today) dates.add(date);
    }
  }
  const days = counterDays(today);
  const milestone = Math.ceil(days / 1000) * 1000;
  if (milestoneText(milestone) !== null) dates.add(shiftDate(today, milestone - days));
  // greetingFor settles ranks and {n}; asking it about these dates alone bounds its lunar work
  for (const date of [...dates].toSorted()) {
    const greeting = greetingFor(date, specialDays);
    if (greeting !== null) return { date, ...greeting };
  }
  return null;
};

export const status = async ({ env, now }: RouteContext): Promise<Response> => {
  const db = env.DB;
  const [times, state, phrases, pins, specialDays] = await Promise.all([
    readPhraseTimes(db),
    readDisplayState(db),
    readPhrases(db),
    readPins(db),
    readSpecialDays(db),
  ]);
  const byId = new Map(phrases.map((phrase) => [phrase.id, phrase]));
  const phraseWithId = (id: number | null | undefined): PhraseRow | undefined =>
    id === null || id === undefined ? undefined : byId.get(id);
  const slot = slotAt(now, times);
  const onWall = phraseWithId(activeOverride(state, now)?.id) ?? phraseWithId(state.phraseId);
  const next = phraseWithId(await scheduledPhrase(slotAt(slot.end, times), phrases, pins));
  const report: ScreenReport | null =
    state.screenReport === null ? null : JSON.parse(state.screenReport);
  return Response.json({
    piLastSeenAt: state.piLastSeenAt,
    onWall:
      onWall === undefined ? null : { id: onWall.id, text: onWall.text, since: onWall.lastShownAt },
    next: next === undefined ? null : { id: next.id, text: next.text, at: slot.end.toISOString() },
    screenAt: state.screenAt,
    report,
    nextSpecialDay: nextSpecialDay(warsawDate(now), specialDays),
  } satisfies StatusResponse);
};

export const screenPng = async ({ env }: RouteContext): Promise<Response> => {
  type Screen = { png: number[] | null };
  const query = "SELECT screen_png AS png FROM display_state WHERE id = 1";
  const row = await env.DB.prepare(query).first<Screen>();
  if (row === null || row.png === null) return notFound();
  const headers = { "Content-Type": "image/png", "Cache-Control": "no-store" };
  return new Response(new Uint8Array(row.png), { headers });
};

const fold = (text: string): string => text.normalize("NFKC").toLowerCase();

const pinFieldsOf = ({ kind, date, month, day, weekday, slot }: Pin): PinFields => ({
  kind,
  date,
  month,
  day,
  weekday,
  slot,
});

export const listPhrases = async ({ request, env }: RouteContext): Promise<Response> => {
  const params = new URL(request.url).searchParams;
  const filter = phraseFilter(params.get("filter"));
  const query = fold(params.get("q") ?? "").trim();
  const [phrases, pins] = await Promise.all([readPhrases(env.DB), readPins(env.DB)]);
  const pinsOf = Map.groupBy(pins, (pin) => pin.phraseId);
  const listed = (phrase: PhraseRow): boolean =>
    fold(phrase.text).includes(query) &&
    match(filter)
      .with("all", () => true)
      .with("pinned", () => pinsOf.has(phrase.id))
      .with("english", () => !HAN.test(phrase.text))
      .exhaustive();
  return Response.json(
    phrases.filter(listed).map((phrase): PhraseSummary => ({
      id: phrase.id,
      text: phrase.text,
      pins: (pinsOf.get(phrase.id) ?? []).map(pinFieldsOf),
      problem: phrase.problem,
      lastShownAt: phrase.lastShownAt,
    })),
  );
};

const insertPins = (
  db: D1Database,
  phraseId: number | "newest",
  pins: PinFields[],
): D1PreparedStatement =>
  db
    .prepare(
      `INSERT INTO pins (phrase_id, kind, date, month, day, weekday, slot)
       SELECT coalesce(?, (SELECT max(id) FROM phrases)), value ->> 'kind', value ->> 'date',
         value ->> 'month', value ->> 'day', value ->> 'weekday', value ->> 'slot'
       FROM json_each(?) ORDER BY key`,
    )
    .bind(phraseId === "newest" ? null : phraseId, JSON.stringify(pins));

export const createPhrase = async ({ request, env, now }: RouteContext): Promise<Response> => {
  const { text, pins } = phraseInput(await jsonBody(request));
  const stamp = now.toISOString();
  const [inserted] = await env.DB.batch<{ id: number }>([
    env.DB.prepare(
      // the id is taken inside the batch, so creates that overlap can't claim the same one
      `INSERT INTO phrases (id, text, created_at, updated_at)
       SELECT coalesce(max(id), 0) + 1, ?, ?, ? FROM phrases RETURNING id`,
    ).bind(text, stamp, stamp),
    insertPins(env.DB, "newest", pins),
  ]);
  const id = inserted?.results[0]?.id;
  if (id === undefined) throw new Error("D1 returned no id for the new phrase");
  return Response.json({ id } satisfies CreatedPhrase, { status: 201 });
};

export const updatePhrase = async ({
  request,
  env,
  now,
  params,
}: RouteContext): Promise<Response> => {
  const id = idParam(params);
  const { text, pins } = phraseInput(await jsonBody(request));
  if ((await readPhrase(env.DB, id)) === null) return notFound();
  await env.DB.batch([
    env.DB.prepare(
      // a problem the Pi reported belongs to the old text
      `UPDATE phrases SET text = ?1, updated_at = ?2, problem = CASE WHEN text = ?1 THEN problem END
       WHERE id = ?3`,
    ).bind(text, now.toISOString(), id),
    env.DB.prepare("DELETE FROM pins WHERE phrase_id = ?").bind(id),
    insertPins(env.DB, id, pins),
  ]);
  return noContent();
};

export const deletePhrase = async ({ env, params }: RouteContext): Promise<Response> => {
  const id = idParam(params);
  const [deleted] = await env.DB.batch([
    env.DB.prepare("DELETE FROM phrases WHERE id = ?").bind(id),
    ...clearFromWall(env.DB, [id]),
  ]);
  return (deleted?.meta.changes ?? 0) > 0 ? noContent() : notFound();
};

const showUntil = (db: D1Database, id: number, until: Date, now: Date): D1PreparedStatement[] => [
  db
    .prepare("UPDATE phrases SET last_shown_at = ?, shown_count = shown_count + 1 WHERE id = ?")
    .bind(now.toISOString(), id),
  db
    .prepare(
      `UPDATE display_state SET override_phrase_id = ?, override_until = ?
       WHERE id = 1 AND EXISTS (SELECT 1 FROM phrases WHERE id = ?)`,
    )
    .bind(id, until.toISOString(), id),
];

export const showPhrase = async ({ env, now, params }: RouteContext): Promise<Response> => {
  const id = idParam(params);
  const slot = slotAt(now, await readPhraseTimes(env.DB));
  const [shown] = await env.DB.batch(showUntil(env.DB, id, slot.end, now));
  return (shown?.meta.changes ?? 0) > 0 ? noContent() : notFound();
};

export const skip = async ({ env, now }: RouteContext): Promise<Response> => {
  const db = env.DB;
  const [times, phrases, pins] = await Promise.all([
    readPhraseTimes(db),
    readPhrases(db),
    readPins(db),
  ]);
  const slot = slotAt(now, times);
  // the next slot's rotation draw, which status lists as next unless that slot is pinned
  const id = await rotationPhrase(slotAt(slot.end, times), phrases, pins);
  if (id === null) {
    return Response.json({ error: "No phrase is left in rotation" } satisfies ErrorResponse, {
      status: 409,
    });
  }
  await db.batch(showUntil(db, id, slot.end, now));
  return noContent();
};

export const fullRefresh = async ({ env }: RouteContext): Promise<Response> => {
  await env.DB.prepare(
    "UPDATE display_state SET full_refresh_seq = full_refresh_seq + 1 WHERE id = 1",
  ).run();
  return noContent();
};

const specialDayFieldsOf = ({
  kind,
  month,
  day,
  greeting,
  baseYear,
  icon,
  rank,
}: SpecialDay): SpecialDayFields => ({ kind, month, day, greeting, baseYear, icon, rank });

export const getSpecialDays = async ({ env }: RouteContext): Promise<Response> =>
  Response.json((await readSpecialDays(env.DB)).map(specialDayFieldsOf));

export const saveSpecialDays = async ({ request, env }: RouteContext): Promise<Response> => {
  const days = specialDaysInput(await jsonBody(request));
  await env.DB.batch([
    env.DB.prepare("DELETE FROM special_days"),
    env.DB.prepare(
      `INSERT INTO special_days (kind, month, day, greeting, base_year, icon, rank)
       SELECT value ->> 'kind', value ->> 'month', value ->> 'day', value ->> 'greeting',
         value ->> 'baseYear', value ->> 'icon', value ->> 'rank'
       FROM json_each(?) ORDER BY key`,
    ).bind(JSON.stringify(days)),
  ]);
  return Response.json(days);
};

const settingsBody = ({ first, second }: PhraseTimes): Settings => ({
  firstPhraseTime: first,
  secondPhraseTime: second,
});

export const getSettings = async ({ env }: RouteContext): Promise<Response> =>
  Response.json(settingsBody(await readPhraseTimes(env.DB)));

export const saveSettings = async ({ request, env, now }: RouteContext): Promise<Response> => {
  const times = phraseTimesInput(await jsonBody(request));
  const db = env.DB;
  const before = slotAt(now, await readPhraseTimes(db));
  const after = slotAt(now, times);
  const statements = [
    db
      .prepare("UPDATE settings SET first_phrase_time = ?, second_phrase_time = ? WHERE id = 1")
      .bind(times.first, times.second),
    // Show now and Skip run to the next phrase time, wherever it has moved
    db
      .prepare("UPDATE display_state SET override_until = ? WHERE id = 1 AND override_until = ?")
      .bind(after.end.toISOString(), before.end.toISOString()),
  ];
  // a slot is its kind and date, so new edges keep its phrase
  if (before.kind === after.kind && before.date === after.date) {
    statements.push(
      db
        .prepare("UPDATE display_state SET slot_start = ? WHERE id = 1 AND slot_start = ?")
        .bind(after.start.toISOString(), before.start.toISOString()),
    );
  }
  await db.batch(statements);
  return Response.json(settingsBody(times));
};
