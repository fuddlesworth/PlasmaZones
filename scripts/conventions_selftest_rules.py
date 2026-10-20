# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
"""Rule-COVERAGE arms of the gate self-test: each one drives a real rule over a fake tree.

Split from conventions_selftest.py, which crossed the 1150-line ceiling once the size
ratchet, the wiring call sites and the remaining prose-extraction arms got the coverage
they had been missing. The cut is a real concern boundary rather than a line count:

  * conventions_selftest.py holds the arms that test a DETECTOR or a constant against
    data: the prose probes, `iter_json_prose`, the shared-text and dep5 detectors, and the
    literal pins on _FINITE_VERBS and DEP5_HEAD_LINES. Two of those arms do build a temp
    tree, so "builds no tree" was the wrong way to state this boundary and said so for a
    round.
  * this module holds the arms that reach INTO the gate's own namespace. Each takes
    `partition_readable.__globals__` and redirects a global there — REPO, CODE_SUFFIXES,
    BASELINE, tracked_files, read_error — before calling the rule. That is the real
    criterion. Two arms here (`_precondition_failures`, `_dead_stanza_failures`) pass
    `repo=` as an argument instead and redirect nothing; they sit here because they belong
    with the fake-tree work, not because they meet that test.

That is also where the gate's own blind spots have all been: a detector nobody handed the
text to, a rule whose arm could be deleted with a green suite, a helper pinned while its
only call site was not, and a constant whose probes were sized relative to itself.

Everything here reaches the gate through `partition_readable.__globals__`, which IS the
gate's namespace, so the gate grows no line and no export for any of it. Nothing here
imports the gate, so no cycle is possible.
"""
from __future__ import annotations

import json
import sys
import tempfile
from pathlib import Path


def _precondition_failures(partition_readable) -> list[str]:
    """The gate's unreadable-path precondition, against a fake tree.

    Pinned here because it is the one thing its round added without a test, and
    neutering it left everything green: its only caller is main(), which nothing
    invokes. Against a temp dir rather than the repo, so a pre-commit run never
    chmods a tracked file."""
    bad: list[str] = []
    with tempfile.TemporaryDirectory() as tmp:
        d = Path(tmp)
        (d / "ok.cpp").write_text("// SPDX-FileCopyrightText: 2026 fuddlesworth\n", encoding="utf-8")
        locked = d / "locked.cpp"
        locked.write_text("// SPDX-FileCopyrightText: 2026 fuddlesworth\n", encoding="utf-8")
        locked.chmod(0o000)
        try:
            locked.read_text(encoding="utf-8")
        except OSError:
            root = False
        else:
            root = True  # running as root, where the mode is not enforced
        if not root:
            readable, problems = partition_readable(["ok.cpp", "locked.cpp"], repo=d)
            if readable != ["ok.cpp"]:
                bad.append(f"partition_readable let an unreadable path through to the rules: {readable}")
            msgs = [v.message for v in problems]
            if not any("cannot be read" in m for m in msgs):
                bad.append(f"partition_readable did not report why a path could not be read: {msgs}")
            if len(problems) != 1:
                bad.append(f"partition_readable reported {len(problems)} problems for one unreadable path")
            if problems and problems[0].rule != "unreadable":
                bad.append(f"the precondition's violations are filed under {problems[0].rule!r}")
        locked.chmod(0o600)
    return bad


