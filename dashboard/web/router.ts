import { match, P } from "ts-pattern";
import { ApiError } from "./api";
import { h } from "./dom";
import type { Page } from "./page";
import { editPage } from "./pages/edit";
import { nowPage } from "./pages/now";
import { phrasesPage } from "./pages/phrases";
import { settingsPage } from "./pages/settings";
import { signInPage } from "./pages/sign-in";
import { specialDaysPage } from "./pages/special-days";

type Route =
  | { page: "now" }
  | { page: "phrases" }
  | { page: "new" }
  | { page: "edit"; id: number }
  | { page: "special-days" }
  | { page: "settings" };

const HOME = "#/now";

const required = <T extends Element>(found: T | null, selector: string): T => {
  if (found === null) throw new Error(`index.html has no ${selector}`);
  return found;
};

const main = required(document.querySelector("main"), "main");
const header = required(document.querySelector<HTMLElement>("header.top"), "header.top");
const navLinks = Array.from(header.querySelectorAll("nav a"));

const parseRoute = (hash: string): Route | null =>
  match(hash.split("/"))
    .with(["#", "now"], (): Route => ({ page: "now" }))
    .with(["#", "phrases"], (): Route => ({ page: "phrases" }))
    .with(["#", "new"], (): Route => ({ page: "new" }))
    .with(["#", "edit", P.string.regex(/^\d+$/)], ([, , id]): Route => ({
      page: "edit",
      id: Number(id),
    }))
    .with(["#", "special-days"], (): Route => ({ page: "special-days" }))
    .with(["#", "settings"], (): Route => ({ page: "settings" }))
    .otherwise(() => null);

const openPage = (route: Route): Promise<Page> =>
  match(route)
    .with({ page: "now" }, nowPage)
    .with({ page: "phrases" }, phrasesPage)
    .with({ page: "new" }, () => editPage(null))
    .with({ page: "edit" }, ({ id }) => editPage(id))
    .with({ page: "special-days" }, specialDaysPage)
    .with({ page: "settings" }, settingsPage)
    .exhaustive();

const navSection = (route: Route): string =>
  match(route.page)
    .with("now", "phrases", "special-days", "settings", (page) => `#/${page}`)
    .with("new", "edit", () => "#/phrases")
    .exhaustive();

const markNav = (section: string): void => {
  for (const link of navLinks) {
    if (link.getAttribute("href") === section) link.setAttribute("aria-current", "page");
    else link.removeAttribute("aria-current");
  }
};

let leavePage: () => void = () => {};
let renders = 0;

const leaveCurrentPage = (): void => {
  leavePage();
  leavePage = () => {};
};

export const showSignIn = (): void => {
  leaveCurrentPage();
  header.hidden = true;
  main.replaceChildren(signInPage(() => void render()));
};

const render = async (): Promise<void> => {
  leaveCurrentPage();
  const route = parseRoute(location.hash === "" ? HOME : location.hash);
  if (route === null) {
    location.replace(HOME);
    return;
  }
  markNav(navSection(route));
  const token = ++renders;
  let opened: Page;
  try {
    opened = await openPage(route);
  } catch (error) {
    if (token !== renders || (error instanceof ApiError && error.status === 401)) return;
    header.hidden = false;
    const message = error instanceof Error ? error.message : String(error);
    main.replaceChildren(
      h("section", {}, h("h1", {}, "This page couldn't load"), h("p", {}, message)),
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
};

export const startRouter = (): void => {
  window.addEventListener("hashchange", () => void render());
  void render();
};
