import { checkPassword, hasSession, isPi, requestTime, sessionCookie } from "./auth";
import type { Env } from "./env";

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

const routes: Route[] = [];

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
      if (match) return route.handler({ request, env, now, params: match.pathname.groups });
    }
    return new Response(null, { status: 404 });
  },
} satisfies ExportedHandler<Env>;
