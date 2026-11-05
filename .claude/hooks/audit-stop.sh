#!/bin/sh
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Stop hook for the code-audit loop (.claude/skills/code-audit, Rule 0 and step 1.6).
#
# While .claude/audit-state.json has complete:false, a turn that ends mid-loop is handed back with the phase's
# next step. The state file is looked up at the git top level first, so an audit run inside a
# .claude/worktrees/<name>/ worktree finds its own file, then in $CLAUDE_PROJECT_DIR, then in the cwd.
#
# It lets the turn end when: there is no state file or no jq; the transcript on stdin never invoked code-audit
# (the file belongs to another session); complete is true; awaiting_agents is true (subagent reports need the turn
# to end); or the file is over six hours old.

f=
for d in "$(git rev-parse --show-toplevel 2>/dev/null)" "$CLAUDE_PROJECT_DIR" .; do
    if [ -n "$d" ] && [ -f "$d/.claude/audit-state.json" ]; then
        f="$d/.claude/audit-state.json"
        break
    fi
done
[ -n "$f" ] || exit 0
command -v jq >/dev/null 2>&1 || exit 0

tp=$(jq -r '.transcript_path // empty' 2>/dev/null)
if [ -n "$tp" ] && [ -f "$tp" ] && ! grep -qE '"content":"(<command-message>[^"]*</command-message>\\n)?<command-name>/code-audit</command-name>|"input":\{"skill":"code-audit"' "$tp"; then
    jq -n --arg f "$f" '{systemMessage: "Audit loop guard: \($f) belongs to another session (this transcript never invoked code-audit), so it does not block."}'
    exit 0
fi

if ! jq -e . "$f" >/dev/null 2>&1; then
    jq -n --arg f "$f" '{decision: "block", reason: "The code-audit loop guard found \($f) but it is not valid JSON. Repair it (or delete it if no audit is running), then continue the loop."}'
    exit 0
fi
jq -e '.complete == true' "$f" >/dev/null 2>&1 && exit 0
if jq -e '.awaiting_agents == true' "$f" >/dev/null 2>&1; then
    printf '%s\n' '{"systemMessage":"Audit loop paused: waiting on dispatched partition reviewers. The loop resumes when their reports arrive."}'
    exit 0
fi
if [ -n "$(find "$f" -mmin +360 2>/dev/null)" ]; then
    jq -n --arg f "$f" '{systemMessage: "The audit-loop guard ignores the stale \($f) (>6h). Delete it or set complete:true."}'
    exit 0
fi

jq --arg f "$f" '
  ([.findings[]? | select(.status == "open")] | length) as $open
  | (.pass // 1) as $pass
  | (if .phase == "B" then "B" else "A" end) as $phase
  | {decision: "block",
     reason: ("code-audit loop is ACTIVE (Phase \($phase), pass \($pass), \($open) findings open) and \($f) has complete:false. "
       + "Rule 0 of the code-audit skill forbids ending the turn before step 12. Do NOT ask the user whether to continue: "
       + "invoking the skill already authorized every pass. "
       + (if $phase == "B"
          then "Phase B: fix the open findings in severity order, CRITICAL and HIGH first (step 5), build, test and verify live (step 6), then the confirmation round: step 9 fresh read, step 10 re-analyze, step 11 decision."
          else "Phase A is read-only: fix nothing. Run the next full audit pass by dispatching every partition reviewer (step 8.5), then step 9 fresh read, step 10 re-analyze, step 11 decision."
          end)
       + " The loop ends only via the step 11 predicate followed by the step 12 Final Report, which is when you set complete:true.")}' "$f"
