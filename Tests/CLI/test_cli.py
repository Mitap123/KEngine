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


if __name__ == "__main__":
    unittest.main()
