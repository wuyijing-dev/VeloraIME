#!/usr/bin/env python3
"""Exchange explicit VeloraIME dictionary exports through an encrypted folder."""

import argparse
from datetime import datetime, timezone
import errno
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
from tempfile import NamedTemporaryFile
import unicodedata


DICTIONARY_LIMIT = 4 * 1024 * 1024
SNAPSHOT_LIMIT = DICTIONARY_LIMIT + 128 * 1024
LOCALES = {"zh-CN", "zh-TW", "en-US", "en-GB"}
LABEL = re.compile(r"[a-z0-9][a-z0-9_-]{0,47}")
SNAPSHOT_ID = re.compile(r"[0-9a-f]{64}")
SNAPSHOT_FILE = re.compile(r"snapshot-([0-9a-f]{64})[.]age")
RECIPIENT = re.compile(r"age1[023456789acdefghjklmnpqrstuvwxyz]{58}")
SPELLING = re.compile(r"[a-z ]{1,48}")
FORMAT = "veloraime-user-dictionary-sync/1"


class SyncError(Exception):
    pass


def checked_label(value):
    if not LABEL.fullmatch(value):
        raise argparse.ArgumentTypeError("use 1–48 lowercase letters, numbers, '-' or '_'")
    return value


def checked_id(value):
    if not SNAPSHOT_ID.fullmatch(value):
        raise argparse.ArgumentTypeError("use the complete 64-character snapshot ID")
    return value


def checked_recipient(value):
    if not RECIPIENT.fullmatch(value):
        raise argparse.ArgumentTypeError("recipient must be a native age X25519 public key")
    return value


def outside_shared(path, shared, purpose):
    if path.resolve().is_relative_to(shared):
        raise SyncError(f"Keep the {purpose} outside the shared directory")


def read_regular(path, limit):
    descriptor = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    with os.fdopen(descriptor, "rb") as stream:
        status = os.fstat(stream.fileno())
        if not stat.S_ISREG(status.st_mode) or status.st_size > limit:
            raise SyncError("Input is not a regular file within the size limit")
        data = stream.read(limit + 1)
    if len(data) > limit:
        raise SyncError("Input exceeds the size limit")
    return data


def encode(value):
    return json.dumps(value, ensure_ascii=False, allow_nan=False,
                      sort_keys=True, separators=(",", ":")).encode("utf-8")


def decode(data):
    try:
        return json.loads(data.decode("utf-8"))
    except (ValueError, RecursionError) as error:
        raise SyncError("Invalid UTF-8 JSON") from error


def validate_dictionary(dictionary):
    if (not isinstance(dictionary, dict) or set(dictionary) != {"version", "entries"}
            or type(dictionary["version"]) is not int or dictionary["version"] != 1
            or not isinstance(dictionary["entries"], list)):
        raise SyncError("Expected an exported VeloraIME version 1 user dictionary")
    seen = set()
    for entry in dictionary["entries"]:
        if (not isinstance(entry, dict)
                or set(entry) != {"locale", "code", "text", "hits", "usedAt", "custom"}):
            raise SyncError("Malformed dictionary entry")
        locale, code, text = entry["locale"], entry["code"], entry["text"]
        hits, used_at, custom = entry["hits"], entry["usedAt"], entry["custom"]
        if (not isinstance(locale, str) or locale not in LOCALES
                or not isinstance(code, str) or not SPELLING.fullmatch(code)
                or not isinstance(text, str) or not text
                or any(unicodedata.category(ch) in {"Cc", "Cs", "Zl", "Zp"} for ch in text)
                or len(text.encode("utf-16-le")) > 200
                or type(hits) is not int or not 0 <= hits <= 10000
                or type(used_at) is not int or not 0 <= used_at <= 2 ** 63 - 1
                or type(custom) is not bool or (not custom and hits == 0)):
            raise SyncError("Dictionary entry is outside the VeloraIME import format")
        identity = (locale, code, text)
        if identity in seen:
            raise SyncError("Duplicate dictionary entry")
        seen.add(identity)


