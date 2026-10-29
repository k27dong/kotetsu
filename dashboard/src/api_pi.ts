import type { ErrorResponse, PiNowResponse } from "./api_types";
import {
  clearFromWall,
  readDisplayState,
  readPhrase,
  readPhrases,
  readPhraseTimes,
  readPins,
  readSpecialDays,
  type DisplayState,
} from "./db";
import type { RouteContext } from "./index";
import { greetingFor, pinnedPhraseFor, rotationPick, type Pin, type PhraseRow } from "./rules";
import { cleanupMinute, slotAt, warsawDate, type Slot } from "./time";
import { InputError, screenReport } from "./validate";

const SCREEN_MAX_BYTES = 512 * 1024;
const PNG_SIGNATURE = [0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a];

type OnWall = { phrase: PhraseRow; until: string };

export const activeOverride = (
  state: DisplayState,
  now: Date,
): { id: number; until: string } | null => {
  const { overridePhraseId: id, overrideUntil: until } = state;
  if (id === null || until === null || Date.parse(until) <= now.getTime()) return null;
  return { id, until };
};

const slotRandom = async (slot: Slot): Promise<() => number> => {
  // seeded by the slot, so the dashboard can name the next phrase before the Pi asks for it
  const seed = new TextEncoder().encode(slot.start.toISOString());
  const fraction = new DataView(await crypto.subtle.digest("SHA-256", seed)).getUint32(0) / 2 ** 32;
  return () => fraction;
};

export const rotationPhrase = async (
  slot: Slot,
  phrases: PhraseRow[],
  pins: Pin[],
): Promise<number | null> => {
  const pinned = new Set(pins.map((pin) => pin.phraseId));
  return rotationPick(phrases, pinned, await slotRandom(slot));
};

export const scheduledPhrase = async (
  slot: Slot,
  phrases: PhraseRow[],
  pins: Pin[],
): Promise<number | null> =>
  pinnedPhraseFor(slot, pins, phrases) ?? (await rotationPhrase(slot, phrases, pins));

const claimSlot = async (
  db: D1Database,
  state: DisplayState,
  slot: Slot,
  id: number,
  now: Date,
): Promise<void> => {
  const unchanged = "slot_start IS ? AND phrase_id IS ?";
  const seen = [state.slotStart, state.phraseId];
  // both statements test the state this request read, so a concurrent pick or clear voids both
  const [, claim] = await db.batch([
    db
      .prepare(
        `UPDATE phrases SET last_shown_at = ?, shown_count = shown_count + 1
         WHERE id = ? AND EXISTS (SELECT 1 FROM display_state WHERE id = 1 AND ${unchanged})`,
      )
      .bind(now.toISOString(), id, ...seen),
    db
      .prepare(
        `UPDATE display_state SET slot_start = ?, phrase_id = ?
         WHERE id = 1 AND ${unchanged} AND EXISTS (SELECT 1 FROM phrases WHERE id = ?)`,
      )
      .bind(slot.start.toISOString(), id, ...seen, id),
  ]);
  if (claim?.meta.changes !== 1) {
    throw new Error(`The slot from ${slot.start.toISOString()} changed while picking phrase ${id}`);
  }
};

const overrideOnWall = async (
  db: D1Database,
  state: DisplayState,
  now: Date,
): Promise<OnWall | null> => {
  const override = activeOverride(state, now);
  if (override === null) return null;
  const phrase = await readPhrase(db, override.id);
  return phrase === null ? null : { phrase, until: override.until };
};

const slotOnWall = async (
  db: D1Database,
  state: DisplayState,
  slot: Slot,
  now: Date,
): Promise<OnWall | null> => {
  const until = slot.end.toISOString();
  if (state.slotStart === slot.start.toISOString() && state.phraseId !== null) {
    const picked = await readPhrase(db, state.phraseId);
    if (picked !== null) return { phrase: picked, until };
  }
  const [phrases, pins] = await Promise.all([readPhrases(db), readPins(db)]);
  const id = await scheduledPhrase(slot, phrases, pins);
  const phrase = phrases.find((row) => row.id === id);
  if (phrase === undefined) return null;
  await claimSlot(db, state, slot, phrase.id, now);
  return { phrase, until };
};

export const piNow = async ({ env, now }: RouteContext): Promise<Response> => {
  const db = env.DB;
  const [, times, state, specialDays] = await Promise.all([
    db
      .prepare("UPDATE display_state SET pi_last_seen_at = ? WHERE id = 1")
      .bind(now.toISOString())
      .run(),
    readPhraseTimes(db),
    readDisplayState(db),
    readSpecialDays(db),
  ]);
  const slot = slotAt(now, times);
  const onWall = (await overrideOnWall(db, state, now)) ?? (await slotOnWall(db, state, slot, now));
  if (onWall === null) {
    console.error("No phrase can go on the wall: every phrase is pinned to other days or flagged");
    return Response.json({ error: "No phrase can be shown" } satisfies ErrorResponse, {
      status: 503,
    });
  }
  return Response.json({
    phrase: { id: onWall.phrase.id, text: onWall.phrase.text, until: onWall.until },
    greeting: greetingFor(warsawDate(now), specialDays),
    full_refresh: state.fullRefreshSeq === 0 ? null : state.fullRefreshSeq,
    cleanupMinute: cleanupMinute(times),
  } satisfies PiNowResponse);
};

const flagPhrases = (
  db: D1Database,
  problem: "too_long" | "missing_glyphs",
  phraseIds: number[],
): D1PreparedStatement =>
  db
    .prepare("UPDATE phrases SET problem = ? WHERE id IN (SELECT value FROM json_each(?))")
    .bind(problem, JSON.stringify(phraseIds));

export const piScreen = async ({ request, env, now }: RouteContext): Promise<Response> => {
  let form: FormData;
  try {
    form = await request.formData();
  } catch {
    throw new InputError("The body must be multipart/form-data");
  }
  const screen = form.get("screen");
  if (!(screen instanceof File) || screen.type !== "image/png") {
    throw new InputError("The screen part must be a file of type image/png");
  }
  if (screen.size > SCREEN_MAX_BYTES) {
    return Response.json(
      { error: `The screen is over ${SCREEN_MAX_BYTES} bytes` } satisfies ErrorResponse,
      { status: 413 },
    );
  }
  const png = new Uint8Array(await screen.arrayBuffer());
  if (!PNG_SIGNATURE.every((byte, index) => png[index] === byte)) {
    throw new InputError("The screen part isn't a PNG");
  }
  const reportText = form.get("report");
  if (typeof reportText !== "string") throw new InputError("The report part must be JSON text");
  const report = screenReport(reportText);

  const db = env.DB;
  await db.batch([
    db
      .prepare(
        "UPDATE display_state SET screen_png = ?, screen_at = ?, screen_report = ? WHERE id = 1",
      )
      .bind(png, now.toISOString(), JSON.stringify(report)),
    flagPhrases(db, "missing_glyphs", report.missingGlyphs),
    flagPhrases(db, "too_long", report.tooLong),
    ...clearFromWall(db, report.tooLong),
  ]);
  if (report.tooLong.length > 0) {
    console.warn(`The Pi found phrases ${report.tooLong.join(", ")} too long for the screen`);
  }
  return new Response(null, { status: 204 });
};
