#!/usr/bin/env python3
# /// script
# requires-python = ">=3.9"
# dependencies = ["lizard==1.26.0"]
# ///
"""Checks the structure limits of a repository: function size and file size.

Synced from the rules repository (`shrippen/regeln`, `tools/`) with `tools/sync-rules.sh`.
Do not edit this copy; change the source and sync.

    pipx run tools/structure-check.py            check, exit 1 on a violation
    pipx run tools/structure-check.py --update   rewrite structure.baseline from the code

What is over a limit today is listed in `structure.baseline` with its value. Such an
entry may stay as it is or shrink, never grow; code outside the list has to keep the
limits. Example line (tab separated): `src/app.rs	refresh	length=90`.
"""

import argparse
import collections
import enum
import fnmatch
import re
import subprocess
import sys
from pathlib import Path

import lizard

# Limits of one function. Example: a function of 95 lines breaks "length".
FUNCTION_LIMITS = {"ccn": 15, "length": 80, "nesting": 4}

# Limit of one file, in lines.
FILE_LIMITS = {"lines": 800}

LIMITS = {**FUNCTION_LIMITS, **FILE_LIMITS}

BASELINE_NAME = "structure.baseline"
FILE_ENTRY = "-"
EXCLUDE_KEY = "exclude"

# Files whose length counts. Functions are measured where lizard knows the language.
CODE_SUFFIXES = {
    ".c", ".cc", ".cjs", ".cpp", ".dart", ".gd", ".go", ".h", ".hpp", ".java", ".js", ".jsx",
    ".kt", ".kts", ".lua", ".mjs", ".php", ".py", ".qml", ".rs", ".sh", ".ts", ".tsx", ".twig",
}

# A file without an ending counts when it starts like a script. Example: "#!/bin/sh".
SHEBANG = b"#!"

# QML carries its logic as JavaScript; lizard reads it under that name.
READ_AS = {".qml": ".js"}

# lizard closes a nested block only at `}` or `;`, never at Lua's `end` or GDScript's
# dedent: the sixth function with one `if` reads as nesting 6, and the count runs on into
# the next file. Both are therefore read without the nesting measure.
NO_NESTING = {".lua", ".gd"}

# Kotlin: lizard reads on to the next `{` or `}` for a function without a block body.
# kotlin_view() rewrites those two kinds before lizard sees the code.
KOTLIN_SUFFIXES = {".kt", ".kts"}

# Kotlin tokens that continue an expression on the next line, and the ones that end a
# line unfinished. Example: "x\n    .map { it }" goes on; "a +\n b" goes on.
KOTLIN_NEXT = ("catch", "finally", "else", ".", "?", "&&", "||")
KOTLIN_OPEN_END = "=+-*/%&|,.:"

# A script without an ending is read as the language its first line names.
# Example: "#!/usr/bin/env python3" -> ".py".
INTERPRETERS = {"python": ".py"}

FOREIGN_DIRS = {"vendor", "node_modules", "third_party", "3rdparty"}

BASELINE_HEAD = """\
# What is over a structure limit today (tools/structure-check.py). An entry may shrink,
# never grow; a raised or new entry needs its reason in the PR. Rewrite with --update.
# Lines: exclude<TAB>glob | path<TAB>function or - for the file<TAB>metric=value ...
"""


class Mode(enum.Enum):
    CHECK = "check"
    UPDATE = "update"


def repo_root():
    out = subprocess.run(
        ["git", "rev-parse", "--show-toplevel"], capture_output=True, text=True, check=True
    )

    return Path(out.stdout.strip())


def tracked_files(root):
    out = subprocess.run(
        ["git", "ls-files", "-z"], cwd=root, capture_output=True, text=True, check=True
    )

    return [p for p in out.stdout.split("\0") if p]


def is_script(file):
    if not file.is_file():
        return False

    with file.open("rb") as handle:
        return handle.read(len(SHEBANG)) == SHEBANG


