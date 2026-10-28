import argparse
import os
import sys
import unicodedata
from collections import Counter
from collections.abc import Mapping, Sequence
from datetime import UTC, datetime
from itertools import pairwise
from pathlib import Path

import requests
from google.auth.transport.requests import AuthorizedSession
from google.oauth2 import service_account

PRIVATE_DIR = Path(__file__).resolve().parent.parent / ".private"

SHEETS_READONLY_SCOPE = "https://www.googleapis.com/auth/spreadsheets.readonly"
SHEET_VALUES_URL = (
    "https://sheets.googleapis.com/v4/spreadsheets/{spreadsheet_id}/values/Sheet1"
)
SPREADSHEET_ID_ENV = "SPREADSHEET_ID"
KEY_FILE_FIELD_TO_ENV = {
    "type": "KEY_TYPE",
    "project_id": "PROJECT_ID",
    "private_key_id": "PRIVATE_KEY_ID",
    "private_key": "PRIVATE_KEY",
    "client_email": "CLIENT_EMAIL",
    "client_id": "CLIENT_ID",
    "auth_uri": "AUTH_URI",
    "token_uri": "TOKEN_URI",
    "auth_provider_x509_cert_url": "AUTH_PROVIDER_X509_CERT_URL",
    "client_x509_cert_url": "CLIENT_X509_CERT_URL",
}

OLD_SCREEN_FULL_LINE_PX = 238  # the old screen fit 15 wide characters, 240 px
ASCII_PX = 11
WIDE_PX = 16
KEEP_BREAK_AFTER = "。！？…，、；：”）》.,;:!?)"
SEAM_CONTEXT_CHARS = 8


def is_blank(row: dict[str, str]) -> bool:
    return not any(cell.strip() for cell in row.values())


def fetch_rows(env: Mapping[str, str]) -> list[dict[str, str]]:
    """Sheet1's data rows keyed by the header and padded; rows[i] is Sheet row i + 2."""
    env_names = [*KEY_FILE_FIELD_TO_ENV.values(), SPREADSHEET_ID_ENV]
    missing = [name for name in env_names if not env.get(name)]
    if missing:
        raise KeyError(f"missing or empty environment variables: {', '.join(missing)}")
    key_file = {field: env[name] for field, name in KEY_FILE_FIELD_TO_ENV.items()}
    # the key may arrive with its newlines escaped
    key_file["private_key"] = key_file["private_key"].replace("\\n", "\n")
    credentials = service_account.Credentials.from_service_account_info(
        key_file, scopes=[SHEETS_READONLY_SCOPE]
    )
    url = SHEET_VALUES_URL.format(spreadsheet_id=env[SPREADSHEET_ID_ENV])
    with AuthorizedSession(credentials) as session:
        response = session.get(url)
    if not response.ok:
        # raise_for_status() would put the URL, which holds the Sheet ID, in the message
        raise requests.HTTPError(
            f"Sheets API answered {response.status_code}: {response.text}",
            response=response,
        )

    values = response.json().get("values")
    if not values:
        raise ValueError("Sheet1 is empty")
    header, *body = values
    absent = {"id", "content"} - set(header)
    if absent:
        raise ValueError(
            "Sheet1's first row must name the id and content columns,"
            f" lacks {sorted(absent)}"
        )
    repeated = [name for name, count in Counter(header).items() if count > 1]
    if repeated:
        raise ValueError(f"Sheet1's header repeats a column name: {repeated}")
    rows = [
        {name: row[i] if i < len(row) else "" for i, name in enumerate(header)}
        for row in body
    ]
    if all(is_blank(row) for row in rows):
        raise ValueError("Sheet1 has a header row but no phrases")
    return rows


def is_hard_wrap(before: str, after: str) -> bool:
    width = sum(ASCII_PX if char.isascii() else WIDE_PX for char in before)
    return (
        bool(after)  # a wrap never leaves an empty continuation line
        and width > OLD_SCREEN_FULL_LINE_PX
        and before[-1] not in KEEP_BREAK_AFTER
        and not before[-1].isspace()
    )


def join_hard_wraps(text: str) -> tuple[str, list[str]]:
    """The list has one entry per join, showing the text on either side of it."""
    lines = text.split("\n")
    joined = [lines[0]]
    joins: list[str] = []
    for before, after in pairwise(lines):
        if is_hard_wrap(before, after):
            seam = before[-1] + after[0]
            # the break stood in for the space between two Latin words
            gap = " " if seam.isascii() and seam.isalnum() else ""
            joined[-1] += gap + after
            joins.append(f"{before[-SEAM_CONTEXT_CHARS:]}↵{after[:SEAM_CONTEXT_CHARS]}")
        else:
            joined.append(after)
    return "\n".join(joined), joins


def sql_string(value: str) -> str:
    return "'" + value.replace("'", "''") + "'"


def to_sql(rows: list[dict[str, str]], now_iso: str) -> str:
    """Raise ValueError naming the Sheet row of any row D1 would reject."""
    stamp = sql_string(now_iso)
    first_row_of_id: dict[int, int] = {}
    statements: list[str] = []
    for sheet_row, row in enumerate(rows, start=2):  # the header is Sheet row 1
        if is_blank(row):
            continue
        raw_id = row["id"]
        if not (raw_id.isascii() and raw_id.isdecimal()) or int(raw_id) == 0:
            raise ValueError(f"Sheet row {sheet_row}: the id is not a positive integer")
        phrase_id = int(raw_id)
        if phrase_id in first_row_of_id:
            raise ValueError(
                f"Sheet row {sheet_row} repeats id {phrase_id},"
                f" first used on row {first_row_of_id[phrase_id]}"
            )
        first_row_of_id[phrase_id] = sheet_row
        text = unicodedata.normalize("NFC", row["content"]).strip()
        if not text:
            raise ValueError(f"Sheet row {sheet_row} (id {phrase_id}) has no text")
        values = ", ".join((str(phrase_id), sql_string(text), stamp, stamp))
        statements.append(
            "INSERT INTO phrases (id, text, created_at, updated_at) "
            f"VALUES ({values});\n"
        )
    return "".join(statements)


def main(argv: Sequence[str] | None = None) -> None:
    parser = argparse.ArgumentParser(
        description="Write SQL that imports the Sheet's phrases into the D1 database."
    )
    parser.add_argument(
        "--out",
        type=Path,
        required=True,
        help="SQL file to write; must be inside dashboard/.private/",
    )
    args = parser.parse_args(argv)
    out: Path = args.out.resolve()
    if PRIVATE_DIR not in out.parents:
        parser.error(f"--out must be a file inside {PRIVATE_DIR}")

    phrases: list[dict[str, str]] = []
    phrases_with_joins = 0
    for row in fetch_rows(os.environ):
        text, joins = join_hard_wraps(row["content"])
        for join in joins:
            print(f"#{row['id']}  {join}", file=sys.stderr)
        phrases_with_joins += bool(joins)
        phrases.append({**row, "content": text})

    now_iso = (
        datetime.now(UTC).isoformat(timespec="milliseconds").replace("+00:00", "Z")
    )
    sql = to_sql(phrases, now_iso)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(sql, encoding="utf-8")
    imported = sum(not is_blank(row) for row in phrases)
    print(
        f"{imported} phrases, {phrases_with_joins} with joined line breaks;"
        f" SQL written to {out}",
        file=sys.stderr,
    )


if __name__ == "__main__":
    main()
