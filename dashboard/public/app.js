import { drawPreview } from "./preview.js";

const PHRASE_FONT = '"LXGW WenKai GB Screen"';
const WARSAW = "Europe/Warsaw";
const PI_ONLINE_MS = 60 * 1000;
const REFOCUS_GAP_MS = 5 * 1000;
const SEARCH_DELAY_MS = 200;
const PREVIEW_DELAY_MS = 60;
const WIFI_QUALITY_MAX = 70;

const PIN_KINDS = [
  ["yearly", "Every year"],
  ["lunar_yearly", "Every year, lunar"],
  ["monthly", "Every month"],
  ["weekly", "Every week"],
  ["date", "One date"],
];
const SPECIAL_KINDS = [
  ["yearly", "公历"],
  ["lunar_yearly", "农历"],
];
const PROBLEMS = { too_long: "Too long for the screen", missing_glyphs: "Missing characters" };
const LUNAR_MONTHS = "正月 二月 三月 四月 五月 六月 七月 八月 九月 十月 冬月 腊月".split(" ");
const LUNAR_DAYS = (
  "初一 初二 初三 初四 初五 初六 初七 初八 初九 初十 " +
  "十一 十二 十三 十四 十五 十六 十七 十八 十九 二十 " +
  "廿一 廿二 廿三 廿四 廿五 廿六 廿七 廿八 廿九 三十"
).split(" ");
const WEEKDAYS = "周一 周二 周三 周四 周五 周六 周日".split(" ");
const ICONS = {
  heart:
    "M12 21.35l-1.45-1.32C5.4 15.36 2 12.28 2 8.5 2 5.42 4.42 3 7.5 3c1.74 0 3.41.81 4.5 2.09C13.09 3.81 14.76 3 16.5 3 19.58 3 22 5.42 22 8.5c0 3.78-3.4 6.86-8.55 11.54L12 21.35z",
  rings:
    "M9 6a7 7 0 1 0 0 14A7 7 0 0 0 9 6zm0 2.5a4.5 4.5 0 1 1 0 9 4.5 4.5 0 0 1 0-9zM15 6a7 7 0 1 0 0 14 7 7 0 0 0 0-14zm0 2.5a4.5 4.5 0 1 1 0 9 4.5 4.5 0 0 1 0-9z",
  cake: "M11 2h2v4h-2zM6 8h12a2 2 0 0 1 2 2v3H4v-3a2 2 0 0 1 2-2zM4 15h16v5a1 1 0 0 1-1 1H5a1 1 0 0 1-1-1z",
  cat: "M4 3l5 4h6l5-4v10a8 8 0 0 1-16 0zm4 8a1.3 1.3 0 1 0 0 2.6A1.3 1.3 0 0 0 8 11zm8 0a1.3 1.3 0 1 0 0 2.6 1.3 1.3 0 0 0 0-2.6zm-4 3l-1.5 1.5L12 17l1.5-1.5z",
  hearts:
    "M8 14.4l-.9-.8C3.9 10.7 2 9 2 6.8 2 5 3.4 3.6 5.2 3.6c1 0 2 .5 2.8 1.3.8-.8 1.8-1.3 2.8-1.3 1.8 0 3.2 1.4 3.2 3.2 0 2.2-1.9 3.9-5.1 6.8zM16 21.5l-.9-.8c-3.2-2.9-5.1-4.6-5.1-6.8 0-1.8 1.4-3.2 3.2-3.2 1 0 2 .5 2.8 1.3.8-.8 1.8-1.3 2.8-1.3 1.8 0 3.2 1.4 3.2 3.2 0 2.2-1.9 3.9-5.1 6.8z",
  star: "M12 2l2.9 6.6 7.1.6-5.4 4.7 1.6 7.1L12 17.3 5.8 21l1.6-7.1L2 9.2l7.1-.6z",
};

const main = document.querySelector("main");
const header = document.querySelector("header.top");
const navLinks = [...header.querySelectorAll("nav a")];

const timeFormat = new Intl.DateTimeFormat("en-GB", {
  timeZone: WARSAW,
  hour: "2-digit",
  minute: "2-digit",
  hourCycle: "h23",
});
const dayFormat = new Intl.DateTimeFormat("en-CA", {
  timeZone: WARSAW,
  year: "numeric",
  month: "2-digit",
  day: "2-digit",
});