def is_measured(root, path, excludes):
    parts = Path(path)
    is_code = parts.suffix in CODE_SUFFIXES or (not parts.suffix and is_script(root / path))

    if not is_code:
        return False

    if FOREIGN_DIRS.intersection(parts.parts):
        return False

    return not any(fnmatch.fnmatch(path, glob) for glob in excludes)


def script_suffix(code):
    first_line = code.split("\n", 1)[0]

    for interpreter, suffix in INTERPRETERS.items():
        if interpreter in first_line:
            return suffix

    return ""


def string_end(code, i):
    """Returns the index after the string that opens at code[i], templates included.

    Example: `"a ${if (x) "}" else "b"}"` is one string.
    """
    quote = '"""' if code.startswith('"""', i) else code[i]
    j = i + len(quote)

    while j < len(code) and not code.startswith(quote, j):
        if code[j] == "\n" and len(quote) == 1:
            return j

        if code.startswith("${", j) and quote != "'":
            j = template_end(code, j + 2)
            continue

        # Raw strings ("""...""") know no escapes.
        j += 2 if code[j] == "\\" and len(quote) == 1 else 1

    return min(j + len(quote), len(code))


def template_end(code, i):
    """Returns the index after the `${ ... }` whose content starts at code[i]."""
    depth = 1

    while i < len(code) and depth:
        if code[i] in "\"'":
            i = string_end(code, i)
            continue

        depth += code[i] == "{"
        depth -= code[i] == "}"
        i += 1

    return i


def kotlin_mask(code):
    """Returns (masked code, spans of strings with a template).

    Masked: comments blanked, strings and chars filled, same length and lines. A string
    stays a token, so `else "0"` still has a body.
    """
    masked = list(code)
    templated = []
    i = 0

    while i < len(code):
        end = None

        if code.startswith("//", i):
            end = code.find("\n", i)
        elif code.startswith("/*", i):
            end = code.find("*/", i) + 2
        elif code[i] in "\"'":
            end = string_end(code, i)

        if end is None:
            i += 1
            continue

        end = len(code) if end <= i else end
        fill = " " if code[i] == "/" else "_"

        for j in range(i, end):
            masked[j] = masked[j] if masked[j] == "\n" else fill

        if code[i] != "/" and "${" in code[i:end]:
            templated.append((i, end))

        i = end

    return "".join(masked), templated


def match_close(text, start):
    """Returns the index after the bracket group that opens at text[start]."""
    depth = 0

    for i in range(start, len(text)):
        depth += text[i] in "([{"
        depth -= text[i] in ")]}"

        if depth == 0:
            return i + 1

    return len(text)


def kotlin_body(text, pos):
    """Returns (kind, index) for the function whose parameter list ends before pos.

    kind is "block" (index of its `{`), "expr" (index of its `=`) or "none".
    """
    depth = 0
    i = pos

    while i < len(text):
        char = text[i]

        if text.startswith("->", i):
            i += 2
            continue

        if depth == 0 and char == "{":
            return "block", i

        if depth == 0 and char == "=":
            return "expr", i

        depth += char in "(<["
        depth -= char in ")>]"

        if depth == 0 and char == "\n":
            after = text[i:].lstrip()

            if not after.startswith(("{", "=")) or after.startswith("=="):
                return "none", i

        i += 1

    return "none", len(text)


def goes_on(text, last, newline):
    """Tells whether the expression ending at `last` continues after the line break."""
    after = text[newline:].lstrip()

    postfix = text[last - 2 : last] in ("++", "--")

    return bool(after) and ((text[last - 1] in KOTLIN_OPEN_END and not postfix) or after.startswith(KOTLIN_NEXT))


def expr_end(text, start, stops=()):
    """Returns the index after the expression that starts at text[start].

    A word in `stops` at the top level ends it too. Example: "else" in `if (a) b else c`.
    """
    depth = 0
    last = start

    for i in range(start, len(text)):
        char = text[i]

        if depth == 0 and i > start and any(re.match(rf"\b{w}\b", text[i:]) for w in stops):
            return last

        depth += char in "([{"
        depth -= char in ")]}"

        if depth < 0 or (depth == 0 and char == ";"):
            return last

        if not char.isspace():
            last = i + 1

        if depth or char != "\n":
            continue

        if not goes_on(text, last, i):
            return last

    return last


