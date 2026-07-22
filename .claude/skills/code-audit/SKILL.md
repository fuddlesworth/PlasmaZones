---
name: "Code Audit"
description: "Audits uncommitted changes, branches, or PRs for bugs, edge cases, SOLID/DRY/SRP, architecture, security, performance, and CLAUDE.md violations — then automatically fixes ALL issues and re-audits in a loop until clean. Use when: audit, review, code review, review this PR, check code, look over this, inspect code, or any variation."
---

# Code Audit

**This skill is a LOOP. You analyze, fix ALL findings (every severity), re-read files from disk, re-analyze, and repeat until zero findings remain. Stopping after one pass is a failure to follow instructions.**

---

## RULE 0: DO NOT YIELD (BINDING, read this before anything else)

Once the loop starts, **ending your turn before step 12 is a violation.** Not a judgement call, not a courtesy — a violation, in the same category as skipping a file.

The loop terminates in exactly one way: you reach step 11, its predicate says stop, and you emit the step 12 Final Report. Until that happens you keep working, in the same turn, without checking in.

**These are all violations. Every one of them ends your turn:**

| What it looks like | Why it feels justified | What it is |
|---|---|---|
| "Which would you like: (1) continue, (2) fix first, (3) something else?" | offering control | abandoning the loop |
| "Want me to run Pass 3?" | confirming before cost | the loop already answered this |
| "Here's what I found so far — let me know how to proceed." | transparency | a status report nobody asked for |
| "Pass 2 is done. Say the word and I'll continue." | politeness | waiting for permission you have |
| "Given the yield was thin, I'd expect diminishing returns — your call." | good judgement | substituting your stopping rule for the skill's |
| Stopping because the turn is long / context is large / a lot happened | prudence | a cost-shortcut, and cost is not your call |

**You already have permission for the whole loop.** Invoking the skill IS the authorization for every pass, every fix, every re-dispatch, up to the step 11 termination predicate. Asking again is not caution; it is failing to follow the instruction you were given.

**The one exception:** a genuine blocker that makes forward progress impossible — a build that cannot be fixed from inside the audit, a credential prompt, a destructive action outside audit scope, or a step-5 finding explicitly marked NEEDS USER INPUT. "This is a lot of work," "I'm unsure whether to continue," and "the user might want to weigh in" are NOT blockers.

**Standing vs one-time instructions.** The loop is a STANDING instruction: it stays in force until step 12 without renewal. A user message like "commit and push" is a ONE-TIME instruction about the current state; it does not become standing permission. Treating these backwards — re-asking for the standing one, generalising the one-time one — is a specific known failure of this skill. Do not.

## Usage

```
/code-audit <scope>
```

**Examples:**

- `/code-audit` — audit uncommitted changes (default)
- `/code-audit branch` — audit current branch vs main
- `/code-audit staged` — audit staged changes only
- `/code-audit PR` or `/code-audit PR #123` — audit a pull request
- `/code-audit src/config/settings.cpp src/core/zone.h` — audit specific files

If `<scope>` is provided, use it directly — do not ask the user to clarify. If omitted, default to uncommitted changes.

**Scope is immutable.** `/code-audit PR #N` and `/code-audit branch` audit the FULL declared scope. You may NOT narrow to "changed since last audit," "recent commits," "since pass N," or "files I haven't already reviewed." Prior audit history (other passes, other commits, review comments saying "reviewed") does NOT reduce coverage on the current run. The skill is stateless with respect to previous runs — every invocation re-audits the entire declared scope from scratch.

---

## Steps

### 1. Determine Scope

Use the argument passed to `/code-audit` if provided. Otherwise infer from context or default to uncommitted:

| Scope                     | Diff source                                     |
| ------------------------- | ----------------------------------------------- |
| **Uncommitted** (default) | `git diff` + `git diff --cached` + `git status` |
| **Staged only**           | `git diff --cached`                             |
| **Branch vs base**        | `git diff main...HEAD`                          |
| **Pull request**          | `gh pr diff <number>` or `gh pr diff`           |
| **Specific files**        | Read named files directly                       |

For each file in scope, read it FULLY. Files >800 lines must be read in chunks (using Read tool offset/limit parameters) until 100% of the file has been covered. Diffs alone are insufficient. Partial reads ("changed functions plus context") are forbidden — they cause cross-file inconsistencies, architectural drift, and pattern violations to be missed entirely.

