# kotetsu

An e-paper screen for the wall, driven by a Raspberry Pi 5. It shows a phrase, the date, the weather and a day counter, and a web dashboard manages the phrases.

## Parts

- `pi/`: the C program for the Raspberry Pi 5 and its e-paper screen. Build it with `make -C pi`.
- `dashboard/`: the Cloudflare Worker that keeps the phrases and serves the dashboard. Build it with `cd dashboard && npm run check`.

## Setting up the Pi

Flash Raspberry Pi OS Lite (64-bit, trixie) with Raspberry Pi Imager, presetting the hostname, user, SSH key, home Wi-Fi and the Europe/Warsaw time zone, and attach the screen. Then, on the Pi:

1. Clone this repository and save the dashboard's `PI_KEY` secret in a file.
2. Run `sudo pi/tools/install.sh --pi-key-file <file>`. With `--dry-run` it lists its eight steps and changes nothing.
3. Delete the key file (the script keeps its own copy) and reboot. The `config.txt` changes (the 4K-page kernel and SPI) apply only after a reboot, and the reboot also starts `kotetsu.service`.

The script installs the build packages and Comitup, creates the `kotetsu` user, downloads the two fonts, builds and installs the program under `/usr/local`, sets up the service and the Wi-Fi hooks, and copies the key to `/etc/kotetsu/pi-key`, readable only by root. To update after a `git pull`, run it again with `--pi-key-file /etc/kotetsu/pi-key`.

When the Pi can't join a Wi-Fi network, Comitup opens an open network called `kotetsu-setup` and the screen shows its QR code. Scan it with a phone, choose the home Wi-Fi and enter its password.