def kotlin_ends(text):
    """Returns the index after each braceless `if`, `else`, `for` and `while` body."""
    ends = []

    for found in re.finditer(r"\b(if|for|while)\s*\(|\belse\b(?!\s*(\{|if\b))", text):
        at = found.end()

        if found.group(1):
            at = match_close(text, at - 1)

        # `} while (x)` closes a do-while: no body follows.
        if found.group(1) == "while" and re.search(r"\}[ \t]*$", text[: found.start()]):
            ends.append(at)
            continue

        body = at + len(text[at:]) - len(text[at:].lstrip())

        if not text.startswith("{", body):
            ends.append(expr_end(text, body, stops=("else",)))

    return ends


def kotlin_arrows(text):
    """Returns the index of each `->` that is not a branch of a `when`."""
    branch_braces = set()

    for found in re.finditer(r"\bwhen\b\s*", text):
        at = found.end()
        at = match_close(text, at) if text.startswith("(", at) else at
        at += len(text[at:]) - len(text[at:].lstrip())

        if text.startswith("{", at):
            branch_braces.add(at)

    arrows, braces = [], []

    for i, char in enumerate(text):
        if char == "{":
            braces.append(i in branch_braces)
        elif char == "}" and braces:
            braces.pop()
        elif text.startswith("->", i) and not (braces and braces[-1]):
            arrows.append(i)

    return arrows


def kotlin_view(code):
    """Returns Kotlin code in which lizard measures every function by its own lines.

    A function without a block ends where lizard misses it, so the rest of the file
    counts to it: `fun f(x: Int) = x + 1` becomes `fun f(x: Int) { x + 1 }`, and
    `fun f(x: Int): Int` (interface, abstract) loses its `fun`. A braceless `if` body
    never closes its nesting, so `;` ends it. A lambda's `->` makes lizard open a function
    that swallows the rest, so it is blanked. An identifier `with` reads as Python's `with`
    and opens a nesting that never closes, so it is renamed. Lines stay the same.
    """
    text, templated = kotlin_mask(code)

    # lizard ends a string at the first inner quote of a template; give it the filled one.
    for begin, end in reversed(templated):
        code = code[:begin] + text[begin:end] + code[end:]

    edits = [(end, end, ";") for end in kotlin_ends(text)]
    edits += [(at, at + 2, "  ") for at in kotlin_arrows(text)]
    edits += [(m.start(), m.end(), "w1th") for m in re.finditer(r"\bwith\b(?!\s*\()", text)]

    for found in re.finditer(r"\bfun\b(?!\s+interface)", text):
        start = text.find("(", found.end())
        kind, at = kotlin_body(text, match_close(text, start))

        if kind == "none":
            edits.append((found.start(), found.end(), " " * 3))
        elif kind == "expr":
            edits.append((at, at + 1, "{"))
            edits.append((expr_end(text, at + 1),) * 2 + ("}",))

    for begin, end, new in sorted(edits, reverse=True):
        code = code[:begin] + new + code[end:]

    return code


def function_values(path, code, analyzers):
    """Yields (function name, line, {metric: value}) for each function lizard finds."""
    ending = Path(path).suffix
    suffix = ending or script_suffix(code)
    # lizard picks its reader by the name: QML and scripts without an ending get one added.
    name = path + (READ_AS.get(suffix, "") if ending else suffix)
    nested = suffix not in NO_NESTING
    code = kotlin_view(code) if suffix in KOTLIN_SUFFIXES else code
    analyzer = analyzers[nested]

    if lizard.get_reader_for(name) is None:
        return

    for func in analyzer.analyze_source_code(name, code).function_list:
        values = {"ccn": func.cyclomatic_complexity, "length": func.length}

        if nested:
            values["nesting"] = func.max_nested_structures

        yield func.name, func.start_line, values


