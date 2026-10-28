import { LunarMonth, Solar } from "lunar-javascript";
import { activeOverride, rotationPhrase, scheduledPhrase } from "./api_pi";
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
import {
  counterDays,
  greetingFor,
  milestoneText,
  type Greeting,
  type Pin,
  type PhraseRow,
  type SpecialDay,
} from "./rules";
import { parseDate, slotAt, warsawDate, type PhraseTimes } from "./time";
import {
  InputError,
  jsonBody,
  phraseFilter,
  phraseInput,
  phraseTimesInput,
  specialDaysInput,
  type PinFields,
  type SpecialDayFields,
} from "./validate";

const HAN = /\p{Script=Han}/u;

function idParam(params: Record<string, string>): number {
  const id = Number(params.id);
  if (!Number.isSafeInteger(id)) throw new InputError("A phrase id must be a whole number");
  return id;
}

function noContent(): Response {
  return new Response(null, { status: 204 });
}

function notFound(): Response {
  return new Response(null, { status: 404 });
}

function isoDay(utcMs: number): string {
  return new Date(utcMs).toISOString().slice(0, 10);
}

function solarDate(year: number, month: number, day: number): string | null {
  const date = new Date(Date.UTC(year, month - 1, day));
  return date.getUTCMonth() === month - 1 ? isoDay(date.getTime()) : null;
}

function lunarDate(lunarYear: number, month: number, day: number): string | null {
  const lunarMonth = LunarMonth.fromYm(lunarYear, month); // a positive month is never the leap one
  if (lunarMonth === null) return null;
  const first = lunarMonth.getFirstJulianDay();
  const length = lunarMonth.next(1).getFirstJulianDay() - first;
  return Solar.fromJulianDay(first + Math.min(day, length) - 1).toYmd();
}

function nextSpecialDay(
  today: string,
  specialDays: SpecialDay[],
): (Greeting & { date: string }) | null {
  const { year, month, day } = parseDate(today);
  const dates = new Set<string>();
  // the lunar year that began last winter may still be running
  for (const candidateYear of [year - 1, year, year + 1]) {
    for (const special of specialDays) {
      const date =
        special.kind === "yearly"
          ? solarDate(candidateYear, special.month, special.day)
          : lunarDate(candidateYear, special.month, special.day);
      if (date !== null && date >= today) dates.add(date);
    }
  }
  const days = counterDays(today);
  const milestone = Math.ceil(days / 1000) * 1000;
  if (milestoneText(milestone) !== null) {
    dates.add(isoDay(Date.UTC(year, month - 1, day + milestone - days)));
  }
  // greetingFor settles ranks and {n}; asking it about these dates alone bounds its lunar work
  for (const date of [...dates].toSorted()) {
    const greeting = greetingFor(date, specialDays);
    if (greeting !== null) return { date, ...greeting };
  }
  return null;
}

export async function status({ env, now }: RouteContext): Promise<Response> {
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
  const report: unknown = state.screenReport === null ? null : JSON.parse(state.screenReport);
  return Response.json({
    piLastSeenAt: state.piLastSeenAt,
    onWall:
      onWall === undefined ? null : { id: onWall.id, text: onWall.text, since: onWall.lastShownAt },
    next: next === undefined ? null : { id: next.id, text: next.text, at: slot.end.toISOString() },
    screenAt: state.screenAt,
    report,
    nextSpecialDay: nextSpecialDay(warsawDate(now), specialDays),
  });
}

export async function screenPng({ env }: RouteContext): Promise<Response> {
  type Screen = { png: number[] | null };
  const query = "SELECT screen_png AS png FROM display_state WHERE id = 1";
  const row = await env.DB.prepare(query).first<Screen>();
  if (row === null || row.png === null) return notFound();
  const headers = { "Content-Type": "image/png", "Cache-Control": "no-store" };
  return new Response(new Uint8Array(row.png), { headers });
}

function fold(text: string): string {
  return text.normalize("NFKC").toLowerCase();
}

function pinFieldsOf({ kind, date, month, day, weekday, slot }: Pin): PinFields {
  return { kind, date, month, day, weekday, slot };
}

export async function listPhrases({ request, env }: RouteContext): Promise<Response> {
  const params = new URL(request.url).searchParams;
  const filter = phraseFilter(params.get("filter"));
  const query = fold(params.get("q") ?? "").trim();
  const [phrases, pins] = await Promise.all([readPhrases(env.DB), readPins(env.DB)]);
  const pinsOf = Map.groupBy(pins, (pin) => pin.phraseId);
  const listed = (phrase: PhraseRow): boolean =>
    fold(phrase.text).includes(query) &&
    (filter !== "pinned" || pinsOf.has(phrase.id)) &&
    (filter !== "english" || !HAN.test(phrase.text));
  return Response.json(
    phrases.filter(listed).map((phrase) => ({
      id: phrase.id,
      text: phrase.text,
      pins: (pinsOf.get(phrase.id) ?? []).map(pinFieldsOf),
      problem: phrase.problem,
      lastShownAt: phrase.lastShownAt,
    })),
  );
}

