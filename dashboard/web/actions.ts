import { ApiError } from "./api";
import { h, type PlainAttributes } from "./dom";

export const showFailure = (messages: HTMLElement, error: unknown): void => {
  if (!(error instanceof ApiError) || error.status === 401) throw error;
  messages.textContent = error.message;
  messages.scrollIntoView({ block: "nearest" });
};

export const perform = async (
  button: HTMLButtonElement,
  messages: HTMLElement,
  action: () => Promise<void>,
): Promise<void> => {
  button.disabled = true;
  messages.textContent = "";
  try {
    await action();
  } catch (error) {
    showFailure(messages, error);
  } finally {
    button.disabled = false;
  }
};

export const actionButton = (
  label: string,
  messages: HTMLElement,
  action: () => Promise<void>,
  attributes: PlainAttributes = {},
): HTMLButtonElement => {
  const button = h("button", attributes, label);
  button.addEventListener("click", () => void perform(button, messages, action));
  return button;
};
