#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
"""Validate bundled data JSON against the committed JSON Schemas.

The schemas under data/schemas/ are the single source of truth shared
with runtime validation: phosphor-fsloader's SchemaValidator (valijson)
compiles the same schema files at load time. This script is the
author-time gate — run from lefthook on commit and from CI — so a
malformed bundled data file fails review rather than shipping and being
skipped at runtime.

The schema dialect is Draft 7 (what valijson supports), validated here
with the `jsonschema` package's Draft7Validator so the two engines agree.

Usage:
    validate-json-schemas.py [--root DIR] [FILE ...]

Both CI and the lefthook hook run with no FILE arguments, so every data file
covered by the SCHEMA_MAP is validated (a schema edit re-checks all of its
data). Passing explicit FILEs is a manual convenience: only those that fall
under a mapped directory are validated, and unmapped files are ignored.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path

# Sentinel set before re-execing into the bootstrapped interpreter, so a second
# failed import there aborts instead of looping.
_BOOTSTRAP_ENV = "PZ_JSONSCHEMA_BOOTSTRAPPED"

# Maps a schema (relative to the source root) to the glob(s) of data
# files it governs. Add an entry here when a new document type gets a
# schema; the runtime side embeds the same schema file via RCC.
SCHEMA_MAP: dict[str, list[str]] = {
    "data/schemas/layout.schema.json": ["data/layouts/*.json"],
    "data/schemas/curve.schema.json": ["data/curves/*.json"],
    "data/schemas/animation-metadata.schema.json": ["data/animations/*/metadata.json"],
    "data/schemas/shader-metadata.schema.json": ["data/shaders/*/metadata.json"],
    "data/schemas/whatsnew.schema.json": ["data/whatsnew.json"],
}


def fail(msg: str) -> None:
    print(f"validate-json-schemas: {msg}", file=sys.stderr)


def load_json(path: Path) -> object:
    with path.open(encoding="utf-8") as handle:
        return json.load(handle)


# Keywords whose values are literal data instances, not subschemas, so a
# "format" key nested inside them is a data field, not the JSON Schema format
# keyword, and must not be collected.
_INSTANCE_KEYWORDS = frozenset({"default", "const", "examples", "enum"})

# Keywords whose value is a map of subschemas keyed by arbitrary names. Those
# names can collide with _INSTANCE_KEYWORDS (e.g. a property literally named
# "default"), so recurse into the map's values directly rather than applying the
# instance-keyword skip to the arbitrary keys.
_SUBSCHEMA_MAP_KEYWORDS = frozenset({"properties", "patternProperties", "definitions", "$defs"})


def formats_used(node: object) -> set[str]:
    """Collect every JSON Schema `format` keyword string used in a schema.

    Distinguishes the `format` keyword from a data field named "format": values
    of instance keywords (default/const/examples/enum) are skipped, while
    subschema-map values (e.g. a property named "default") are still traversed.
    """
    found: set[str] = set()
    if isinstance(node, dict):
        fmt = node.get("format")
        if isinstance(fmt, str):
            found.add(fmt)
        for key, value in node.items():
            if key in _INSTANCE_KEYWORDS:
                continue
            if key in _SUBSCHEMA_MAP_KEYWORDS and isinstance(value, dict):
                for subschema in value.values():
                    found |= formats_used(subschema)
            else:
                found |= formats_used(value)
    elif isinstance(node, list):
        for item in node:
            found |= formats_used(item)
    return found


def ensure_jsonschema() -> None:
    """Make `jsonschema` importable, bootstrapping a cached venv if needed.

    Returns normally once jsonschema can be imported. If it is missing, this
    creates (once) a cached venv under the user cache dir, installs
    jsonschema[format] into it, and re-execs this script with that interpreter.

    This keeps the lefthook hook a hard gate without forcing every contributor
    to install anything: a global `pip install` is unavailable or PEP 668
    blocked on most modern distros, so a managed venv is the portable path. CI
    already has jsonschema, so the import succeeds there and this is a no-op.

    Exits with status 2 only if the bootstrap itself fails (e.g. an offline
    first run) so the caller treats "validator unavailable" as a hard failure
    rather than silently skipping validation.
    """
    try:
        import jsonschema  # noqa: F401
        return
    except ImportError:
        pass

    if os.environ.get(_BOOTSTRAP_ENV) == "1":
        fail("jsonschema still unavailable after bootstrap")
        sys.exit(2)

    cache_root_env = os.environ.get("XDG_CACHE_HOME")
    try:
        cache_root = Path(cache_root_env) if cache_root_env else (Path.home() / ".cache")
    except RuntimeError:
        # Neither XDG_CACHE_HOME nor a resolvable home dir: nowhere to cache a
        # venv, so fail closed rather than crash with an uncaught traceback.
        fail("cannot locate a cache directory to bootstrap jsonschema (set XDG_CACHE_HOME or HOME)")
        sys.exit(2)
    venv_dir = cache_root / "plasmazones" / "jsonschema-venv"
    venv_py = venv_dir / "bin" / "python"
    # Success stamp written only AFTER pip install returns 0. Gate on BOTH the
    # stamp and the interpreter: the stamp catches a partially-built venv (e.g.
    # an offline run where venv creation succeeds but pip install fails), and
    # venv_py.exists() catches a venv whose interpreter went missing or dangling
    # (a rolling-distro Python minor bump leaves bin/python a broken symlink).
    # Either condition rebuilds the venv rather than re-execing into a dead
    # interpreter. Mirrors the extract-once stamp used for vendored Luau.
    ready_stamp = venv_dir / ".jsonschema-installed"

    if not (ready_stamp.exists() and venv_py.exists()):
        print("validate-json-schemas: bootstrapping JSON-schema validator (one-time)...", file=sys.stderr)
        import shutil
        import subprocess
        import venv as venv_module

        try:
            # Clear any partial/stale venv left by a prior failed attempt.
            if venv_dir.exists():
                shutil.rmtree(venv_dir)
            venv_module.create(venv_dir, with_pip=True)
            subprocess.run(
                [str(venv_py), "-m", "pip", "install", "--quiet", "jsonschema[format]"],
                check=True,
            )
            ready_stamp.touch()
        except Exception as exc:  # noqa: BLE001 - any failure means we cannot validate
            fail(f"could not bootstrap jsonschema ({exc}); install it manually or connect to a network")
            sys.exit(2)

    # Re-exec this script under the bootstrapped interpreter, preserving args.
    os.environ[_BOOTSTRAP_ENV] = "1"
    try:
        os.execv(str(venv_py), [str(venv_py), os.path.abspath(__file__), *sys.argv[1:]])
    except OSError as exc:
        # Defense in depth: the gate above rebuilds a missing/dangling venv, so
        # this should be unreachable, but a dead interpreter must fail closed at
        # exit 2 rather than surface an uncaught traceback at exit 1.
        fail(f"could not run the bootstrapped jsonschema interpreter ({exc})")
        sys.exit(2)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--root",
        type=Path,
        default=Path.cwd(),
        help="Source root that the SCHEMA_MAP paths are relative to (default: cwd).",
    )
    parser.add_argument(
        "files",
        nargs="*",
        type=Path,
        help="Specific files to validate (default: every mapped data file).",
    )
    args = parser.parse_args()

    # Guarantee the validator is present (bootstraps a cached venv on first use
    # if needed, or exits non-zero if it cannot). Never silently skips.
    ensure_jsonschema()
    from jsonschema import Draft7Validator, FormatChecker

    # Enforce string `format` (e.g. "date") so this author-time gate matches
    # runtime: valijson checks date/date-time/time formats, and jsonschema
    # only does so when handed a FormatChecker. Keeps the two engines aligned.
    format_checker = FormatChecker()

    root: Path = args.root.resolve()

    # Resolve the explicit file list (if any) to absolute paths for
    # membership testing against each schema's matched set.
    requested: set[Path] | None = None
    if args.files:
        requested = {(p if p.is_absolute() else (root / p)).resolve() for p in args.files}

    failures = 0
    checked = 0

    for schema_rel, globs in sorted(SCHEMA_MAP.items()):
        schema_path = root / schema_rel
        if not schema_path.is_file():
            fail(f"schema not found: {schema_rel}")
            failures += 1
            continue

        try:
            schema = load_json(schema_path)
            Draft7Validator.check_schema(schema)
        except Exception as exc:  # noqa: BLE001 - report any schema defect (parse or schema error)
            fail(f"invalid schema {schema_rel}: {exc}")
            failures += 1
            continue

        # Fail closed if the schema uses a `format` this environment can't
        # actually check (the jsonschema[format] extra supplies date-time, email,
        # uri, etc.; "date" is built in). Without this, an unenforceable format
        # would be silently skipped, letting a bad value pass locally while CI
        # (which installs the extra) rejects it. The bootstrap venv and CI both
        # have the extra, so this only guards a manual run on a bare jsonschema.
        unenforceable = sorted(f for f in formats_used(schema) if f not in format_checker.checkers)
        if unenforceable:
            fail(f"{schema_rel}: cannot enforce format(s) {', '.join(unenforceable)}; install jsonschema[format]")
            failures += 1
            continue

        validator = Draft7Validator(schema, format_checker=format_checker)

        targets: list[Path] = []
        for pattern in globs:
            targets.extend(sorted(root.glob(pattern)))

        # In full-set mode (no explicit files), a mapped glob that matches
        # nothing means a data directory was renamed/emptied out from under the
        # schema and validation silently stopped covering it. Fail rather than
        # report a misleading green. (Skipped in explicit-files mode, where a
        # schema legitimately has no staged files.)
        if requested is None and not targets:
            fail(f"{schema_rel}: no data files matched {', '.join(globs)} (mapping is stale?)")
            failures += 1
            continue

        for target in targets:
            if requested is not None and target.resolve() not in requested:
                continue
            checked += 1
            rel = target.relative_to(root)
            try:
                document = load_json(target)
            except json.JSONDecodeError as exc:
                fail(f"{rel}: malformed JSON: {exc}")
                failures += 1
                continue

            errors = sorted(validator.iter_errors(document), key=lambda e: list(e.path))
            if errors:
                failures += 1
                fail(f"{rel}: fails {schema_rel}")
                for err in errors:
                    location = "/" + "/".join(str(p) for p in err.path) if err.path else "(root)"
                    print(f"    {location}: {err.message}", file=sys.stderr)

    if failures:
        fail(f"{failures} validation failure(s) across {checked} file(s)")
        return 1

    print(f"validate-json-schemas: OK ({checked} file(s) validated)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