class ApiError extends Error {
  constructor(status, message) {
    super(message);
    this.status = status;
  }
}

function h(tag, attributes = {}, ...children) {
  const element = document.createElement(tag);
  for (const [name, value] of Object.entries(attributes)) {
    if (value === null || value === undefined || value === false) continue;
    if (name.startsWith("on")) element.addEventListener(name.slice(2), value);
    else if (name === "value") element.value = value;
    else if (value === true) element.setAttribute(name, "");
    else element.setAttribute(name, String(value));
  }
  element.append(...children.flat().filter((child) => child != null && child !== false));
  return element;
}

function icon(name) {
  const span = h("span", { class: "icon", "aria-hidden": "true" });
  span.innerHTML = `<svg viewBox="0 0 24 24"><path d="${ICONS[name]}"/></svg>`;
  return span;
}

function select(options, selected, onchange, attributes = {}) {
  return h(
    "select",
    { ...attributes, onchange: (event) => onchange(event.target.value) },
    options.map(([value, label]) =>
      h("option", { value, selected: String(value) === String(selected) }, label),
    ),
  );
}

function numbered(count, suffix, names) {
  return Array.from({ length: count }, (_, index) => [
    index + 1,
    names ? names[index] : `${index + 1}${suffix}`,
  ]);
}

async function request(path, method = "GET", body) {
  let response;
  try {
    response = await fetch(path, {
      method,
      headers: body === undefined ? {} : { "Content-Type": "application/json" },
      body: body === undefined ? undefined : JSON.stringify(body),
    });
  } catch {
    throw new ApiError(0, "Couldn't reach the server");
  }
  if (response.status === 401) {
    showSignIn();
    throw new ApiError(401, "Signed out");
  }
  if (!response.ok) {
    let message = `${response.status} from ${path}`;
    try {
      message = (await response.json()).error ?? message;
    } catch {
      // the body wasn't the API's { error } JSON, so the status line stands
    }
    throw new ApiError(response.status, message);
  }
  return response;
}

async function api(path, method, body) {
  const response = await request(path, method, body);
  return response.status === 204 ? null : response.json();
}

function showFailure(messages, error) {
  if (!(error instanceof ApiError) || error.status === 401) throw error;
  messages.textContent = error.message;
  messages.scrollIntoView({ block: "nearest" });
}

async function perform(button, messages, action) {
  button.disabled = true;
  messages.textContent = "";
  try {
    await action();
  } catch (error) {
    showFailure(messages, error);
  } finally {
    button.disabled = false;
  }
}

function clock(iso) {
  return timeFormat.format(new Date(iso));
}

function chineseDate(day, withYear) {
  const [year, month, date] = day.split("-").map(Number);
  return `${withYear ? `${year}年` : ""}${month}月${date}日`;
}

function when(iso, now) {
  const day = dayFormat.format(new Date(iso));
  const today = dayFormat.format(now);
  if (day === today) return clock(iso);
  if (day === dayFormat.format(new Date(now.getTime() - 86400 * 1000))) {
    return `yesterday ${clock(iso)}`;
  }
  return `${chineseDate(day, day.slice(0, 4) !== today.slice(0, 4))} ${clock(iso)}`;
}

function ago(iso, now) {
  const seconds = Math.max(0, Math.round((now.getTime() - Date.parse(iso)) / 1000));
  if (seconds < 60) return `${seconds} s ago`;
  if (seconds < 3600) return `${Math.round(seconds / 60)} min ago`;
  if (seconds < 86400) return `${Math.round(seconds / 3600)} h ago`;
  return when(iso, now);
}

function todayInWarsaw() {
  const [year, month, day] = dayFormat.format(new Date()).split("-").map(Number);
  return { year, month, day, iso: dayFormat.format(new Date()) };
}

function pinText(pin) {
  const repeats = {
    date: () => `On ${chineseDate(pin.date, true)}`,
    yearly: () => `Every year, ${pin.month}月${pin.day}日`,
    lunar_yearly: () => `Every year, 农历${LUNAR_MONTHS[pin.month - 1]}${LUNAR_DAYS[pin.day - 1]}`,
    monthly: () => `Every month, ${pin.day}日`,
    weekly: () => `Every week, ${WEEKDAYS[pin.weekday - 1]}`,
  }[pin.kind]();
  const slot = { all_day: "", morning: ", morning", evening: ", evening" }[pin.slot];
  return repeats + slot;
}