def _spdx_suffix_failures(partition_readable) -> list[str]:
    """rule_spdx's CODE_SUFFIXES reach, against a fake tree.

    Reached WITHOUT the gate growing a line, which an audit round wrongly recorded as
    impossible. partition_readable is already a module-level function of the gate, so
    its __globals__ IS the gate's namespace: the rule, the suffix set and the REPO the
    rule resolves against are all reachable from here. That matters because the set is
    otherwise pinned by nothing at all — every tracked file it covers already carries a
    header, so narrowing it back changes no tree finding and no existing case."""
    g = partition_readable.__globals__
    rule_spdx, suffixes = g["rule_spdx"], g["CODE_SUFFIXES"]
    bad: list[str] = []
    # MEMBERSHIP pinned in BOTH directions against the literal below, because neither
    # direction is visible any other way: every tracked file the set already covers carries
    # a header, so narrowing it changes no tree finding, and widening it is what quietly
    # brought .spec into the size ratchet. A deliberate change edits this literal in the
    # same commit and says so.
    expected = frozenset({".c", ".cc", ".cmake", ".cpp", ".cxx", ".desktop", ".frag", ".glsl",
                          ".h", ".hpp", ".js", ".luau", ".py", ".qml", ".sh", ".spec", ".vert"})
    for suffix in sorted(expected - suffixes):
        bad.append(f"CODE_SUFFIXES no longer covers {suffix}")
    for suffix in sorted(suffixes - expected):
        bad.append(f"CODE_SUFFIXES gained {suffix} without this selftest's literal being updated")
    with tempfile.TemporaryDirectory() as tmp:
        d = Path(tmp)
        # TIER-PREFIXED, not a bare data/: the exemption pattern is a MID-PATH one and the
        # gate explains at length why it uses .search() rather than .match(). A probe at
        # data/probe.json starts at position 0, so .match() would match it too and that
        # reasoning stayed unpinned — every real data JSON in the tree is tier-prefixed,
        # so with .match() the forward cover would guard nothing.
        (d / "plasmazones" / "data").mkdir(parents=True)
        # A headerless probe PER MEMBER, generated from the set rather than a hand-picked
        # few, so the reach is pinned for every suffix and not only for the ones an audit
        # round happened to name. rule_spdx matches the SPDX tags as plain substrings in the
        # head, so one comment syntax covers every suffix here.
        headerless = [f"headerless{s}" for s in sorted(suffixes)]
        if len(headerless) != len(suffixes):
            bad.append("the headerless probes are no longer generated one per member")
        for name in headerless:
            (d / name).write_text("# nothing here\n", encoding="utf-8")
        # A VALID header must NOT be reported. This is what pins that the rule READS the head
        # rather than merely matching the suffix: with only the headerless probes, deleting
        # them entirely left the arm green, because read() answers "" for a missing file and a
        # headerless read is indistinguishable from an unreadable one.
        good = "good.sh"
        (d / good).write_text("# SPDX-FileCopyrightText: 2026 fuddlesworth\n"
                              "# SPDX-License-Identifier: GPL-3.0-or-later\n", encoding="utf-8")
        # A copyright line with NO identifier must be reported, which pins the primary arm
        # separately from the FileCopyrightText one. Disabling either used to leave both green.
        idless = "idless.sh"
        (d / idless).write_text("# SPDX-FileCopyrightText: 2026 fuddlesworth\n", encoding="utf-8")
        # And the mirror, an identifier with no copyright line, which is the only shape that
        # reaches the SECOND arm. Without it, deleting that arm left the suite green.
        copyless = "copyless.sh"
        (d / copyless).write_text("# SPDX-License-Identifier: GPL-3.0-or-later\n", encoding="utf-8")
        # A header straddling the window's edge must be reported, which pins
        # SPDX_HEAD_LINES in the WIDENING direction. The tags sit on 6 and 7, so exactly
        # one of them is inside a window of 6 and BOTH are inside any larger one: the
        # probe therefore fails at 7 as well as at 60, where a bare past-the-end probe
        # only failed from 8 up and left 7 resting on nothing.
        toodeep = "toodeep.sh"
        (d / toodeep).write_text("#\n" * 5
                                 + "# SPDX-FileCopyrightText: 2026 fuddlesworth\n"
                                   "# SPDX-License-Identifier: GPL-3.0-or-later\n", encoding="utf-8")
        # And the LOWER edge, with the tags on the last two lines the window admits. good.sh
        # only pins 2, so narrowing SPDX_HEAD_LINES to 4 or 5 left the selftest green and the
        # band 5-7 rested on a whole-tree run finding update-aur.sh. With this probe the
        # selftest closes it on its own: 4 and 5 report a file that is correct, 7 and above
        # stop reporting toodeep, and only 6 passes both.
        deep_ok = "deep_ok.sh"
        (d / deep_ok).write_text("#\n" * 4
                                 + "# SPDX-FileCopyrightText: 2026 fuddlesworth\n"
                                   "# SPDX-License-Identifier: GPL-3.0-or-later\n", encoding="utf-8")
        # Data JSON is exempt by FORMAT. .json is added to the set for this call on purpose, so
        # SPDX_EXEMPT is the thing that skips it — a bare probe.json is skipped by the suffix
        # filter instead, which tests nothing AND turns the widening that pattern exists for
        # into a selftest failure. The path needs a data/ segment because the pattern requires
        # one.
        exempt = "plasmazones/data/probe.json"
        (d / exempt).write_text("{}\n", encoding="utf-8")
        saved_repo, saved_suffixes = g["REPO"], g["CODE_SUFFIXES"]
        g["REPO"] = d
        g["CODE_SUFFIXES"] = saved_suffixes | {".json"}
        try:
            reported = {v.path for v in rule_spdx([*headerless, good, idless, copyless, toodeep, deep_ok, exempt])}
        finally:
            g["REPO"], g["CODE_SUFFIXES"] = saved_repo, saved_suffixes
        for name in headerless:
            if name not in reported:
                bad.append(f"rule_spdx does not read {name}, so a missing header there is silent")
        if good in reported:
            bad.append("rule_spdx reported a file carrying a valid head header")
        if idless not in reported:
            bad.append("rule_spdx missed a copyright line with no licence identifier")
        if copyless not in reported:
            bad.append("rule_spdx missed an identifier with no copyright line")
        if toodeep not in reported:
            bad.append(f"rule_spdx read a header past line {g['SPDX_HEAD_LINES']}, so the window has widened "
                       f"and the message's line count is now wrong")
        if deep_ok in reported:
            bad.append(f"rule_spdx reported a header ending on line {g['SPDX_HEAD_LINES']}, so the window has "
                       f"narrowed and a correct file is now a finding")
        if exempt in reported:
            bad.append(f"rule_spdx reported {exempt}, which SPDX_EXEMPT covers by format")
    return bad


