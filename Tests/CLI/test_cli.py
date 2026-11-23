"""CLI tests for the `k` executable.

Runs the real binary and asserts on stdout, stderr and exit codes. The binary
path comes from $KENGINE_CLI (set by `make cli-test`); a missing or
non-executable binary is a hard failure, never a skip.
"""
import json
import os
import re
import subprocess
import tempfile
import unittest


def cli_path() -> str:
    path = os.environ.get("KENGINE_CLI", "")
    if not path:
        raise RuntimeError("KENGINE_CLI is not set; run via `make cli-test`")
    if not os.access(path, os.X_OK):
        raise RuntimeError(f"KENGINE_CLI does not point to an executable: {path}")
    return path


def run(*args: str):
    return subprocess.run([cli_path(), *args], capture_output=True, text=True, timeout=60)


class VersionTests(unittest.TestCase):
    def test_version_prints_name_and_semver_like_string(self):
        r = run("--version")
        self.assertEqual(r.returncode, 0)
        self.assertRegex(r.stdout, r"^k \d+\.\d+\.\d+(-[A-Za-z0-9.]+)?\n$")
        self.assertEqual(r.stderr, "")

    def test_version_rejects_extra_arguments(self):
        r = run("--version", "extra")
        self.assertEqual(r.returncode, 2)
        self.assertEqual(r.stdout, "")
        self.assertIn("--version", r.stderr)


class HelpAndUsageTests(unittest.TestCase):
    def test_help_variants_succeed_and_list_only_implemented_commands(self):
        for flag in ("--help", "-h", "help"):
            with self.subTest(flag=flag):
                r = run(flag)
                self.assertEqual(r.returncode, 0)
                self.assertIn("k doctor", r.stdout)
                self.assertIn("k --version", r.stdout)
                self.assertIn("k scene validate", r.stdout)
                self.assertIn("k scene dump", r.stdout)
                self.assertIn("--log-file", r.stdout)
                # Commands from the roadmap that do not exist yet must not be advertised.
                for unimplemented in ("create", "build", "run", "editor", "asset", "cook"):
                    self.assertNotRegex(r.stdout, rf"\bk {unimplemented}\b")

    def test_no_arguments_is_a_usage_error(self):
        r = run()
        self.assertEqual(r.returncode, 2)
        self.assertEqual(r.stdout, "")
        self.assertIn("usage", r.stderr)

    def test_unknown_command_is_a_usage_error_and_names_the_command(self):
        r = run("frobnicate")
        self.assertEqual(r.returncode, 2)
        self.assertEqual(r.stdout, "")
        self.assertIn("frobnicate", r.stderr)

    def test_unimplemented_roadmap_commands_fail_instead_of_pretending(self):
        for cmd in ("build", "create", "run", "editor"):
            with self.subTest(cmd=cmd):
                r = run(cmd, "somewhere")
                self.assertEqual(r.returncode, 2)
                self.assertIn("unknown command", r.stderr)


class DoctorTests(unittest.TestCase):
    def test_doctor_text_reports_facts_and_passing_checks(self):
        r = run("doctor")
        self.assertEqual(r.returncode, 0, msg=r.stdout + r.stderr)
        self.assertIn("K Engine doctor", r.stdout)
        self.assertIn("compiler", r.stdout)
        self.assertIn("kengine.scene v1", r.stdout)
        self.assertIn("all checks passed", r.stdout)
        self.assertNotIn("FAIL", r.stdout)

    def test_doctor_json_is_valid_and_complete(self):
        r = run("doctor", "--json")
        self.assertEqual(r.returncode, 0, msg=r.stdout + r.stderr)
        doc = json.loads(r.stdout)
        self.assertIs(doc["ok"], True)
        self.assertRegex(doc["version"], r"^\d+\.\d+\.\d+")
        self.assertIn(doc["platform"]["pointer_bits"], (32, 64))
        self.assertGreaterEqual(doc["build"]["cplusplus"], 202002)
        self.assertEqual(doc["formats"]["scene_schema"], "kengine.scene")
        self.assertEqual(doc["formats"]["scene_schema_version"], 1)
        names = {c["name"] for c in doc["checks"]}
        self.assertEqual(names, {"uuid_generate", "handle_invalidation", "scene_round_trip"})
        for c in doc["checks"]:
            self.assertIs(c["ok"], True, msg=c)
            self.assertTrue(c["detail"])

    def test_doctor_json_matches_text_mode_verdict(self):
        self.assertEqual(run("doctor").returncode, run("doctor", "--json").returncode)

    def test_doctor_rejects_unknown_option(self):
        r = run("doctor", "--yaml")
        self.assertEqual(r.returncode, 2)
        self.assertEqual(r.stdout, "")
        self.assertIn("--yaml", r.stderr)