def age_process(age, arguments, data):
    try:
        result = subprocess.run([age, *arguments], input=data, capture_output=True, timeout=30)
    except subprocess.TimeoutExpired as error:
        raise SyncError("age timed out") from error
    if result.returncode:
        raise SyncError("age failed to process the file with the supplied keys")
    return result.stdout


def age_identity_flags(shared, identities):
    flags = []
    for path in identities:
        outside_shared(path, shared, "age identity")
        resolved = path.resolve(strict=True)
        if not resolved.is_file():
            raise SyncError("Expected a local age identity file")
        flags.extend(("--identity", str(resolved)))
    return flags


def profile_directory(shared, profile):
    directory = shared / profile
    if directory.is_symlink():
        raise SyncError("Profile directory cannot be a symbolic link")
    return directory


def read_snapshot(age, identity_flags, directory, profile, snapshot_id):
    try:
        sealed = read_regular(directory / f"snapshot-{snapshot_id}.age", SNAPSHOT_LIMIT)
        if hashlib.sha256(sealed).hexdigest() != snapshot_id:
            raise SyncError("ciphertext SHA-256 does not match its filename")
        document = decode(age_process(age, ["--decrypt", *identity_flags], sealed))
        if (not isinstance(document, dict)
                or set(document) != {"format", "profile", "device", "createdAt", "parents",
                                     "dictionary"}
                or document["format"] != FORMAT or document["profile"] != profile
                or not isinstance(document["device"], str) or not LABEL.fullmatch(document["device"])
                or not isinstance(document["parents"], list)
                or not all(isinstance(parent, str) and SNAPSHOT_ID.fullmatch(parent)
                           for parent in document["parents"])
                or snapshot_id in document["parents"]
                or len(set(document["parents"])) != len(document["parents"])
                or not isinstance(document["createdAt"], str)):
            raise SyncError("invalid encrypted snapshot metadata")
        stamp = datetime.strptime(document["createdAt"], "%Y-%m-%dT%H:%M:%SZ")
        if stamp.strftime("%Y-%m-%dT%H:%M:%SZ") != document["createdAt"]:
            raise SyncError("invalid snapshot timestamp")
        validate_dictionary(document["dictionary"])
        return document
    except (OSError, ValueError, SyncError) as error:
        raise SyncError(f"Snapshot {snapshot_id}: {error}") from error


def history(age, identity_flags, directory, profile):
    records = {}
    if directory.exists():
        for path in sorted(directory.iterdir()):
            match = SNAPSHOT_FILE.fullmatch(path.name)
            if match:
                snapshot_id = match.group(1)
                entry = read_snapshot(age, identity_flags, directory, profile, snapshot_id)
                records[snapshot_id] = {
                    "id": snapshot_id,
                    "device": entry["device"],
                    "createdAt": entry["createdAt"],
                    "parents": entry["parents"],
                    "entries": len(entry["dictionary"]["entries"]),
                }
    parent_ids = {parent for entry in records.values() for parent in entry["parents"]}
    return {
        "heads": sorted(records.keys() - parent_ids),
        "missingParents": sorted(parent_ids - records.keys()),
        "snapshots": list(records.values()),
    }


