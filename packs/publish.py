#!/usr/bin/env python3
"""Validate reviewed word entries and build a signed static feed."""

import argparse
import base64
from datetime import date, datetime, timedelta, timezone
import hashlib
import json
from pathlib import Path
import re
import subprocess
from tempfile import TemporaryDirectory
import unicodedata

from collect_trends import BASE_URL


ID_PATTERN = re.compile(r"[a-z0-9][a-z0-9-]{0,39}")
PINYIN_PATTERN = re.compile(r"[a-z]+(?:'[a-z]+)*")
MAX_ENTRIES = 20_000
MAX_BYTES = 2 * 1024 * 1024


def iso_date(value):
    date = datetime.fromisoformat(value)
    if date.utcoffset() != timedelta(0) or date.microsecond:
        raise ValueError("dates must use UTC with whole-second precision")
    return date


def iso_string(value):
    return value.strftime("%Y-%m-%dT%H:%M:%SZ")


def as_json(value):
    return (json.dumps(value, ensure_ascii=False, allow_nan=False,
                       sort_keys=True, separators=(",", ":")) + "\n").encode("utf-8")


def load_trends(source, published_at):
    if source is None:
        return {}, {}
    try:
        with source.open(encoding="utf-8") as stream:
            draft = json.loads(stream.read(128 * 1024 + 1))
        day = date.fromisoformat(draft["date"])
        age = (published_at.date() - day).days
        entries = draft["entries"]
        if (not 0 <= age <= 7 or draft.get("reviewRequired") is not True
                or draft["source"] != BASE_URL + day.strftime("%Y/%m/%d")
                or not isinstance(entries, list) or not 0 < len(entries) <= 100):
            raise ValueError("expected a Wikimedia trend draft at most seven days old")
        rank = {}
        for index, entry in enumerate(entries):
            if (type(entry["views"]) is int and entry["views"] > 0
                    and isinstance(entry["title"], str)):
                rank.setdefault(entry["title"], index)
        return rank, {"trendSource": draft["source"], "trendDate": day.isoformat()}
    except (OSError, ValueError, KeyError, TypeError):
        return {}, {}


