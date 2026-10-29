import type { SpecialDayFields } from "../../src/api_types";
import { actionButton } from "../actions";
import { api } from "../api";
import { h, numbered, type Option, select } from "../dom";
import { daysInMonth, LUNAR_DAYS, LUNAR_MONTHS, todayInWarsaw } from "../format";
import { icon, ICON_NAMES } from "../icons";
import type { Page } from "../page";

const KINDS: readonly Option<SpecialDayFields["kind"]>[] = [
  ["yearly", "公历"],
  ["lunar_yearly", "农历"],
];

let iconGroups = 0;

const specialDayEditor = (day: SpecialDayFields, onRemove: () => void): HTMLLIElement => {
  const iconGroup = `icon-${++iconGroups}`;
  const greeting = h("input", {
    class: "greeting",
    value: day.greeting,
    placeholder: "Greeting",
    "aria-label": "Greeting",
    oninput: () => {
      day.greeting = greeting.value;
      counting.hidden = !day.greeting.includes("{n}");
    },
  });
  const baseYear = h("input", {
    type: "number",
    min: 1,
    max: 9999,
    value: day.baseYear ?? "",
    placeholder: "Year",
    onchange: () => {
      day.baseYear = baseYear.value === "" ? null : Number(baseYear.value);
    },
  });
  const counting = h(
    "label",
    { class: "field", hidden: !day.greeting.includes("{n}") },
    "Counts the years from",
    baseYear,
  );
  const rank = h("input", {
    type: "number",
    min: 0,
    max: 1000,
    value: day.rank,
    onchange: () => {
      day.rank = Number(rank.value);
    },
  });
  const dateRow = h("div", { class: "row" });
  const fillDate = (): void => {
    const lunar = day.kind === "lunar_yearly";
    dateRow.replaceChildren(
      select(
        KINDS,
        day.kind,
        (kind) => {
          day.kind = kind;
          day.day = Math.min(day.day, kind === "lunar_yearly" ? 30 : daysInMonth(day.month));
          fillDate();
        },
        { "aria-label": "Calendar" },
      ),
      select(
        numbered(12, "月", lunar ? LUNAR_MONTHS : undefined),
        day.month,
        (month) => {
          day.month = month;
          if (!lunar) day.day = Math.min(day.day, daysInMonth(month));
          fillDate();
        },
        { "aria-label": "Month" },
      ),
      select(
        numbered(lunar ? 30 : daysInMonth(day.month), "日", lunar ? LUNAR_DAYS : undefined),
        day.day,
        (date) => {
          day.day = date;
        },
        { "aria-label": "Day" },
      ),
    );
  };
  fillDate();
  return h(
    "li",
    { class: "special-day" },
    greeting,
    dateRow,
    h(
      "div",
      { class: "icons", role: "radiogroup", "aria-label": "Icon" },
      ICON_NAMES.map((choice) =>
        h(
          "label",
          { title: choice },
          h("input", {
            type: "radio",
            name: iconGroup,
            value: choice,
            checked: day.icon === choice,
            onchange: () => {
              day.icon = choice;
            },
          }),
          icon(choice),
        ),
      ),
    ),
    h("div", { class: "row pair" }, counting, h("label", { class: "field" }, "Rank", rank)),
    h("button", { type: "button", class: "link remove", onclick: onRemove }, "Remove"),
  );
};

export const specialDaysPage = async (): Promise<Page> => {
  let days = (await api.specialDays()).map((day) => ({ ...day }));
  const list = h("ol", { class: "special-days" });
  const renderList = (): void => {
    list.replaceChildren(
      ...days.map((day, index) =>
        specialDayEditor(day, () => {
          days.splice(index, 1);
          renderList();
        }),
      ),
    );
  };
  renderList();
  const messages = h("p", { class: "error" });
  const saved = h("p", { class: "notice" });
  const view = h(
    "section",
    {},
    h("h1", {}, "Special days"),
    h(
      "p",
      {},
      "Each puts its greeting on the day counter. When two fall on one day, the lower rank wins. " +
        "Write {n} to count the years from a base year, as in 结婚{n}周年.",
    ),
    list,
    h(
      "button",
      {
        type: "button",
        class: "link add",
        onclick: () => {
          const today = todayInWarsaw();
          days.push({
            kind: "yearly",
            month: today.month,
            day: today.day,
            greeting: "",
            baseYear: null,
            icon: "heart",
            rank: 50,
          });
          renderList();
          list.lastElementChild?.querySelector("input")?.focus();
        },
      },
      "Add a day",
    ),
    h(
      "div",
      { class: "actions" },
      actionButton(
        "Save",
        messages,
        async () => {
          saved.textContent = "";
          days = (await api.saveSpecialDays(days)).map((day) => ({ ...day }));
          renderList();
          saved.textContent = "Saved.";
        },
        { class: "primary" },
      ),
    ),
    messages,
    saved,
  );
  return { view };
};
