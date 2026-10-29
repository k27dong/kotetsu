import { match, P } from "ts-pattern";
import type { ScreenReport, StatusResponse } from "../../src/api_types";
import { actionButton, showFailure } from "../actions";
import { api } from "../api";
import { h } from "../dom";
import { ago, chineseDate, clock, warsawDay, when } from "../format";
import { icon } from "../icons";
import type { Page } from "../page";

const PI_ONLINE_MS = 60 * 1000;
const REFOCUS_GAP_MS = 5 * 1000;
const WIFI_QUALITY_MAX = 70;
const SCREEN_UPDATE_NOTICE = "Requested. The screen updates within 3 minutes.";

type Shown = { id: number; text: string };
type Wall = { at: string | null; node: HTMLElement };

const slotSection = (heading: string, phrase: Shown | null, actions?: HTMLElement): HTMLElement =>
  h(
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

const wallPicture = (screenAt: string | null): HTMLElement =>
  screenAt === null
    ? h("div", { class: "blank" }, "No picture from the Pi yet")
    : h("img", {
        src: `/api/screen.png?t=${encodeURIComponent(screenAt)}`,
        alt: "The screen as the Pi last drew it",
        width: 800,
        height: 480,
      });

const healthRows = (
  report: ScreenReport | null,
  special: StatusResponse["nextSpecialDay"],
  now: Date,
): HTMLElement[] => {
  const rows: [string, string | HTMLElement[]][] = [];
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
  const today = warsawDay(now);
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
};

export const nowPage = async (): Promise<Page> => {
  const content = h("div");
  const trouble = h("p", { class: "error" });
  // one node each, kept across fills, so a failure after a refill still lands on the page
  const messages = h("p", { class: "notice" });
  const refreshMessages = h("p", { class: "notice" });
  const view = h("section", { class: "now" }, trouble, content);
  let wall: Wall | null = null;
  let lastLoad = 0;
  let loading: Promise<void> | null = null;

  const fill = (status: StatusResponse, now: Date): void => {
    const { piLastSeenAt, onWall, next, screenAt, report, nextSpecialDay } = status;
    if (wall === null || wall.at !== screenAt) wall = { at: screenAt, node: wallPicture(screenAt) };
    const online = piLastSeenAt !== null && now.getTime() - Date.parse(piLastSeenAt) < PI_ONLINE_MS;
    const piText =
      piLastSeenAt === null
        ? "The Pi hasn't checked in yet."
        : `Pi ${online ? "online" : "offline"}, checked ${ago(piLastSeenAt, now)}.`;
    const wallHeading = match(onWall)
      .with(null, () => "Nothing on the wall")
      .with({ since: null }, () => "On the wall")
      .with({ since: P.string }, ({ since }) => `On the wall since ${when(since, now)}`)
      .exhaustive();
    const requestThenReload = (request: () => Promise<void>) => async () => {
      await request();
      await loadAgain();
      messages.textContent = SCREEN_UPDATE_NOTICE;
    };

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
      slotSection(wallHeading, onWall),
      next === null
        ? slotSection("Nothing to show next", null)
        : slotSection(
            `Next at ${clock(next.at)}`,
            next,
            h(
              "div",
              { class: "actions" },
              actionButton(
                "Show now",
                messages,
                requestThenReload(() => api.showPhrase(next.id)),
              ),
              actionButton("Skip", messages, requestThenReload(api.skip)),
              messages,
            ),
          ),
      h("dl", { class: "health" }, healthRows(report, nextSpecialDay, now)),
      h(
        "div",
        { class: "actions" },
        actionButton("Full refresh", refreshMessages, async () => {
          await api.fullRefresh();
          refreshMessages.textContent = SCREEN_UPDATE_NOTICE;
        }),
        refreshMessages,
      ),
    );
  };

  const load = (): Promise<void> => {
    if (loading === null) {
      lastLoad = Date.now();
      trouble.textContent = "";
      messages.textContent = "";
      refreshMessages.textContent = "";
      loading = api
        .status()
        .then((status) => fill(status, new Date()))
        .finally(() => {
          loading = null;
        });
    }
    return loading;
  };
  const reload = (): void => {
    load().catch((error: unknown) => showFailure(trouble, error));
  };
  const loadAgain = async (): Promise<void> => {
    // a load already in flight was asked before the action, so its answer can't show the action
    if (loading !== null) await loading.catch(() => undefined); // its failure is on the page already
    return load();
  };

  await load();
  // status reads every phrase, so it is fetched on return, not on a timer
  const onReturn = (): void => {
    if (document.visibilityState === "visible" && Date.now() - lastLoad > REFOCUS_GAP_MS) reload();
  };
  document.addEventListener("visibilitychange", onReturn);
  window.addEventListener("focus", onReturn);
  const leave = (): void => {
    document.removeEventListener("visibilitychange", onReturn);
    window.removeEventListener("focus", onReturn);
  };
  return { view, leave };
};
