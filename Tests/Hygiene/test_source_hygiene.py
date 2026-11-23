"""Source hygiene checks that GCC cannot do for us.

Why this exists: MSVC 19.44 (Visual Studio 17.14, the GitHub windows-2022 runner) rejects a
universal-character-name for a lone surrogate (\\ud83d) even inside a raw string literal, where the
standard says it must be left alone; GCC and newer MSVC accept it. A test that spells JSON escapes
that way built fine on Linux and on the newest MSVC and broke CI. The rule: C++ source never contains
a real backslash-u / backslash-U escape sequence. Write `\\\\u` in an ordinary string literal instead.

A backslash-u is "real" when the run of backslashes before the `u` has odd length (one backslash, or
three...). An even run (`\\\\u`) is an escaped backslash followed by a letter and is fine.
"""
import os
import re
import unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SOURCE_DIRS = ("Engine", "Editor", "Tools", "Tests")
EXTENSIONS = (".cpp", ".h", ".hpp", ".cc")
ESCAPE = re.compile(r"(\\+)[uU][0-9a-fA-F]?")


def find_ucn_like_escapes(text):
    """Returns [(line_number, line)] for every real backslash-u / backslash-U in `text`."""
    hits = []
    for number, line in enumerate(text.split("\n"), start=1):
        for m in ESCAPE.finditer(line):
            if len(m.group(1)) % 2 == 1:
                hits.append((number, line.strip()))
                break
    return hits


class LintSelfTests(unittest.TestCase):
    """The lint must be able to fail: these are the shapes that broke CI and the shapes that are fine."""

    def test_flags_a_surrogate_escape_in_a_raw_string(self):
        self.assertTrue(find_ucn_like_escapes('KCHECK(!parses(R"("\\ud83d")"));'))

    def test_flags_upper_case_u_and_short_forms_and_comments(self):
        self.assertTrue(find_ucn_like_escapes('const char* s = "\\U0001F600";'))
        self.assertTrue(find_ucn_like_escapes('R"(\\u12)"'))
        self.assertTrue(find_ucn_like_escapes("// \\u escapes are decoded"))

    def test_flags_an_odd_run_of_backslashes(self):
        self.assertTrue(find_ucn_like_escapes('"\\\\\\ud83d"'))        # three backslashes then u

    def test_accepts_an_escaped_backslash_followed_by_u(self):
        self.assertFalse(find_ucn_like_escapes('Json::parse("\\"\\\\ud83d\\"")'))      # \\ud83d
        self.assertFalse(find_ucn_like_escapes('{"\\\\u", "\\\\ud800"}'))
        self.assertFalse(find_ucn_like_escapes('"\\\\\\\\u0041"'))                      # four backslashes

    def test_accepts_ordinary_text_with_the_letter_u(self):
        self.assertFalse(find_ucn_like_escapes('std::string name = "unused";  // use'))
        self.assertFalse(find_ucn_like_escapes('printf("%s\\n", "u");'))


class SourceTreeTests(unittest.TestCase):
    def test_no_cpp_source_contains_a_real_backslash_u_escape(self):
        scanned = 0
        problems = []
        for d in SOURCE_DIRS:
            for dirpath, _dirs, files in os.walk(os.path.join(ROOT, d)):
                for name in files:
                    if not name.endswith(EXTENSIONS):
                        continue
                    path = os.path.join(dirpath, name)
                    with open(path, encoding="utf-8") as f:
                        for number, line in find_ucn_like_escapes(f.read()):
                            problems.append(f"{os.path.relpath(path, ROOT)}:{number}: {line}")
                    scanned += 1
        self.assertGreater(scanned, 30, "the scan must really see the source tree")
        self.assertEqual(problems, [], "write \\\\u (escaped backslash) in an ordinary string literal instead")


if __name__ == "__main__":
    unittest.main()
