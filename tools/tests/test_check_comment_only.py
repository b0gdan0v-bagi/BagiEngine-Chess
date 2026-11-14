"""tools/check_comment_only.py: a change of comments only passes, any other change fails. Needs git, no game.

    python -m unittest discover -s tools/tests -p "test_check_comment_only.py"
"""

import contextlib
import io
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import check_comment_only
from check_comment_only import cpp_tokens, first_cpp_difference, first_python_difference

GIT_ENV = ["-c", "user.name=t", "-c", "user.email=t@example.com", "-c", "commit.gpgsign=false"]


BACKSLASH = chr(92)


def texts(source: str) -> list[str]:
    return [token.text for token in cpp_tokens(source)]


class CppScannerTest(unittest.TestCase):
    def test_comments_are_dropped_and_whitespace_is_ignored(self):
        old = "int a = 1; // why\n/* block\n over lines */ int b;\n"
        self.assertEqual(texts(old), texts("int   a=1;\n\n\nint\nb ;"))

    def test_a_comment_between_tokens_is_a_space(self):
        self.assertEqual(texts("a/**/b"), ["a", "b"])

    def test_comment_marks_inside_a_string_are_code(self):
        self.assertEqual(texts('auto s = "// not /* a comment */";'), ["auto", "s", "=", '"// not /* a comment */"', ";"])

    def test_comment_marks_inside_a_raw_string_are_code(self):
        source = 'auto s = R"x(// a )" /* b */)x"; int y; // gone'
        self.assertEqual(texts(source), ["auto", "s", "=", 'R"x(// a )" /* b */)x"', ";", "int", "y", ";"])

    def test_a_quote_in_a_char_literal_does_not_open_a_string(self):
        self.assertEqual(texts("char q = '\"'; // c\nint z;"), ["char", "q", "=", "'\"'", ";", "int", "z", ";"])

    def test_a_digit_separator_is_no_char_literal(self):
        self.assertEqual(texts("int n = 1'000; // c\nint m;"), ["int", "n", "=", "1'000", ";", "int", "m", ";"])

    def test_a_backslash_continues_a_line_comment(self):
        self.assertEqual(texts("int a; // one \\\n still comment\nint b;"), ["int", "a", ";", "int", "b", ";"])

    def test_markers_are_code(self):
        self.assertIn("//-NoStd: engine-file-layer", texts("std::ifstream f; //-NoStd: engine-file-layer\n"))
        self.assertIn("// NOLINTNEXTLINE(x) [+1]", texts("// NOLINTNEXTLINE(x)\nint a;"))
        self.assertIn("// clang-format off", texts("// clang-format off\nint a;"))
        self.assertIn("/* clang-format on */", texts("/* clang-format on */ int a;"))

    def test_an_ordinary_comment_that_names_std_is_no_marker(self):
        self.assertEqual(texts("// std: reason\nint a;"), ["int", "a", ";"])

    def test_a_line_comment_ends_at_a_newline_after_any_backslash(self):
        # a backslash right before the newline is a splice, one or two of them alike (the splice is translation phase 2, before the comments)
        expected = ["int", "a", ";", "int", "b", ";"]
        self.assertEqual(texts("int a; // c " + BACKSLASH + "\nint hidden;\nint b;"), expected)
        self.assertEqual(texts("int a; // c " + BACKSLASH * 2 + "\nint hidden;\nint b;"), expected)
        self.assertEqual(texts("int a; // c " + BACKSLASH + "\n\nint b;"), expected)

    def test_a_backslash_in_the_middle_of_a_line_comment_ends_nothing(self):
        self.assertEqual(texts("int a; // c " + BACKSLASH + "n d\nint b;"), ["int", "a", ";", "int", "b", ";"])

    def test_a_spliced_line_comment_keeps_the_line_count(self):
        found = first_cpp_difference("// c " + BACKSLASH + "\nint a;\nint b;\n", "// c " + BACKSLASH + "\nint a;\nint c;\n")
        self.assertEqual(found[0], 3)

    def test_a_crlf_source_with_a_trailing_backslash(self):
        source = "int x; // c " + BACKSLASH + "\r\nint hidden;\r\nint b;\r\n"
        self.assertEqual(texts(source), ["int", "x", ";", "int", "b", ";"])

    def test_the_end_of_a_directive_is_a_token(self):
        self.assertNotEqual(texts("#define A 1\nint b;"), texts("#define A 1 int b;"))
        self.assertEqual(texts("#define A \\\n  1\nint b;"), texts("#define A 1\nint b;"))


