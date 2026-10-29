import type { Icon } from "../src/api_types";
import { h } from "./dom";

const SVG = "http://www.w3.org/2000/svg";

const PATHS: Record<Icon, string> = {
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

export const ICON_NAMES = Object.keys(PATHS) as readonly Icon[];

export const icon = (name: Icon): HTMLSpanElement => {
  const svg = document.createElementNS(SVG, "svg");
  svg.setAttribute("viewBox", "0 0 24 24");
  const path = document.createElementNS(SVG, "path");
  path.setAttribute("d", PATHS[name]);
  svg.append(path);
  return h("span", { class: "icon", "aria-hidden": "true" }, svg);
};
