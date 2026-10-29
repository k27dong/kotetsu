import { match, P } from "ts-pattern";
import type { PinFields, PinKind, PinSlot } from "../src/api_types";
import { chineseDate, LUNAR_DAYS, LUNAR_MONTHS, todayInWarsaw, WEEKDAYS } from "./format";

// the API carries every date field for every kind; the pages work on the kind's own fields
export type Pin = { slot: PinSlot } & (
  | { kind: "date"; date: string }
  | { kind: "yearly" | "lunar_yearly"; month: number; day: number }
  | { kind: "monthly"; day: number }
  | { kind: "weekly"; weekday: number }
);

export const pinOf = (fields: PinFields): Pin =>
  match(fields)
    .with({ kind: "date", date: P.string }, ({ kind, date, slot }) => ({ kind, date, slot }))
    .with(
      { kind: P.union("yearly", "lunar_yearly"), month: P.number, day: P.number },
      ({ kind, month, day, slot }) => ({ kind, month, day, slot }),
    )
    .with({ kind: "monthly", day: P.number }, ({ kind, day, slot }) => ({ kind, day, slot }))
    .with({ kind: "weekly", weekday: P.number }, ({ kind, weekday, slot }) => ({
      kind,
      weekday,
      slot,
    }))
    .otherwise(({ kind }) => {
      throw new Error(`A ${kind} pin arrived without its date fields`);
    });

export const pinFieldsOf = (pin: Pin): PinFields => ({
  date: null,
  month: null,
  day: null,
  weekday: null,
  ...pin,
});

export const freshPin = (kind: PinKind, slot: PinSlot): Pin => {
  const today = todayInWarsaw();
  return match(kind)
    .with("date", (kind) => ({ kind, date: today.iso, slot }))
    .with("yearly", (kind) => ({ kind, month: today.month, day: today.day, slot }))
    .with("lunar_yearly", (kind) => ({ kind, month: 8, day: 15, slot }))
    .with("monthly", (kind) => ({ kind, day: 1, slot }))
    .with("weekly", (kind) => ({ kind, weekday: 1, slot }))
    .exhaustive();
};

export const pinText = (pin: Pin): string => {
  const repeats = match(pin)
    .with({ kind: "date" }, ({ date }) => `On ${chineseDate(date, true)}`)
    .with({ kind: "yearly" }, ({ month, day }) => `Every year, ${month}月${day}日`)
    .with(
      { kind: "lunar_yearly" },
      ({ month, day }) => `Every year, 农历${LUNAR_MONTHS[month - 1]}${LUNAR_DAYS[day - 1]}`,
    )
    .with({ kind: "monthly" }, ({ day }) => `Every month, ${day}日`)
    .with({ kind: "weekly" }, ({ weekday }) => `Every week, ${WEEKDAYS[weekday - 1]}`)
    .exhaustive();
  const slot = match(pin.slot)
    .with("all_day", () => "")
    .with("morning", () => ", morning")
    .with("evening", () => ", evening")
    .exhaustive();
  return repeats + slot;
};