function signInPage() {
  const error = h("p", { class: "error" });
  const password = h("input", {
    type: "password",
    placeholder: "Password",
    autocomplete: "current-password",
    required: true,
    oninput: () => {
      error.textContent = "";
    },
  });
  const form = h(
    "form",
    {
      class: "sign-in",
      onsubmit: async (event) => {
        event.preventDefault();
        let response;
        try {
          response = await fetch("/api/login", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({ password: password.value }),
          });
        } catch {
          error.textContent = "Couldn't reach the server";
          return;
        }
        if (response.status === 401) {
          error.textContent = "Wrong password";
          password.select();
          return;
        }
        if (!response.ok) {
          error.textContent = `Couldn't sign in (${response.status})`;
          return;
        }
        render();
      },
    },
    h("h1", {}, "kotetsu"),
    password,
    h("button", { class: "primary" }, "Sign in"),
    error,
  );
  requestAnimationFrame(() => password.focus());
  return form;
}

function showSignIn() {
  leavePage();
  leavePage = () => {};
  header.hidden = true;
  main.replaceChildren(signInPage());
}

function slotSection(heading, phrase, actions) {
  return h(
    "section",
    { class: "slot" },
    h(
      "h2",
      {},
      h("span", {}, heading),
      phrase && h("a", { class: "ref", href: `#/edit/${phrase.id}` }, `#${phrase.id}`),
    ),
    phrase && h("p", { class: "phrase" }, phrase.text),
    actions,
  );
}

function healthRows(report, special, now) {
  const rows = [];
  if (report !== null) {
    rows.push([
      "Weather",
      report.weatherAt === null ? "no data yet" : `updated ${when(report.weatherAt, now)}`,
    ]);
    rows.push(["Last refresh", `${report.refresh}, ${when(report.at, now)}`]);
    rows.push([
      "Wi-Fi signal",
      report.wifiSignal === null ? "unknown" : `${report.wifiSignal}/${WIFI_QUALITY_MAX}`,
    ]);
    rows.push([
      "Pi temperature",
      report.piTemperatureC === null ? "unknown" : `${Math.round(report.piTemperatureC)} °C`,
    ]);
    if (report.lastError !== null) rows.push(["Last error", report.lastError]);
  }
  const today = dayFormat.format(now);
  rows.push([
    "Next special day",
    special === null
      ? "none in the coming year"
      : [
          icon(special.icon),
          h(
            "span",
            {},
            // the next one is within a year, so its date needs no year
            `${special.date === today ? "today" : chineseDate(special.date, false)}, ${special.text}`,
          ),
        ],
  ]);
  return rows.flatMap(([term, detail]) => [h("dt", {}, term), h("dd", {}, detail)]);
}

