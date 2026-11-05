---
name: record-lesson
description: "Record what PlasmaZones work taught, in the place the next session will look for it. Use it after a bug took more than one attempt to find, when the same mistake happened twice, when a debug or verification technique worked, when an audit pass kept re-finding the same shape, or when the user corrects how you work. Use when: remember this, note this for next time, record the lesson, don't let this happen again."
---

<!--
SPDX-FileCopyrightText: 2026 fuddlesworth
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Record a lesson

The next session does not see a lesson that stays only in the chat. Put the lesson where a session will read it.

| Lesson | Where | Form |
| --- | --- | --- |
| Why code does something that looks wrong (a KWin effect API rule, a Qt or Wayland quirk, a compositor retry) | A comment at that code | One or two lines that say why, and name the API, protocol or issue |
| A cross-cutting trap that a future change could bring back | CLAUDE.md, "Lessons that cost a session" | One bullet: the trap and the rule |
| A step of a procedure that a session missed | The skill that covers it (`pz-build`, `pz-verify-live`, `pz-add-setting`, `pz-algorithm-api`, `pz-shader-pack`, `shader-theme`, `code-audit`) | An imperative line with the reason |
| A command that must never run | `.claude/hooks/guard.py`, with a case in `.claude/hooks/test_guard.py` | A rule with the message to show. Run `python3 .claude/hooks/test_guard.py` |
| A past bug shape in one domain | "Known past-bug shapes to check for" in that domain's `.claude/agents/review/pz-*-reviewer.md` | One bullet: what went wrong, what to check, naming the real symbol |
| A rule that a program can decide by inspection | A rule in `scripts/check-conventions.py`, with a selftest case | Only when the shape recurred; see the existing rules for the form |
| A script that worked | `scripts/` with a usage comment and the GPL SPDX header, or the skill's own directory if only Claude uses it | The scratchpad does not survive the session |
| How the user wants to work | The session memory | With the reason the user gave |

Prove it first. For a rule from one occurrence, write what that occurrence needed, not a general rule. Write the
general rule only when a second case confirms it. Name a symbol or path only after you grep the current tree for it.
When a lesson turns out wrong, correct it. Do not add a second lesson that contradicts it.

CLAUDE.md is long and every session loads it. A lesson goes there only when it crosses domains. A trap that lives in
one domain goes to that domain's reviewer, and a step goes to its skill.

Commit lessons with the work that found them, or alone as `docs(claude): note ...`.