def _prose_extraction_failures(partition_readable) -> list[str]:
    """rule_prose's EXTRACTION arms, one planted violation per surface.

    The detector itself is well pinned (every arm of prose_problems has a probe), but the
    code that decides WHICH text reaches it was covered for `iter_json_prose` alone.
    Breaking DEB_DESC, RPM_DESC, PKG_DESC, the `packaging/` prefix gate or the
    CHANGELOG.md name gate all left the suite green, so those surfaces each worked and
    none could be kept working.

    An earlier version of this docstring said "one per surface" while planting seven of
    the rule's arms, leaving the two LARGEST unpinned: TR_LITERAL / TR_CONTEXT_LITERAL
    (6369 literals in the tree) and SCHEMA_DESCRIPTION (167 KeyDef descriptions), plus the
    .luau arm, the .github/workflows arm, NIX_LONG_DESC and the data-JSON gate. Breaking
    any of those left both the selftest and a whole-tree run green, because a clean tree
    holds no violation for the arm to stop finding. All of them are planted now.

    Every plant is an em-dash splice, which the detector catches unconditionally, so a
    failure here is always the extraction and never the detector."""
    g = partition_readable.__globals__
    rule_prose = g["rule_prose"]
    bad: list[str] = []
    splice = "Blurs the pane — and lifts saturation."

    files = {
        # deb822: a one-line synopsis then a space-indented continuation block.
        # deb822: the bullets are indented TWICE — one space for the continuation and one for
        # the indent — exactly as packaging/debian/control writes them, so this probe pins the
        # deb822 half of the bullet strip the way the .spec probe below pins the RPM half.
        # The live file does pin this one as well, because its own bullets are indented too;
        # the probe is here so the pin does not depend on nobody ever reflowing that file.
        "packaging/debian/control": (
            "Source: plasmazones\n"
            "\n"
            "Package: plasmazones\n"
            "Description: Window snapping for KDE Plasma\n"
            f" {splice}\n"
            " .\n"
            " Features include:\n"
            "  - first feature\n"
            "  - second feature\n"),
        # RPM: %description runs to the next % section. The INDENTED bullet list is what
        # pins the bullet strip: without it those markers read as spaced hyphens standing in
        # for dashes and every bullet becomes a finding, which is what an ordinary reflow of
        # the live spec would have produced. The live file passes only because its own
        # markers sit at column 0, so it cannot pin this either — the strip could be deleted
        # with a green selftest AND a green tree before this probe existed. The splice is
        # still here, so the arm is asserted to find EXACTLY the planted violation and not
        # the bullets.
        "packaging/rpm/x.spec": (f"Name: x\nSummary: Fine\n\n%description\n{splice}\n"
                                 "  - first feature\n  - second feature\n  * third feature\n\n%files\n"),
        # Nix: description and longDescription.
        "packaging/nix/x.nix": f'{{\n  meta = {{\n    description = "{splice}";\n  }};\n}}\n',
        # Arch: pkgdesc, which the line-oriented PKG_DESC arm reads.
        "packaging/arch/PKGBUILD": f'pkgdesc="{splice}"\n',
        # CHANGELOG entry BODY, after the Keep-a-Changelog bold lead-in.
        "CHANGELOG.md": f"## [1.0.0]\n\n- **Thing**: {splice}\n",
        # .desktop Name/GenericName/Comment.
        "x.desktop": f"[Desktop Entry]\nName=Fine\nComment={splice}\n",
        # AppStream summary.
        "x.metainfo.xml": f"<component>\n  <summary>{splice}</summary>\n</component>\n",
        # Nix longDescription, the multi-line body the line-oriented PKG_DESC arm cannot
        # reach. Its own arm, separate from `description` above.
        "packaging/nix/y.nix": f'{{\n  meta = {{\n    longDescription = \'\'\n      {splice}\n    \'\';\n  }};\n}}\n',
        # The BIGGEST surface in the tree, and it was unpinned: every translatable literal
        # in C++ and QML, through PhosphorI18n::tr() and QML i18n()/i18nc().
        "src/a.cpp": f'void f() {{ label = PhosphorI18n::tr("{splice}"); }}\n',
        "src/A.qml": f'Item {{ text: i18n("{splice}") }}\n',
        # The SECOND biggest, and the one CLAUDE.md names as user-facing prose in its own
        # right: a settings-schema KeyDef description. Gated on the FILENAME prefix, so the
        # probe has to be called settingsschema*.
        "src/settingsschema_probe.cpp": (
            f'void reg() {{ add({{enabledKey(), false, QMetaType::Bool,\n'
            f'                   QStringLiteral("{splice}"), none}}); }}\n'),
        # A bundled Luau algorithm's description field. The arm gates on the full
        # plasmazones/data/algorithms/ prefix, so the probe has to sit there.
        "plasmazones/data/algorithms/probe.luau": f'local algorithm = {{\n  description = "{splice}",\n}}\n',
        # The draft pkgdesc generated in CI, outside packaging/ and reached only by the
        # .github/workflows arm.
        ".github/workflows/probe.yml": f'jobs:\n  b:\n    run: |\n      pkgdesc="{splice}"\n',
        # And the data-JSON gate itself. iter_json_prose has four dedicated assertions, but
        # the `data/` + .json test that routes files INTO it had none, so narrowing that
        # gate silenced 208 files.
        "plasmazones/data/surface/probe/metadata.json": f'{{"description": "{splice}"}}\n',
    }

    saved_repo = g["REPO"]
    with tempfile.TemporaryDirectory() as tmp:
        d = Path(tmp)
        g["REPO"] = d
        try:
            for rel, text in files.items():
                p = d / rel
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_text(text, encoding="utf-8")
                found = rule_prose([rel])
                if not found:
                    bad.append(f"rule_prose did not extract the planted em-dash splice from {rel}")
                # EXACTLY one, not at least one. Two of these bodies carry an indented bullet
                # list beside the splice, one per half of the bullet strip (deb822 and RPM),
                # and a bullet marker reads as a spaced hyphen unless the strip removes it
                # first. So a count is what pins the strip, while "did it find something" is
                # satisfied by the splice alone.
                elif len(found) != 1:
                    bad.append(f"rule_prose reported {len(found)} findings for {rel}, not just the planted "
                               f"splice: {[v.message for v in found]}")
        finally:
            g["REPO"] = saved_repo
    return bad


def _dead_stanza_failures() -> list[str]:
    """The dead-stanza arm of the dep5 rule, in both directions, plus its two exemptions.

    It had NO positive coverage when it was written: deleting it, inverting its any(), or
    widening DEP5_BUILD_TIME_PREFIXES to ("",) all left the suite green, because the only
    arm reachable from a temp tree was the git-failed one and the caller's assertion did
    not look at it. That is the same shape as the defect the check was added to fix.
    """
    from conventions_dep5 import _dead_stanza_problems, parse_dep5

    bad: list[str] = []
    with tempfile.TemporaryDirectory() as tmp:
        d = Path(tmp)
        dep5 = d / "packaging" / "debian" / "copyright"
        dep5.parent.mkdir(parents=True)
        dep5.write_text(
            "Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/\n"
            "\n"
            "Files: *\n"
            "Copyright: 2026 fuddlesworth\n"
            "License: GPL-3.0-or-later\n"
            "\n"
            "Files: src/live.cpp\n"
            "Copyright: 2026 fuddlesworth\n"
            "License: MIT\n"
            "\n"
            "Files: src/typo.cpp\n"
            "Copyright: 2026 fuddlesworth\n"
            "License: MIT\n"
            "\n"
            "Files: debian/rules\n"
            "Copyright: 2026 fuddlesworth\n"
            "License: GPL-3.0-or-later\n",
            encoding="utf-8")
        stanzas = parse_dep5(dep5)
        # Only src/live.cpp exists, so src/typo.cpp is the dead stanza. `debian/rules` is
        # the build-time prefix and must stay silent even though it matches nothing.
        problems = _dead_stanza_problems(stanzas, dep5, d, list_paths=lambda _repo: ["src/live.cpp"])
        messages = [m for _p, _l, m in problems]
        if not any("src/typo.cpp" in m for m in messages):
            bad.append(f"the dead-stanza check missed a Files pattern matching nothing: {messages}")
        if any("src/live.cpp" in m for m in messages):
            bad.append("the dead-stanza check reported a Files pattern that DOES match a tracked path")
        if any("debian/rules" in m for m in messages):
            bad.append("the dead-stanza check reported a build-time-only path (DEP5_BUILD_TIME_PREFIXES)")
        # FORWARD COVER, and the `pat == "*"` skip it guards is dead defensive code:
        # fnmatchcase(p, "*") matches every path, so `not any(...)` is False whether the
        # skip runs or not. It can only matter if the tracked list comes back EMPTY, which
        # the boom probe below already covers. Kept as a statement of intent, and recorded
        # here as such so it is not mistaken for a pin.
        if any("Files: *" in m for m in messages):
            bad.append("the dead-stanza check reported the catch-all")
        # And the error arm still has to report rather than pass silently.
        def boom(_repo):
            raise OSError("no git here")

        if not any("cannot list tracked paths" in m
                   for _p, _l, m in _dead_stanza_problems(stanzas, dep5, d, list_paths=boom)):
            bad.append("the dead-stanza check swallows a failure to list tracked paths")
    return bad


