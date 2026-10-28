export type ErrorResponse = { error: string };
export type LoginRequest = { password: string };

export type PinKind = "date" | "yearly" | "lunar_yearly" | "monthly" | "weekly";
export type PinSlot = "morning" | "evening" | "all_day";
export type PinFields = {
  kind: PinKind;
  date: string | null;
  month: number | null;
  day: number | null;
  weekday: number | null;
  slot: PinSlot;
};

export type Icon = "heart" | "rings" | "cake" | "cat" | "hearts" | "star";
export type Greeting = { icon: Icon; text: string };
export type SpecialDayFields = {
  kind: "yearly" | "lunar_yearly";
  month: number;
  day: number;
  greeting: string;
  baseYear: number | null;
  icon: Icon;
  rank: number;
};

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

export type PiNowResponse = {
  phrase: { id: number; text: string; until: string };
  greeting: Greeting | null;
  full_refresh: number | null;
  cleanupMinute: number;
};

export type StatusResponse = {
  piLastSeenAt: string | null;
  onWall: { id: number; text: string; since: string | null } | null;
  next: { id: number; text: string; at: string } | null;
  screenAt: string | null;
  report: ScreenReport | null;
  nextSpecialDay: (Greeting & { date: string }) | null;
};

export type PhraseFilter = "all" | "pinned" | "english";
export type PhraseSummary = {
  id: number;
  text: string;
  pins: PinFields[];
  problem: string | null;
  lastShownAt: string | null;
};
export type PhraseFields = { text: string; pins: PinFields[] };
export type CreatedPhrase = { id: number };

export type Settings = { firstPhraseTime: string; secondPhraseTime: string };
