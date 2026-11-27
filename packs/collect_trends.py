#!/usr/bin/env python3
"""Fetch public top-page statistics for editorial review."""

import argparse
from datetime import date, datetime, timedelta, timezone
import json
from pathlib import Path
import re
from urllib.error import HTTPError
from urllib.parse import quote
from urllib.request import Request, urlopen


BASE_URL = "https://wikimedia.org/api/rest_v1/metrics/pageviews/top/zh.wikipedia.org/all-access/"
HAN = re.compile(r"[\u3400-\u4dbf\u4e00-\u9fff]")
EXCLUDED = {"首页", "首頁", "主頁", "主页", "Main Page"}


def collect(day, limit, contact):
    url = BASE_URL + day.strftime("%Y/%m/%d")
    request = Request(url, headers={"User-Agent": f"VeloraIME-TrendReview/1.0 ({contact})",
                                    "Accept": "application/json"})
    with urlopen(request, timeout=15) as response:
        content = response.read(512 * 1024 + 1)
    if len(content) > 512 * 1024:
        raise ValueError("trends response exceeds size limit")
    articles = json.loads(content)["items"][0]["articles"]
    entries = []
    for article in articles:
        text = article["article"].replace("_", " ").strip()
        if text in EXCLUDED or not 2 <= len(text) <= 32 or not HAN.search(text):
            continue
        if any(ch in text for ch in ":：/#@（）()\t\n\r"):
            continue
        entries.append({
            "title": text,
            "views": article["views"],
            "source": "https://zh.wikipedia.org/wiki/" + quote(article["article"]),
        })
        if len(entries) == limit:
            break
    if not entries:
        raise ValueError("no reviewable titles found")
    return {"date": day.isoformat(), "source": url, "reviewRequired": True, "entries": entries}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--date", type=date.fromisoformat, help="the requested UTC date")
    parser.add_argument("--limit", type=int, default=50)
    parser.add_argument("--contact", default="https://github.com/wuyijing-dev/VeloraIME")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if not 1 <= args.limit <= 100:
        parser.error("--limit must be between 1 and 100")

    day = args.date or datetime.now(timezone.utc).date() - timedelta(days=2)
    attempts = 1 if args.date else 4
    for offset in range(attempts):
        try:
            draft = collect(day - timedelta(days=offset), args.limit, args.contact)
        except HTTPError as error:
            if error.code != 404 or offset + 1 == attempts:
                parser.exit(1, f"Wikimedia request returned HTTP {error.code}\n")
        except (OSError, ValueError, KeyError, IndexError, TypeError) as error:
            parser.exit(1, f"Could not generate trends draft: {error}\n")
        else:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(json.dumps(draft, ensure_ascii=False, indent=2) + "\n",
                                   encoding="utf-8")
            return


if __name__ == "__main__":
    main()