function insertPins(
  db: D1Database,
  phraseId: number | null,
  pins: PinFields[],
): D1PreparedStatement {
  return db
    .prepare(
      `INSERT INTO pins (phrase_id, kind, date, month, day, weekday, slot)
       SELECT coalesce(?, (SELECT max(id) FROM phrases)), value ->> 'kind', value ->> 'date',
         value ->> 'month', value ->> 'day', value ->> 'weekday', value ->> 'slot'
       FROM json_each(?) ORDER BY key`,
    )
    .bind(phraseId, JSON.stringify(pins));
}

export async function createPhrase({ request, env, now }: RouteContext): Promise<Response> {
  const { text, pins } = phraseInput(await jsonBody(request));
  const stamp = now.toISOString();
  const [inserted] = await env.DB.batch<{ id: number }>([
    env.DB.prepare(
      "INSERT INTO phrases (text, created_at, updated_at) VALUES (?, ?, ?) RETURNING id",
    ).bind(text, stamp, stamp),
    insertPins(env.DB, null, pins), // null: the phrase inserted just above, the newest row
  ]);
  const id = inserted?.results[0]?.id;
  if (id === undefined) throw new Error("D1 didn't return the new phrase's id");
  return Response.json({ id }, { status: 201 });
}

export async function updatePhrase({ request, env, now, params }: RouteContext): Promise<Response> {
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
}

export async function deletePhrase({ env, params }: RouteContext): Promise<Response> {
  const id = idParam(params);
  const [deleted] = await env.DB.batch([
    env.DB.prepare("DELETE FROM phrases WHERE id = ?").bind(id),
    ...clearFromWall(env.DB, [id]),
  ]);
  return (deleted?.meta.changes ?? 0) > 0 ? noContent() : notFound();
}

function showUntil(db: D1Database, id: number, until: Date, now: Date): D1PreparedStatement[] {
  return [
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
}

export async function showPhrase({ env, now, params }: RouteContext): Promise<Response> {
  const id = idParam(params);
  const slot = slotAt(now, await readPhraseTimes(env.DB));
  const [shown] = await env.DB.batch(showUntil(env.DB, id, slot.end, now));
  return (shown?.meta.changes ?? 0) > 0 ? noContent() : notFound();
}

export async function skip({ env, now }: RouteContext): Promise<Response> {
  const db = env.DB;
  const [times, phrases, pins] = await Promise.all([
    readPhraseTimes(db),
    readPhrases(db),
    readPins(db),
  ]);
  const slot = slotAt(now, times);
  // drawn as the next slot will draw, so Skip shows the phrase status lists as next
  const id = await rotationPhrase(slotAt(slot.end, times), phrases, pins);
  if (id === null) {
    return Response.json({ error: "No phrase is left in rotation" }, { status: 409 });
  }
  await db.batch(showUntil(db, id, slot.end, now));
  return noContent();
}

export async function fullRefresh({ env }: RouteContext): Promise<Response> {
  await env.DB.prepare(
    "UPDATE display_state SET full_refresh_seq = full_refresh_seq + 1 WHERE id = 1",
  ).run();
  return noContent();
}

function specialDayFieldsOf({
  kind,
  month,
  day,
  greeting,
  baseYear,
  icon,
  rank,
}: SpecialDay): SpecialDayFields {
  return { kind, month, day, greeting, baseYear, icon, rank };
}

export async function getSpecialDays({ env }: RouteContext): Promise<Response> {
  return Response.json((await readSpecialDays(env.DB)).map(specialDayFieldsOf));
}

export async function saveSpecialDays({ request, env }: RouteContext): Promise<Response> {
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
}

function settingsBody({ first, second }: PhraseTimes) {
  return { firstPhraseTime: first, secondPhraseTime: second };
}

export async function getSettings({ env }: RouteContext): Promise<Response> {
  return Response.json(settingsBody(await readPhraseTimes(env.DB)));
}

export async function saveSettings({ request, env, now }: RouteContext): Promise<Response> {
  const times = phraseTimesInput(await jsonBody(request));
  const db = env.DB;
  const before = slotAt(now, await readPhraseTimes(db));
  const after = slotAt(now, times);
  // a slot is its kind and date, so new edges keep its phrase, and Show now runs to the new end
  const moved = [
    db
      .prepare("UPDATE display_state SET slot_start = ? WHERE id = 1 AND slot_start = ?")
      .bind(after.start.toISOString(), before.start.toISOString()),
    db
      .prepare("UPDATE display_state SET override_until = ? WHERE id = 1 AND override_until = ?")
      .bind(after.end.toISOString(), before.end.toISOString()),
  ];
  const sameSlot = before.kind === after.kind && before.date === after.date;
  await db.batch([
    db
      .prepare("UPDATE settings SET first_phrase_time = ?, second_phrase_time = ? WHERE id = 1")
      .bind(times.first, times.second),
    ...(sameSlot ? moved : []),
  ]);
  return Response.json(settingsBody(times));
}