def sync_directory(path):
    try:
        descriptor = os.open(path, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(descriptor)
        finally:
            os.close(descriptor)
    except OSError as error:
        unsupported = {errno.EINVAL, errno.ENOTSUP, errno.EBADF}
        if error.errno not in unsupported:
            raise


def write_snapshot(path, content):
    """Commit a verified content-addressed ciphertext on filesystems including FAT."""
    if path.exists():
        if read_regular(path, SNAPSHOT_LIMIT) == content:
            return
        raise SyncError("An invalid object occupies this snapshot filename")
    with NamedTemporaryFile(prefix=".velora-", dir=path.parent, delete=False) as stream:
        temporary = Path(stream.name)
        try:
            stream.write(content)
            stream.flush()
            os.fsync(stream.fileno())
            os.replace(temporary, path)
            sync_directory(path.parent)
        finally:
            temporary.unlink(missing_ok=True)


def write_private_export(path, content):
    """Publish only complete local plaintext and never replace an existing file."""
    if len(content) + 1 > DICTIONARY_LIMIT:
        raise SyncError("Decrypted dictionary exceeds the VeloraIME import size limit")
    with NamedTemporaryFile(prefix=".velora-", dir=path.parent, delete=False) as stream:
        temporary = Path(stream.name)
        try:
            stream.write(content + b"\n")
            stream.flush()
            os.fsync(stream.fileno())
            os.link(temporary, path)
            sync_directory(path.parent)
        finally:
            temporary.unlink(missing_ok=True)


def push(args, age, shared, directory, flags, revisions):
    if revisions["missingParents"]:
        raise SyncError("Some parent snapshots are missing. Finish syncing the directory first")
    heads = set(revisions["heads"])
    if set(args.parent) != heads or len(args.parent) != len(heads):
        raise SyncError("Pass every current head once with --parent; run list to inspect history")

    outside_shared(args.input, shared, "plaintext export")
    dictionary = decode(read_regular(args.input, DICTIONARY_LIMIT))
    validate_dictionary(dictionary)
    stamp = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    document = {
        "format": FORMAT, "profile": args.profile, "device": args.device, "createdAt": stamp,
        "parents": sorted(args.parent), "dictionary": dictionary,
    }
    recipient_flags = [item for key in dict.fromkeys(args.recipient) for item in ("--recipient", key)]
    plaintext = encode(document)
    sealed = age_process(age, recipient_flags, plaintext)
    if len(sealed) > SNAPSHOT_LIMIT:
        raise SyncError("Encrypted snapshot exceeds the size limit")
    # Confirm this device can read the new snapshot before publishing it.
    if age_process(age, ["--decrypt", *flags], sealed) != plaintext:
        raise SyncError("The publishing device cannot verify this snapshot")
    snapshot_id = hashlib.sha256(sealed).hexdigest()
    directory.mkdir(parents=True, exist_ok=True)
    write_snapshot(directory / f"snapshot-{snapshot_id}.age", sealed)
    print(snapshot_id)


def pull(args, age, shared, directory, flags, revisions):
    outside_shared(args.output, shared, "plaintext output")
    snapshot_id = args.snapshot
    if not snapshot_id:
        if revisions["missingParents"]:
            raise SyncError("Some parent snapshots are missing; finish syncing or select a snapshot")
        if len(revisions["heads"]) != 1:
            raise SyncError("Expected one head. Run list and select a snapshot with --snapshot")
        snapshot_id = revisions["heads"][0]
    snapshot = read_snapshot(age, flags, directory, args.profile, snapshot_id)
    write_private_export(args.output, encode(snapshot["dictionary"]))
    print(snapshot_id)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--shared", type=Path, required=True, help="shared or removable directory")
    parser.add_argument("--profile", type=checked_label, required=True, help="profile group label")
    parser.add_argument("--identity", type=Path, action="append", required=True,
                        help="local age identity file; repeat for key rotations")
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("list", help="show decrypted history, current heads, and missing parents")
    publish = commands.add_parser("push", help="publish an encrypted copy of a local dictionary")
    publish.add_argument("--input", type=Path, required=True)
    publish.add_argument("--device", type=checked_label, required=True)
    publish.add_argument("--recipient", type=checked_recipient, action="append", required=True)
    publish.add_argument("--parent", type=checked_id, action="append", default=[])
    restore = commands.add_parser("pull", help="decrypt a snapshot to a new private local JSON file")
    restore.add_argument("--snapshot", type=checked_id)
    restore.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        age = shutil.which("age")
        if not age:
            raise SyncError("Install age and ensure the age command is on PATH")
        shared = args.shared.resolve()
        directory = profile_directory(shared, args.profile)
        flags = age_identity_flags(shared, args.identity)

        if args.command == "pull" and args.snapshot:
            pull(args, age, shared, directory, flags, {})
            return
        revisions = history(age, flags, directory, args.profile)
        if args.command == "list":
            print(json.dumps(revisions, ensure_ascii=False, indent=2))
        elif args.command == "push":
            push(args, age, shared, directory, flags, revisions)
        else:
            pull(args, age, shared, directory, flags, revisions)
    except (OSError, SyncError) as error:
        parser.exit(1, f"Dictionary synchronization failed: {error}\n")


if __name__ == "__main__":
    main()
