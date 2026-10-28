import { LunarYear, Solar } from "lunar-javascript";
import { daysBetween, parseDate } from "./time";

export type LunarDate = { month: number; day: number; leap: boolean; monthLength: 29 | 30 };

type MonthStart = { month: number; leap: boolean; start: string };

// 6tail puts this new moon 25 s after midnight China time, HKO before it; HKO wins
const HKO_MONTH_STARTS = new Map([["2057-09-29", "2057-09-28"]]);
// 6tail remembers one year at a time, and rebuilding one costs about 0.2 ms
const monthStartsByYear = new Map<number, MonthStart[]>();

function monthStartsOf(year: number): MonthStart[] {
  const cached = monthStartsByYear.get(year);
  if (cached !== undefined) return cached;
  // 6tail's list runs from the previous 11th month to the next 1st, so it covers the Gregorian year
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
}

export function lunarOf(date: string): LunarDate {
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
}