class CppDifferenceTest(unittest.TestCase):
    def test_equal_after_a_comment_is_cut(self):
        self.assertIsNone(first_cpp_difference("// a\nint a; /* b */\n", "int a;\n"))

    def test_a_changed_token_is_reported_with_its_line_in_the_new_source(self):
        found = first_cpp_difference("int a;\nint b = 1;\n", "// c\n// d\nint a;\nint b = 2;\n")
        self.assertEqual(found[0], 4)
        self.assertIn("`2`", found[1])

    def test_a_renamed_identifier_fails(self):
        self.assertIsNotNone(first_cpp_difference("int count;", "int counter;"))

    def test_a_removed_line_of_code_fails(self):
        found = first_cpp_difference("int a;\nint b;\n", "int a;\n")
        self.assertIsNotNone(found)
        self.assertIn("missing", found[1])

    def test_an_added_line_of_code_fails(self):
        found = first_cpp_difference("int a;\n", "int a;\nint b;\n")
        self.assertEqual(found[0], 2)

    def test_a_changed_or_removed_marker_fails(self):
        self.assertIsNotNone(first_cpp_difference("f(); //-NoStd: a\n", "f(); //-NoStd: b\n"))
        self.assertIsNotNone(first_cpp_difference("f(); //-NoStd: a\n", "f();\n"))
        self.assertIsNotNone(first_cpp_difference("f(); // NOLINT\n", "f();\n"))
        self.assertIsNotNone(first_cpp_difference("// clang-format off\nint a;\n", "int a;\n"))

    def test_a_marker_may_be_rewrapped(self):
        self.assertIsNone(first_cpp_difference("f(); //-NoStd:  a\n", "f();   //-NoStd: a\n"))

    def test_a_line_between_a_lone_marker_and_its_code_fails(self):
        for marker in ("// NOLINTNEXTLINE(x)", "//-NoStd: why"):
            for between in ("// c\n", "\n", "/* c */\n"):
                found = first_cpp_difference(f"{marker}\nint a;\n", f"{marker}\n{between}int a;\n")
                self.assertIsNotNone(found, (marker, between))
                self.assertIn("the code moved from its marker", found[1])

    def test_a_line_taken_out_between_a_lone_marker_and_its_code_fails(self):
        self.assertIsNotNone(first_cpp_difference("// NOLINTNEXTLINE\n\nint a;\n", "// NOLINTNEXTLINE\nint a;\n"))

    def test_a_lone_marker_keeps_its_gap_when_comments_around_it_change(self):
        for marker in ("// NOLINTNEXTLINE(x)", "//-NoStd: why"):
            self.assertIsNone(first_cpp_difference(f"// why\n// more\n{marker}\nint a;\n", f"{marker}\nint a;\n"))
        self.assertIsNone(first_cpp_difference("// NOLINTNEXTLINE\n\nint a;\n", "// reworded\n// NOLINTNEXTLINE\n\nint a;\n"))

    def test_a_trailing_marker_has_no_gap(self):
        self.assertIsNone(first_cpp_difference("int a; // NOLINT\n// c\nint b;\n", "int a; // NOLINT\nint b;\n"))
        self.assertIsNone(first_cpp_difference("f(); //-NoStd: why\n\nint b;\n", "f(); //-NoStd: why\nint b;\n"))

    def test_a_marker_at_the_end_of_the_file_has_no_gap(self):
        self.assertIsNone(first_cpp_difference("int a;\n// NOLINTNEXTLINE\n", "int a;\n// NOLINTNEXTLINE\n\n"))

    def test_a_changed_string_fails(self):
        self.assertIsNotNone(first_cpp_difference('log("a");', 'log("b");'))


