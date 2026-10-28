from __future__ import annotations

import argparse
import os
import sys
import unicodedata
from collections.abc import Mapping, Sequence
from datetime import UTC, datetime
from itertools import pairwise
from pathlib import Path

from google.auth.transport.requests import AuthorizedSession
from google.oauth2 import service_account

PRIVATE_DIR = Path(__file__).resolve().parent.parent / ".private"

SHEETS_READONLY_SCOPE = "https://www.googleapis.com/auth/spreadsheets.readonly"
SHEET_VALUES_URL = (
    "https://sheets.googleapis.com/v4/spreadsheets/{spreadsheet_id}/values/Sheet1"
)
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
KEEP_BREAK_AFTER = "。！？…，、；：”）》"
SEAM_CONTEXT_CHARS = 8


def fetch_rows(env: Mapping[str, str]) -> list[dict[str, str]]:
    """Sheet1's rows keyed by the header; short rows padded, blank rows dropped."""
    key_file = {field: env[name] for field, name in KEY_FILE_FIELD_TO_ENV.items()}
    # the key may arrive with its newlines escaped
    key_file["private_key"] = key_file["private_key"].replace("\\n", "\n")
    credentials = service_account.Credentials.from_service_account_info(
        key_file, scopes=[SHEETS_READONLY_SCOPE]
    )
    url = SHEET_VALUES_URL.format(spreadsheet_id=env["SPREADSHEET_ID"])
    with AuthorizedSession(credentials) as session:
        response = session.get(url)
    response.raise_for_status()

    header, *body = response.json()["values"]
    if not {"id", "content"} <= set(header):
        raise ValueError(
            f"Sheet1's first row must name the id and content columns, found {header}"
        )
    return [
        {name: row[i] if i < len(row) else "" for i, name in enumerate(header)}
        for row in body
        if any(cell.strip() for cell in row)
    ]


def is_hard_wrap(before: str, after: str) -> bool:
    width = sum(ASCII_PX if char.isascii() else WIDE_PX for char in before)
    return (
        bool(after)  # a wrap never leaves an empty continuation line
        and width > OLD_SCREEN_FULL_LINE_PX
        and before[-1] not in KEEP_BREAK_AFTER
        and not before[-1].isspace()
    )


def join_hard_wraps(text: str) -> tuple[str, list[str]]:
    """Join hard wraps; the list has one entry per join, with the text either side."""
    lines = text.split("\n")
    joined = [lines[0]]
    joins: list[str] = []
    for before, after in pairwise(lines):
        if is_hard_wrap(before, after):
            joined[-1] += after
            joins.append(f"{before[-SEAM_CONTEXT_CHARS:]}↵{after[:SEAM_CONTEXT_CHARS]}")
        else:
            joined.append(after)
    return "\n".join(joined), joins


def sql_string(value: str) -> str:
    return "'" + value.replace("'", "''") + "'"


def to_sql(rows: list[dict[str, str]], now_iso: str) -> str:
    """Raise ValueError for a row D1 would reject: a bad or repeated id, or no text."""
    stamp = sql_string(now_iso)
    seen_ids: set[int] = set()
    statements: list[str] = []
    for row in rows:
        phrase_id = int(row["id"])
        if phrase_id in seen_ids:
            raise ValueError(f"duplicate phrase id {phrase_id}")
        seen_ids.add(phrase_id)
        text = unicodedata.normalize("NFC", row["content"]).strip()
        if not text:
            raise ValueError(f"phrase {phrase_id} has no text")
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
    print(
        f"{len(phrases)} phrases, {phrases_with_joins} with joined line breaks;"
        f" SQL written to {out}",
        file=sys.stderr,
    )


if __name__ == "__main__":
    main()
