#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
"""Make QML i18n() calls visible to lupdate.

lupdate's QML parser only knows qsTr()/qsTranslate().  Our QML calls
i18n()/i18nc()/i18np()/i18ncp(), which PhosphorLocalizedContext forwards to
QCoreApplication::translate() with "plasmazones" as the context and, for the
"c" variants, the first argument as the disambiguation.  lupdate therefore
extracted nothing at all from QML: every string in the QML UI was untranslatable.

-tr-function-alias cannot express that shape.  Aliasing qsTr makes the enclosing
QML component the context instead of "plasmazones", and aliasing qsTranslate
turns i18nc's "@info"-style disambiguation into a context of its own.

So instead we transcribe each call into a C++ stub that lupdate already reads
correctly, and hand it those stubs alongside the real sources:

    i18n("t")                -> PhosphorI18n::tr("t")
    i18nc("c", "t")          -> PhosphorI18n::tr("t", "c")
    i18np("s", "p", n)       -> PhosphorI18n::tr("s", nullptr, n)
    i18ncp("c", "s", "p", n) -> PhosphorI18n::tr("s", "c", n)

Qt derives plural forms from the target language, so the English plural is not
part of the message; the singular is the source text and n makes it a numerus
entry.  That mirrors what i18np() does at runtime.

One stub per QML file, padded so each tr() lands on the line its i18n() call
occupies in the .qml.  Locations in the .ts then read
".qml-stubs/src/.../Foo.qml.cpp:42", which points a translator at the right
line of the right file.  Stub paths are relative to the source root so the
generated .ts is identical on every machine.

Calls whose text argument is not a literal cannot be extracted; they are
reported on stderr and counted, because each one is a string that silently
stays English.
"""

import argparse
import os
import re
import sys

CALL_RE = re.compile(r"\bi18n(cp|np|c)?\s*\(")

# A JS string literal, single or double quoted, with backslash escapes.
STRING_RE = re.compile(r"""\s*(?:"((?:[^"\\]|\\.)*)"|'((?:[^'\\]|\\.)*)')\s*""")


def strip_comments(text):
    """Blank out // and /* */ comments so a commented-out call is not extracted.

    String literals are preserved, because they hold the messages; a // or /*
    inside one must not start a comment, so scan rather than split.  Comment
    bytes become spaces and newlines are kept, so line numbers still line up
    with the original file.
    """
    out = []
    quote = None
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if quote:
            out.append(c)
            if c == "\\" and i + 1 < n:
                out.append(text[i + 1])
                i += 2
                continue
            if c == quote:
                quote = None
            i += 1
        elif c in "\"'":
            quote = c
            out.append(c)
            i += 1
        elif c == "/" and i + 1 < n and text[i + 1] == "/":
            while i < n and text[i] != "\n":
                out.append(" ")
                i += 1
        elif c == "/" and i + 1 < n and text[i + 1] == "*":
            end = text.find("*/", i + 2)
            end = n if end < 0 else end + 2
            for j in range(i, end):
                out.append("\n" if text[j] == "\n" else " ")
            i = end
        else:
            out.append(c)
            i += 1
    return "".join(out)


def read_args(text, pos, want):
    """Read the first `want` string-literal arguments starting at pos.

    Only the leading message arguments have to be literals.  Everything after
    them is a %1 substitution value and is usually an expression, so reading
    stops as soon as enough literals are in hand.  Adjacent literals joined by
    + are concatenated into one argument, which is how the longer messages are
    wrapped in the QML.
    """
    args = []
    while len(args) < want:
        m = STRING_RE.match(text, pos)
        if not m:
            return args
        parts = [m.group(1) if m.group(1) is not None else m.group(2)]
        pos = m.end()
        # Fold "a" + "b" + "c" into a single argument.
        while pos < len(text) and text[pos] == "+":
            m2 = STRING_RE.match(text, pos + 1)
            if not m2:
                break
            parts.append(m2.group(1) if m2.group(1) is not None else m2.group(2))
            pos = m2.end()
        args.append("".join(parts))
        if len(args) < want:
            if pos >= len(text) or text[pos] != ",":
                return args
            pos += 1
    return args


