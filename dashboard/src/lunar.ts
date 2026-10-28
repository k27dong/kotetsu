import { LunarYear, Solar } from "lunar-javascript";
import { daysBetween, parseDate, shiftDate } from "./time";

export type LunarDate = { month: number; day: number; leap: boolean; monthLength: 29 | 30 };

type MonthStart = { month: number; leap: boolean; start: string };

// 6tail puts this new moon 25 s after midnight China time, HKO before it; HKO wins
const HKO_MONTH_STARTS = new Map([["2057-09-29", "2057-09-28"]]);
// 6tail remembers one year at a time, and rebuilding one costs about 0.2 ms
const monthStartsByYear = new Map<number, MonthStart[]>();

const monthStartsOf = (year: number): MonthStart[] => {
  const cached = monthStartsByYear.get(year);
  if (cached !== undefined) return cached;
  // 6tail's 15 months start at the previous 11th and end at the next 1st, or at this year's 12th
  // when a leap month falls among them; either way they cover the Gregorian year
  const starts = LunarYear.fromYear(year)
    .getMonths()
    .map((month) => {
      const start = Solar.fromJulianDay(month.getFirstJulianDay()).toYmd();
      return {
        month: Math.abs(month.getMonth()),
        leap: month.getMonth() < 0,
        start: HKO_MONTH_STARTS.get(start) ?? start,
      };
    });
  monthStartsByYear.set(year, starts);
  return starts;
};

export const lunarOf = (date: string): LunarDate => {
  const starts = monthStartsOf(parseDate(date).year);
  const index = starts.findLastIndex((month) => month.start <= date);
  const current = starts[index];
  const next = starts[index + 1];
  if (current === undefined || next === undefined) throw new Error(`No lunar month for ${date}`);
  const monthLength = daysBetween(current.start, next.start);
  if (monthLength !== 29 && monthLength !== 30) {
    throw new Error(`The lunar month of ${date} lasts ${monthLength} days`);
  }
  return {
    month: current.month,
    day: daysBetween(current.start, date) + 1,
    leap: current.leap,
    monthLength,
  };
};

export const solarOf = (
  lunarYear: number,
  month: number,
  day: number,
  leap: boolean,
): string | null => {
  const starts = monthStartsOf(lunarYear);
  // the list opens with the previous year's 11th and 12th months
  const newYear = starts.findIndex((start) => start.month === 1 && !start.leap);
  if (newYear === -1) throw new Error(`Lunar year ${lunarYear} has no first month`);
  const found = starts.slice(newYear).find((start) => start.month === month && start.leap === leap);
  if (found === undefined || day < 1 || day > lunarOf(found.start).monthLength) return null;
  return shiftDate(found.start, day - 1);
};
