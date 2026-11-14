#!/usr/bin/env python3
"""Check that a change touched comments only.

    python tools/check_comment_only.py --base <rev> [--root <dir>] [paths...]

Each changed .h .hpp .inl .cpp .py file is compared between <rev> and the working tree, without comments and with the
whitespace ignored; exit 0 when every file is equal, 1 otherwise (the file, the line in the new file and the first
differing token are printed), 2 when git cannot answer. A file added or deleted since <rev> is a difference.

C++ files are compared token by token. Comments are dropped, except the markers that tools read: `//-NoStd:`, `NOLINT*`,
`clang-format off` / `on`, which stay in the compared text, so changing or deleting one fails. A `//-NoStd:` or
`NOLINTNEXTLINE` marker alone on its line also keeps the number of lines down to its code, so a comment or a blank line put
between them fails. Comments and the insides of string, character and raw string literals are told apart by the scanner.
Python files are compared by their syntax tree without docstrings (comments are not in it). XML, CMake and Markdown files
are not compared.

Without `paths` the files are the ones `git diff <rev>` lists (and the untracked ones); a path (file or folder, relative to
--root) narrows them. Standard library only.
"""

from __future__ import annotations

import argparse
import ast
import re
import subprocess
import sys
from pathlib import Path
from typing import Iterator, NamedTuple

CPP_SUFFIXES = {".h", ".hpp", ".inl", ".cpp"}
PYTHON_SUFFIXES = {".py"}
SUFFIXES = CPP_SUFFIXES | PYTHON_SUFFIXES

# A comment that is code to the check: a marker a tool reads. The text of the whole comment is compared.
MARKER = re.compile(r"^//-NoStd:|NOLINT|clang-format\s+(?:off|on)\b")

# A marker that covers the next line when it stands alone on its line. The number of lines between it and its code is part
# of the compared text (`[+1]`: the code is on the next line), so a comment or a blank line put between them, or taken out,
# is seen.
NEXT_LINE_MARKER = re.compile(r"^//-NoStd:|NOLINTNEXTLINE")
GAP = re.compile(r" \[\+(\d+)\]$")

# A line comment goes on past a newline only when a backslash comes right before it. A backslash, spaces and a newline (which GCC
# and Clang splice with a warning) end the comment here, so the next line is read as code: the check can fail on a line the
# compiler hides, never pass a line it sees. Keep such a line out of the files under the check.
# One alternative per kind, leftmost wins (the same shape as `mask` of engine/CI/scripts/nostd.py, which blanks instead of
# splitting). A raw string comes before a plain one, and both before an identifier, so that `u8"x"` and `R"x(..)x"` are
# literals; a number is consumed whole so that the `'` of `1'000` is no character literal.
TOKEN = re.compile(
    r"""
      (?P<line>//(?:[^\n]|(?<=\\)\n)*)
    | (?P<block>/\*.*?(?:\*/|\Z))
    | (?P<raw>(?:u8|u|U|L)?R"(?P<delim>[^()\\\s"]{0,16})\(.*?\)(?P=delim)")
    | (?P<str>(?:u8|u|U|L)?"(?:[^"\\\n]|\\.)*")
    | (?P<chr>(?:u8|u|U|L)?'(?:[^'\\\n]|\\.)*')
    | (?P<ident>[A-Za-z_]\w*)
    | (?P<num>\.?\d(?:[\w']|(?<=[eEpP])[+-]|\.)*)
    | (?P<cont>\\\n)
    | (?P<newline>\n)
    | (?P<space>[^\S\n]+)
    | (?P<punct>.)
    """,
    re.VERBOSE | re.DOTALL,
)

NEWLINE_TOKEN = "\n"  # the end of a preprocessor line: a directive ends where its line does


class Token(NamedTuple):
    text: str
    line: int