def _wiring_failures(partition_readable) -> list[str]:
    """main() and dep5_problems END TO END, so a dropped CALL is a failure.

    Every arm above reaches a rule or a helper DIRECTLY, which pins the helper and leaves
    its wiring free. Three call sites were unpinned in exactly that way, and each was also
    invisible to a whole-tree run because a clean tree produces nothing for them to report:

      * `out += _dead_stanza_problems(...)` in dep5_problems. Deleting it left the suite
        green even though _dead_stanza_failures has five assertions on the helper.
      * `files, violations = partition_readable(files)` in main(). partition_readable's own
        docstring records that it was EXTRACTED so it could be pinned, and then its only
        caller stayed unpinned.
      * rule_prose's `data/` + .json gate, now also planted in _prose_extraction_failures.

    Drives main() through its `files` argument with REPO redirected, which exercises the
    argument path, partition_readable's call site, and the rule dispatch in one go."""
    import io
    import contextlib

    import conventions_dep5
    from conventions_dep5 import dep5_problems

    g = partition_readable.__globals__
    main = g["main"]
    bad: list[str] = []

    with tempfile.TemporaryDirectory() as tmp:
        d = Path(tmp)
        # An ordinary file that read_error is stubbed to REFUSE, which is the portable way
        # to reach partition_readable's finding from main(). A chmod 000 is a no-op when
        # the suite runs as root (CI containers do), and anything genuinely unreadable at
        # the OS level (a directory, a FIFO, a dangling symlink) is dropped by main()'s own
        # is_file() filter before partition_readable ever sees it. The stub leaves
        # partition_readable itself real, which is what this arm is about: whether main()
        # still CALLS it.
        unreadable = d / "unreadable.cpp"
        unreadable.write_text("// SPDX-FileCopyrightText: 2026 fuddlesworth\n", encoding="utf-8")
        # A dep5 file whose one non-catch-all stanza matches nothing tracked. Reported only
        # if dep5_problems still CALLS the dead-stanza check.
        dep5 = d / "packaging" / "debian" / "copyright"
        dep5.parent.mkdir(parents=True)
        dep5.write_text(
            "Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/\n"
            "\n"
            "Files: *\n"
            "Copyright: 2026 fuddlesworth\n"
            "License: GPL-3.0-or-later\n"
            "\n"
            "Files: src/gone.cpp\n"
            "Copyright: 2026 fuddlesworth\n"
            "License: MIT\n",
            encoding="utf-8")

        def run_main(argv: list[str]) -> tuple[int, str]:
            """main() reads sys.argv through argparse, so drive it the way a shell does."""
            saved_argv = sys.argv
            out, err = io.StringIO(), io.StringIO()
            sys.argv = ["check-conventions.py", *argv]
            try:
                with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
                    rc = main()
            finally:
                sys.argv = saved_argv
            return rc, out.getvalue() + err.getvalue()

        saved_repo = g["REPO"]
        saved_read_error = g["read_error"]
        saved_dead = conventions_dep5._dead_stanza_problems
        # main() assigns the gate's _SELECTED_RULES, so driving it leaks the whole rule set
        # into every later arm. rule_license's no-identifier deferral reads that global and a
        # license arm runs after this one, which makes the leak an order dependency rather
        # than only untidiness.
        saved_selected = set(g["_SELECTED_RULES"])
        g["REPO"] = d
        g["read_error"] = lambda path, *, repo=None: (
            "Permission denied" if path.endswith("unreadable.cpp") else saved_read_error(path, repo=repo))
        # dep5_problems calls _dead_stanza_problems with no lister, so its default is baked
        # in at definition time and patching the module's _git_ls_files would not reach it.
        # Wrap the real function instead: the temp tree is not a git repository, so the real
        # lister raises and only the error arm would be reachable.
        conventions_dep5._dead_stanza_problems = (
            lambda stanzas, dep5_path, repo, **kw: saved_dead(
                stanzas, dep5_path, repo, list_paths=lambda _r: ["src/here.cpp"]))
        try:
            # A bogus --rules must be REFUSED rather than silently checking nothing.
            rc, _ = run_main(["--rules", "notarule"])
            if rc != 2:
                bad.append(f"main() accepted an unknown --rules value (rc={rc})")

            # partition_readable's CALL SITE. Only it can report an unreadable path, and
            # only main() calls it, so dropping the call is invisible everywhere else.
            rc, printed = run_main(["--rules", "spdx", str(unreadable)])
            if "[unreadable]" not in printed:
                bad.append(f"main() does not run partition_readable over its file list: {printed!r}")

            # --staged with nothing staged returns 0 without sweeping the tree.
            rc, printed = run_main(["--staged"])
            if rc != 0:
                bad.append(f"main() --staged with no files returned {rc}: {printed!r}")

            # dep5_problems' own call to the dead-stanza check. Naming the DEP-5 file is
            # what widens `targets` to the tracked list, which is the gate that call sits
            # behind.
            messages = [m for _p, _l, m in dep5_problems(
                ["packaging/debian/copyright"], repo=d, line_of=g["line_of"],
                tracked_files=lambda: ["src/here.cpp"])]
        finally:
            g["REPO"] = saved_repo
            g["read_error"] = saved_read_error
            g["_SELECTED_RULES"] = saved_selected
            conventions_dep5._dead_stanza_problems = saved_dead

        if not any("src/gone.cpp" in m for m in messages):
            bad.append(f"dep5_problems no longer calls the dead-stanza check: {messages}")
    return bad