# How many leading literals each variant needs, and how to lay them out as
# tr(text, comment) / tr(singular, comment, n).
NEEDED = {"i18n": 1, "i18nc": 2, "i18np": 2, "i18ncp": 3}


def scan(source):
    """Yield (line_number, variant, [literal args], complete) for every i18n call.

    Calls are matched against the whole file rather than line by line so a call
    whose arguments wrap across lines is still read in full.
    """
    cleaned = strip_comments(source)
    for m in CALL_RE.finditer(cleaned):
        variant = "i18n" + (m.group(1) or "")
        want = NEEDED[variant]
        args = read_args(cleaned, m.end(), want)
        line = cleaned.count("\n", 0, m.start()) + 1
        yield line, variant, args, len(args) == want


def cxx_escape(s):
    # The literal is already JS-escaped; \" \\ \n \t carry over to C++ as-is.
    # A lone \' is valid JS but not meaningful in a C++ double-quoted string.
    return s.replace("\\'", "'")


def render(variant, args):
    if variant == "i18n":
        return 'PhosphorI18n::tr("%s");' % cxx_escape(args[0])
    if variant == "i18nc":
        return 'PhosphorI18n::tr("%s", "%s");' % (cxx_escape(args[1]), cxx_escape(args[0]))
    if variant == "i18np":
        return 'PhosphorI18n::tr("%s", nullptr, 2);' % cxx_escape(args[0])
    return 'PhosphorI18n::tr("%s", "%s", 2);' % (cxx_escape(args[1]), cxx_escape(args[0]))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--source-root", required=True)
    ap.add_argument("--out-dir", required=True)
    ap.add_argument("--list", help="write the generated stub paths here, one per line")
    ap.add_argument("qml", nargs="+")
    args = ap.parse_args()

    written = []
    total = skipped = 0
    for path in sorted(args.qml):
        text = open(path, encoding="utf-8").read()
        found = list(scan(text))
        calls = [(l, v, a) for l, v, a, ok in found if ok]
        bad = [(l, v) for l, v, _, ok in found if not ok]
        for line, variant in bad:
            rel = os.path.relpath(path, args.source_root)
            print("%s:%d: %s() has a non-literal message, not extractable" % (rel, line, variant), file=sys.stderr)
        skipped += len(bad)
        if not calls:
            continue
        total += len(calls)

        rel = os.path.relpath(path, args.source_root)
        out_path = os.path.join(args.out_dir, rel + ".cpp")
        os.makedirs(os.path.dirname(out_path), exist_ok=True)

        # Pad so each tr() sits on its call's line, then wrap in a function so
        # the file is still valid C++ for lupdate's parser.
        # lines[] is 0-based but file lines are 1-based, so a call on .qml line
        # L has to land at index L-1 for the .ts location to point at it.
        body = {}
        for line, variant, a in calls:
            body.setdefault(line, []).append(render(variant, a))
        last = max(body)
        lines = [""] * last
        lines[0] = '#include "phosphor_i18n.h"'
        for line, stmts in body.items():
            lines[line - 1] = " ".join(stmts)
        # The opening brace has to precede the first statement; put it on the
        # highest free line before it so no call line is displaced.
        first = min(body)
        for i in range(first - 2, 0, -1):
            if not lines[i]:
                lines[i] = "static void generated_%d() {" % first
                break
        else:
            raise SystemExit("no free line before first call in %s" % rel)
        lines.append("}")

        os.makedirs(os.path.dirname(out_path), exist_ok=True)
        with open(out_path, "w", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")
        written.append(out_path)

    if args.list:
        with open(args.list, "w", encoding="utf-8") as f:
            f.write("\n".join(written) + "\n")
    print("qml-i18n-stubs: %d calls from %d files, %d not extractable" % (total, len(written), skipped),
          file=sys.stderr)


if __name__ == "__main__":
    main()