### 1.5 File Inventory

Before analysis, enumerate every file in scope and produce a numbered checklist:

1. Run the appropriate diff command from step 1 to extract the file list.
2. For each file, get its line count (`wc -l <file>` for existing files; new files in the diff start at 0 and have their final size after the patch).
3. Output the inventory as a numbered list:

```
File Inventory (N files in scope):
  1. path/file1.ext (NNN lines)
  2. path/file2.ext (NNN lines)
  ...
  N. path/fileN.ext (NNN lines)
Total: N files, M lines.
```

### 1.6 Audit State File (BINDING)

Write `.claude/audit-state.json` (relative to the repo root / cwd — this exact path, so the Stop hook can find it) immediately after the inventory, and **update it at the end of every pass, before step 9**:

```json
{
  "scope": "PR #822",
  "partitions": 6,
  "pass": 1,
  "inventory_files": 57,
  "complete": false,
  "findings": [
    {"id": "F1", "sev": "HIGH", "file": "src/foo.cpp:42", "status": "open", "raised_pass": 1}
  ]
}
```

**`awaiting_agents`** (optional, default false). Set it `true` in the same action that dispatches a pass's partition reviewers, and back to `false` the moment their reports land. Dispatched subagents report by task-notification, which mechanically requires the turn to end — that is not a yield, and the Stop hook lets it through when this flag is set. It is NOT a general escape: with the flag absent or false, a turn ending mid-loop is still blocked. Setting it while no agents are actually in flight is the same falsification as setting `complete: true` early.

