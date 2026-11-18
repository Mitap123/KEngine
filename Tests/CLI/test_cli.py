"""CLI tests for the `k` executable.

Runs the real binary and asserts on stdout, stderr and exit codes. The binary
path comes from $KENGINE_CLI (set by `make cli-test`); a missing or
non-executable binary is a hard failure, never a skip.
"""
import json
import os
import re
import subprocess
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


if __name__ == "__main__":
    unittest.main()