async function nowPage() {
  const content = h("div");
  const trouble = h("p", { class: "error" });
  // one node each, kept across fills, so a failure after a refill still lands on the page
  const messages = h("p", { class: "notice" });
  const refreshMessages = h("p", { class: "notice" });
  const view = h("section", { class: "now" }, trouble, content);
  let wall = { at: null, node: null };
  let lastLoad = 0;
  let loading = null;

  function fill(status, now) {
    const { piLastSeenAt, onWall, next, screenAt, report, nextSpecialDay } = status;
    if (screenAt !== wall.at) {
      wall = {
        at: screenAt,
        node:
          screenAt === null
            ? h("div", { class: "blank" }, "No picture from the Pi yet")
            : h("img", {
                src: `/api/screen.png?t=${encodeURIComponent(screenAt)}`,
                alt: "The screen as the Pi last drew it",
                width: 800,
                height: 480,
              }),
      };
    }
    const online = piLastSeenAt !== null && now.getTime() - Date.parse(piLastSeenAt) < PI_ONLINE_MS;
    const piText =
      piLastSeenAt === null
        ? "The Pi hasn't checked in yet."
        : `Pi ${online ? "online" : "offline"}, checked ${ago(piLastSeenAt, now)}.`;

    content.replaceChildren(
      h(
        "figure",
        { class: "wall" },
        wall.node,
        h(
          "figcaption",
          {},
          h("p", {}, screenAt === null ? "" : `Picture from ${when(screenAt, now)}. `, piText),
          h(
            "p",
            {},
            `Status from ${clock(now.toISOString())}. `,
            h("button", { class: "link", onclick: reload }, "Refresh"),
          ),
        ),
      ),
      slotSection(
        onWall === null
          ? "Nothing on the wall"
          : onWall.since === null
            ? "On the wall"
            : `On the wall since ${when(onWall.since, now)}`,
        onWall,
      ),
      slotSection(
        next === null ? "Nothing to show next" : `Next at ${clock(next.at)}`,
        next,
        next &&
          h(
            "div",
            { class: "actions" },
            h(
              "button",
              {
                onclick: (event) =>
                  perform(event.target, messages, async () => {
                    await api(`/api/phrases/${next.id}/show`, "POST");
                    await loadAgain();
                  }),
              },
              "Show now",
            ),
            h(
              "button",
              {
                onclick: (event) =>
                  perform(event.target, messages, async () => {
                    await api("/api/skip", "POST");
                    await loadAgain();
                  }),
              },
              "Skip",
            ),
            messages,
          ),
      ),
      h("dl", { class: "health" }, healthRows(report, nextSpecialDay, now)),
      h(
        "div",
        { class: "actions" },
        h(
          "button",
          {
            onclick: (event) =>
              perform(event.target, refreshMessages, async () => {
                await api("/api/full-refresh", "POST");
                refreshMessages.textContent = "Requested. The Pi picks it up within 15 s.";
              }),
          },
          "Full refresh",
        ),
        refreshMessages,
      ),
    );
  }

  function load() {
    if (loading === null) {
      lastLoad = Date.now();
      trouble.textContent = "";
      loading = api("/api/status")
        .then((status) => fill(status, new Date()))
        .finally(() => {
          loading = null;
        });
    }
    return loading;
  }
  function reload() {
    load().catch((error) => showFailure(trouble, error));
  }
  async function loadAgain() {
    // a load already in flight was asked before the action, so its answer can't show the action
    if (loading !== null) await loading.catch(() => undefined); // its failure is on the page already
    return load();
  }

  await load();
  // status reads every phrase, so it is fetched on return, not on a timer
  const onReturn = () => {
    if (document.visibilityState === "visible" && Date.now() - lastLoad > REFOCUS_GAP_MS) reload();
  };
  document.addEventListener("visibilitychange", onReturn);
  window.addEventListener("focus", onReturn);
  const leave = () => {
    document.removeEventListener("visibilitychange", onReturn);
    window.removeEventListener("focus", onReturn);
  };
  return { view, leave };
}

const search = { q: "", filter: "all", total: null };

function phraseRow(phrase) {
  return h(
    "li",
    {},
    h(
      "a",
      { href: `#/edit/${phrase.id}` },
      h("span", { class: "id" }, `#${phrase.id}`),
      h("span", { class: "text" }, phrase.text),
      phrase.pins.length > 0 && h("span", { class: "pin" }, phrase.pins.map(pinText).join("; ")),
      phrase.problem !== null &&
        h("span", { class: "flag" }, PROBLEMS[phrase.problem] ?? phrase.problem),
    ),
  );
}

async function phrasesPage() {
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
  async function load() {
    const token = ++loads;
    messages.textContent = "";
    const params = new URLSearchParams();
    if (search.q.trim() !== "") params.set("q", search.q);
    if (search.filter !== "all") params.set("filter", search.filter);
    const query = params.toString();
    const phrases = await api(`/api/phrases${query === "" ? "" : `?${query}`}`);
    if (token !== loads) return;
    if (query === "") {
      search.total = phrases.length;
      input.placeholder = `Search ${search.total} phrases`;
    }
    list.replaceChildren(...phrases.map(phraseRow));
    empty.textContent =
      phrases.length > 0 ? "" : query === "" ? "No phrases yet." : "Nothing matches.";
  }
  const reload = () => load().catch((error) => showFailure(messages, error));

  const chips = h(
    "div",
    { class: "chips", role: "group", "aria-label": "Filter" },
    [
      ["all", "All"],
      ["pinned", "Pinned"],
      ["english", "English"],
    ].map(([filter, label]) =>
      h(
        "button",
        {
          type: "button",
          "aria-pressed": String(search.filter === filter),
          onclick: () => {
            search.filter = filter;
            for (const chip of chips.children) {
              chip.setAttribute("aria-pressed", String(chip.textContent === label));
            }
            reload();
          },
        },
        label,
      ),
    ),
  );
  let pending;
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
    chips,
    messages,
    list,
    empty,
  );
  return { view, leave: () => clearTimeout(pending) };
}

