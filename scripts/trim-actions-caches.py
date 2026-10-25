#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
"""Delete the GitHub Actions caches no CI run will restore again.

The repository's caches share 10 GB, and past that GitHub evicts the least
recently used, which can be the very entry the next run on main restores. CI
saves a new compiler cache on every run (.github/workflows/ci.yml), so without
this the store fills with entries nothing reads:

  * a closed pull request's caches, which only that pull request restores;
  * a deleted branch's, and a tag's, which no later run restores;
  * on a live ref, every entry of a cache family but the newest. A key is its
    family plus a part that changes from one entry to the next: a run id and
    attempt (ccache-arch-ON-<run>-<attempt>), an ISO week
    (pacman-arch-<year>-<week>), or the timestamp the old ccache action used.
    A run restores the newest entry of its family, so the older ones are dead.

Run by .github/workflows/cache-cleanup.yml. Needs gh with GH_REPO set and a
token allowed actions: write. --dry-run lists what would go and deletes nothing.
"""

import argparse
import json
import re
import subprocess
import sys
import time

VOLATILE = re.compile(
    r"-\d+-\d+$"  # <run id>-<attempt>
    r"|-\d{4}-\d{2}$"  # <ISO year>-<ISO week>
    r"|-\d{4}-\d{2}-\d{2}T[0-9:.]+Z$"  # the old ccache action's timestamp
)
PULL_REF = re.compile(r"refs/pull/(\d+)/(?:merge|head)")


def family(key):
    """The cache family @key belongs to: the key without its volatile end."""
    return VOLATILE.sub("", key, count=1)


def plan(caches, open_pulls, branches):
    """The caches to delete, as (cache, reason) pairs."""
    doomed = []
    newest = {}
    for cache in caches:
        ref = cache["ref"]
        pull = PULL_REF.fullmatch(ref)
        if pull and int(pull.group(1)) not in open_pulls:
            doomed.append((cache, f"pull request #{pull.group(1)} is closed"))
        elif ref.startswith("refs/heads/") and ref[len("refs/heads/"):] not in branches:
            doomed.append((cache, "its branch is gone"))
        elif ref.startswith("refs/tags/"):
            doomed.append((cache, "a tag's, which no later run restores"))
        else:
            newest.setdefault((ref, family(cache["key"])), []).append(cache)
    for entries in newest.values():
        # The newest by creation is the one a prefix restore picks next.
        entries.sort(key=lambda c: (c["createdAt"], c["id"]), reverse=True)
        doomed += [(cache, f"replaced by {entries[0]['key']}") for cache in entries[1:]]
    return doomed


def gh(*args):
    return subprocess.run(["gh", *args], check=True, capture_output=True, text=True).stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--dry-run", action="store_true", help="list what would be deleted, delete nothing")
    args = parser.parse_args()

    caches = json.loads(gh("cache", "list", "--limit", "100000", "--json", "id,key,ref,sizeInBytes,createdAt"))
    open_pulls = {p["number"] for p in json.loads(gh("pr", "list", "--state", "open", "--limit", "100000", "--json", "number"))}
    branches = set(gh("api", "--paginate", "repos/{owner}/{repo}/branches", "--jq", ".[].name").split())
    # A listing that came back short would read every branch as gone and take
    # all of their caches with it.
    default = gh("api", "repos/{owner}/{repo}", "--jq", ".default_branch").strip()
    if default not in branches:
        sys.exit(f"the branch listing lacks the default branch {default}: deleting nothing")

    doomed = plan(caches, open_pulls, branches)
    failed = 0
    for cache, why in doomed:
        print(f"{cache['sizeInBytes'] / 1e6:8.1f} MB  {cache['ref']}  {cache['key']}: {why}")
        if args.dry_run:
            continue
        try:
            gh("cache", "delete", str(cache["id"]))
        except subprocess.CalledProcessError as error:
            failed += 1
            print(f"    not deleted: {error.stderr.strip()}")
        time.sleep(0.2)  # GitHub allows 400 cache deletions a minute
    total = sum(c["sizeInBytes"] for c in caches)
    freed = sum(c["sizeInBytes"] for c, _ in doomed)
    verb = "would free" if args.dry_run else "freed"
    print(f"{verb} {freed / 1e9:.2f} of {total / 1e9:.2f} GB ({len(doomed)} of {len(caches)} caches)")
    if failed:
        # The next pass retries them. Failing the job would put a red mark on
        # main for something no build depends on.
        print(f"::warning::{failed} caches were not deleted")


if __name__ == "__main__":
    main()
