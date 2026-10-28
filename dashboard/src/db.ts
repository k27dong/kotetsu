import type { Pin, PhraseRow, SpecialDay } from "./rules";
import type { PhraseTimes } from "./time";

export type DisplayState = {
  slotStart: string | null;
  phraseId: number | null;
  overridePhraseId: number | null;
  overrideUntil: string | null;
  fullRefreshSeq: number;
  piLastSeenAt: string | null;
  screenAt: string | null;
  screenReport: string | null;
};

const PHRASE_COLUMNS = "id, text, last_shown_at AS lastShownAt, problem";
const LISTED_IDS = "IN (SELECT value FROM json_each(?))";

export async function readPhraseTimes(db: D1Database): Promise<PhraseTimes> {
  const times = await db
    .prepare(
      "SELECT first_phrase_time AS first, second_phrase_time AS second FROM settings WHERE id = 1",
    )
    .first<PhraseTimes>();
  if (times === null) throw new Error("The settings row is missing");
  return times;
}

export async function readDisplayState(db: D1Database): Promise<DisplayState> {
  const state = await db
    .prepare(
      `SELECT slot_start AS slotStart, phrase_id AS phraseId,
         override_phrase_id AS overridePhraseId, override_until AS overrideUntil,
         full_refresh_seq AS fullRefreshSeq, pi_last_seen_at AS piLastSeenAt,
         screen_at AS screenAt, screen_report AS screenReport
       FROM display_state WHERE id = 1`,
    )
    .first<DisplayState>();
  if (state === null) throw new Error("The display_state row is missing");
  return state;
}

export function readPhrase(db: D1Database, id: number): Promise<PhraseRow | null> {
  return db
    .prepare(`SELECT ${PHRASE_COLUMNS} FROM phrases WHERE id = ?`)
    .bind(id)
    .first<PhraseRow>();
}

export async function readPhrases(db: D1Database): Promise<PhraseRow[]> {
  const { results } = await db
    .prepare(`SELECT ${PHRASE_COLUMNS} FROM phrases ORDER BY id`)
    .all<PhraseRow>();
  return results;
}

export async function readPins(db: D1Database): Promise<Pin[]> {
  const { results } = await db
    .prepare(
      "SELECT id, phrase_id AS phraseId, kind, date, month, day, weekday, slot FROM pins ORDER BY id",
    )
    .all<Pin>();
  return results;
}

export async function readSpecialDays(db: D1Database): Promise<SpecialDay[]> {
  const { results } = await db
    .prepare(
      `SELECT id, kind, month, day, greeting, base_year AS baseYear, icon, rank
       FROM special_days ORDER BY id`,
    )
    .all<SpecialDay>();
  return results;
}

export function clearFromWall(db: D1Database, phraseIds: number[]): D1PreparedStatement[] {
  const ids = JSON.stringify(phraseIds);
  return [
    db
      .prepare(
        `UPDATE display_state SET slot_start = NULL, phrase_id = NULL
         WHERE id = 1 AND phrase_id ${LISTED_IDS}`,
      )
      .bind(ids),
    db
      .prepare(
        `UPDATE display_state SET override_phrase_id = NULL, override_until = NULL
         WHERE id = 1 AND override_phrase_id ${LISTED_IDS}`,
      )
      .bind(ids),
  ];
}
