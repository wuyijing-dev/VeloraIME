#!/usr/bin/env python3
"""Generate signed fixtures and run tests with an isolated HTTPS publisher."""

from base64 import b64decode, b64encode
from datetime import datetime, timedelta, timezone
from functools import partial
import hashlib
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import shutil
import ssl
import subprocess
import sys
from tempfile import TemporaryDirectory
from threading import Thread


PUBLISHER = Path(__file__).resolve().parents[1] / "publish.py"


class Publisher(SimpleHTTPRequestHandler):
    etag_hits = 0
    unexpected_requests = []

    def log_message(self, fmt, *args):
        pass

    def do_GET(self):
        if (self.path.startswith("/offline-disabled/") or "?" in self.path
                or self.headers.get("Cookie") or self.headers.get("Authorization")
                or self.headers.get("User-Agent") != "VeloraIME-Pack/1"):
            type(self).unexpected_requests.append(self.path)
        if self.path.endswith("/manifest.json"):
            path = Path(self.translate_path(self.path))
            if path.is_file():
                content = path.read_bytes()
                etag = '"' + hashlib.sha256(content).hexdigest() + '"'
                if self.headers.get("If-None-Match") == etag:
                    type(self).etag_hits += 1
                    self.send_response(304)
                    self.end_headers()
                    return
                self.send_response(200)
                self.send_header("ETag", etag)
                self.send_header("Set-Cookie", "fixture=ignore; Path=/; Secure")
                self.send_header("Content-Length", str(len(content)))
                self.end_headers()
                self.wfile.write(content)
                return
        super().do_GET()


def run(*command):
    subprocess.run(command, check=True, capture_output=True)


def verify_reviewed_trends(root, now):
    reviewed = root / "reviewed.json"
    entries = [
        {"text": "触摸板", "pinyin": "chumoban", "weight": 10, "sourceTitle": "触摸板"},
        {"text": "键盘", "pinyin": "jianpan", "weight": 12},
        {"text": "过期词", "pinyin": "guoqici", "weight": 99,
         "expiresAt": (now - timedelta(days=1)).isoformat()},
    ]
    reviewed.write_text(json.dumps(entries), encoding="utf-8")
    draft = root / "review.json"
    day = now.date() - timedelta(days=2)
    draft.write_text(json.dumps({
        "date": day.isoformat(),
        "source": ("https://wikimedia.org/api/rest_v1/metrics/pageviews/top/"
                   "zh.wikipedia.org/all-access/" + day.strftime("%Y/%m/%d")),
        "reviewRequired": True,
        "entries": [{"title": "未经审核词", "views": 5000}, {"title": "触摸板", "views": 1000}],
    }), encoding="utf-8")

    def generate(output, trend_file):
        run(sys.executable, PUBLISHER, "--entries", reviewed, "--key", root / "private.pem",
            "--key-id", "test-1", "--pack-id", "test-zh", "--kind", "hotwords",
            "--version", "1", "--output", output, "--published-at", now.isoformat(),
            "--trend-draft", trend_file)
        words = json.loads((output / "pack-1.json").read_bytes())
        envelope = json.loads((output / "manifest.json").read_bytes())
        fields = json.loads(b64decode(envelope["signed"]))
        return {word["text"]: word["weight"] for word in words}, fields["provenance"]

    weights, provenance = generate(root / "trend", draft)
    assert weights == {"触摸板": 20, "键盘": 12}
    assert provenance["matchedReviewedTitles"] == 1 and provenance["trendDate"] == day.isoformat()
    fallback, provenance = generate(root / "trend-fallback", root / "missing.json")
    assert fallback == {"触摸板": 10, "键盘": 12}
    assert provenance["matchedReviewedTitles"] == 0 and "trendDate" not in provenance


def create_fixtures(root):
    run("openssl", "genpkey", "-algorithm", "Ed25519", "-out", root / "private.pem")
    run("openssl", "pkey", "-in", root / "private.pem", "-pubout", "-out", root / "public.pem")
    source = root / "entries.json"
    source.write_text('[{"text":"维洛拉","pinyin":"velora","weight":10}]', encoding="utf-8")
    now = datetime.now(timezone.utc).replace(microsecond=0)
    verify_reviewed_trends(root, now)

    def pack(directory, version, publication=now, *extra, entries=source):
        run(sys.executable, PUBLISHER, "--entries", entries, "--key", root / "private.pem",
            "--key-id", "test-1", "--pack-id", "test-zh", "--kind", "hotwords",
            "--version", str(version), "--output", root / directory, "--valid-days", "1",
            "--published-at", publication.isoformat(), *extra)

    pack("first", 1)
    pack("second", 2)
    pack("invalid-signature", 3)
    pack("invalid-digest", 3)
    pack("expired", 3, now - timedelta(days=2))
    pack("future", 3, now + timedelta(days=2))
    pack("bad-version-range", 3, now, "--min-client", "2", "--max-client", "3")
    large = root / "large-entries.json"
    large.write_text(json.dumps([{"text": f"限额测试 {number}", "pinyin": "ceshi", "weight": 20}
                                 for number in range(800)], ensure_ascii=False), encoding="utf-8")
    pack("large-update", 3, entries=large)
    expiring = root / "entry-expiry.json"
    expiry = now + timedelta(seconds=12)
    expiring.write_text(json.dumps([{"text": "热词", "pinyin": "reci", "weight": 20,
                                     "expiresAt": expiry.isoformat()}]), encoding="utf-8")
    pack("expiring-entry", 3, entries=expiring)
    shutil.copytree(root / "invalid-digest", root / "oversize")
    manifest = root / "invalid-signature" / "manifest.json"
    document = json.loads(manifest.read_bytes())
    signature = bytearray(b64decode(document["signature"]))
    signature[0] ^= 1
    document["signature"] = b64encode(signature).decode("ascii")
    manifest.write_text(json.dumps(document), encoding="utf-8")
    altered = root / "invalid-digest" / "pack-3.json"
    altered.write_bytes(altered.read_bytes().replace(b"velora", b"errorx"))
    (root / "oversize" / "pack-3.json").write_bytes(b"x" * (2 * 1024 * 1024 + 1))

    offline_only = ("private.pem", "entries.json", "entry-expiry.json", "large-entries.json",
                    "review.json", "reviewed.json")
    for name in offline_only:
        (root / name).unlink()


