# kotetsu

An e-paper screen for the wall, driven by a Raspberry Pi 5. It shows a phrase, the date, the weather and a day counter, and a web dashboard manages the phrases.

## Parts

- `pi/`: the C program for the Raspberry Pi 5 and its e-paper screen. Build it with `make -C pi`.
- `dashboard/`: the Cloudflare Worker that keeps the phrases and serves the dashboard. Build it with `cd dashboard && npm run check`.