def cpp_tokens(source: str) -> list[Token]:
    """The tokens of a C++ source without comments (markers excepted) and without whitespace. The end of a preprocessor
    directive is a token of its own, so that joining a `#define` with the next line is seen."""
    source = source.replace("\r\n", "\n").replace("\r", "\n")
    tokens: list[Token] = []
    line = 1
    at_line_start = True
    in_directive = False
    lone_marker: tuple[int, int] | None = None  # (index in tokens, the line it ends on) of a marker waiting for its code

    def add(text: str, text_line: int) -> None:
        nonlocal lone_marker
        if lone_marker is not None:
            index, end_line = lone_marker
            tokens[index] = Token(f"{tokens[index].text} [+{text_line - end_line}]", tokens[index].line)
            lone_marker = None
        tokens.append(Token(text, text_line))

    for match in TOKEN.finditer(source):
        kind = match.lastgroup
        text = match.group(0)
        start_line = line
        line += text.count("\n")
        if kind in ("line", "block"):
            if MARKER.search(text):
                alone = at_line_start and NEXT_LINE_MARKER.search(text) is not None
                add(" ".join(text.split()), start_line)
                if alone:
                    lone_marker = (len(tokens) - 1, line)
                at_line_start = False
            continue
        if kind in ("space", "cont"):
            continue
        if kind == "newline":
            if in_directive:
                add(NEWLINE_TOKEN, start_line)
                in_directive = False
            at_line_start = True
            continue
        if kind == "punct" and text == "#" and at_line_start:
            in_directive = True
        at_line_start = False
        add(text, start_line)
    return tokens


def first_cpp_difference(old: str, new: str) -> tuple[int, str] | None:
    """(line in the new source, what differs) of the first difference of the two sources, or None when they are equal."""
    old_tokens = cpp_tokens(old)
    new_tokens = cpp_tokens(new)
    for index in range(min(len(old_tokens), len(new_tokens))):
        if old_tokens[index].text != new_tokens[index].text:
            old_text, new_text = old_tokens[index].text, new_tokens[index].text
            if GAP.sub("", old_text) == GAP.sub("", new_text):
                marker = GAP.sub("", new_text)
                old_gap, new_gap = GAP.search(old_text), GAP.search(new_text)
                if old_gap and new_gap:
                    return new_tokens[index].line, (
                        f"the code moved from its marker `{marker}` (the next token is {new_gap.group(1)} lines below it, was {old_gap.group(1)})"
                    )
                # Only one side has a gap: the marker is alone on its line with code after it in one text and not in the other
                if old_gap:
                    what = "has no code after it now" if index == len(new_tokens) - 1 else "is no longer alone on its line with code after it"
                else:
                    what = "is alone on its line with code after it now"
                return new_tokens[index].line, f"the marker `{marker}` {what}"
            return new_tokens[index].line, f"{describe(new_text)} instead of {describe(old_text)}"
    if len(new_tokens) > len(old_tokens):
        extra = new_tokens[len(old_tokens)]
        return extra.line, f"extra {describe(extra.text)}"
    if len(old_tokens) > len(new_tokens):
        missing = old_tokens[len(new_tokens)]
        last_line = new_tokens[-1].line if new_tokens else 1
        return last_line, f"missing {describe(missing.text)} of line {missing.line} of the base"
    return None


def describe(token: str) -> str:
    return "the end of a preprocessor line" if token == NEWLINE_TOKEN else f"`{token}`"


def strip_docstrings(tree: ast.AST) -> None:
    for node in ast.walk(tree):
        if isinstance(node, (ast.Module, ast.ClassDef, ast.FunctionDef, ast.AsyncFunctionDef)):
            body = node.body
            if body and isinstance(body[0], ast.Expr) and isinstance(body[0].value, ast.Constant) and isinstance(body[0].value.value, str):
                del body[0]
                if not body:
                    body.append(ast.Pass())