def _size_ratchet_failures(partition_readable) -> list[str]:
    """rule_size, both arms, against a fake tree and a fake baseline.

    This rule had ZERO positive coverage from either the selftest or a clean whole-tree
    run: disabling the growth arm, disabling the new-file arm, or raising SIZE_CEILING to
    5000 each left `--selftest` green AND the tree at 0 findings, because a clean tree by
    construction contains no file the rule reports. It is also the rule with real
    historical drift, the one CLAUDE.md singles out, and the one an audit round broke on
    itself by pushing a file from 1148 to 1151.

    Redirects REPO the way the two arms below do, and BASELINE with it, so it adds no line
    to the gate and reads no repo file."""
    g = partition_readable.__globals__
    rule_size = g["rule_size"]
    bad: list[str] = []

    # The ceiling's VALUE, pinned to the literal CLAUDE.md states. Every probe below is
    # sized RELATIVE to SIZE_CEILING, which is correct for the arms but blind to the
    # constant itself: raising it to 5000 raised the probes with it and left the whole
    # suite green while every file in the tree became compliant. CLAUDE.md fixes this
    # number ("hard ceiling 1150"), and the baseline ratchet means nothing if it can drift.
    if g["SIZE_CEILING"] != 1150:
        bad.append(f"SIZE_CEILING is {g['SIZE_CEILING']}, not the 1150 CLAUDE.md states as the hard ceiling")

    with tempfile.TemporaryDirectory() as d:
        root = Path(d)
        ceiling = g["SIZE_CEILING"]
        # A NEW file one line over the ceiling, absent from the baseline. Disabling that arm
        # or raising the ceiling both silence this.
        newbig = "newbig.cpp"
        (root / newbig).write_text("//\n" * (ceiling + 1), encoding="utf-8")
        # A file at EXACTLY the ceiling must pass, which pins the comparison as <= rather
        # than <. CLAUDE.md tolerates 1000-1150 and several tracked files sit on 1150.
        atceiling = "atceiling.cpp"
        (root / atceiling).write_text("//\n" * ceiling, encoding="utf-8")
        # A baselined file GROWN by one line, and its twin SHRUNK by one. The rule is
        # growth-only by design (a net shrink is allowed even while over the ceiling), so
        # both directions have to be asserted or `elif n > base[f]` could become `!=`.
        grown = "grown.cpp"
        (root / grown).write_text("//\n" * (ceiling + 20), encoding="utf-8")
        shrunk = "shrunk.cpp"
        (root / shrunk).write_text("//\n" * (ceiling + 20), encoding="utf-8")
        # A non-CODE_SUFFIXES file far over the ceiling must be skipped entirely.
        notcode = "huge.md"
        (root / notcode).write_text("x\n" * (ceiling + 500), encoding="utf-8")

        (root / "scripts").mkdir()
        (root / "scripts" / "oversize-baseline.json").write_text(
            json.dumps({"files": {grown: ceiling + 19, shrunk: ceiling + 21}}), encoding="utf-8")

        saved_repo, saved_baseline = g["REPO"], g["BASELINE"]
        g["REPO"] = root
        g["BASELINE"] = root / "scripts" / "oversize-baseline.json"
        try:
            reported = {v.path: v.message for v in rule_size([newbig, atceiling, grown, shrunk, notcode])}
        finally:
            g["REPO"], g["BASELINE"] = saved_repo, saved_baseline

        if newbig not in reported:
            bad.append(f"rule_size missed a NEW file at {ceiling + 1} lines, one over the ceiling")
        elif "hard ceiling" not in reported[newbig]:
            bad.append(f"rule_size reported the new file through the wrong arm: {reported[newbig]}")
        if atceiling in reported:
            bad.append(f"rule_size reported a file at exactly the {ceiling}-line ceiling, which is allowed")
        if grown not in reported:
            bad.append("rule_size missed a baselined file grown by one line")
        elif "grandfathered" not in reported[grown]:
            bad.append(f"rule_size reported the grown file through the wrong arm: {reported[grown]}")
        if shrunk in reported:
            bad.append("rule_size reported a baselined file that SHRANK; the ratchet is growth-only")
        if notcode in reported:
            bad.append("rule_size reported a file outside CODE_SUFFIXES")
    return bad


