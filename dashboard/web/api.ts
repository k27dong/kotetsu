import { isMatching, P } from "ts-pattern";
import type {
  CreatedPhrase,
  LoginRequest,
  PhraseFields,
  PhraseSummary,
  Settings,
  SpecialDayFields,
  StatusResponse,
} from "../src/api_types";

export class ApiError extends Error {
  constructor(
    readonly status: number,
    message: string,
  ) {
    super(message);
  }
}

export type LoginResult =
  | { outcome: "signed-in" | "wrong-password" | "unreachable" }
  | { outcome: "failed"; status: number };

let signedOut: () => void = () => {
  throw new Error("A 401 arrived before onSignedOut wired the sign-in page");
};

export const onSignedOut = (handler: () => void): void => {
  signedOut = handler;
};

const failureMessage = async (response: Response, path: string): Promise<string> => {
  const statusLine = `${response.status} from ${path}`;
  try {
    const body: unknown = await response.json();
    return isMatching({ error: P.string }, body) ? body.error : statusLine;
  } catch {
    return statusLine; // the body wasn't the API's { error } JSON
  }
};

const request = async (path: string, method: string, body?: unknown): Promise<Response> => {
  const init: RequestInit =
    body === undefined
      ? { method }
      : { method, headers: { "Content-Type": "application/json" }, body: JSON.stringify(body) };
  let response: Response;
  try {
    response = await fetch(path, init);
  } catch {
    throw new ApiError(0, "Couldn't reach the server");
  }
  if (response.status === 401) {
    signedOut();
    throw new ApiError(401, "Signed out");
  }
  if (!response.ok) throw new ApiError(response.status, await failureMessage(response, path));
  return response;
};

const json = async <T>(path: string, method = "GET", body?: unknown): Promise<T> =>
  (await request(path, method, body)).json() as Promise<T>;

const send = async (path: string, method: string, body?: unknown): Promise<void> => {
  await request(path, method, body);
};

export const api = {
  login: async (password: string): Promise<LoginResult> => {
    let response: Response;
    try {
      response = await fetch("/api/login", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ password } satisfies LoginRequest),
      });
    } catch {
      return { outcome: "unreachable" };
    }
    if (response.status === 401) return { outcome: "wrong-password" };
    return response.ok ? { outcome: "signed-in" } : { outcome: "failed", status: response.status };
  },
  status: (): Promise<StatusResponse> => json("/api/status"),
  screenPng: async (): Promise<Blob | null> => {
    try {
      return await (await request("/api/screen.png", "GET")).blob();
    } catch (error) {
      if (error instanceof ApiError && error.status === 404) return null; // nothing uploaded yet
      throw error;
    }
  },
  phrases: (): Promise<PhraseSummary[]> => json("/api/phrases"),
  createPhrase: (fields: PhraseFields): Promise<CreatedPhrase> =>
    json("/api/phrases", "POST", fields),
  updatePhrase: (id: number, fields: PhraseFields): Promise<void> =>
    send(`/api/phrases/${id}`, "PUT", fields),
  deletePhrase: (id: number): Promise<void> => send(`/api/phrases/${id}`, "DELETE"),
  showPhrase: (id: number): Promise<void> => send(`/api/phrases/${id}/show`, "POST"),
  skip: (): Promise<void> => send("/api/skip", "POST"),
  fullRefresh: (): Promise<void> => send("/api/full-refresh", "POST"),
  specialDays: (): Promise<SpecialDayFields[]> => json("/api/special-days"),
  saveSpecialDays: (days: SpecialDayFields[]): Promise<SpecialDayFields[]> =>
    json("/api/special-days", "PUT", days),
  settings: (): Promise<Settings> => json("/api/settings"),
  saveSettings: (settings: Settings): Promise<Settings> => json("/api/settings", "PUT", settings),
};
