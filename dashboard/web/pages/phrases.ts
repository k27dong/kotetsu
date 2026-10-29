import type { PhraseFilter, PhraseSummary } from "../../src/api_types";
import { showFailure } from "../actions";
import { api } from "../api";
import { h, type Option } from "../dom";
import { problemText } from "../format";
import type { Page } from "../page";
import { pinOf, pinText } from "../pins";

const SEARCH_DELAY_MS = 200;
const FILTERS: readonly Option<PhraseFilter>[] = [
  ["all", "All"],
  ["pinned", "Pinned"],
  ["english", "English"],
];

type Search = { q: string; filter: PhraseFilter; total: number | null };

// kept across visits, so the list comes back as it was left
const search: Search = { q: "", filter: "all", total: null };

const phraseRow = (phrase: PhraseSummary): HTMLLIElement =>
  h(
    "li",
    {},
    h(
      "a",
      { href: `#/edit/${phrase.id}` },
      h("span", { class: "id" }, `#${phrase.id}`),
      h("span", { class: "text" }, phrase.text),
      phrase.pins.length > 0 &&
        h("span", { class: "pin" }, phrase.pins.map((pin) => pinText(pinOf(pin))).join("; ")),
      phrase.problem !== null && h("span", { class: "flag" }, problemText(phrase.problem)),
    ),
  );

export const phrasesPage = async (): Promise<Page> => {
  const list = h("ol", { class: "phrases" });
  const empty = h("p", { class: "empty" });
  const messages = h("p", { class: "error" });
  const input = h("input", {
    class: "search",
    type: "search",
    placeholder: search.total === null ? "Search phrases" : `Search ${search.total} phrases`,
    value: search.q,
    autocomplete: "off",
    enterkeyhint: "search",
    "aria-label": "Search phrases",
  });

  let loads = 0;
  const load = async (): Promise<void> => {
    const token = ++loads;
    messages.textContent = "";
    const unfiltered = search.q.trim() === "" && search.filter === "all";
    const phrases = await api.phrases(search.q, search.filter);
    if (token !== loads) return;
    if (unfiltered) {
      search.total = phrases.length;
      input.placeholder = `Search ${search.total} phrases`;
    }
    list.replaceChildren(...phrases.map(phraseRow));
    empty.textContent =
      phrases.length > 0 ? "" : unfiltered ? "No phrases yet." : "Nothing matches.";
  };
  const reload = (): void => {
    load().catch((error: unknown) => showFailure(messages, error));
  };

  const chips = FILTERS.map(([filter, label]) => ({
    filter,
    button: h(
      "button",
      {
        type: "button",
        onclick: () => {
          search.filter = filter;
          markChips();
          reload();
        },
      },
      label,
    ),
  }));
  const markChips = (): void => {
    for (const { filter, button } of chips) {
      button.setAttribute("aria-pressed", String(filter === search.filter));
    }
  };
  markChips();

  let pending: ReturnType<typeof setTimeout> | undefined;
  input.addEventListener("input", () => {
    search.q = input.value;
    clearTimeout(pending);
    pending = setTimeout(reload, SEARCH_DELAY_MS);
  });
  await load();

  const view = h(
    "section",
    {},
    h(
      "div",
      { class: "page-head" },
      h("h1", {}, "Phrases"),
      h("a", { class: "button primary", href: "#/new" }, "New"),
    ),
    input,
    h(
      "div",
      { class: "chips", role: "group", "aria-label": "Filter" },
      chips.map(({ button }) => button),
    ),
    messages,
    list,
    empty,
  );
  return { view, leave: () => clearTimeout(pending) };
};