def _baseline_writer_failures(partition_readable) -> list[str]:
    """update_baseline(), which owns the ratchet's data and had NO coverage at all.

    Every other arm here tests a rule that READS the baseline; nothing tested the function
    that WRITES it. Two mutations survived both the selftest and a clean whole-tree run:
    dropping the unreadable-file arm that preserves the prior entry (the exact bug that
    arm's own comment describes), and relaxing `n > SIZE_CEILING` to `>=`. A broken writer
    silently drops or inflates entries, and the size rule then mis-grandfathers on the next
    run — a file whose entry vanished is reported as new-over-ceiling, and one recorded a
    line too low fails on its very next edit.

    Drives the real function with REPO, BASELINE and tracked_files redirected, then reads
    back the JSON it wrote. A second run adds files the writer must REFUSE to absorb (a raise
    and an addition with no FILE-SIZE EXCEPTION comment) and checks that it fails and leaves
    the baseline untouched."""
    g = partition_readable.__globals__
    update_baseline = g["update_baseline"]
    marker = g["SIZE_EXCEPTION_MARKER"]
    bad: list[str] = []

    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        ceiling = g["SIZE_CEILING"]

        # New and over the ceiling, so it is recorded only because it carries the marker.
        over = "over.cpp"
        (root / over).write_text(f"// {marker}: one class.\n" + "//\n" * (ceiling + 4), encoding="utf-8")
        # Grandfathered with no marker, unchanged and shrunk: both re-record freely.
        same = "same.cpp"
        (root / same).write_text("//\n" * (ceiling + 5), encoding="utf-8")
        shrunk = "shrunk.cpp"
        (root / shrunk).write_text("//\n" * (ceiling + 3), encoding="utf-8")
        # The two shapes the writer must refuse without a marker.
        grown = "grown.cpp"
        (root / grown).write_text("//\n" * (ceiling + 5), encoding="utf-8")
        added = "added.cpp"
        (root / added).write_text("//\n" * (ceiling + 5), encoding="utf-8")
        at = "at.cpp"
        (root / at).write_text("//\n" * ceiling, encoding="utf-8")
        under = "under.cpp"
        (root / under).write_text("//\n" * 10, encoding="utf-8")
        notcode = "big.md"
        (root / notcode).write_text("x\n" * (ceiling + 5), encoding="utf-8")
        # A file the sweep cannot read, with a PRIOR entry that must survive. A directory
        # where a file is expected makes read_error report, portably and as any uid —
        # chmod 000 is a no-op as root, which CI containers run as.
        unreadable = "unreadable.cpp"
        (root / unreadable).mkdir()

        (root / "scripts").mkdir()
        baseline = root / "scripts" / "oversize-baseline.json"
        prior = {unreadable: ceiling + 99, "gone.cpp": ceiling + 1, same: ceiling + 5, shrunk: ceiling + 9,
                 grown: ceiling + 2}
        baseline.write_text(json.dumps({"files": prior}), encoding="utf-8")

        saved = (g["REPO"], g["BASELINE"], g["tracked_files"])
        g["REPO"] = root
        g["BASELINE"] = baseline
        try:
            import contextlib
            import io
            # Refusal first, so the baseline it must leave alone is still the prior one.
            g["tracked_files"] = lambda: [over, same, shrunk, grown, added, unreadable]
            refusal_err = io.StringIO()
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(refusal_err):
                refused_rc = update_baseline()
            after_refusal = json.loads(baseline.read_text(encoding="utf-8"))
            g["tracked_files"] = lambda: [over, at, under, notcode, unreadable, same, shrunk]
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                rc = update_baseline()
            written = json.loads(baseline.read_text(encoding="utf-8"))
        finally:
            g["REPO"], g["BASELINE"], g["tracked_files"] = saved

    if refused_rc != 1:
        bad.append(f"update_baseline returned {refused_rc}, not 1, for a raise and an addition with no "
                   f"{marker} comment; it absorbed growth silently")
    if after_refusal.get("files") != prior:
        bad.append("update_baseline wrote the baseline on a run it refused; a partial write hides the refusal")
    for name in (grown, added):
        if name not in refusal_err.getvalue():
            bad.append(f"update_baseline's refusal did not name {name}")
    for name in (over, same, shrunk):
        if name in refusal_err.getvalue():
            bad.append(f"update_baseline refused {name}, which carries the marker or did not grow")
    if rc != 0:
        bad.append(f"update_baseline returned {rc}, not 0")
    rec = written.get("files", {})
    if rec.get(over) != ceiling + 5:
        bad.append(f"update_baseline did not record a file over the ceiling at its real length: {rec.get(over)}")
    if rec.get(same) != ceiling + 5:
        bad.append(f"update_baseline did not re-record an unchanged grandfathered file: {rec.get(same)}")
    if rec.get(shrunk) != ceiling + 3:
        bad.append(f"update_baseline did not ratchet a shrunk file down to its length: {rec.get(shrunk)}")
    if at in rec:
        bad.append(f"update_baseline recorded a file at EXACTLY the ceiling ({ceiling}), which is not an overrun; "
                   f"the comparison must be > and not >=")
    if under in rec:
        bad.append("update_baseline recorded a file under the ceiling")
    if notcode in rec:
        bad.append("update_baseline recorded a file outside CODE_SUFFIXES")
    if rec.get(unreadable) != ceiling + 99:
        bad.append(f"update_baseline did not preserve the prior entry of an UNREADABLE file "
                   f"({rec.get(unreadable)}); its entry vanishes and the size rule then reports a grandfathered "
                   f"file as new-over-ceiling once the mode is fixed")
    if "gone.cpp" in rec:
        bad.append("update_baseline kept an entry for a file no longer tracked")
    if written.get("ceiling") != ceiling:
        bad.append(f"update_baseline wrote ceiling {written.get('ceiling')}, not {ceiling}")
    return bad