def measure(root, paths):
    """Returns {(path, name): (line, {metric: value})}, only values over their limit.

    Functions of one name in a file are numbered in file order, so each needs its own
    baseline entry. Example: "(anonymous)", "(anonymous)#2".
    """
    # One analyzer with the nesting measure ("ns"), one without it.
    analyzers = {
        True: lizard.FileAnalyzer(lizard.get_extensions(["ns"])),
        False: lizard.FileAnalyzer(lizard.get_extensions([])),
    }
    over = {}

    for path in paths:
        code = (root / path).read_text(encoding="utf-8", errors="replace")
        found = [(FILE_ENTRY, 1, {"lines": code.count("\n")})]
        found.extend(function_values(path, code, analyzers))

        seen = collections.Counter()

        for name, line, values in found:
            excess = {m: v for m, v in values.items() if v > LIMITS[m]}

            if not excess:
                continue

            seen[name] += 1
            key = name if seen[name] == 1 else f"{name}#{seen[name]}"
            over[(path, key)] = (line, excess)

    return over


def read_baseline(file):
    """Returns (exclude globs, {(path, name): {metric: value}})."""
    excludes, allowed = [], {}

    if not file.exists():
        return excludes, allowed

    for number, line in enumerate(file.read_text(encoding="utf-8").splitlines(), 1):
        if not line or line.startswith("#"):
            continue

        fields = line.split("\t")

        if fields[0] == EXCLUDE_KEY and len(fields) == 2:
            excludes.append(fields[1])
            continue

        if len(fields) != 3:
            sys.exit(f"{file.name}:{number}: expected 3 tab separated fields")

        pairs = (pair.split("=") for pair in fields[2].split())
        allowed[(fields[0], fields[1])] = {metric: int(value) for metric, value in pairs}

    return excludes, allowed


def write_baseline(file, excludes, over):
    lines = [f"{EXCLUDE_KEY}\t{glob}" for glob in excludes]

    for (path, name), (_, values) in sorted(over.items()):
        pairs = " ".join(f"{metric}={value}" for metric, value in sorted(values.items()))
        lines.append(f"{path}\t{name}\t{pairs}")

    file.write_text(BASELINE_HEAD + "\n".join(lines) + "\n", encoding="utf-8")


def violations(over, allowed):
    """Yields a message for each value above its limit and above its baseline entry."""
    for (path, name), (line, values) in sorted(over.items()):
        known = allowed.get((path, name), {})
        what = "file" if name == FILE_ENTRY else name

        for metric, value in sorted(values.items()):
            if metric in known and value <= known[metric]:
                continue

            bound = f"baseline {known[metric]}" if metric in known else f"limit {LIMITS[metric]}"
            yield f"{path}:{line}: {what}: {metric} {value} > {bound}"


def shrinkable(over, allowed):
    """Counts baseline values the code no longer needs (fixed, smaller or gone)."""
    count = 0

    for key, known in allowed.items():
        current = over.get(key, (0, {}))[1]
        count += sum(1 for metric, value in known.items() if current.get(metric, 0) < value)

    return count


def run(mode):
    root = repo_root()
    file = root / BASELINE_NAME
    excludes, allowed = read_baseline(file)

    paths = [p for p in tracked_files(root) if is_measured(root, p, excludes) and (root / p).is_file()]
    over = measure(root, paths)

    if mode is Mode.UPDATE:
        write_baseline(file, excludes, over)
        print(f"structure-check: {BASELINE_NAME} written, {len(over)} entries")
        return 0

    failed = list(violations(over, allowed))

    for message in failed:
        print(message)

    stale = shrinkable(over, allowed)

    if stale:
        print(f"structure-check: {stale} baseline values can shrink (--update)")

    print(f"structure-check: {len(paths)} files, {len(failed)} violations, {len(allowed)} in baseline")

    return 1 if failed else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--update", action="store_const", const=Mode.UPDATE, default=Mode.CHECK, dest="mode")

    sys.exit(run(parser.parse_args().mode))


if __name__ == "__main__":
    main()