function freshFields(kind) {
  const today = todayInWarsaw();
  const fields = { date: null, month: null, day: null, weekday: null };
  switch (kind) {
    case "date":
      return { ...fields, date: today.iso };
    case "yearly":
      return { ...fields, month: today.month, day: today.day };
    case "lunar_yearly":
      return { ...fields, month: 8, day: 15 };
    case "monthly":
      return { ...fields, day: 1 };
    case "weekly":
      return { ...fields, weekday: 1 };
  }
}

function daysInMonth(month) {
  return new Date(Date.UTC(2000, month, 0)).getUTCDate(); // 2000 is a leap year, so February has 29
}

function dateFields(pin, rerender) {
  const set = (field) => (value) => {
    pin[field] = Number(value);
  };
  switch (pin.kind) {
    case "date":
      return [
        h("input", {
          class: "wide",
          type: "date",
          value: pin.date ?? "",
          "aria-label": "Date",
          onchange: (event) => {
            pin.date = event.target.value;
          },
        }),
      ];
    case "yearly":
      return [
        select(
          numbered(12, "月"),
          pin.month,
          (value) => {
            pin.month = Number(value);
            pin.day = Math.min(pin.day, daysInMonth(pin.month));
            rerender();
          },
          { "aria-label": "Month" },
        ),
        select(numbered(daysInMonth(pin.month), "日"), pin.day, set("day"), {
          "aria-label": "Day",
        }),
      ];
    case "lunar_yearly":
      return [
        select(numbered(12, "", LUNAR_MONTHS), pin.month, set("month"), {
          "aria-label": "Lunar month",
        }),
        select(numbered(30, "", LUNAR_DAYS), pin.day, set("day"), { "aria-label": "Lunar day" }),
      ];
    case "monthly":
      return [
        select(numbered(31, "日"), pin.day, set("day"), { class: "wide", "aria-label": "Day" }),
      ];
    case "weekly":
      return [
        select(numbered(7, "", WEEKDAYS), pin.weekday, set("weekday"), {
          class: "wide",
          "aria-label": "Weekday",
        }),
      ];
  }
}

