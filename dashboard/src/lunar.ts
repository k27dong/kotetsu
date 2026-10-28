import { LunarMonth, Solar } from "lunar-javascript";
import { daysBetween, parseDate } from "./time";

export type LunarDate = { month: number; day: number; leap: boolean; monthLength: 29 | 30 };

// 6tail puts this new moon 25 s after midnight China time, HKO before it; HKO wins
const HKO_MONTH_STARTS = new Map([["2057-09-29", "2057-09-28"]]);

function startOf(month: LunarMonth): string {
  const start = Solar.fromJulianDay(month.getFirstJulianDay()).toYmd();
  return HKO_MONTH_STARTS.get(start) ?? start;
}

export function lunarOf(date: string): LunarDate {
  const { year, month, day } = parseDate(date);
  const lunar = Solar.fromYmd(year, month, day).getLunar();
  const found = LunarMonth.fromYm(lunar.getYear(), lunar.getMonth());
  if (found === null) throw new Error(`No lunar month for ${date}`);
  // a start moved a day earlier pulls that day into the month 6tail begins tomorrow
  const following = found.next(1);
  const current = startOf(following) <= date ? following : found;
  const monthLength = daysBetween(startOf(current), startOf(current.next(1)));
  if (monthLength !== 29 && monthLength !== 30) {
    throw new Error(`The lunar month of ${date} lasts ${monthLength} days`);
  }
  return {
    month: Math.abs(current.getMonth()),
    day: daysBetween(startOf(current), date) + 1,
    leap: current.getMonth() < 0,
    monthLength,
  };
}
