import { match } from "ts-pattern";
import type { PhraseFields, PinKind, PinSlot } from "../../src/api_types";
import { actionButton, showFailure } from "../actions";
import { api, ApiError } from "../api";
import { h, numbered, type Option, select } from "../dom";
import { daysInMonth, LUNAR_DAYS, LUNAR_MONTHS, TOO_LONG, WEEKDAYS } from "../format";
import type { Page } from "../page";
import { freshPin, type Pin, pinFieldsOf, pinOf } from "../pins";
import { drawPreview, FONT_FAMILY } from "../preview";

const PREVIEW_DELAY_MS = 60;
const PIN_KINDS: readonly Option<PinKind>[] = [
  ["yearly", "Every year"],
  ["lunar_yearly", "Every year, lunar"],
  ["monthly", "Every month"],
  ["weekly", "Every week"],
  ["date", "One date"],
];

type Mode = "rotation" | "pinned";

const dateFields = (pin: Pin, rerender: () => void): HTMLElement[] =>
  match(pin)
    .with({ kind: "date" }, (dated) => {
      const input = h("input", {
        class: "wide",
        type: "date",
        value: dated.date,
        "aria-label": "Date",
      });
      input.addEventListener("change", () => {
        dated.date = input.value;
      });
      return [input];
    })
    .with({ kind: "yearly" }, (yearly) => [
      select(
        numbered(12, "月"),
        yearly.month,
        (month) => {
          yearly.month = month;
          yearly.day = Math.min(yearly.day, daysInMonth(month));
          rerender();
        },
        { "aria-label": "Month" },
      ),
      select(
        numbered(daysInMonth(yearly.month), "日"),
        yearly.day,
        (day) => {
          yearly.day = day;
        },
        { "aria-label": "Day" },
      ),
    ])
    .with({ kind: "lunar_yearly" }, (lunar) => [
      select(
        numbered(12, "", LUNAR_MONTHS),
        lunar.month,
        (month) => {
          lunar.month = month;
        },
        { "aria-label": "Lunar month" },
      ),
      select(
        numbered(30, "", LUNAR_DAYS),
        lunar.day,
        (day) => {
          lunar.day = day;
        },
        { "aria-label": "Lunar day" },
      ),
    ])
    .with({ kind: "monthly" }, (monthly) => [
      select(
        numbered(31, "日"),
        monthly.day,
        (day) => {
          monthly.day = day;
        },
        { class: "wide", "aria-label": "Day" },
      ),
    ])
    .with({ kind: "weekly" }, (weekly) => [
      select(
        numbered(7, "", WEEKDAYS),
        weekly.weekday,
        (weekday) => {
          weekly.weekday = weekday;
        },
        { class: "wide", "aria-label": "Weekday" },
      ),
    ])
    .exhaustive();

const pinEditor = (
  initial: Pin,
  slots: readonly Option<PinSlot>[],
  onReplace: (pin: Pin) => void,
  onRemove: () => void,
): HTMLDivElement => {
  let pin = initial;
  const row = h("div", { class: "pin-row" });
  const fill = (): void => {
    row.replaceChildren(
      select(
        PIN_KINDS,
        pin.kind,
        (kind) => {
          pin = freshPin(kind, pin.slot);
          onReplace(pin);
          fill();
        },
        { class: "wide", "aria-label": "Repeats" },
      ),
      ...dateFields(pin, fill),
      select(
        slots,
        pin.slot,
        (slot) => {
          pin.slot = slot;
        },
        { class: "wide", "aria-label": "Time of day" },
      ),
      h("button", { type: "button", class: "link remove", onclick: onRemove }, "Remove"),
    );
  };
  fill();
  return row;
};

const dangerZone = (id: number, messages: HTMLElement): HTMLDivElement => {
  const confirmation = h(
    "div",
    { class: "confirm", hidden: true },
    h("span", {}, "Delete this phrase for good?"),
    actionButton(
      "Delete",
      messages,
      async () => {
        await api.deletePhrase(id);
        location.hash = "#/phrases";
      },
      { type: "button", class: "danger" },
    ),
    h(
      "button",
      {
        type: "button",
        class: "link",
        onclick: () => {
          confirmation.hidden = true;
          deleteLink.hidden = false;
        },
      },
      "Keep",
    ),
  );
  const deleteLink = h(
    "button",
    {
      type: "button",
      class: "link",
      onclick: () => {
        deleteLink.hidden = true;
        confirmation.hidden = false;
      },
    },
    "Delete phrase",
  );
  return h("div", { class: "danger-zone" }, deleteLink, confirmation);
};