def _license_tree_failures(partition_readable) -> list[str]:
    """rule_license's per-tier split, against a fake tree.

    The LGPL half was entirely unpinned: replacing its `libs/phosphor-` test with `if
    False:` produced ZERO findings and a green suite, because without that arm the two
    library trees match none of the GPL-3 prefixes either, so `expected_license` returns
    None and both are SKIPPED rather than misreported. CLAUDE.md calls the split out by
    name ("Never 'fix' a lib header to GPL-3 without understanding the split"), and half
    the machine check for it could be deleted invisibly.

    Reaches the rule the same way _spdx_suffix_failures does, by redirecting the gate's
    REPO, so it adds no line to the gate."""
    g = partition_readable.__globals__
    rule_license = g["rule_license"]
    bad: list[str] = []
    lgpl = "LGPL-2.1-or-later"
    gpl3 = "GPL-3.0-or-later"

    def header(ident: str) -> str:
        return f"// SPDX-FileCopyrightText: 2026 fuddlesworth\n// SPDX-License-Identifier: {ident}\n"

    # (path, identifier in the header, must it be reported?)
    cases = [
        # A library tree with a GPL-3 header is the defect the LGPL arm exists to catch.
        ("phosphor-libs/libs/phosphor-x/src/a.cpp", gpl3, True),
        ("phosphor-libs/libs/phosphor-x/src/a.cpp", lgpl, False),
        # A library's own TESTS follow the library, not the app-tier rule.
        ("phosphor-libs/libs/phosphor-x/tests/test_a.cpp", gpl3, True),
        ("phosphor-libs/libs/phosphor-x/tests/test_a.cpp", lgpl, False),
        # The shell-libs tree takes the same arm.
        ("phosphor-shell-libs/libs/phosphor-y/src/b.cpp", gpl3, True),
        # The app tier is the mirror: LGPL there is the defect.
        ("plasmazones/src/a.cpp", lgpl, True),
        ("plasmazones/src/a.cpp", gpl3, False),
        ("phosphor-shell/src/b.cpp", lgpl, True),
        ("scripts/x.py", lgpl, True),
        # The shell-libs EXAMPLES are app-tier, unlike its libs.
        ("phosphor-shell-libs/examples/demo/c.cpp", lgpl, True),
        # The two data trees are deliberately ungoverned: the licence follows the
        # incorporated content, so neither identifier may be reported.
        ("plasmazones/data/surface/glass/effect.frag", gpl3, False),
        ("plasmazones/data/surface/glass/effect.frag", lgpl, False),
        # FORWARD COVER, not a pin. The `phosphor-libs` half of LICENSE_UNGOVERNED cannot
        # change a verdict today: this path matches neither `(^|/)libs/phosphor-` (the
        # substring is `phosphor-libs/`, not `libs/phosphor-`) nor any GPL-3 prefix, so
        # expected_license answers None with or without the alternative, and no tracked
        # file under phosphor-libs/data/ carries a CODE_SUFFIX at all (the six schemas are
        # .json). Narrowing the pattern to `plasmazones` alone therefore survives. The
        # plasmazones half above IS load-bearing.
        ("phosphor-libs/data/schemas/x.frag", gpl3, False),
        # A tests/**/data/ path is NOT one of those trees — the anchor is what keeps it
        # governed, and an unanchored pattern used to swallow it.
        ("plasmazones/tests/unit/data/d.cpp", lgpl, True),
    ]

    saved_repo = g["REPO"]
    with tempfile.TemporaryDirectory() as tmp:
        d = Path(tmp)
        g["REPO"] = d
        try:
            for rel, ident, want_reported in cases:
                p = d / rel
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_text(header(ident), encoding="utf-8")
                reported = bool(rule_license([rel]))
                if reported != want_reported:
                    verb = "did not report" if want_reported else "reported"
                    bad.append(f"rule_license {verb} {rel} carrying {ident}")
        finally:
            g["REPO"] = saved_repo
    return bad


