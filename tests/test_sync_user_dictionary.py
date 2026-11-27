from collections import namedtuple
import json
from pathlib import Path
import shutil
import stat
import subprocess
import sys
from tempfile import TemporaryDirectory
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "tools" / "sync_user_dictionary.py"
Device = namedtuple("Device", "name identity recipient")


@unittest.skipUnless(shutil.which("age") and shutil.which("age-keygen"), "age is optional")
class DictionarySyncTests(unittest.TestCase):
    def setUp(self):
        place = TemporaryDirectory()
        self.addCleanup(place.cleanup)
        self.root = Path(place.name)
        self.shared = self.root / "shared"
        self.shared.mkdir()
        self.a = self.key("device-a")
        self.b = self.key("device-b")

    def key(self, name):
        identity = self.root / f"{name}.agekey"
        subprocess.run(["age-keygen", "-o", identity], check=True, capture_output=True)
        recipient = subprocess.check_output(["age-keygen", "-y", identity], text=True).strip()
        return Device(name, identity, recipient)

    def call_tool(self, shared, device, *arguments, ok=True):
        result = subprocess.run(
            [sys.executable, SCRIPT, "--shared", shared, "--profile", "personal",
             "--identity", device.identity, *arguments],
            text=True, capture_output=True, timeout=30
        )
        if ok:
            self.assertEqual(result.returncode, 0, result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0)
        return result.stdout.strip() if ok else result.stderr.strip()

    def export(self, name, *words):
        payload = {"version": 1, "entries": [
            {"locale": "zh-CN", "code": code, "text": text, "hits": 1,
             "usedAt": 1_700_000_000, "custom": True}
            for code, text in words
        ]}
        path = self.root / f"{name}.json"
        path.write_text(json.dumps(payload, ensure_ascii=False), encoding="utf-8")
        return path

    def publish(self, shared, device, source, *parents, ok=True):
        parent_flags = [flag for parent in parents for flag in ("--parent", parent)]
        return self.call_tool(shared, device, "push", "--device", device.name, "--input", source,
                              "--recipient", self.a.recipient, "--recipient", self.b.recipient,
                              *parent_flags, ok=ok)

    def test_encrypted_round_trip_and_tamper(self):
        exported = self.export("initial", ("siren", "私人词"))
        snapshot = self.publish(self.shared, self.a, exported)
        sealed = self.shared / "personal" / f"snapshot-{snapshot}.age"
        self.assertNotIn("私人词".encode("utf-8"), sealed.read_bytes())
        self.assertTrue(sealed.read_bytes().startswith(b"age-encryption.org/v1\n"))

        output = self.root / "opened.json"
        self.call_tool(self.shared, self.b, "pull", "--output", output)
        self.assertEqual(json.loads(exported.read_bytes()), json.loads(output.read_bytes()))
        self.assertEqual(stat.S_IMODE(output.stat().st_mode), 0o600)
        self.call_tool(self.shared, self.b, "pull", "--output", output, ok=False)
        self.call_tool(self.shared, self.b, "pull", "--output", self.shared / "bad.json", ok=False)
        self.assertFalse((self.shared / "bad.json").exists())
        other = self.key("other")
        self.call_tool(self.shared, other, "pull", "--output", self.root / "unauthorized", ok=False)

        with sealed.open("ab") as device:
            device.write(b"modified")
        error = self.call_tool(self.shared, self.b, "list", ok=False)
        self.assertIn("SHA-256", error)

    def test_concurrent_branch_merge_and_missing_parent(self):
        exported = self.export("base", ("xuexi", "学习"))
        base = self.publish(self.shared, self.a, exported)
        offline = self.root / "offline"
        shutil.copytree(self.shared, offline)

        left = self.publish(self.shared, self.a, self.export("left", ("zuoce", "左侧")), base)
        right = self.publish(offline, self.b, self.export("right", ("youce", "右侧")), base)
        self.publish(self.shared, self.b, exported, base, ok=False)
        shutil.copy2(offline / "personal" / f"snapshot-{right}.age", self.shared / "personal")

        history = json.loads(self.call_tool(self.shared, self.b, "list"))
        self.assertEqual(set(history["heads"]), {left, right})
        self.call_tool(self.shared, self.b, "pull", "--output", self.root / "conflict.json", ok=False)
        self.call_tool(self.shared, self.b, "pull", "--snapshot", right,
                       "--output", self.root / "review.json")
        merged = self.export("merged", ("xuexi", "学习"), ("zuoce", "左侧"), ("youce", "右侧"))
        latest = self.publish(self.shared, self.b, merged, left, right)
        history = json.loads(self.call_tool(self.shared, self.a, "list"))
        self.assertEqual(history["heads"], [latest])
        self.assertEqual(len(history["snapshots"]), 4)

        incomplete = self.root / "incomplete"
        (incomplete / "personal").mkdir(parents=True)
        shutil.copy2(self.shared / "personal" / f"snapshot-{latest}.age", incomplete / "personal")
        history = json.loads(self.call_tool(incomplete, self.a, "list"))
        self.assertEqual(set(history["missingParents"]), {left, right})
        self.call_tool(incomplete, self.a, "pull", "--output", self.root / "incomplete.json", ok=False)


if __name__ == "__main__":
    unittest.main()
