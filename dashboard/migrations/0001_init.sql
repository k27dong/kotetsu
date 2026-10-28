CREATE TABLE phrases (
  id            INTEGER PRIMARY KEY,
  text          TEXT NOT NULL CHECK (length(trim(text)) > 0),
  created_at    TEXT NOT NULL,
  updated_at    TEXT NOT NULL,
  last_shown_at TEXT,
  shown_count   INTEGER NOT NULL DEFAULT 0,
  problem       TEXT                -- from the Pi's report: 'too_long' or 'missing_glyphs'
);

CREATE TABLE pins (
  id          INTEGER PRIMARY KEY,
  phrase_id   INTEGER NOT NULL REFERENCES phrases (id) ON DELETE CASCADE,
  kind        TEXT NOT NULL CHECK (kind IN ('date', 'yearly', 'lunar_yearly', 'monthly', 'weekly')),
  date        TEXT,                 -- 'date': YYYY-MM-DD
  month       INTEGER,              -- 'yearly' and 'lunar_yearly'
  day         INTEGER,              -- 'yearly', 'lunar_yearly', 'monthly'
  weekday     INTEGER,              -- 'weekly': 1 = Monday … 7 = Sunday
  slot        TEXT NOT NULL CHECK (slot IN ('morning', 'evening', 'all_day'))
);

CREATE TABLE special_days (
  id         INTEGER PRIMARY KEY,
  kind       TEXT NOT NULL CHECK (kind IN ('yearly', 'lunar_yearly')),
  month      INTEGER NOT NULL,
  day        INTEGER NOT NULL,
  greeting   TEXT NOT NULL,         -- may contain {n}: years since base_year
  base_year  INTEGER,
  icon       TEXT NOT NULL CHECK (icon IN ('heart', 'rings', 'cake', 'cat', 'hearts', 'star')),
  rank       INTEGER NOT NULL       -- lower wins when two fall on one day
);

CREATE TABLE settings (
  id                 INTEGER PRIMARY KEY CHECK (id = 1),
  first_phrase_time  TEXT NOT NULL DEFAULT '04:00',   -- Warsaw time
  second_phrase_time TEXT NOT NULL DEFAULT '16:00'
);

CREATE TABLE display_state (
  id                  INTEGER PRIMARY KEY CHECK (id = 1),
  slot_start          TEXT,         -- start of the current phrase slot
  phrase_id           INTEGER,      -- chosen for the current slot
  override_phrase_id  INTEGER,      -- Show now or Skip, until the next phrase time
  override_until      TEXT,
  full_refresh_seq    INTEGER NOT NULL DEFAULT 0,
  pi_last_seen_at     TEXT,
  screen_png          BLOB,
  screen_at           TEXT,
  screen_report       TEXT          -- JSON from the Pi's last upload
);
