const WARSAW = "Europe/Warsaw";
const DAY_MS = 24 * 60 * 60 * 1000;

export const TOO_LONG = "Too long for the screen";
const PROBLEMS: Record<string, string> = {
  too_long: TOO_LONG,
  missing_glyphs: "Missing characters",
};

export const LUNAR_MONTHS = "正月 二月 三月 四月 五月 六月 七月 八月 九月 十月 冬月 腊月".split(
  " ",
);
export const LUNAR_DAYS = (
  "初一 初二 初三 初四 初五 初六 初七 初八 初九 初十 " +
  "十一 十二 十三 十四 十五 十六 十七 十八 十九 二十 " +
  "廿一 廿二 廿三 廿四 廿五 廿六 廿七 廿八 廿九 三十"
).split(" ");
export const WEEKDAYS = "周一 周二 周三 周四 周五 周六 周日".split(" ");

const timeFormat = new Intl.DateTimeFormat("en-GB", {
  timeZone: WARSAW,
  hour: "2-digit",
  minute: "2-digit",
  hourCycle: "h23",
});
const dayFormat = new Intl.DateTimeFormat("en-CA", {
  timeZone: WARSAW,
  year: "numeric",
  month: "2-digit",
  day: "2-digit",
});

const partsOf = (day: string): { year: number; month: number; date: number } => ({
  year: Number(day.slice(0, 4)),
  month: Number(day.slice(5, 7)),
  date: Number(day.slice(8, 10)),
});

export const problemText = (problem: string): string => PROBLEMS[problem] ?? problem;

export const warsawDay = (date: Date): string => dayFormat.format(date);

export const clock = (iso: string): string => timeFormat.format(new Date(iso));

export const chineseDate = (day: string, withYear: boolean): string => {
  const { year, month, date } = partsOf(day);
  return `${withYear ? `${year}年` : ""}${month}月${date}日`;
};

export const when = (iso: string, now: Date): string => {
  const day = warsawDay(new Date(iso));
  const today = warsawDay(now);
  if (day === today) return clock(iso);
  if (day === warsawDay(new Date(now.getTime() - DAY_MS))) return `yesterday ${clock(iso)}`;
  return `${chineseDate(day, day.slice(0, 4) !== today.slice(0, 4))} ${clock(iso)}`;
};

export const ago = (iso: string, now: Date): string => {
  const seconds = Math.max(0, Math.round((now.getTime() - Date.parse(iso)) / 1000));
  if (seconds < 60) return `${seconds} s ago`;
  if (seconds < 3600) return `${Math.round(seconds / 60)} min ago`;
  if (seconds < 86400) return `${Math.round(seconds / 3600)} h ago`;
  return when(iso, now);
};

export const todayInWarsaw = (): { month: number; day: number; iso: string } => {
  const iso = warsawDay(new Date());
  const { month, date } = partsOf(iso);
  return { month, day: date, iso };
};

export const daysInMonth = (month: number): number =>
  new Date(Date.UTC(2000, month, 0)).getUTCDate(); // 2000 is a leap year, so February has 29
