import {
  createPhrase,
  deletePhrase,
  fullRefresh,
  getSettings,
  getSpecialDays,
  listPhrases,
  saveSettings,
  saveSpecialDays,
  screenPng,
  showPhrase,
  skip,
  status,
  updatePhrase,
} from "./api_dash";
import { piNow, piScreen } from "./api_pi";
import { checkPassword, hasSession, isPi, requestTime, sessionCookie } from "./auth";
import type { Env } from "./env";
import { InputError } from "./validate";

export type RouteContext = {
  request: Request;
  env: Env;
  now: Date;
  params: Record<string, string>;
};

type Route = {
  method: "GET" | "POST" | "PUT" | "DELETE";
  path: string;
  handler: (context: RouteContext) => Promise<Response>;
};

const routes: Route[] = [
  { method: "GET", path: "/api/pi/now", handler: piNow },
  { method: "PUT", path: "/api/pi/screen", handler: piScreen },
  { method: "GET", path: "/api/status", handler: status },
  { method: "GET", path: "/api/screen.png", handler: screenPng },
  { method: "GET", path: "/api/phrases", handler: listPhrases },
  { method: "POST", path: "/api/phrases", handler: createPhrase },
  { method: "PUT", path: "/api/phrases/:id(\\d+)", handler: updatePhrase },
  { method: "DELETE", path: "/api/phrases/:id(\\d+)", handler: deletePhrase },
  { method: "POST", path: "/api/phrases/:id(\\d+)/show", handler: showPhrase },
  { method: "POST", path: "/api/skip", handler: skip },
  { method: "POST", path: "/api/full-refresh", handler: fullRefresh },
  { method: "GET", path: "/api/special-days", handler: getSpecialDays },
  { method: "PUT", path: "/api/special-days", handler: saveSpecialDays },
  { method: "GET", path: "/api/settings", handler: getSettings },
  { method: "PUT", path: "/api/settings", handler: saveSettings },
];

const matchers = routes.map((route) => ({
  ...route,
  pattern: new URLPattern({ pathname: route.path }),
}));

async function login(request: Request, env: Env, now: Date): Promise<Response> {
  const body: unknown = await request.json().catch(() => null);
  const password =
    typeof body === "object" && body !== null && "password" in body ? body.password : undefined;
  if (typeof password !== "string") return new Response(null, { status: 400 });
  if (!(await checkPassword(password, env))) return new Response(null, { status: 401 });
  const headers = { "Set-Cookie": await sessionCookie(env, now) };
  return new Response(null, { status: 204, headers });
}

async function respond(route: Route, context: RouteContext): Promise<Response> {
  try {
    return await route.handler(context);
  } catch (error) {
    if (error instanceof InputError) {
      return Response.json({ error: error.message }, { status: 400 });
    }
    throw error;
  }
}

export default {
  async fetch(request, env): Promise<Response> {
    const url = new URL(request.url);
    if (!url.pathname.startsWith("/api/")) return new Response(null, { status: 404 });

    const now = requestTime(request, env);
    if (request.method === "POST" && url.pathname === "/api/login") {
      return login(request, env, now);
    }

    const allowed = url.pathname.startsWith("/api/pi/")
      ? isPi(request, env)
      : await hasSession(request, env, now);
    if (!allowed) return new Response(null, { status: 401 });

    for (const route of matchers) {
      const match = route.method === request.method && route.pattern.exec(request.url);
      if (match) return respond(route, { request, env, now, params: match.pathname.groups });
    }
    return new Response(null, { status: 404 });
  },
} satisfies ExportedHandler<Env>;