`complete` stays `false` for the whole loop. Set it to `true` **only** as the last action of step 12, after the Final Report is written. A **`Stop` hook reads this file and refuses to let your turn end while `complete` is `false`** — so if you try to yield mid-loop, you will be handed back control with an instruction to continue. Do not set `complete: true` to escape the loop; that is falsifying the audit. The only legitimate ways out are the step 11 predicate and a genuine blocker (Rule 0's exception), and a blocker still requires emitting step 12 with the verdict explaining what stopped you.

`status` is one of `open` | `fixed` | `descoped`. `descoped` additionally requires `"descope_reason"` and `"raised_pass"` ≥ 2 passes earlier (see step 5).

This exists because your memory of the loop decays as context fills, and the decay is silent. The file is the authority, not your recollection:

- **"Am I done?" is a QUERY, not a judgement.** Any finding with `status: "open"` means the answer is no. Re-read the file at the start of every pass rather than recalling state.
- **It makes silent descoping impossible.** A finding you quietly stopped working on sits in the file as `open` and blocks the CLEAN verdict.
- **It survives context loss.** If the conversation is summarised mid-loop, this file tells you exactly where you are.

At the start of every pass ≥ 2, re-read it and echo one line before doing anything else:

```
LOOP STATE: pass N of ≤8 · P partitions · X findings open · next: step 9 fresh read · DO NOT YIELD (Rule 0)
```

This list is the coverage checklist for steps 9 and 12. Every file here MUST be read fully and analyzed. Skipping any file is a skill failure.

### 2. Read Project Rules

Read `CLAUDE.md` and any files it references. Skip if none exists.

### 2.25 Tooling: ast-grep + grep

The audit uses two complementary search tools. **Prefer ast-grep for code; use plain grep for text and comments.**

`ast-grep` matches the parse tree, so it ignores hits inside strings, comments, and tokens that happen to spell the pattern. Use it whenever the pattern is a code shape (function call, declaration, control-flow construct). `grep` works on text — use it for comments, docstrings, error messages, and exact string literals.

Common audit-time patterns (C++ shown; ast-grep supports most languages — replace `--lang cpp` accordingly):

| Goal | Command |
|---|---|
| Find every caller of a method (catch dead code) | `ast-grep --pattern '$X.methodName($$$)' --lang cpp` and `ast-grep --pattern '$X->methodName($$$)' --lang cpp` |
| Find every instantiation of a class | `ast-grep --pattern 'ClassName $_' --lang cpp` |
| Find every `Q_ASSERT` (check release-build pair) | `ast-grep --pattern 'Q_ASSERT($COND)' --lang cpp` |
| Find every `if-log-no-return` (guard that doesn't guard) | `ast-grep --pattern 'if ($COND) { qCWarning($$$); }' --lang cpp` |
| Find every TODO/FIXME/HACK marker | `grep -rn 'TODO\|FIXME\|HACK\|XXX' --include="*.cpp" --include="*.h"` |
| Find a stale comment claim (e.g., "WHY ONLY THESE THREE") | `grep -rn "WHY ONLY THESE" --include="*.cpp" --include="*.h"` |
| Find AI-ism punctuation in user-facing strings | `grep -rn '—' <changed UI/data/doc files>` for em-dashes; then check quoted UI strings for clause-joining `;` and stand-in ` - ` (exclude code comments, log messages, and backticked code) |
| Find every use of an accessor that should now route through a helper | `ast-grep --pattern 'QStringLiteral("RenderingBackend")' --lang cpp` (catches the literal even when wrapped) |
| Find every signature using `bool autotileMode` after a rename to `QString modeToken` | `ast-grep --pattern 'bool autotileMode' --lang cpp` |
| Find every store-mutation that should trigger a repaint | `ast-grep --pattern '$X->setRules($$$)' --lang cpp` then verify each callsite is followed by `effects->addRepaintFull()` or equivalent |

Don't memorise the list — the principle is: **if a finding is "this pattern is used at site A, but should also be checked at sites B…N", you have a grep/ast-grep step to take before you finish the finding.** Reporting a refactor-completeness finding without having enumerated the matches is incomplete.

### 2.5 Parallel Reviewer Decision

Compute scope size from the inventory:

- **≤10 files AND ≤2000 lines changed**: single-threaded analysis — you do step 3 directly.
- **>10 files OR >2000 lines changed**: MUST partition into domain-specialist reviewer agents. Single-threaded analysis on a large scope is a skill failure — no single context window can hold deep analysis of 50+ files.

For partitioned scopes:

1. Group files by coherent domain (e.g. `controller/`, `rendering/`, `qml/`, `tests/`, `config/`). 4-6 partitions is typical; aim for ≤15 files per partition.
2. **Reviewer selection.** Check the available agent types for project-defined domain reviewers (by convention in `.claude/agents/review/`, e.g. `pz-*-reviewer` in PlasmaZones; each one's description names the directories/domains it covers). If any exist, partition boundaries SHOULD follow the specialists' domains, and each partition MUST be dispatched to the specialist whose description matches it — a specialist encodes stack expertise and past-bug patterns a generic reviewer lacks. Only for a partition no specialist covers (or when a project defines none) fall back to `subagent_type=code-analyzer`. Dispatch ONE `Agent` call per partition; all Agent calls go in a SINGLE message so they run in parallel.
3. Each agent prompt MUST include:
   - The exact file paths in its partition (explicit paths, not a directory glob).
   - Instructions to read every file FULLY (no partial reads, no diff-only).
   - The full step 3 analysis dimensions, listed verbatim — including refactor-completeness, comment-code synchronization, defensive-code pair audit, and side-effect completeness.
   - The project rules from `CLAUDE.md` (quoted relevant sections, not "see the file").
   - **Grep scope ≠ read scope.** The agent's *read* scope is limited to its partition files, but its *grep / ast-grep* scope is the WHOLE repo. Specifically the agent MUST grep beyond its partition for: (a) any old-pattern remnant of a refactor it's verifying ("X should now use Y" → enumerate all remaining X uses), (b) callers of any public function its partition touches, (c) comments naming a count or method/class it can verify. The partition limits where the agent reads files fully, not where it searches.
   - A request for prioritized findings in the format: `file:line — description — suggested fix — severity`.
4. Wait for all agents to return.
5. Aggregate their findings into a single deduplicated list, then continue to step 4.

If a reviewer agent returns generic boilerplate, crashes, or covers fewer files than its partition listed, re-dispatch that partition with sharper instructions. A non-responsive partition means that part of the scope wasn't audited.

### 3. Analyze

Examine every changed line and surrounding context against all dimensions. Omit dimensions with no findings.

- **Correctness**: Off-by-one, null deref, wrong operator, inverted condition, missing break/return, races, deadlocks, resource leaks, swallowed errors, type mismatches
- **Edge Cases**: Empty/null/zero/negative inputs, boundary values, unicode/locale/timezone, concurrency, partial failure
- **SOLID / DRY / SRP**: SRP violations, duplicated logic, tight coupling, god objects, feature envy, premature abstraction
- **Architecture**: Fits or fights existing design, layering violations, missing abstractions, encapsulation breaks, misleading names
- **Security**: Injection, auth/authz gaps, secrets in code, unsafe deserialization, SSRF, insufficient input validation
- **Performance**: Hot-path allocations, O(n^2) when O(n) possible, N+1 queries, blocking on async, unbounded growth
- **Project Rules**: Deviations from CLAUDE.md conventions — quote the specific rule violated
- **User-facing prose (AI-isms)**: Text a user reads — UI strings, labels, tooltips, descriptions, error/notification messages, translatable strings (`tr()`, `i18n()`/`i18nc()`, gettext), CHANGELOG / release notes, and bundled data descriptions (JSON `description`/`name`, script metadata) — must read as plain, human-written prose. Flag and rewrite: em-dash (`—`, or its escaped form) used to splice clauses or tack on an appositive; semicolons joining two independent clauses; a spaced hyphen (` - `) standing in for a dash; dramatic "Label: payload" colons used for effect; rule-of-three triads and "not just X, but Y" flourishes. Rewrite as two sentences or a plain connector (and / with / where / so / because). Do NOT flag: code comments, log / `qCWarning` messages, semicolons inside backticked code or genuine comma-bearing lists, real field-label colons (incl. Keep-a-Changelog `**Term**:` lead-ins), or a literal typographic separator between two nouns (e.g. `%1 — %2`, `Settings → X` breadcrumbs). Severity LOW (NIT for a lone separator). If the project CLAUDE.md has a "user-facing text" / plain-prose rule, treat a violation as a Project Rules finding and quote it.
- **Refactor completeness**: For any finding of the shape "X should now use Y" (an accessor was introduced, an API was renamed, a helper was extracted), grep the WHOLE repo (not just the partition) for every remaining use of the old pattern. List every site found and either fix it or mark it out-of-scope with a reason. A refactor finding that doesn't enumerate every old-pattern site is incomplete. Use `ast-grep` for code patterns (e.g. `ast-grep --pattern 'QStringLiteral("RenderingBackend")' --lang cpp`) and plain `grep` for substrings — the AST search avoids matching in unrelated tokens or comments.
- **Comment-code synchronization**: For every comment that names a quantity ("the three adaptors", "WHY ONLY THESE FOUR"), a method/class (`see ::routerFor`), or a list ("the eight raw-Qt-parented..."), verify the claim against current code. Numeric counts: literally count the items the comment describes. Named methods/classes: `ast-grep --pattern 'class $_' --lang cpp` or `grep -rn "::routerFor"` to confirm the name exists. Stale counts and dangling method references are real LOW findings — they accumulate silently and mislead future readers.
- **Defensive-code pair audit**:
  - Every `Q_ASSERT` / `Q_ASSERT_X` / `assert` / `debug_assert` MUST be paired with a release-build runtime check covering the SAME condition. `ast-grep --pattern 'Q_ASSERT($COND)' --lang cpp` then verify each match has a matching release guard. Asymmetric coverage = debug-only safety with release crashes.
  - Every release-build guard that LOGS a warning must also `return` / `throw` / otherwise prevent the downstream code from running with the bad state. A guard that only logs is a guard that doesn't guard. `ast-grep --pattern 'if ($COND) { $LOG; }' --lang cpp` and check whether `$LOG` is just `qCWarning(...)` without a control-flow exit.
  - Every late-bound dependency (any `setX()` called post-construction to wire a member) should be cleared symmetrically in shutdown / `clearEngine` / `detach` paths. Grep for `m_<name> =` assignments in setters and verify a matching `m_<name> = nullptr` in teardown.
- **Side-effect completeness**: When a store mutation affects rendered output (paint pipeline, shader inputs, opacity, geometry, animation), verify that the appropriate compositor / UI signal is also emitted (`addRepaintFull`, per-window damage, `invalidate()`, `update()`). A mutation that lands in storage but never reaches the renderer is invisible until incidental damage — a real correctness bug.

### 4. Report Findings

**Keep this brief — do NOT output the full changelog table during intermediate passes. Save the detailed changelog for the final report (step 12).** During each pass, just output a short numbered list of findings like:

```
Pass N findings:
1. [CRITICAL] `file.ext:NN` — brief description (AUTO)
2. [HIGH] `file.ext:NN` — brief description (AUTO)
3. [MEDIUM] `file.ext:NN` — brief description (AUTO)
4. [LOW] `file.ext:NN` — brief description (AUTO)
5. [NIT] `file.ext:NN` — brief description (AUTO)
```

Severity levels:

- **CRITICAL**: Data loss, security breach, or production crash.
- **HIGH**: Likely bug or significant design flaw.
- **MEDIUM**: Code smell, maintainability concern, or moderate risk.
- **LOW**: Minor style/naming/convention issue.
- **NIT**: Purely stylistic.

### 5. Fix ALL Findings

**Fix every finding at every severity level. Do not ask permission. Do not skip LOW or NIT.**

**A finding raised in THIS pass may not be descoped.** Descoping is available only for a carry-over — a finding raised in an earlier pass and re-flagged — and only per step 11's rules, with the reason written into `audit-state.json`. Deciding a fresh finding is "out of scope," "pre-existing," "shared with other code," "riskier than it's worth," or "better as a follow-up" is not a descope; it is skipping a finding, and the CLEAN verdict is then false.

Two specific dodges to refuse:

- **"Pre-existing, not introduced by this PR."** Irrelevant. The skill audits the files in scope, not the diff. If a bug is live in a file you are auditing, it is a finding.
- **"The correct fix is too structural / touches too much."** That is what "Architecturally correct, always" is for. If the proper fix is genuinely larger than the audit can land, that is one of the rare NEEDS USER INPUT cases — raise it as such and keep looping. It is not a silent drop.

When a finding offers two legitimate remedies (the finding itself says "either X or document Y"), picking one IS fixing it. Say which you picked and why.

**Fix in priority order:** CRITICAL first, then HIGH, MEDIUM, LOW, NIT. A higher-severity fix may make a lower-severity finding obsolete or change what the correct fix is.

- **AUTO**: Correct fix is unambiguous — apply it.
- **NEEDS USER INPUT**: Requires a design decision — ask the user, wait, then apply.

**Before changing any function signature, return type, or public API:** use Grep to find ALL call sites, consumers, and references. Update every one of them. Do not rely on the build to catch missed callers — some may be in dynamic code, templates, or test files that survive compilation with stale signatures.

List each fix: `file:line` — what changed.

### 6. Build Verification

Run the project build (per CLAUDE.md or standard tooling). If it fails, treat each error as CRITICAL, fix, rebuild. Do not proceed until the build is green.

### 7. Diff Review Before Commit

Run `git diff` and review the full diff of everything about to be committed. Check for:

- Unintended changes (accidental whitespace, stray edits outside the audit scope)
- Debug lines or temporary code that should not be committed
- Edits that drifted from the intended fix
- Changes to files outside the audit scope

If anything looks wrong, fix it before proceeding.

### 8. Commit & Push (PR/branch scopes only)

For PR or branch scope: stage, commit (`fix(audit): pass N — <summary>`), push.
For uncommitted local scope: skip — leave fixes uncommitted.

---

## LOOP: Repeat Until Clean (up to 8 passes)

**After every fix pass, you MUST re-read and re-analyze. Do not skip this.**

### 8.5 Pass Definition (BINDING)

A "pass" is **every partition reviewer agent from step 2.5 re-dispatched in parallel**, on every file in the original inventory, full file reads. No exceptions.

**NOT a pass:**
- A single "verifier" or "spot-check" agent
- A subset of partitions (even if "Pass N−1 was clean for them" or "no Pass N−1 changes landed in those files")
- A grep-only verification without agent dispatch
- Inlining the analysis yourself instead of using agents

If the original scope used 6 partitions, every pass ≥2 dispatches EXACTLY 6 agents. Skipping any partition because "it was clean last time," "Pass N−1 didn't touch those files," or "I'm pretty sure nothing changed there" is **forbidden**. The skill is stateless between passes by design — a clean partition can be broken by a fix in another partition that changes a shared header, modifies an interface contract, or shifts call-site behaviour. You cannot rule that out without re-reading.

**If you dispatch fewer than the established partition count from step 2.5, the pass is INVALID.** The result does not count toward the 8-pass cap, cannot contribute to a CLEAN verdict, and the loop MUST re-dispatch the missing partitions before proceeding. State the violation explicitly ("Pass N was partial: dispatched A of P partitions — re-dispatching the missing P−A now") and continue the work.

**Anti-rationalization watch.** If you find yourself reasoning along any of these lines while planning a pass, you are about to downgrade the audit, NOT optimize it:
- "Pass N−1 was clean for partition X, skip it"
- "No Pass N−1 changes touched those files"
- "A focused verifier covers the concern"
- "This should be enough"
- "Re-running on unchanged files won't catch anything new"
- "I'll dispatch fewer agents to save tokens / time / context"

Each of those is a cost-shortcut, not a value judgement. Token budgeting is the **user's** job (via `+500k`-style directives), not yours. Run the full work.

**Anti-yield watch (Rule 0).** The list above is about downgrading a pass. This one is about ending the loop, which is the more common failure. If you catch yourself thinking any of these, you are about to violate Rule 0:
- "I should check in before starting another pass"
- "The user would probably want to weigh in here"
- "I'll report what I have and let them decide"
- "This is a natural stopping point"
- "The turn is getting long / a lot has happened"
- "I'll ask whether to keep going, to be safe"
- "I'll note this as out of scope and mention it in the summary"
- "The last pass mostly found problems my own fixes caused, so maybe I should stop and surface that"

That last one is the trap that catches most often. **A pass whose findings are mostly your previous pass's damage is the loop working exactly as designed.** It is the single strongest reason to run another pass, not a reason to stop. Fixes introduce defects; that is why re-analysis exists.

None of these is caution. Each is you replacing the skill's termination predicate with your own comfort. Keep going.

### 9. Fresh Read (NO EXCEPTIONS)

Use the Read tool to re-read **ALL files in the original audit scope** — not just the ones you edited. A fix in file A can invalidate assumptions in file B (callers, shared state, interface contracts). Your context is stale for every file, not just edited ones. Call Read on each file and look at actual disk contents. Do NOT rely on memory, context, or edit diffs.

**Mandatory checklist output.** Every pass ≥2 must begin with an explicit re-read confirmation matching the inventory from step 1.5:

```
Re-read confirmation (Pass N):
  1. path/file1.ext ✓
  2. path/file2.ext ✓
  ...
  M. path/fileM.ext ✓
  Re-read: M/M files
```

If the count does not match the inventory total, the pass is invalid — return to step 9 and read the missing files. For partitioned scopes (step 2.5), re-dispatch the partitioned reviewer agents with their respective file lists for the re-analysis; do not collapse a partitioned scope into single-threaded re-reads.

### 10. Re-Analyze (Full Scope, Not Just Edits)

Using ONLY fresh file contents from step 9, run the full step 3 analysis against **every file in the audit scope** — not just files you touched. Specifically check:

- Did any fix introduce a new issue in the edited file?
- Did any fix break a caller, consumer, or sibling file that wasn't edited?
- Did any fix regress another dimension?
- Are there issues in unedited files that I missed on the first pass?
- Did I miss anything earlier that I can now see with fresh eyes?

### 10.5 Dead-Code Scan

For every public function newly introduced or non-trivially modified in this audit's scope, grep for callers across the entire repo:

```bash
# Free functions / methods — find all call sites
ast-grep --pattern '$FN(args:$$$)' --lang cpp   # template form
grep -rn "->methodName\b\|\.methodName\b\|methodName(" --include="*.cpp" --include="*.h"

# Class declarations — find all instantiations
ast-grep --pattern 'ClassName $_' --lang cpp
```

- **Zero callers** → either remove the function, or document the call path preserving it (e.g., shutdown-window safety, exported API for plugins, future-use scaffolding). A function that this audit is the first to grep for callers is a function nobody else verified is used.
- **Only self-callers (tests + the definition site)** → flag for likely-dead status; verify the test isn't the sole reason the function exists.

This step is mandatory on every pass — dead code accumulates between passes when fixes remove the last caller of a helper without removing the helper.

### 11. Decision

Evaluate in order. This predicate is the ONLY thing that may end the loop — not your read of the yield, the effort spent, or how the last pass went.

1. **Any finding in `audit-state.json` with `status: "open"`** → go to step 4. Not optional.
2. **New findings exist** (not previously raised in earlier passes) → go to step 4, repeat through 11.
3. **Only carry-over findings persist** (same finding re-flagged from a prior pass) → resolve them now: fix, or descope to a tracked artifact (issue link, PR description note, removal from scope) with the reason written into the state file. The skill is NOT allowed to re-flag the same finding for 3+ passes without action. File-size violations, missing test coverage, lossy collapses — these accumulate forever when "stay in scope" is used as a dodge. Once every carry-over is `fixed` or `descoped`, go to step 11.5.
4. **Zero findings** → go to step 11.5, then step 12.
5. **8 passes done, findings persist** → go to step 11.5, then step 12, with the verdict recording what is stuck and why.

**Every branch ends at step 12.** There is no path out of this loop that skips the Final Report. If you are about to end your turn and have not emitted step 12, you are in violation of Rule 0 — go back and finish.

**Diminishing returns is not a termination condition.** "The last pass found little," "the remaining findings are minor," and "another pass probably won't find much" do not appear in the predicate above. Only the numbered conditions do. If you want to argue a pass is not worth running, that argument belongs in the step 12 verdict AFTER the predicate says stop — never as a reason to stop early.

### 11.5 Pre-Final-Report Self-Audit

Before writing the final report, answer each question explicitly:

0a. **Rule 0 audit.** Did I end a turn anywhere between step 4 and here — to ask permission, present options, report progress, or confirm the next pass? If yes, that was a violation. Note it plainly in the final report's Summary rather than hiding it; the user needs to know the loop ran discontinuously and why.

0b. **State-file audit.** Does `audit-state.json` contain any finding with `status: "open"`? If yes, I am not at step 11.5 — return to step 4. Does every `descoped` entry carry a `descope_reason` AND a `raised_pass` at least two passes old? A fresh finding marked `descoped` is a skipped finding, and the verdict cannot be CLEAN.

0. **Pass-count audit.** For every pass N ≥ 2, count the agent dispatches. Did I dispatch the SAME number of partition reviewers that step 2.5 established for Pass 1? If any pass was partial (fewer agents, a focused verifier, a single re-check, a grep-only walk in place of agent dispatch) — that pass is INVALID per step 8.5. Re-run the missing partitions and continue the loop before writing the final report. Do NOT classify a partial pass as "clean" — partial passes have no verdict at all.

1. Did I read every file in the inventory FULLY? (Compare step 1.5 inventory to actual Read calls.)
2. Did I analyze every file against every step-3 dimension — not just the dimensions where I expected findings? Including refactor-completeness, comment-code synchronization, defensive-code pair audit, side-effect completeness?
3. For partitioned scopes: did every reviewer agent return concrete findings (or explicitly "none")? If any agent crashed, returned generic boilerplate, or covered fewer files than its partition listed, that partition was not audited.
4. Did I assume prior pass coverage instead of re-reading? (Anti-trust rule from step 1.)
5. Are the file counts in step 12's `Files in Scope` and `Files Fully Read` going to match? If not, I have not finished.
6. For every refactor-shape finding I raised (introduced an accessor, renamed an API, extracted a helper), did I `ast-grep` / `grep` the WHOLE repo for the old pattern and either fix or descope every match — not just the matches inside my partition?
7. For every numeric or named claim in any comment I edited or left in place this pass ("WHY ONLY THESE FOUR", "the other nine", "see ::engineFor"), did I verify the claim against current code? Stale comment counts and dangling method references in the diff are findings I'm supposed to catch.
8. For every `Q_ASSERT` / runtime-guard / late-bound dependency I touched, did I verify the release-build pair? Asymmetric guard coverage = real LOW (sometimes MEDIUM) finding I should have raised.
9. For every store mutation in this PR that affects rendered output, did I trace forward to confirm a corresponding repaint / damage / update signal? "It works because the next frame happens to re-paint" is not a verification.
10. For every new or non-trivially modified public function in this PR, did I `ast-grep` / `grep` for callers? Dead helpers introduced by a fix are real findings.

If any answer is "no," "skipped," "I assumed," or "partially," return to step 9 and complete the missing work. Only proceed to step 12 when every answer is "yes."

---

### 12. Final Report

```
## Code Audit — Final (Pass N)

### Summary
<2-3 sentences: what was found, what was fixed, overall quality>

### Changelog

#### Pass 1
| # | Severity | File | Finding | Fix Applied |
|---|----------|------|---------|-------------|
| 1 | CRITICAL | `path/file.ext:NN` | <what was wrong> | <what was changed — be specific: old value → new value, added/removed call, restructured logic, etc.> |
| 2 | HIGH | `path/file.ext:NN` | <what was wrong> | <specific change> |
| 3 | MEDIUM | `path/file.ext:NN` | <what was wrong> | <specific change> |
| 4 | LOW | `path/file.ext:NN` | <what was wrong> | <specific change> |
| 5 | NIT | `path/file.ext:NN` | <what was wrong> | <specific change> |

#### Pass 2 (if applicable)
| # | Severity | File | Finding | Fix Applied |
|---|----------|------|---------|-------------|
| 6 | LOW | `path/file.ext:NN` | <what was wrong> | <specific change> |

...

### Audit Summary

**IMPORTANT: These totals are CUMULATIVE across ALL passes, not just the last pass. Count every finding from every pass.**

| Metric | Value |
|---|---|
| Passes | N |
| Files in Scope | N |
| Files Fully Read | N |
| Coverage | N% |
| Files Modified | N |
| Total Findings (all passes) | N |
| CRITICAL (all passes) | N found → N fixed |
| HIGH (all passes) | N found → N fixed |
| MEDIUM (all passes) | N found → N fixed |
| LOW (all passes) | N found → N fixed |
| NIT (all passes) | N found → N fixed |
| Build | PASS |
| Verdict | CLEAN |

**Coverage rule.** Coverage = `Files Fully Read / Files in Scope × 100`. The skill cannot declare CLEAN with Coverage <100%. If Coverage <100%, the verdict header is `INCOMPLETE` (not `CLEAN`), and the verdict body must list which inventory files were skipped and explain why. Do not paper over an incomplete audit by relabeling skipped files as "out of scope" — they were already declared in scope at step 1.5.

To compute findings: go through the Changelog tables above, count every row at each severity level across all passes. The "found" and "fixed" numbers must match the total rows in the Changelog.

### Verdict
CLEAN — <one sentence rationale>
```

---

## Fix Philosophy

- **Architecturally correct, always.** Never apply a quick hack, shortcut, or workaround. Every fix must be the proper solution — the change a senior engineer would make if they owned this code long-term. If the correct fix is harder or touches more files, that's the fix.
- **No TODOs, no follow-ups.** Never leave `TODO`, `FIXME`, `HACK`, `XXX`, "revisit later", or any comment implying future work. Never suggest a follow-up task. The code after audit must have zero outstanding action items.
- **Solve the root cause.** If a finding is a symptom, fix the underlying problem. Don't patch symptoms.

## Guidelines

- **Be direct.** No praise sandwiches.
- **Be concrete.** Every finding must cite a file and line.
- **Be proportional.** Scale depth to the size and risk of the change.
- **Don't invent problems.** If the code is solid, say so.
- **Respect project style.** Flag CLAUDE.md deviations, not personal preferences.
- **Read before judging.** A "bug" in the diff may be correct in context.
- **Fix confidently.** Don't ask permission for obvious corrections.
- **Minimize disruption.** Surgical fixes only — don't refactor surrounding code unless necessary to resolve a finding.
- **Stay in scope, but not as a dodge.** Analyze the changed code and its immediate context (callers, callees, shared state). Do not expand the audit to the entire codebase. BUT: if a finding has been raised in 2+ prior passes and continues to be deferred under "out of scope," that pattern is itself a finding — either fix it this pass or descope it to a tracked artifact (issue link, PR description note) so it stops re-appearing. "Carry-over scope" is not a permanent exemption. **This guideline bounds where you LOOK, never whether you fix what you found** — read it with step 5, which forbids descoping a finding raised in the current pass. Using "stay in scope" to drop a fresh finding inverts this line's intent.
- **Read scope ≠ grep scope.** Reading is bounded by the audit scope. Grepping / ast-grepping is repo-wide whenever a finding requires enumerating call sites, old-pattern remnants, or stale comment references.