class PythonDifferenceTest(unittest.TestCase):
    def test_comments_and_docstrings_are_cut(self):
        old = '"""module"""\n\n# c\ndef f():\n    """doc"""\n    return 1  # why\n\n\nclass C:\n    """doc"""\n'
        new = "def f():\n    return 1\n\n\nclass C:\n    pass\n"
        self.assertIsNone(first_python_difference(old, new))

    def test_a_code_change_fails_with_its_line(self):
        found = first_python_difference("def f():\n    return 1\n", "def f():\n    # c\n    return 2\n")
        self.assertEqual(found[0], 3)

    def test_a_string_that_is_no_docstring_is_code(self):
        self.assertIsNotNone(first_python_difference("x = 1\n'a'\n", "x = 1\n"))

    def test_a_file_that_does_not_parse_fails(self):
        found = first_python_difference("x = 1\n", "x = (\n")
        self.assertIsNotNone(found)
        self.assertIn("does not parse", found[1])


class CommandTest(unittest.TestCase):
    """The command line against a temporary git repository."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.git("init", "-q")
        self.write("src/a.cpp", "int a;\n// why\nint b = 1;\n")
        self.write("src/b.py", '"""doc"""\nx = 1\n')
        self.write("notes.md", "one\n")
        self.git("add", "-A")
        self.git("commit", "-q", "-m", "base")
        self.base = self.git("rev-parse", "HEAD").strip()

    def git(self, *args: str) -> str:
        result = subprocess.run(["git", *GIT_ENV, *args], cwd=self.root, capture_output=True, text=True, check=True)
        return result.stdout

    def write(self, name: str, text: str) -> None:
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8", newline="\n")

    def run_check(self, *paths: str) -> tuple[int, str, str]:
        out, err = io.StringIO(), io.StringIO()
        argv = ["check_comment_only.py", "--base", self.base, "--root", str(self.root), *paths]
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            saved = sys.argv
            sys.argv = argv
            try:
                code = check_comment_only.main()
            finally:
                sys.argv = saved
        return code, out.getvalue(), err.getvalue()

    def test_nothing_changed_is_ok(self):
        self.assertEqual(self.run_check()[0], 0)

    def test_a_cut_comment_passes(self):
        self.write("src/a.cpp", "int a;\nint   b = 1;\n")
        self.write("src/b.py", "x = 1\n")
        code, out, _ = self.run_check("src")
        self.assertEqual(code, 0, out)

    def test_a_code_change_fails_and_prints_the_file_and_the_line(self):
        self.write("src/a.cpp", "int a;\n// c\nint b = 2;\n")
        code, out, _ = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn("src/a.cpp:3:", out)
        self.assertIn("`2`", out)

    def test_a_path_narrows_the_check(self):
        self.write("src/a.cpp", "int a;\nint b = 2;\n")
        self.assertEqual(self.run_check("src/b.py")[0], 0)
        self.assertEqual(self.run_check("src/a.cpp")[0], 1)
        self.assertEqual(self.run_check("src")[0], 1)

    def test_a_new_file_fails(self):
        self.write("src/c.cpp", "// only a comment\n")
        code, out, _ = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn("src/c.cpp: added", out)

    def test_a_deleted_file_fails(self):
        (self.root / "src/a.cpp").unlink()
        code, out, _ = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn("src/a.cpp: deleted", out)

    def test_a_changed_python_file_fails(self):
        self.write("src/b.py", "x = 2\n")
        code, out, _ = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn("src/b.py:1:", out)

    def test_a_comment_put_between_a_marker_and_its_code_fails_with_the_line_of_the_marker(self):
        self.write("src/a.cpp", "int a;\n// NOLINTNEXTLINE(x)\nint b = 1;\n")
        self.git("commit", "-q", "-am", "marker")
        self.base = self.git("rev-parse", "HEAD").strip()
        self.write("src/a.cpp", "int a;\n// NOLINTNEXTLINE(x)\n// why\nint b = 1;\n")
        code, out, _ = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn("src/a.cpp:2:", out)
        self.assertIn("the code moved from its marker", out)

    def test_other_files_are_not_compared(self):
        self.write("notes.md", "two\n")
        self.assertEqual(self.run_check()[0], 0)

    def test_an_unknown_revision_is_an_error(self):
        self.base = "no-such-revision"
        code, _, err = self.run_check()
        self.assertEqual(code, 2)
        self.assertIn("check_comment_only", err)


if __name__ == "__main__":
    unittest.main()
