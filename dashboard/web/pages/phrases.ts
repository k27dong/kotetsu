import type { PhraseSummary } from "../../src/api_types";
import { api, ApiError } from "../api";
import { h, type Option, select } from "../dom";
import { problemText, when } from "../format";
import type { Page } from "../page";
import { pinOf, pinText } from "../pins";
import { phraseEditor } from "./edit";

export type Order = "newest" | "oldest" | "shown";

const ORDERS: readonly Option<Order>[] = [
  ["newest", "Newest first"],
  ["oldest", "Oldest first"],
  ["shown", "Last shown first"],
];
const WIDE_SCREEN = "(min-width: 1000px)"; // keep in step with the side-by-side layout in style.css

// kept across visits, so the list comes back as it was left
const search: { query: string; order: Order } = { query: "", order: "newest" };

export const isWideScreen = (): boolean => window.matchMedia(WIDE_SCREEN).matches;

const fold = (text: string): string => text.normalize("NFKC").toLowerCase();

const shownMs = (phrase: PhraseSummary): number =>
  phrase.lastShownAt === null ? -Infinity : Date.parse(phrase.lastShownAt);

const COMPARE: Record<Order, (a: PhraseSummary, b: PhraseSummary) => number> = {
  newest: (a, b) => b.id - a.id,
  oldest: (a, b) => a.id - b.id,
  shown: (a, b) => shownMs(b) - shownMs(a) || b.id - a.id, // never-shown pairs give NaN, so by number
};

export const findPhrases = (
  phrases: readonly PhraseSummary[],
  query: string,
  order: Order,
): PhraseSummary[] => {
  const wanted = fold(query).trim();
  const number = /^#?(\d+)$/.exec(wanted)?.[1];
  return phrases
    .filter((phrase) => fold(phrase.text).includes(wanted) || String(phrase.id) === number)
    .sort(COMPARE[order]);
};

const rowNote = (phrase: PhraseSummary, now: Date): HTMLSpanElement | null => {
  if (phrase.problem !== null) {
    return h("span", { class: "note flag" }, problemText(phrase.problem));
  }
  if (phrase.pins.length > 0) {
    const pins = phrase.pins.map((pin) => pinText(pinOf(pin))).join("; ");
    return h("span", { class: "note" }, pins);
  }
  if (phrase.lastShownAt !== null)
    return h("span", { class: "note" }, when(phrase.lastShownAt, now));
  return null;
};

const latestShown = (phrases: readonly PhraseSummary[]): PhraseSummary | undefined =>
  findPhrases(phrases, "", "shown").find((phrase) => phrase.lastShownAt !== null);

// a phrase id opens that phrase beside the list, null a new one, undefined the one on the wall
export const phrasesPage = async (opening?: number | null): Promise<Page> => {
  const wide = isWideScreen();
  const [loaded, settings] = await Promise.all([api.phrases(), wide ? api.settings() : null]);
  let phrases = loaded;
  let current: number | null = null;
  let leaveEditor: () => void = () => {};

  const list = h("ol", { class: "phrases" });
  const count = h("p", { class: "count" });
  const empty = h("p", { class: "empty" });
  const panel = h("div", { class: "panel" });

  const renderList = (): void => {
    const found = findPhrases(phrases, search.query, search.order);
    const now = new Date();
    count.textContent = found.length === 1 ? "1 phrase" : `${found.length} phrases`;
    list.replaceChildren(
      ...found.map((phrase) =>
        h(
          "li",
          {},
          h(
            "a",
            {
              href: `#/edit/${phrase.id}`,
              "aria-current": phrase.id === current ? "true" : null,
              onclick: (event) => {
                if (settings === null || !isWideScreen()) return; // a phone opens the editor as its own page
                event.preventDefault();
                open(phrase);
                history.replaceState(null, "", `#/edit/${phrase.id}`);
              },
            },
            h("span", { class: "id" }, `#${phrase.id}`),
            h("span", { class: "text" }, phrase.text),
            rowNote(phrase, now),
          ),
        ),
      ),
    );
    empty.textContent =
      found.length > 0 ? "" : phrases.length === 0 ? "No phrases yet." : "Nothing matches.";
  };

  const showHint = (): void => {
    leaveEditor();
    leaveEditor = () => {};
    current = null;
    panel.replaceChildren(h("p", { class: "hint" }, "Pick a phrase to edit it."));
    renderList();
  };

  const reload = async (openId: number | null): Promise<void> => {
    phrases = await api.phrases();
    const phrase = phrases.find((row) => row.id === openId);
    if (phrase === undefined) {
      showHint();
      history.replaceState(null, "", "#/phrases");
    } else {
      open(phrase);
      history.replaceState(null, "", `#/edit/${phrase.id}`);
    }
  };

  const open = (phrase: PhraseSummary | null): void => {
    if (settings === null) return;
    leaveEditor();
    const editor = phraseEditor(phrase, settings, {
      saved: (id) => reload(id),
      deleted: () => reload(null),
    });
    leaveEditor = editor.leave;
    current = phrase === null ? null : phrase.id;
    panel.replaceChildren(
      h("h2", { class: "panel-title" }, phrase === null ? "New phrase" : `Phrase #${phrase.id}`),
      editor.view,
    );
    renderList();
  };

  const input = h("input", {
    class: "search",
    type: "search",
    placeholder: "Search text or #number",
    value: search.query,
    autocomplete: "off",
    enterkeyhint: "search",
    "aria-label": "Search phrases",
  });
  input.addEventListener("input", () => {
    search.query = input.value;
    renderList();
  });
  const order = select(
    ORDERS,
    search.order,
    (value) => {
      search.order = value;
      renderList();
    },
    { "aria-label": "Order" },
  );
  const newButton = h(
    "a",
    {
      class: "button primary",
      href: "#/new",
      onclick: (event) => {
        if (settings === null || !isWideScreen()) return;
        event.preventDefault();
        open(null);
        history.replaceState(null, "", "#/new");
      },
    },
    "New",
  );

  if (settings === null) {
    renderList();
  } else if (opening === null) {
    open(null);
  } else if (opening === undefined) {
    const onWall = latestShown(phrases);
    if (onWall === undefined) showHint();
    else open(onWall);
  } else {
    const phrase = phrases.find((row) => row.id === opening);
    if (phrase === undefined) throw new ApiError(404, `There is no phrase #${opening}`);
    open(phrase);
    requestAnimationFrame(() => {
      list.querySelector('[aria-current="true"]')?.scrollIntoView({ block: "center" }); // after the router's scroll to the top
    });
  }

  const view = h(
    "section",
    { class: "phrases-page" },
    h("div", { class: "page-head" }, h("h1", {}, "Phrases"), newButton),
    h("div", { class: "tools" }, input, order),
    h(
      "div",
      { class: "split" },
      h("div", { class: "list" }, count, list, empty),
      settings === null ? null : panel,
    ),
  );
  return { view, leave: () => leaveEditor() };
};