def reviewed_entries(source, published_at, trend_rank, kind):
    values = json.loads(source.read_text(encoding="utf-8"))
    if not isinstance(values, list) or not 0 < len(values) <= MAX_ENTRIES:
        raise ValueError(f"expected between 1 and {MAX_ENTRIES} reviewed entries")
    seen = set()
    entries = []
    matched = 0
    for index, value in enumerate(values, 1):
        if not isinstance(value, dict):
            raise ValueError(f"entry {index} must be an object")
        text = value.get("text")
        pinyin = value.get("pinyin")
        weight = value.get("weight")
        if (not isinstance(text, str) or not text or text != text.strip()
                or unicodedata.normalize("NFC", text) != text
                or any(unicodedata.category(ch) in {"Cc", "Cf", "Cs", "Zl", "Zp"}
                       for ch in text)
                or len(text.encode("utf-16-le")) > 96
                or not isinstance(pinyin, str) or len(pinyin) > 64
                or not PINYIN_PATTERN.fullmatch(pinyin)
                or type(weight) is not int or not 1 <= weight <= 100):
            raise ValueError(f"invalid text, pinyin, or weight in entry {index}")
        if (text, pinyin) in seen:
            raise ValueError(f"duplicate entry {index}")
        seen.add((text, pinyin))
        entry = {"text": text, "pinyin": pinyin, "weight": weight}
        if "expiresAt" in value:
            expires_at = iso_date(value["expiresAt"])
            if expires_at <= published_at:
                if kind == "hotwords":
                    continue
                raise ValueError(f"entry {index} expires before publication")
            entry["expiresAt"] = iso_string(expires_at)
        # Only a reviewed title can influence its corresponding approved term.
        source_title = value.get("sourceTitle")
        if isinstance(source_title, str) and source_title in trend_rank:
            bonus = max(1, 10 - trend_rank[source_title] // 5)
            entry["weight"] = min(100, weight + bonus)
            matched += 1
        entries.append(entry)
    if not entries:
        raise ValueError("the reviewed list has no unexpired entries")
    return sorted(entries, key=lambda entry: (entry["pinyin"], entry["text"])), matched


def sign(payload, key):
    with TemporaryDirectory(prefix="velora-sign-") as directory:
        path = Path(directory) / "metadata.json"
        path.write_bytes(payload)
        signature = subprocess.run(
            ["openssl", "pkeyutl", "-sign", "-rawin", "-inkey", str(key), "-in", str(path)],
            check=True, capture_output=True
        ).stdout
    if len(signature) != 64:
        raise ValueError("signing key must be Ed25519")
    return signature


def write_file(path, content, immutable=False):
    if immutable and path.exists() and path.read_bytes() != content:
        raise ValueError(f"refusing to replace a different versioned payload at {path}")
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_bytes(content)
    temporary.replace(path)


def publish(args):
    if not ID_PATTERN.fullmatch(args.pack_id) or not ID_PATTERN.fullmatch(args.key_id):
        raise ValueError("pack ID and key ID must match [a-z0-9][a-z0-9-]{0,39}")
    if not 0 < args.version <= 2_147_483_647:
        raise ValueError("pack version must be a positive 32-bit integer")
    if not 1 <= args.min_client <= args.max_client <= 2_147_483_647:
        raise ValueError("invalid compatible client range")
    if not 1 <= args.valid_days <= 366:
        raise ValueError("validity must be between 1 and 366 days")
    if args.trend_draft is not None and args.kind != "hotwords":
        raise ValueError("trend ranking only applies to hotwords")

    published_at = (iso_date(args.published_at) if args.published_at
                    else datetime.now(timezone.utc).replace(microsecond=0))
    trend_rank, provenance = load_trends(args.trend_draft, published_at)
    entries, matched = reviewed_entries(args.entries, published_at, trend_rank, args.kind)
    payload = as_json(entries)
    if len(payload) > MAX_BYTES:
        raise ValueError(f"pack exceeds the {MAX_BYTES}-byte limit")

    name = f"pack-{args.version}.json"
    manifest = as_json({
        "schemaVersion": 1,
        "packId": args.pack_id,
        "kind": args.kind,
        "locale": args.locale,
        "version": args.version,
        "publishedAt": iso_string(published_at),
        "expiresAt": iso_string(published_at + timedelta(days=args.valid_days)),
        "minClientVersion": args.min_client,
        "maxClientVersion": args.max_client,
        "size": len(payload),
        "sha256": hashlib.sha256(payload).hexdigest(),
        "url": name,
        "keyId": args.key_id,
        "provenance": {
            **provenance,
            "reviewedList": args.entries.name,
            "matchedReviewedTitles": matched,
        },
    })
    envelope = as_json({
        "signed": base64.b64encode(manifest).decode("ascii"),
        "signature": base64.b64encode(sign(manifest, args.key)).decode("ascii"),
    })
    args.output.mkdir(parents=True, exist_ok=True)
    write_file(args.output / name, payload, immutable=True)
    write_file(args.output / "manifest.json", envelope)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--entries", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--key", type=Path, required=True)
    parser.add_argument("--key-id", required=True)
    parser.add_argument("--pack-id", required=True)
    parser.add_argument("--kind", choices=("dictionary", "hotwords"), required=True)
    parser.add_argument("--locale", choices=("zh-CN", "en-US"), default="zh-CN")
    parser.add_argument("--version", type=int, required=True)
    parser.add_argument("--published-at", help="UTC timestamp for reproducible builds")
    parser.add_argument("--valid-days", type=int, default=14)
    parser.add_argument("--min-client", type=int, default=1)
    parser.add_argument("--max-client", type=int, default=1)
    parser.add_argument("--trend-draft", type=Path,
                        help="optional recent Wikimedia draft for reviewed hotword ranking")
    args = parser.parse_args()
    try:
        publish(args)
    except (ValueError, TypeError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Word-pack publication failed: {error}\n")


if __name__ == "__main__":
    main()