function pinEditor(pin, slots, onRemove) {
  const row = h("div", { class: "pin-row" });
  const fill = () => {
    row.replaceChildren(
      select(
        PIN_KINDS,
        pin.kind,
        (kind) => {
          Object.assign(pin, { kind }, freshFields(kind));
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
}

async function loadScreen() {
  let response;
  try {
    response = await request("/api/screen.png");
  } catch (error) {
    if (error instanceof ApiError && error.status === 404) return null; // nothing uploaded yet
    throw error;
  }
  return createImageBitmap(await response.blob());
}

async function editPage(id) {
  const [phrase, settings] = await Promise.all([
    id === null
      ? { text: "", pins: [] }
      : api("/api/phrases").then((phrases) => phrases.find((row) => row.id === id)),
    api("/api/settings"),
  ]);
  if (phrase === undefined) throw new ApiError(404, `There is no phrase #${id}`);

  const slots = [
    ["all_day", "All day"],
    ["morning", `Morning, ${settings.firstPhraseTime} to ${settings.secondPhraseTime}`],
    ["evening", `Evening, ${settings.secondPhraseTime} to ${settings.firstPhraseTime}`],
  ];
  const pins = phrase.pins.map((pin) => ({ ...pin }));
  let mode = pins.length > 0 ? "pinned" : "rotation";
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
  const screen = loadScreen().catch((error) => {
    showFailure(messages, error);
    return null;
  });
  let draws = 0;
  let pendingDraw;
  async function preview() {
    const token = ++draws;
    const text = textarea.value;
    await document.fonts.load(`32px ${PHRASE_FONT}`, text);
    const strip = await screen;
    if (token !== draws) return;
    const fit = drawPreview(canvas, text, strip);
    sizeLabel.textContent = text.trim() === "" ? "" : `${fit.sizePx} px`;
  }
  function grow() {
    textarea.style.height = "auto";
    textarea.style.height = `${textarea.scrollHeight + 2}px`;
  }
  textarea.addEventListener("input", () => {
    grow();
    clearTimeout(pendingDraw);
    pendingDraw = setTimeout(preview, PREVIEW_DELAY_MS);
  });

  const pinList = h("div", { class: "pins" });
  const renderPins = () => {
    pinList.hidden = mode !== "pinned";
    pinList.replaceChildren(
      ...pins.map((pin, index) =>
        pinEditor(pin, slots, () => {
          pins.splice(index, 1);
          if (pins.length === 0) {
            mode = "rotation";
            rotation.checked = true;
          }
          renderPins();
        }),
      ),
      h(
        "button",
        {
          type: "button",
          class: "link add",
          onclick: () => {
            pins.push({ kind: "yearly", slot: "all_day", ...freshFields("yearly") });
            renderPins();
          },
        },
        pins.length === 0 ? "Add a date" : "Add another date",
      ),
    );
  };
  const setMode = (next) => {
    mode = next;
    if (mode === "pinned" && pins.length === 0) {
      pins.push({ kind: "yearly", slot: "all_day", ...freshFields("yearly") });
    }
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

  async function save() {
    const body = { text: textarea.value, pins: mode === "pinned" ? pins : [] };
    if (id !== null) {
      await api(`/api/phrases/${id}`, "PUT", body);
      return id;
    }
    const created = await api("/api/phrases", "POST", body);
    return created.id;
  }

  const confirmation = h(
    "div",
    { class: "confirm", hidden: true },
    h("span", {}, "Delete this phrase for good?"),
    h(
      "button",
      {
        type: "button",
        class: "danger",
        onclick: (event) =>
          perform(event.target, messages, async () => {
            await api(`/api/phrases/${id}`, "DELETE");
            location.hash = "#/phrases";
          }),
      },
      "Delete",
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

  requestAnimationFrame(() => {
    grow();
    preview();
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
      h(
        "button",
        {
          class: "primary",
          onclick: (event) =>
            perform(event.target, messages, async () => {
              await save();
              location.hash = "#/phrases";
            }),
        },
        "Save",
      ),
      h(
        "button",
        {
          onclick: (event) =>
            perform(event.target, messages, async () => {
              const shown = await save();
              await api(`/api/phrases/${shown}/show`, "POST");
              location.hash = "#/now";
            }),
        },
        "Show now",
      ),
    ),
    messages,
    id !== null && h("div", { class: "danger-zone" }, deleteLink, confirmation),
  );
  return { view, leave: () => clearTimeout(pendingDraw) };
}

let specialDayCount = 0;

function specialDayEditor(day, onRemove) {
  const name = `icon-${++specialDayCount}`;
  const item = h("li", { class: "special-day" });
  const greeting = h("input", {
    class: "greeting",
    value: day.greeting,
    placeholder: "Greeting",
    "aria-label": "Greeting",
    oninput: (event) => {
      day.greeting = event.target.value;
      counting.hidden = !day.greeting.includes("{n}");
    },
  });
  const counting = h(
    "label",
    { class: "field", hidden: !day.greeting.includes("{n}") },
    "Counts the years from",
    h("input", {
      type: "number",
      min: 1,
      max: 9999,
      value: day.baseYear ?? "",
      placeholder: "Year",
      onchange: (event) => {
        day.baseYear = event.target.value === "" ? null : Number(event.target.value);
      },
    }),
  );
  const dateRow = h("div", { class: "row" });
  const fillDate = () => {
    const lunar = day.kind === "lunar_yearly";
    dateRow.replaceChildren(
      select(
        SPECIAL_KINDS,
        day.kind,
        (kind) => {
          day.kind = kind;
          day.day = Math.min(day.day, kind === "lunar_yearly" ? 30 : daysInMonth(day.month));
          fillDate();
        },
        { "aria-label": "Calendar" },
      ),
      select(
        numbered(12, "月", lunar ? LUNAR_MONTHS : null),
        day.month,
        (value) => {
          day.month = Number(value);
          if (!lunar) day.day = Math.min(day.day, daysInMonth(day.month));
          fillDate();
        },
        { "aria-label": "Month" },
      ),
      select(
        numbered(lunar ? 30 : daysInMonth(day.month), "日", lunar ? LUNAR_DAYS : null),
        day.day,
        (value) => {
          day.day = Number(value);
        },
        { "aria-label": "Day" },
      ),
    );
  };
  fillDate();
  item.append(
    greeting,
    dateRow,
    h(
      "div",
      { class: "icons", role: "radiogroup", "aria-label": "Icon" },
      Object.keys(ICONS).map((choice) =>
        h(
          "label",
          { title: choice },
          h("input", {
            type: "radio",
            name,
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
    h(
      "div",
      { class: "row pair" },
      counting,
      h(
        "label",
        { class: "field" },
        "Rank",
        h("input", {
          type: "number",
          min: 0,
          max: 1000,
          value: day.rank,
          onchange: (event) => {
            day.rank = Number(event.target.value);
          },
        }),
      ),
    ),
    h("button", { type: "button", class: "link remove", onclick: onRemove }, "Remove"),
  );
  return item;
}

async function specialDaysPage() {
  let days = (await api("/api/special-days")).map((day) => ({ ...day }));
  const list = h("ol", { class: "special-days" });
  const renderList = () => {
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
          list.lastElementChild.querySelector("input").focus();
        },
      },
      "Add a day",
    ),
    h(
      "div",
      { class: "actions" },
      h(
        "button",
        {
          class: "primary",
          onclick: (event) =>
            perform(event.target, messages, async () => {
              saved.textContent = "";
              days = (await api("/api/special-days", "PUT", days)).map((day) => ({ ...day }));
              renderList();
              saved.textContent = "Saved.";
            }),
        },
        "Save",
      ),
    ),
    messages,
    saved,
  );
  return { view };
}

async function settingsPage() {
  const settings = await api("/api/settings");
  const first = h("input", { type: "time", value: settings.firstPhraseTime, required: true });
  const second = h("input", { type: "time", value: settings.secondPhraseTime, required: true });
  const messages = h("p", { class: "error" });
  const saved = h("p", { class: "notice" });
  const button = h("button", { class: "primary" }, "Save");
  const view = h(
    "section",
    {},
    h("h1", {}, "Settings"),
    h("p", { class: "lead" }, "The phrase changes at these two times, Warsaw time."),
    h(
      "form",
      {
        class: "settings",
        onsubmit: (event) => {
          event.preventDefault();
          perform(button, messages, async () => {
            saved.textContent = "";
            const stored = await api("/api/settings", "PUT", {
              firstPhraseTime: first.value,
              secondPhraseTime: second.value,
            });
            first.value = stored.firstPhraseTime;
            second.value = stored.secondPhraseTime;
            saved.textContent = "Saved.";
          });
        },
      },
      h("label", { class: "field" }, "First phrase time", first),
      h("label", { class: "field" }, "Second phrase time", second),
      h("div", { class: "actions" }, button),
      messages,
      saved,
    ),
  );
  return { view };
}

const pages = [
  [/^#\/now$/, nowPage],
  [/^#\/phrases$/, phrasesPage],
  [/^#\/new$/, () => editPage(null)],
  [/^#\/edit\/(\d+)$/, (id) => editPage(Number(id))],
  [/^#\/special-days$/, specialDaysPage],
  [/^#\/settings$/, settingsPage],
];
let leavePage = () => {};
let renders = 0;

function markNav(hash) {
  const section = hash.startsWith("#/edit/") || hash === "#/new" ? "#/phrases" : hash;
  for (const link of navLinks) {
    if (link.getAttribute("href") === section) link.setAttribute("aria-current", "page");
    else link.removeAttribute("aria-current");
  }
}

async function render() {
  leavePage();
  leavePage = () => {};
  const hash = location.hash === "" ? "#/now" : location.hash;
  const entry = pages.find(([pattern]) => pattern.test(hash));
  if (entry === undefined) {
    location.replace("#/now");
    return;
  }
  const [pattern, page] = entry;
  markNav(hash);
  const token = ++renders;
  let opened;
  try {
    opened = await page(...hash.match(pattern).slice(1));
  } catch (error) {
    if (token !== renders || (error instanceof ApiError && error.status === 401)) return;
    header.hidden = false;
    main.replaceChildren(
      h("section", {}, h("h1", {}, "This page couldn't load"), h("p", {}, error.message)),
    );
    throw error;
  }
  const { view, leave = () => {} } = opened;
  if (token !== renders) {
    leave();
    return;
  }
  leavePage = leave;
  header.hidden = false;
  main.replaceChildren(view);
  window.scrollTo({ top: 0, behavior: "instant" });
}

window.addEventListener("hashchange", render);
render();