def first_node_difference(old: object, new: object, line: int) -> tuple[int, str] | None:
    """The first difference of two syntax trees: the line of the node in the new tree (or of its parent), what differs."""
    if isinstance(new, ast.AST):
        line = getattr(new, "lineno", line)
        if type(old) is not type(new):
            return line, f"`{type(new).__name__}` instead of `{type(old).__name__}`"
        for (name, old_value), (_, new_value) in zip(ast.iter_fields(old), ast.iter_fields(new)):
            found = first_node_difference(old_value, new_value, line)
            if found:
                return found
        return None
    if isinstance(new, list) and isinstance(old, list):
        for old_item, new_item in zip(old, new):
            found = first_node_difference(old_item, new_item, line)
            if found:
                return found
        if len(old) != len(new):
            longer = new if len(new) > len(old) else old
            item = longer[min(len(old), len(new))]
            where = getattr(item, "lineno", line) if longer is new else line
            return where, ("extra" if longer is new else "missing") + f" `{type(item).__name__}`"
        return None
    if old != new or type(old) is not type(new):
        return line, f"`{new!r}` instead of `{old!r}`"
    return None


def first_python_difference(old: str, new: str) -> tuple[int, str] | None:
    try:
        old_tree = ast.parse(old)
    except SyntaxError as error:
        return 1, f"the base does not parse ({error.msg})"
    try:
        new_tree = ast.parse(new)
    except SyntaxError as error:
        return error.lineno or 1, f"does not parse ({error.msg})"
    strip_docstrings(old_tree)
    strip_docstrings(new_tree)
    return first_node_difference(old_tree, new_tree, 1)


class GitError(Exception):
    pass


def git(root: Path, *args: str) -> bytes:
    result = subprocess.run(["git", *args], cwd=root, capture_output=True)
    if result.returncode != 0:
        raise GitError(result.stderr.decode("utf-8", "replace").strip() or f"git {' '.join(args)} failed")
    return result.stdout


def read_base(root: Path, base: str, path: str) -> str | None:
    """The file at <base>, or None when it did not exist there."""
    result = subprocess.run(["git", "show", f"{base}:./{path}"], cwd=root, capture_output=True)
    if result.returncode != 0:
        return None
    return result.stdout.decode("utf-8", "replace")


def changed_files(root: Path, base: str, paths: list[str]) -> list[str]:
    pathspec = ["--", *paths] if paths else []
    git(root, "rev-parse", "--verify", f"{base}^{{commit}}")
    listed = git(root, "diff", "--name-only", "--no-renames", "--relative", base, *pathspec).decode("utf-8", "replace").split("\n")
    untracked = git(root, "ls-files", "--others", "--exclude-standard", *pathspec).decode("utf-8", "replace").split("\n")
    return sorted({name for name in listed + untracked if name and Path(name).suffix in SUFFIXES})


def check_file(root: Path, base: str, path: str) -> Iterator[str]:
    old = read_base(root, base, path)
    file = root / path
    new = file.read_text(encoding="utf-8", errors="replace") if file.is_file() else None
    if old is None and new is None:
        return
    if old is None:
        yield f"{path}: added since {base}"
        return
    if new is None:
        yield f"{path}: deleted since {base}"
        return
    difference = (first_python_difference if Path(path).suffix in PYTHON_SUFFIXES else first_cpp_difference)(old, new)
    if difference:
        line, what = difference
        yield f"{path}:{line}: not a comment change: {what}"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--base", required=True, help="the git revision to compare with")
    parser.add_argument("--root", default=".", help="the repository (or a folder in it); git runs here (default: .)")
    parser.add_argument("paths", nargs="*", help="files or folders, relative to --root, that narrow the check")
    args = parser.parse_args()
    root = Path(args.root)
    try:
        files = changed_files(root, args.base, args.paths)
    except GitError as error:
        print(f"check_comment_only: {error}", file=sys.stderr)
        return 2
    failures = [line for path in files for line in check_file(root, args.base, path)]
    for line in failures:
        print(line)
    if failures:
        print(f"check_comment_only: {len(failures)} of {len(files)} files changed more than comments", file=sys.stderr)
        return 1
    print(f"check_comment_only: {len(files)} files, comments only")
    return 0


if __name__ == "__main__":
    sys.exit(main())
