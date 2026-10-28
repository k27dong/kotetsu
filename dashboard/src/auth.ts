import type { Env } from "./env";
import { isIsoTimeWithOffset } from "./time";

const SESSION_COOKIE = "kotetsu_session";
const SESSION_SECONDS = 365 * 24 * 60 * 60;
const SESSION_VALUE = new RegExp(String.raw`^${SESSION_COOKIE}=(\d+)\.([\w-]{43})$`);

const encoder = new TextEncoder();

const secret = (env: Env, name: "DASHBOARD_PASSWORD" | "COOKIE_SIGNING_KEY" | "PI_KEY"): string => {
  const value = env[name];
  if (!value) throw new Error(`${name} is not set`);
  return value;
};

const matchesSecret = (given: string, expected: string): boolean => {
  const [x, y] = [encoder.encode(given), encoder.encode(expected)];
  if (x.byteLength !== y.byteLength) return false; // lengths aren't secret
  return crypto.subtle.timingSafeEqual(x, y);
};

const sign = async (payload: string, env: Env): Promise<string> => {
  const key = await crypto.subtle.importKey(
    "raw",
    encoder.encode(secret(env, "COOKIE_SIGNING_KEY")),
    { name: "HMAC", hash: "SHA-256" },
    false,
    ["sign"],
  );
  const mac = new Uint8Array(await crypto.subtle.sign("HMAC", key, encoder.encode(payload)));
  return btoa(String.fromCharCode(...mac))
    .replaceAll("+", "-")
    .replaceAll("/", "_")
    .replace(/=+$/, "");
};

export const checkPassword = async (given: string, env: Env): Promise<boolean> => {
  const [givenHash, passwordHash] = await Promise.all([
    crypto.subtle.digest("SHA-256", encoder.encode(given)),
    crypto.subtle.digest("SHA-256", encoder.encode(secret(env, "DASHBOARD_PASSWORD"))),
  ]);
  return crypto.subtle.timingSafeEqual(givenHash, passwordHash); // hashes hide the length
};

export const sessionCookie = async (env: Env, now: Date): Promise<string> => {
  const expiry = String(Math.floor(now.getTime() / 1000) + SESSION_SECONDS);
  const attributes = `HttpOnly; Secure; SameSite=Lax; Path=/; Max-Age=${SESSION_SECONDS}`;
  return `${SESSION_COOKIE}=${expiry}.${await sign(expiry, env)}; ${attributes}`;
};

export const hasSession = async (request: Request, env: Env, now: Date): Promise<boolean> => {
  for (const cookie of request.headers.get("Cookie")?.split(";") ?? []) {
    const [, expiry, signature] = SESSION_VALUE.exec(cookie.trim()) ?? [];
    if (expiry === undefined || signature === undefined) continue;
    if (Number(expiry) * 1000 <= now.getTime()) return false;
    return matchesSecret(signature, await sign(expiry, env));
  }
  return false;
};

export const isPi = (request: Request, env: Env): boolean => {
  const expected = `Bearer ${secret(env, "PI_KEY")}`;
  return matchesSecret(request.headers.get("Authorization") ?? "", expected);
};

export const requestTime = (request: Request, env: Env): Date => {
  const override = request.headers.get("X-Kotetsu-Now");
  if (env.ALLOW_TIME_OVERRIDE !== "1" || override === null) return new Date();
  if (!isIsoTimeWithOffset(override)) {
    throw new Error(`X-Kotetsu-Now needs an ISO 8601 date, time and offset, not "${override}"`);
  }
  return new Date(override);
};