A = "11111111-1111-4111-8111-111111111111"
B = "22222222-2222-4222-8222-222222222222"
C = "33333333-3333-4333-8333-333333333333"


def scene_text(objects, version=1, extra_top=""):
    return ('{"schema":"kengine.scene","schema_version":%d%s,"objects":[%s]}' % (version, extra_top, ",".join(objects)))


def obj(oid, name="n", kind="cube", parent=None, extra=""):
    p = "null" if parent is None else '"%s"' % parent
    return '{"id":"%s","name":%s,"kind":"%s","parent":%s%s}' % (oid, json.dumps(name), kind, p, extra)


class SceneCommandTests(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.dir = self._tmp.name

    def write(self, name, text, binary=False):
        path = os.path.join(self.dir, name)
        with open(path, "wb" if binary else "w", **({} if binary else {"encoding": "utf-8", "newline": "\n"})) as f:
            f.write(text)
        return path

    def good(self):
        return self.write("good.kscene", scene_text([
            obj(A, "Root", "empty"),
            obj(B, "Child", "cube", A, ',"transform":{"position":[1,2.5,-3],"scale":[2,2,2]}'),
            obj(C, "Other", "sphere"),
        ]))

    # ---- validate ----
    def test_validate_accepts_a_good_file(self):
        r = run("scene", "validate", self.good())
        self.assertEqual(r.returncode, 0, msg=r.stderr)
        self.assertIn("OK:", r.stdout)
        self.assertIn("3 objects", r.stdout)
        self.assertIn("0 warnings", r.stdout)
        self.assertEqual(r.stderr, "")

    def test_validate_json_success_shape(self):
        r = run("scene", "validate", self.good(), "--json")
        self.assertEqual(r.returncode, 0)
        doc = json.loads(r.stdout)
        self.assertEqual(doc, {"ok": True, "objects": 3, "warnings": []})

    def test_validate_reports_unknown_fields_as_warnings_but_succeeds(self):
        path = self.write("w.kscene", scene_text([obj(A, extra=',"postion":[1,2,3]')]))
        r = run("scene", "validate", path)
        self.assertEqual(r.returncode, 0)
        self.assertIn("warning:", r.stderr)
        self.assertIn("postion", r.stderr)
        self.assertIn("1 warnings", r.stdout)
        doc = json.loads(run("scene", "validate", path, "--json").stdout)
        self.assertEqual(len(doc["warnings"]), 1)

    def test_validate_rejects_invalid_files_with_stable_codes(self):
        cases = {
            "missing.kscene": (None, "file.not_found"),
            "empty.kscene": ("", "json.syntax"),
            "truncated.kscene": ('{"schema":"kengine.scene","schema_ver', "json.syntax"),
            "newer.kscene": (scene_text([], version=2), "scene.schema.unsupported_version"),
            "dup.kscene": (scene_text([obj(A), obj(A)]), "scene.duplicate_uuid"),
            "dangling.kscene": (scene_text([obj(A, parent=B)]), "scene.object.missing_parent"),
            "cycle.kscene": (scene_text([obj(A, parent=B), obj(B, parent=A)]), "scene.cycle"),
            "badkind.kscene": (scene_text([obj(A, kind="cylinder")]), "scene.object.unknown_kind"),
        }
        for name, (text, code) in cases.items():
            with self.subTest(case=name):
                path = os.path.join(self.dir, name) if text is None else self.write(name, text)
                r = run("scene", "validate", path)
                self.assertEqual(r.returncode, 1, msg=r.stdout + r.stderr)
                self.assertEqual(r.stdout, "")
                self.assertIn("error[%s]" % code, r.stderr)
                rj = run("scene", "validate", path, "--json")
                self.assertEqual(rj.returncode, 1)
                doc = json.loads(rj.stdout)
                self.assertIs(doc["ok"], False)
                self.assertEqual(doc["error"]["code"], code)

    def test_validate_directory_and_non_json_garbage(self):
        r = run("scene", "validate", self.dir)
        self.assertEqual(r.returncode, 1)
        self.assertIn("file.not_regular", r.stderr)
        path = self.write("bin.kscene", b"\x00\x01\xff\xfe not json", binary=True)
        r = run("scene", "validate", path)
        self.assertEqual(r.returncode, 1)
        self.assertIn("json.syntax", r.stderr)

    # ---- dump ----
    def test_dump_prints_an_indented_tree(self):
        r = run("scene", "dump", self.good())
        self.assertEqual(r.returncode, 0, msg=r.stderr)
        lines = r.stdout.splitlines()
        self.assertEqual(lines[0], "scene: 3 objects (kengine.scene v1)")
        self.assertTrue(lines[1].startswith("Root [empty] " + A))
        self.assertTrue(lines[2].startswith("  Child [cube] " + B))      # child indented under its parent
        self.assertIn("pos=(1, 2.5, -3)", lines[2])
        self.assertIn("scale=(2, 2, 2)", lines[2])
        self.assertTrue(lines[3].startswith("Other [sphere] " + C))      # later root is not indented
        self.assertEqual(len(lines), 4)

    def test_dump_json_is_canonical_and_stable(self):
        first = run("scene", "dump", self.good(), "--json")
        self.assertEqual(first.returncode, 0)
        doc = json.loads(first.stdout)
        self.assertEqual(doc["schema"], "kengine.scene")
        self.assertEqual([o["name"] for o in doc["objects"]], ["Root", "Child", "Other"])
        # Feeding the output back in validates and re-dumps to identical bytes.
        again = self.write("again.kscene", first.stdout)
        self.assertEqual(run("scene", "validate", again).returncode, 0)
        self.assertEqual(run("scene", "dump", again, "--json").stdout, first.stdout)

    def test_dump_escapes_control_characters_so_files_cannot_forge_output(self):
        evil = "ok\nFAKE LINE: all clear\x1b[31m\x07"
        path = self.write("evil.kscene", scene_text([obj(A, name=evil)]))
        r = run("scene", "dump", path)
        self.assertEqual(r.returncode, 0)
        self.assertEqual(len(r.stdout.splitlines()), 2)                  # header + exactly one object line
        self.assertNotIn("\x1b", r.stdout)
        self.assertNotIn("\x07", r.stdout)
        self.assertIn("\\n", r.stdout)                                    # shown escaped instead
        # Error messages that quote file content are escaped too.
        bad = self.write("evilkind.kscene", scene_text([obj(A, kind="x\\u001b[2Jy")]))
        r = run("scene", "validate", bad)
        self.assertEqual(r.returncode, 1)
        self.assertNotIn("\x1b", r.stderr)

    def test_dump_of_invalid_file_fails_like_validate(self):
        r = run("scene", "dump", self.write("x.kscene", "{"))
        self.assertEqual(r.returncode, 1)
        self.assertIn("error[json.syntax]", r.stderr)
        self.assertEqual(r.stdout, "")

    def test_deep_hierarchy_dumps_in_linear_time(self):
        n = 3000
        objs = [obj("%08x-0000-4000-8000-000000000000" % (i + 1), "n%d" % i, "empty",
                    None if i == 0 else "%08x-0000-4000-8000-000000000000" % i) for i in range(n)]
        path = self.write("deep.kscene", scene_text(objs))
        r = run("scene", "dump", path)
        self.assertEqual(r.returncode, 0, msg=r.stderr[:200])
        self.assertEqual(len(r.stdout.splitlines()), n + 1)
        self.assertTrue(r.stdout.splitlines()[-1].startswith(" " * (2 * (n - 1)) + "n%d [empty]" % (n - 1)))

    # ---- usage errors ----
    def test_scene_usage_errors_exit_2(self):
        good = self.good()
        cases = [
            ("scene",),
            ("scene", "frobnicate"),
            ("scene", "validate"),
            ("scene", "dump"),
            ("scene", "validate", good, good),
            ("scene", "validate", good, "--bogus"),
            ("scene", "dump", "--yaml", good),
        ]
        for args in cases:
            with self.subTest(args=args):
                r = run(*args)
                self.assertEqual(r.returncode, 2, msg=r.stdout + r.stderr)
                self.assertEqual(r.stdout, "")
                self.assertNotEqual(r.stderr, "")


def run_bytes(*args):
    """Like run() but takes raw byte arguments and returns undecoded output."""
    return subprocess.run([cli_path().encode(), *args], capture_output=True, timeout=60)


class LogFileTests(unittest.TestCase):
    """`--log-file <path>`: JSON Lines, append, flushed per line, untrusted strings sanitised."""

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.dir = self._tmp.name
        self.log = os.path.join(self.dir, "run.jsonl")

    def scene(self, name="s.kscene", objects=None):
        path = os.path.join(self.dir, name)
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(scene_text(objects if objects is not None else [obj(A, "Root", "empty"), obj(B, "Cube", "cube", A)]))
        return path

    def records(self, path=None):
        """Parse the log strictly: valid UTF-8, one JSON object per non-empty line, ending in a newline."""
        with open(path or self.log, "rb") as f:
            raw = f.read()
        text = raw.decode("utf-8")  # strict: raises on invalid UTF-8
        self.assertTrue(text.endswith("\n"), "log must end with a newline")
        lines = text.split("\n")[:-1]
        recs = []
        for line in lines:
            self.assertNotEqual(line, "")
            rec = json.loads(line)
            self.assertRegex(rec["ts"], r"^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ$")
            self.assertIn(rec["level"], ("info", "warn", "error"))
            recs.append(rec)
        return recs

    def events(self, path=None):
        return [r["event"] for r in self.records(path)]

    # ---- content ----
    def test_validate_success_logs_start_result_finish_and_leaves_output_unchanged(self):
        scene = self.scene()
        plain = run("scene", "validate", scene)
        logged = run("scene", "validate", scene, "--log-file", self.log)
        self.assertEqual((logged.returncode, logged.stdout, logged.stderr), (plain.returncode, plain.stdout, plain.stderr))
        recs = self.records()
        self.assertEqual([r["event"] for r in recs], ["start", "result", "finish"])
        self.assertEqual(recs[0]["args"], ["scene", "validate", scene])
        self.assertRegex(recs[0]["version"], r"^\d+\.\d+\.\d+")
        self.assertEqual((recs[1]["objects"], recs[1]["warnings"]), (2, 0))
        self.assertEqual((recs[2]["ok"], recs[2]["exit_code"], recs[2]["level"]), (True, 0, "info"))

    def test_json_mode_stdout_is_identical_with_and_without_a_log(self):
        scene = self.scene()
        a = run("scene", "validate", scene, "--json")
        b = run("scene", "validate", scene, "--json", "--log-file", self.log)
        self.assertEqual((a.returncode, a.stdout), (b.returncode, b.stdout))
        c = run("scene", "dump", scene, "--json")
        d = run("scene", "dump", scene, "--json", "--log-file", self.log)
        self.assertEqual((c.returncode, c.stdout), (d.returncode, d.stdout))

    def test_invalid_scene_logs_the_stable_error_code_and_exit_1(self):
        bad = os.path.join(self.dir, "bad.kscene")
        with open(bad, "w") as f:
            f.write('{"schema":"kengine.scene","schema_ver')
        r = run("scene", "validate", bad, "--json", "--log-file", self.log)
        self.assertEqual(r.returncode, 1)
        recs = self.records()
        self.assertEqual([x["event"] for x in recs], ["start", "error", "finish"])
        self.assertEqual(recs[1]["code"], json.loads(r.stdout)["error"]["code"])
        self.assertEqual(recs[1]["level"], "error")
        self.assertEqual((recs[2]["ok"], recs[2]["exit_code"], recs[2]["level"]), (False, 1, "error"))

    def test_warnings_are_logged_individually(self):
        path = self.scene("w.kscene", [obj(A, extra=',"postion":[1,2,3]')])
        r = run("scene", "validate", path, "--log-file", self.log)
        self.assertEqual(r.returncode, 0)
        recs = self.records()
        self.assertEqual([x["event"] for x in recs], ["start", "warning", "result", "finish"])
        self.assertEqual(recs[1]["level"], "warn")
        self.assertIn("postion", recs[1]["message"])
        self.assertEqual(recs[2]["warnings"], 1)

    def test_dump_and_doctor_are_logged_too(self):
        run("scene", "dump", self.scene(), "--log-file", self.log)
        self.assertEqual(self.events(), ["start", "result", "finish"])
        os.remove(self.log)
        r = run("doctor", "--log-file", self.log)
        self.assertEqual(r.returncode, 0, msg=r.stdout)
        recs = self.records()
        self.assertEqual([x["event"] for x in recs], ["start"] + ["check"] * 3 + ["finish"])
        self.assertTrue(all(x["ok"] and x["name"] and x["detail"] for x in recs[1:4]))

    def test_the_log_is_appended_not_overwritten(self):
        scene = self.scene()
        run("scene", "validate", scene, "--log-file", self.log)
        run("scene", "validate", scene, "--log-file", self.log)
        self.assertEqual(self.events(), ["start", "result", "finish"] * 2)

    def test_the_option_can_appear_anywhere_after_the_command(self):
        scene = self.scene()
        for args in (("scene", "--log-file", self.log, "validate", scene), ("scene", "validate", "--log-file", self.log, scene)):
            with self.subTest(args=args):
                if os.path.exists(self.log):
                    os.remove(self.log)
                self.assertEqual(run(*args).returncode, 0)
                self.assertEqual(self.events(), ["start", "result", "finish"])

    # ---- failure modes ----
    def test_usage_errors_are_logged_with_exit_code_2(self):
        r = run("scene", "bogus", "--log-file", self.log)
        self.assertEqual(r.returncode, 2)
        recs = self.records()
        self.assertEqual([x["event"] for x in recs], ["start", "finish"])
        self.assertEqual((recs[1]["exit_code"], recs[1]["ok"]), (2, False))

    def test_bad_log_option_usage(self):
        for args in (("doctor", "--log-file"), ("doctor", "--log-file", ""), ("doctor", "--log-file", "a", "--log-file", "b"),
                     ("--version", "--log-file", self.log)):
            with self.subTest(args=args):
                r = run(*args)
                self.assertEqual(r.returncode, 2, msg=r.stdout + r.stderr)
                self.assertEqual(r.stdout, "")
        self.assertFalse(os.path.exists(self.log))

    def test_unopenable_log_stops_the_command_before_it_runs(self):
        for bad in (self.dir, os.path.join(self.dir, "no", "such", "dir", "x.jsonl")):
            with self.subTest(path=bad):
                r = run("scene", "validate", self.scene(), "--log-file", bad)
                self.assertEqual(r.returncode, 1)
                self.assertEqual(r.stdout, "")             # the command did not run
                self.assertIn("cannot open log file", r.stderr)

    @unittest.skipUnless(os.path.exists("/dev/full"), "needs /dev/full (Linux)")
    def test_a_failing_log_write_turns_success_into_exit_1(self):
        r = run("scene", "validate", self.scene(), "--log-file", "/dev/full")
        self.assertIn("OK:", r.stdout)                      # the command itself still reported its result
        self.assertEqual(r.returncode, 1)
        self.assertIn("log", r.stderr)

    # ---- untrusted text ----
    @unittest.skipIf(os.name == "nt", "raw non-UTF-8 argv bytes are a POSIX thing")
    def test_invalid_utf8_arguments_never_make_the_log_invalid(self):
        bad_sequences = {
            "stray high bytes": b"\xff\xfe",
            "overlong 2-byte": b"\xc0\xaf",
            "overlong 3-byte": b"\xe0\x80\xaf",
            "encoded surrogate": b"\xed\xa0\x80",
            "beyond U+10FFFF": b"\xf4\x90\x80\x80",
            "truncated at the end": b"\xe2\x82",
            "truncated 4-byte at the end": b"\xf0\x9f\x98",
            "lone continuation byte": b"\x80",
        }
        for label, seq in bad_sequences.items():
            with self.subTest(label):
                if os.path.exists(self.log):
                    os.remove(self.log)
                path = os.fsencode(self.dir) + b"/a" + seq
                r = run_bytes(b"scene", b"validate", path, b"--log-file", os.fsencode(self.log))
                self.assertEqual(r.returncode, 1)                   # the file does not exist
                recs = self.records()                               # strict UTF-8 decode inside
                self.assertEqual([x["event"] for x in recs], ["start", "error", "finish"])
                self.assertIn("\ufffd", recs[0]["args"][2])

    @unittest.skipIf(os.name == "nt", "raw non-UTF-8 argv bytes are a POSIX thing")
    def test_valid_multibyte_text_is_preserved_exactly(self):
        text = "é€😀日本"   # 2, 3, 4 and 3-byte sequences
        path = os.fsencode(self.dir) + b"/" + text.encode("utf-8")
        r = run_bytes(b"scene", b"validate", path, b"--log-file", os.fsencode(self.log))
        self.assertEqual(r.returncode, 1)
        recs = self.records()
        self.assertTrue(recs[0]["args"][2].endswith("/" + text))
        self.assertNotIn("\ufffd", recs[0]["args"][2])

    def test_newlines_and_control_characters_in_names_cannot_forge_log_lines(self):
        evil = os.path.join(self.dir, "a\nb\x1b[31m\"x.kscene")
        r = run("scene", "validate", evil, "--log-file", self.log)
        self.assertEqual(r.returncode, 1)
        with open(self.log, "rb") as f:
            lines = f.read().split(b"\n")
        self.assertEqual(len(lines), 4)                      # start, error, finish + the empty tail
        self.assertEqual([json.loads(x)["event"] for x in lines[:3]], ["start", "error", "finish"])
        self.assertIn("a\nb\x1b[31m\"x.kscene", json.loads(lines[0])["args"][2])  # content preserved, only escaped


if __name__ == "__main__":
    unittest.main()
