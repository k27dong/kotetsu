declare module "lunar-javascript" {
  export interface Lunar {
    getYear(): number;
    getMonth(): number;
    getDay(): number;
  }
  export interface Solar {
    getLunar(): Lunar;
    toYmd(): string;
  }
  export interface LunarMonth {
    getMonth(): number;
    getFirstJulianDay(): number;
    next(months: number): LunarMonth;
  }
  export const Solar: {
    fromYmd(year: number, month: number, day: number): Solar;
    fromJulianDay(julianDay: number): Solar;
  };
  export const LunarMonth: { fromYm(year: number, month: number): LunarMonth | null };
}
