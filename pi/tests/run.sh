#!/usr/bin/env bash
# usage: tests/run.sh <test program>...
set -euo pipefail

tests=$(cd "$(dirname "$0")" && pwd)
font_dir=${KOTETSU_FONT_DIR:?set KOTETSU_FONT_DIR to the folder with the fonts}
for font in LXGWWenKaiGBScreen.ttf Inter-SemiBold.ttf; do
    if [ ! -f "$font_dir/$font" ]; then
        echo "run.sh: no $font in $font_dir; put the fonts there or set KOTETSU_FONT_DIR" >&2
        exit 1
    fi
done
export TZ=Europe/Warsaw
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 # by default UBSan reports and carries on, so the test would pass

passed=0
failed=0
skipped=0
for program in "$@"; do
    program=$(cd "$(dirname "$program")" && pwd)/$(basename "$program")
    name=$(basename "$program")
    log=$program.log
    status=0
    (cd "$tests" && "$program") >"$log" 2>&1 || status=$? # from tests/, where the fixtures are
    summary=$(grep -E '^[0-9]+ Tests [0-9]+ Failures [0-9]+ Ignored' "$log" | tail -n 1 | sed 's/ *$//' || true)
    failures=$(sed -n 's/^[0-9]* Tests \([0-9]*\) Failures.*/\1/p' <<<"$summary") # Unity's exit code is its failure count, which can wrap to 0 or land on 77
    if [ "${failures:-0}" -eq 0 ] && [ "$status" -eq 0 ]; then
        echo "PASS  $name  ${summary:-exit 0}"
        passed=$((passed + 1))
    elif [ "${failures:-0}" -eq 0 ] && [ "$status" -eq 77 ]; then
        echo "SKIP  $name  $(tail -n 1 "$log")"
        skipped=$((skipped + 1))
    else
        echo "FAIL  $name  exit $status"
        sed 's/^/      /' "$log"
        failed=$((failed + 1))
    fi
done

echo "$passed passed, $failed failed, $skipped skipped"
[ "$failed" -eq 0 ]
