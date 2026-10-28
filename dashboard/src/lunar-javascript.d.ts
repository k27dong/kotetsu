declare module "lunar-javascript" {
  export interface Solar {
    toYmd(): string;
  }
  export interface LunarMonth {
    getMonth(): number;
    getFirstJulianDay(): number;
  }
  export interface LunarYear {
    getMonths(): LunarMonth[];
  }
  export const Solar: { fromJulianDay(julianDay: number): Solar };
  export const LunarYear: { fromYear(year: number): LunarYear };
}