def verify_state_commit_failures(binary, root):
    tracer = shutil.which("strace")
    if not sys.platform.startswith("linux") or not tracer:
        return

    env = {**os.environ, "VELORA_PACK_TEST_FIXTURES": str(root)}
    with TemporaryDirectory(prefix="velora-pack-state-fault-") as directory:
        faults = Path(directory)

        def import_args(storage, version):
            return [binary, "--import-fixture", str(storage), str(root / version)]

        def trace(storage, path, injection=None):
            options = [tracer, "-qq", "-s", "100", "-e", "trace=write", "-o", str(path)]
            if injection:
                options += ["-e", f"inject=write:{injection}"]
            return subprocess.run(options + import_args(storage, "large-update"),
                                  env=env, capture_output=True, timeout=10)

        baseline = faults / "baseline"
        subprocess.run(import_args(baseline, "first"), env=env, check=True, timeout=5)
        baseline_trace = faults / "baseline.trace"
        completed = trace(baseline, baseline_trace)
        if completed.returncode and b"Operation not permitted" in completed.stderr:
            print("Skipping strace commit fault injection because ptrace is unavailable")
            return
        completed.check_returncode()
        writes = [line for line in baseline_trace.read_text().splitlines() if line.startswith("write(")]
        commit_write = next(index for index, line in enumerate(writes, 1)
                            if r'{\"current\":3,' in line)

        for name, injection, evidence in (
            ("storage-full", "error=ENOSPC", "ENOSPC (No space left on device) (INJECTED)"),
            ("interrupted", "error=EINTR:signal=SIGKILL", "killed by SIGKILL"),
        ):
            storage = faults / name
            subprocess.run(import_args(storage, "first"), env=env, check=True, timeout=5)
            cache = storage / "test-zh"
            original_state = (cache / "state.json").read_bytes()
            original_pack = (cache / "v1.pack").read_bytes()
            failed_trace = faults / (name + ".trace")
            failed = trace(storage, failed_trace, f"{injection}:when={commit_write}")
            report = failed_trace.read_text()
            assert failed.returncode != 0 and evidence in report
            assert r'{\"current\":3,' in report
            assert (cache / "v3.pack").read_bytes() == (root / "large-update/pack-3.json").read_bytes()
            assert (cache / "state.json").read_bytes() == original_state
            assert (cache / "v1.pack").read_bytes() == original_pack
            subprocess.run(import_args(storage, "large-update"), env=env, check=True, timeout=5)
            assert json.loads((cache / "state.json").read_bytes()) == {
                "current": 3, "previous": 1, "highest": 3,
            }
        print("State commit ENOSPC and SIGKILL preserved the previous package; both retries passed")


def main():
    if len(sys.argv) == 3 and sys.argv[1] == "--fixtures-only":
        root = Path(sys.argv[2])
        root.mkdir(parents=True, exist_ok=True)
        if any(root.iterdir()):
            raise ValueError("Choose an empty fixture directory")
        create_fixtures(root)
        print(root)
        return
    if len(sys.argv) != 2:
        raise ValueError("Supply a test binary or --fixtures-only DIRECTORY")

    with TemporaryDirectory(prefix="velora-word-packs-test-") as directory:
        root = Path(directory)
        create_fixtures(root)
        verify_state_commit_failures(sys.argv[1], root)
        with ThreadingHTTPServer(("127.0.0.1", 0), partial(Publisher, directory=root)) as server:
            # The server needs its private TLS key, which is not a publishing credential.
            key = root / "local-tls-key.pem"
            run("openssl", "req", "-new", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "2",
                "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost",
                "-keyout", key, "-out", root / "tls.crt")
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            context.load_cert_chain(root / "tls.crt", key)
            key.unlink()
            server.socket = context.wrap_socket(server.socket, server_side=True)
            thread = Thread(target=server.serve_forever, daemon=True)
            thread.start()
            try:
                env = {
                    **os.environ, "VELORA_PACK_TEST_FIXTURES": directory,
                    "VELORA_PACK_TEST_URL": f"https://localhost:{server.server_port}/",
                }
                subprocess.run([sys.argv[1]], env=env, check=True, timeout=45)
                if Publisher.etag_hits != 1:
                    raise RuntimeError("client did not revalidate its manifest with If-None-Match")
                if Publisher.unexpected_requests:
                    raise RuntimeError("unexpected offline request or credentials in pack traffic")
            finally:
                server.shutdown()
                thread.join()


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as error:
        if error.stderr:
            sys.stderr.buffer.write(error.stderr)
        sys.exit(error.returncode)
