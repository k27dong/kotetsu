import { match } from "ts-pattern";
import { api } from "../api";
import { h } from "../dom";

export const signInPage = (onSignedIn: () => void): HTMLFormElement => {
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
  const submit = async (event: Event): Promise<void> => {
    event.preventDefault();
    match(await api.login(password.value))
      .with({ outcome: "signed-in" }, onSignedIn)
      .with({ outcome: "wrong-password" }, () => {
        error.textContent = "Wrong password";
        password.select();
      })
      .with({ outcome: "unreachable" }, () => {
        error.textContent = "Couldn't reach the server";
      })
      .with({ outcome: "failed" }, ({ status }) => {
        error.textContent = `Couldn't sign in (${status})`;
      })
      .exhaustive();
  };
  const form = h(
    "form",
    { class: "sign-in", onsubmit: submit },
    h("h1", {}, "kotetsu"),
    password,
    h("button", { class: "primary" }, "Sign in"),
    error,
  );
  requestAnimationFrame(() => password.focus());
  return form;
};