export const editPage = async (id: number | null): Promise<Page> => {
  const blank: PhraseFields = { text: "", pins: [] };
  const [phrase, settings] = await Promise.all([
    id === null
      ? blank
      : api.phrases("", "all").then((phrases) => phrases.find((row) => row.id === id)),
    api.settings(),
  ]);
  if (phrase === undefined) throw new ApiError(404, `There is no phrase #${id}`);

  const slots: readonly Option<PinSlot>[] = [
    ["all_day", "All day"],
    ["morning", `Morning, ${settings.firstPhraseTime} to ${settings.secondPhraseTime}`],
    ["evening", `Evening, ${settings.secondPhraseTime} to ${settings.firstPhraseTime}`],
  ];
  const pins = phrase.pins.map(pinOf);
  let mode: Mode = pins.length > 0 ? "pinned" : "rotation";
  const messages = h("p", { class: "error" });

  const textarea = h("textarea", {
    class: "phrase",
    rows: 3,
    placeholder: "The phrase",
    autocapitalize: "off",
    value: phrase.text,
  });
  const canvas = h("canvas", { width: 800, height: 480 });
  const sizeLabel = h("p", { class: "size" });
  const screen = api
    .screenPng()
    .then((png) => (png === null ? null : createImageBitmap(png)))
    .catch((error: unknown) => {
      showFailure(messages, error);
      return null;
    });
  let draws = 0;
  let pendingDraw: ReturnType<typeof setTimeout> | undefined;
  const preview = async (): Promise<void> => {
    const token = ++draws;
    const text = textarea.value;
    await document.fonts.load(`32px ${FONT_FAMILY}`, text);
    const strip = await screen;
    if (token !== draws) return;
    const fit = drawPreview(canvas, text, strip);
    sizeLabel.textContent = text.trim() === "" ? "" : fit.fits ? `${fit.sizePx} px` : TOO_LONG;
  };
  const grow = (): void => {
    textarea.style.height = "auto";
    textarea.style.height = `${textarea.scrollHeight + 2}px`;
  };
  textarea.addEventListener("input", () => {
    grow();
    clearTimeout(pendingDraw);
    pendingDraw = setTimeout(() => void preview(), PREVIEW_DELAY_MS);
  });

  const pinList = h("div", { class: "pins" });
  const renderPins = (): void => {
    pinList.hidden = mode !== "pinned";
    pinList.replaceChildren(
      ...pins.map((pin, index) =>
        pinEditor(
          pin,
          slots,
          (next) => {
            pins[index] = next;
          },
          () => {
            pins.splice(index, 1);
            if (pins.length === 0) {
              mode = "rotation";
              rotation.checked = true;
            }
            renderPins();
          },
        ),
      ),
      h(
        "button",
        {
          type: "button",
          class: "link add",
          onclick: () => {
            pins.push(freshPin("yearly", "all_day"));
            renderPins();
          },
        },
        pins.length === 0 ? "Add a date" : "Add another date",
      ),
    );
  };
  const setMode = (next: Mode): void => {
    mode = next;
    if (mode === "pinned" && pins.length === 0) pins.push(freshPin("yearly", "all_day"));
    renderPins();
  };
  const rotation = h("input", {
    type: "radio",
    name: "mode",
    checked: mode === "rotation",
    onchange: () => setMode("rotation"),
  });
  const pinned = h("input", {
    type: "radio",
    name: "mode",
    checked: mode === "pinned",
    onchange: () => setMode("pinned"),
  });
  renderPins();

  const save = async (): Promise<number> => {
    const fields: PhraseFields = {
      text: textarea.value,
      pins: mode === "pinned" ? pins.map(pinFieldsOf) : [],
    };
    if (id !== null) {
      await api.updatePhrase(id, fields);
      return id;
    }
    return (await api.createPhrase(fields)).id;
  };

  requestAnimationFrame(() => {
    grow();
    void preview();
    if (id === null) textarea.focus();
  });

  const view = h(
    "section",
    { class: "edit" },
    h("a", { class: "crumb", href: "#/phrases" }, "‹ Phrases"),
    h("h1", {}, id === null ? "New phrase" : `Phrase #${id}`),
    textarea,
    h("div", { class: "preview" }, canvas, sizeLabel),
    h(
      "fieldset",
      {},
      h("legend", {}, "Shows"),
      h(
        "div",
        { class: "modes" },
        h("label", { class: "choice" }, rotation, "In rotation"),
        h("label", { class: "choice" }, pinned, "Pinned to a date"),
      ),
      pinList,
    ),
    h(
      "div",
      { class: "actions" },
      actionButton(
        "Save",
        messages,
        async () => {
          await save();
          location.hash = "#/phrases";
        },
        { class: "primary" },
      ),
      actionButton("Show now", messages, async () => {
        await api.showPhrase(await save());
        location.hash = "#/now";
      }),
    ),
    messages,
    id === null ? null : dangerZone(id, messages),
  );
  return { view, leave: () => clearTimeout(pendingDraw) };
};