def _remaining_rule_failures(partition_readable) -> list[str]:
    """The three rules that still had no positive case: i18n-cpp, config-keys, js-pragma.

    Same defect class as rule_size before it got an arm, and the same reason it was
    invisible: a clean tree holds nothing for any of them to report, so neutering a rule
    or narrowing one of its carve-outs changed no finding anywhere. Each case below is
    asserted in BOTH directions, because the carve-outs are the part that rots — the
    `/tests/` exemptions, the allow-lists, and js-pragma's four filename filters, none of
    which had a probe.

    One arm for three rules rather than three arms, because each is a handful of
    single-file cases over the same temp tree and the shared setup is most of the code."""
    g = partition_readable.__globals__
    rule_i18n_cpp, rule_config_keys, rule_js_pragma = g["rule_i18n_cpp"], g["rule_config_keys"], g["rule_js_pragma"]
    bad: list[str] = []
    window = g["JS_PRAGMA_WINDOW"]
    # The window's VALUE, pinned to the literal. It is not a project preference: it mirrors
    # the byte budget qt_add_qml_module's own file(STRINGS ... LIMIT_INPUT) scan uses, so a
    # different number here makes the rule disagree with the thing it models. Every js
    # probe below is sized RELATIVE to it, which is right for the arms and blind to the
    # constant — raising it to 4096 raised the probes with it and left the suite green.
    if window != 128:
        bad.append(f"JS_PRAGMA_WINDOW is {window}, not the 128 bytes Qt's own qmldir scan reads")

    with tempfile.TemporaryDirectory() as tmp:
        d = Path(tmp)

        def write(rel: str, text: str | bytes) -> str:
            p = d / rel
            p.parent.mkdir(parents=True, exist_ok=True)
            if isinstance(text, bytes):
                p.write_bytes(text)
            else:
                p.write_text(text, encoding="utf-8")
            return rel

        # i18n-cpp. Both arms, the /tests/ carve-out, and the allow-list. The bridge
        # headers are the ONLY files allowed to name the QML API, and an allow-list that
        # silently widened would let it back into ordinary C++.
        i18n_include = write("src/inc.cpp", "#include <KLocalizedString>\nvoid f() {}\n")
        i18n_call = write("src/call.cpp", 'void f() { auto s = i18nc("ctx", "text"); }\n')
        i18n_comment = write("src/comment.cpp", '// i18n("in a comment") and <KLocalizedString>\nvoid f() {}\n')
        i18n_test = write("plasmazones/tests/unit/t.cpp", 'void f() { auto s = i18n("text"); }\n')
        i18n_allowed = write("plasmazones/src/phosphor_qml_i18n.cpp", 'void f() { auto s = i18n("text"); }\n')
        # A method whose NAME ends in i18n must not match: the lookbehind is what stops
        # `obj.i18n(` and `Ns::i18n(` from reading as the free function.
        i18n_member = write("src/member.cpp", "void f() { ctx.i18n(1); Ns::i18n(2); }\n")

        # config-keys. A v2 dot-path literal outside the three definition files.
        cfg_bad = write("src/daemon/x.cpp", 'auto g = QStringLiteral("Snapping.Behavior.ZoneSpan");\n')
        cfg_accessor = write("src/daemon/y.cpp", "auto g = ConfigDefaults::snappingBehaviorGroup();\n")
        cfg_def = write("plasmazones/src/config/configkeys.h",
                        'inline QString g() { return QStringLiteral("Snapping.Behavior.ZoneSpan"); }\n')
        cfg_test = write("plasmazones/tests/unit/cfg.cpp", 'auto g = QStringLiteral("Snapping.Behavior.ZoneSpan");\n')
        # A dot-path that is not one of the known v2 groups must not match, or the rule
        # would fire on every dotted string literal in the tree.
        cfg_other = write("src/daemon/z.cpp", 'auto s = QStringLiteral("Some.Other.Thing");\n')

        # js-pragma. The pragma must be inside the window, on its own line, and each of
        # the four filename filters mirrors one of Qt's own.
        js_ok = write("Good.js", b".pragma library\nvar x = 1;\n")
        js_missing = write("Missing.js", b"var x = 1;\n")
        js_late = write("Late.js", b"// " + b"x" * window + b"\n.pragma library\n")
        # Inside a comment, and trailing another statement: neither is what CMake matches.
        js_commented = write("Commented.js", b"// .pragma library\nvar x = 1;\n")
        js_trailing = write("Trailing.js", b"var x = 1; .pragma library\n")
        # A BOM counts against Qt's byte budget (CMake's LIMIT_INPUT counts it), so a
        # pragma that fits WITHOUT one falls outside WITH it. Both halves are asserted,
        # which is what makes the BOM accounting load-bearing rather than decorative:
        # this payload ends the pragma at byte 128 with no BOM and at 131 with one.
        payload = b"//" + b"x" * (window - len(b".pragma library") - 3) + b"\n.pragma library\n"
        js_bom = write("Bom.js", b"\xef\xbb\xbf" + payload)
        js_nobom = write("NoBom.js", payload)
        # The three filename shapes Qt itself skips.
        js_lower = write("lower.js", b"var x = 1;\n")
        js_multidot = write("Foo.bar.js", b"var x = 1;\n")
        js_nonascii = write("Ärger.js", b"var x = 1;\n")

        saved_repo = g["REPO"]
        g["REPO"] = d
        try:
            i18n = {v.path for v in rule_i18n_cpp(
                [i18n_include, i18n_call, i18n_comment, i18n_test, i18n_allowed, i18n_member])}
            cfg = {v.path for v in rule_config_keys([cfg_bad, cfg_accessor, cfg_def, cfg_test, cfg_other])}
            # The MESSAGES too, not just which paths were reported: js_bom is reported by
            # either of two arms, and membership alone cannot tell them apart. Dropping the
            # BOM term from the window arithmetic left it reported by the not-alone-on-its-
            # own-line arm instead, which sends the author to move a line that is already
            # correct — so the mutation survived a membership-only assertion.
            js_all = rule_js_pragma(
                [js_ok, js_missing, js_late, js_commented, js_trailing, js_bom, js_nobom,
                 js_lower, js_multidot, js_nonascii])
            js_msg = {v.path: v.message for v in js_all}
        finally:
            g["REPO"] = saved_repo

        # Keying the messages by path is only sound while the rule reports at most one
        # violation per file, which is its shape today (the window arm and the not-alone arm
        # are the two branches of one else). A second violation for one path would overwrite
        # the first here and the message assertions below would read whichever arm ran last,
        # so the collision is asserted rather than assumed.
        if len(js_msg) != len(js_all):
            bad.append(f"rule_js_pragma reported {len(js_all)} violations across {len(js_msg)} paths, "
                       f"so a message assertion below reads whichever arm ran last")

        for path, want, why in (
            (i18n_include, True, "an #include <KLocalizedString> in C++"),
            (i18n_call, True, "an i18nc() call in C++"),
            (i18n_comment, False, "i18n text inside a C++ COMMENT (strip_c_comments)"),
            (i18n_test, False, "an i18n() call under /tests/, which the rule exempts"),
            (i18n_allowed, False, "an i18n() call in a file on I18N_BRIDGE_ALLOW"),
            (i18n_member, False, "a MEMBER or namespaced call whose name ends in i18n"),
        ):
            if (path in i18n) != want:
                bad.append(f"rule_i18n_cpp {'missed' if want else 'reported'} {why}")

        for path, want, why in (
            (cfg_bad, True, "an inline v2 config dot-path outside the definition files"),
            (cfg_accessor, False, "a ConfigDefaults:: accessor call"),
            (cfg_def, False, "the dot-path literal inside configkeys.h, which defines it"),
            (cfg_test, False, "a dot-path literal under /tests/, which pins the on-disk layout"),
            (cfg_other, False, "a dotted literal that names no v2 group"),
        ):
            if (path in cfg) != want:
                bad.append(f"rule_config_keys {'missed' if want else 'reported'} {why}")

        js = set(js_msg)
        # The fourth field is a substring the MESSAGE must carry, for a probe that two arms
        # can both report. None where membership is enough.
        for path, want, why, needle in (
            (js_ok, False, "a .pragma library on line 1", None),
            (js_missing, True, "a PascalCase .js library with no .pragma library at all", None),
            (js_late, True, f"a .pragma library ending past Qt's {window}-byte window", None),
            (js_commented, True, "a .pragma library that only appears inside a comment", None),
            (js_trailing, True, "a .pragma library trailing another statement on the same line", None),
            (js_bom, True, "a .pragma library pushed past the window by a UTF-8 BOM", "past Qt's"),
            (js_nobom, False, "the same payload WITHOUT a BOM, which fits the window exactly", None),
            (js_lower, False, "a lowercase .js basename, which gets no qmldir entry", None),
            (js_multidot, False, "a multi-dot .js basename, which CMake's EXT test skips", None),
            (js_nonascii, False, "a non-ASCII-initial .js basename, which Qt's ^[A-Z] skips", None),
        ):
            if (path in js) != want:
                bad.append(f"rule_js_pragma {'missed' if want else 'reported'} {why}")
            elif want and needle and needle not in js_msg[path]:
                bad.append(f"rule_js_pragma reported {why} through the WRONG ARM: expected a message carrying "
                           f"{needle!r}, got {js_msg[path]!r}")
    return bad


def rule_coverage_failures(partition_readable) -> list[str]:
    """Every fake-tree arm in this module, in one call.

    One entry point rather than nine imports, so conventions_selftest.py names this module
    once and adding an arm here needs no edit there. Order reads cheapest-first — the
    precondition and the SPDX window, then the wiring, then the rules — but it is only a
    reading order: no arm depends on running before or after another, which was checked by
    running them all in reverse with every redirected global restored.
    """
    return [*_precondition_failures(partition_readable),
            *_spdx_suffix_failures(partition_readable),
            *_wiring_failures(partition_readable),
            *_size_ratchet_failures(partition_readable),
            *_baseline_writer_failures(partition_readable),
            *_license_tree_failures(partition_readable),
            *_remaining_rule_failures(partition_readable),
            *_dead_stanza_failures(),
            *_prose_extraction_failures(partition_readable)]
