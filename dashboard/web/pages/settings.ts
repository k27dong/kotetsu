import { perform } from "../actions";
import { api } from "../api";
import { h } from "../dom";
import type { Page } from "../page";

export const settingsPage = async (): Promise<Page> => {
  const settings = await api.settings();
  const first = h("input", { type: "time", value: settings.firstPhraseTime, required: true });
  const second = h("input", { type: "time", value: settings.secondPhraseTime, required: true });
  const messages = h("p", { class: "error" });
  const saved = h("p", { class: "notice" });
  const button = h("button", { class: "primary" }, "Save");
  const submit = (event: Event): void => {
    event.preventDefault();
    void perform(button, messages, async () => {
      saved.textContent = "";
      const stored = await api.saveSettings({
        firstPhraseTime: first.value,
        secondPhraseTime: second.value,
      });
      first.value = stored.firstPhraseTime;
      second.value = stored.secondPhraseTime;
      saved.textContent = "Saved.";
    });
  };
  const view = h(
    "section",
    {},
    h("h1", {}, "Settings"),
    h("p", { class: "lead" }, "The phrase changes at these two times, Warsaw time."),
    h(
      "form",
      { class: "settings", onsubmit: submit },
      h("label", { class: "field" }, "First phrase time", first),
      h("label", { class: "field" }, "Second phrase time", second),
      h("div", { class: "actions" }, button),
      messages,
      saved,
    ),
  );
  return { view };
};
