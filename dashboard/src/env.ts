export interface Env {
  DB: D1Database;
  ASSETS: Fetcher;
  DASHBOARD_PASSWORD: string;
  COOKIE_SIGNING_KEY: string;
  PI_KEY: string;
  ALLOW_TIME_OVERRIDE?: string;
}
