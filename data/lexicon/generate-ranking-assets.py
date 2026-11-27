#!/usr/bin/env python3
"""Regenerate the offline ranking tables from pinned upstream archives."""

import argparse
import gzip
import hashlib
from pathlib import Path
import re
import zipfile

import msgpack


CEDICT_SHA256 = "00e6c188df0056c42c0448386d0b4f532aa805d9fdd87aef139f72b421ace2f4"
WORDFREQ_SHA256 = "c5bfdf26cc26437d670ef145326e4e395cf0a4f4de2da85a331ddf3d2a334588"
CEDICT_ENTRY = re.compile(r"^(\S+) (\S+) \[([^\]]+)\] ")
PINYIN = re.compile(r"([a-z]+)[1-5]\Z")
ENGLISH = re.compile(r"[a-z]{1,48}\Z")
HAN_RANGES = (
    (0x3400, 0x4DBF), (0x4E00, 0x9FFF), (0xF900, 0xFAFF),
    (0x20000, 0x2EE5F), (0x2F800, 0x2FA1F), (0x30000, 0x3347F),
)


def check_hash(path: Path, expected: str) -> None:
    with path.open("rb") as source:
        actual = hashlib.file_digest(source, "sha256").hexdigest()
    if actual != expected:
        raise ValueError(f"{path} has SHA-256 {actual}, expected {expected}")


def han_word(text: str) -> bool:
    return all(
        char == "〇" or any(low <= ord(char) <= high for low, high in HAN_RANGES)
        for char in text
    )


def cedict_words(path: Path):
    with gzip.open(path, "rt", encoding="utf-8") as source:
        for line in source:
            match = CEDICT_ENTRY.match(line)
            if not match:
                continue
            traditional, simplified, reading = match.groups()
            if not 2 <= len(traditional) <= 8:
                continue
            if not han_word(traditional) or not han_word(simplified):
                continue
            syllables = reading.lower().replace("u:", "v").replace("ü", "v").split()
            if len(syllables) != len(traditional):
                continue
            base = [PINYIN.fullmatch(syllable) for syllable in syllables]
            if all(base):
                yield traditional, simplified, " ".join(
                    "er" if syllable[1] == "r" else syllable[1] for syllable in base
                )


def english_words(path: Path):
    with zipfile.ZipFile(path) as wheel:
        bins = msgpack.unpackb(gzip.decompress(wheel.read("wordfreq/data/large_en.msgpack.gz")), raw=False)
    if not bins or bins[0] != {"format": "cB", "version": 1}:
        raise ValueError("Unexpected wordfreq header")
    for centibel, words in enumerate(bins[1:]):
        zipf_hundredths = 900 - centibel
        if zipf_hundredths < 300:
            break
        for word in words:
            if ENGLISH.fullmatch(word):
                yield word, zipf_hundredths


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cedict", type=Path, required=True)
    parser.add_argument("--wordfreq", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path(__file__).parent)
    args = parser.parse_args()
    check_hash(args.cedict, CEDICT_SHA256)
    check_hash(args.wordfreq, WORDFREQ_SHA256)
    args.output.mkdir(parents=True, exist_ok=True)

    with (args.output / "cedict-pronunciations.tsv").open("w", encoding="utf-8", newline="\n") as file:
        file.write(
            "# CC-CEDICT community and MDBG, snapshot 2026-10-07T03:26:22Z.\n"
            "# Builds on CEDICT, copyright 1997, 1998 Paul Andrew Denisowski.\n"
            "# Adaptation: Han compounds of two to eight characters, without definitions or tones.\n"
            "# https://cc-cedict.org/wiki/ | https://creativecommons.org/licenses/by-sa/4.0/\n"
            "# Traditional\tSimplified\tPinyin\n"
        )
        for values in sorted(set(cedict_words(args.cedict))):
            file.write("\t".join(values) + "\n")

    with (args.output / "english-frequency.tsv").open("w", encoding="utf-8", newline="\n") as file:
        file.write(
            "# wordfreq v3.0.2, copyright 2022 Robyn Speer. DOI 10.5281/zenodo.7199437.\n"
            "# Adaptation: ASCII English tokens with Zipf frequency at least 3.00.\n"
            "# Includes freely available SUBTLEX-US by Marc Brysbaert and Boris New,\n"
            "# and SUBTLEX-UK by Walter J. B. van Heuven, Pawel Mandera, Emmanuel Keuleers\n"
            "# and Marc Brysbaert, plus OpenSubtitles and Google Books Ngram data.\n"
            "# https://github.com/rspeer/wordfreq | https://creativecommons.org/licenses/by-sa/4.0/\n"
            "# Word\tZipf hundredths\n"
        )
        for word, frequency in sorted(english_words(args.wordfreq)):
            file.write(f"{word}\t{frequency}\n")


if __name__ == "__main__":
    main()
