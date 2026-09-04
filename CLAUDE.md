# PlasmaZones — Claude Code Configuration
# KDE/Plasma Window Placement — Qt6/C++20/QML/Kirigami

## Project
PlasmaZones: window snapping, tiling and scrolling for KDE Plasma. Qt6, KF6, Kirigami, C++20, Wayland-only.

### Placement Modes
Three mutually exclusive modes. Each screen runs exactly one, resolved per (screen, desktop, activity):
- **Snapping** — drag a window with a modifier held, drop it into a user-drawn zone. Engine: `libs/phosphor-snap-engine`. Artifacts: layouts (`data/layouts`, user copies in `~/.local/share/plasmazones/layouts/`).
- **Tiling** — windows place themselves via a scripted algorithm. Engine: `libs/phosphor-tile-engine` running Luau through `phosphor-tiles` / `phosphor-scripting`. Artifacts: algorithms (`data/algorithms/*.luau`).
- **Scrolling** — windows form columns on an endless strip, modeled on niri. Engine: `libs/phosphor-scroll-engine`. Artifacts: templates (`data/scrolling-templates`).

Shared placement policy lives in `libs/phosphor-engine`. A verdict from one mode never gates another — see the float-is-per-mode invariant, written up at `libs/phosphor-engine/include/PhosphorEngine/WindowPlacement.h` (each engine keeps its own float slot and state, independent of the others). When adding a cross-cutting feature, check whether all three modes need an arm before calling it done.

## Behavioral Rules (Always Enforced)
- NEVER question or doubt what the user says they did (installed, restarted, tested, etc.) — trust them and focus on the code
- Do what has been asked; nothing more, nothing less
- NEVER create files unless absolutely necessary; prefer editing existing files
- NEVER proactively create documentation files (*.md) or README files unless explicitly requested
- NEVER save working files, text/mds, or tests to the root folder
- ALWAYS read a file before editing it
- NEVER commit secrets, credentials, or .env files
- ALWAYS run tests after making code changes
- ALWAYS verify build succeeds before committing
- NEVER run `cmake --install` or `sudo` — the user handles installation
- NEVER use temporary workarounds, TODOs, "for now" hacks, or deferred fixes — solve the root cause properly the first time

## File Organization
- NEVER save to root folder — use the directories below
- Use `/src` for source code files
- Use `/tests` for test files
- Use `/docs` for documentation and markdown files
- Use `/config` for configuration files
- Use `/scripts` for utility scripts
- Use `/examples` for example code

## License
- SPDX headers on every file whose format supports comments: `// SPDX-FileCopyrightText: 2026 fuddlesworth`. Data assets in formats with no comment syntax are exempt, which in practice means `data/**/*.json` and the `manifest.json.in` fixtures under `libs/phosphor-registry/tests/` — never add a header to those, it makes the file invalid.
- License identifier depends on the tree:
  - **App / daemon / editor / settings / KCM / examples / top-level tests** (`src/**`, `kcm/**`, `kwin-effect/**`, `examples/**`, top-level `tests/**`): `GPL-3.0-or-later`
  - **Reusable libraries, including their own tests** (`libs/phosphor-*/**`, which subsumes `libs/phosphor-*/tests/**`): `LGPL-2.1-or-later`
  - A library's own `tests/` follow the library (LGPL), NOT the top-level GPL `tests/**` rule: test code that links and ships inside an LGPL lib must not taint that lib's build tree with GPL. The GPL `tests/**` rule means only the top-level app test tree.
  - **Bundled animation shader packs** (`data/animations/**` shader source: `.frag`, `.vert`, `.glsl`): `LGPL-2.1-or-later` for PlasmaZones-original shaders, so a third-party pack or tool can build on them. The exception is incorporated upstream copyleft. A shader that copies verbatim or ports GPL-3.0 upstream code (the Burn-My-Windows ports and their `shared/bmw_compat.glsl` shim, the niri `honeycomb` port) MUST stay `GPL-3.0-or-later` and carries a second `SPDX-FileCopyrightText` crediting the upstream author, because PlasmaZones is not the copyright holder of those bodies and cannot relicense them. The license follows the incorporated content and is never a per-directory blanket, so a pack's `.frag` and `.vert` may legitimately differ (a GPL-derived `.frag` beside a PlasmaZones-original LGPL `.vert`). Ports of permissively-licensed upstreams such as the MIT gl-transitions `desktop-*` frags may be LGPL. Generated editor aids like `p_generated.glsl` are gitignored and carry no SPDX header. (`data/overlays/**` is currently GPL and `data/surface/**` is mixed. Neither has been normalized, so follow the existing header in those trees.)
  - Rationale: the shell is GPL; libraries are LGPL so third-party plugins / tools can link them without inheriting GPL. Never "fix" a lib header to GPL-3 without understanding the split.
- `#pragma once` for C++ headers

## C++ Style

### Naming
- Classes: `PascalCase` — Functions: `camelCase` — Members: `m_camelCase`
- Struct POD fields: `camelCase` (no prefix) — Constants: `PascalCase` (class) / `UPPER_SNAKE` (global)
- Signals: past tense (`layoutChanged`) — Slots: action verb (`saveLayout`)

### Core Rules
- C++20, `namespace PlasmaZones`, `explicit` single-param constructors, `override` on virtuals
- `Q_OBJECT`, `Q_EMIT`, `Q_SIGNALS:`, `Q_SLOTS:`, `Q_PROPERTY` with READ/WRITE/NOTIFY
- Only emit signals when value actually changes
- Parent-based ownership for QObjects; `std::unique_ptr`/`QPointer` otherwise; never manual delete
- Forward declare in headers; group includes: own header → project → KDE → Qt
- `PLASMAZONES_EXPORT` on public API classes
- Keep files under 1000 lines, with a 15% grace (hard ceiling 1150). Under 1000 is the target; 1000–1150 is tolerated and not a review finding on its own. Past 1150, split by concern.
- The ceiling binds NEW files and files being substantially rewritten. Around 39 existing files are already over it (the largest are `kwin-effect/plasmazoneseffect/plasmazoneseffect.h`, `kwin-effect/tilinghandler/tiling.cpp` and `src/config/settings.h`); those are grandfathered. Do not raise an existing overrun as a review finding on its own, and do not split one as a drive-by. Growing one further, or adding a new file over the ceiling, is a finding.
- Input validation at system boundaries

### Qt6 String Literals (CRITICAL)
- `QLatin1String()` for JSON keys and string comparisons
- `QStringLiteral()` for constants, MIME types, paths
- NEVER use raw `"string"` with QString/QJsonObject (deleted constructor in Qt6)

### QUuid Convention
- `toString()` (with braces) everywhere — EXCEPT filesystem paths use `WithoutBraces`

## QML Style
- Qt Quick 6, Kirigami, QtQuick.Controls/Layouts
- Components/files: `PascalCase.qml` — IDs/props/functions: `camelCase`
- Prefer bindings over JS assignments; typed properties over `var`; `required property` for mandatory props
- Use `Kirigami.Theme` for colors, `Kirigami.Units` for spacing — never hardcode
- Zone IDs (QUuid), never indices — `Accessible.name` on interactive elements

## Architecture
- Service-oriented with DI via constructor (the editor's `ILayoutService`, `ZoneManager`, `SnappingService` are the reference shape)
- Placement runs in the daemon behind the three engines above; the KWin effect draws overlays, decorations, and tab indicators
- Business logic in C++, UI in QML; controllers bridge via `Q_PROPERTY`
- Zone IDs everywhere, never indices
- JSON persistence in `~/.local/share/plasmazones/layouts/` with relative geometry (0.0–1.0)
- Wayland only (custom layer-shell QPA plugin for overlays); XWayland windows handled within Wayland session

## i18n
- C++: `PhosphorI18n::tr()` — NEVER `KLocalizedString`/`i18n()`/`i18nc()` in C++
- QML: `i18n()` / `i18nc()` (via `PhosphorLocalizedContext`)
- Extract: `cmake --build build --target update-ts`

## User-Facing Text (Plain Prose)
User-facing strings MUST read like plain, human-written prose with no LLM tics. This applies to every surface a user reads: `description`/`name` fields in `data/**/*.json` (animation, shader, layout, and scrolling-template metadata), `data/whatsnew.json` highlights, `data/algorithms/*.luau` `description` fields, `CHANGELOG.md` entries, the `.desktop` `Name`/`GenericName`/`Comment` fields, AppStream `.metainfo.xml` summaries and descriptions, packaging descriptions (`packaging/**` pkgdesc / Summary / %description / Debian Description / Nix meta), and every translatable string (`PhosphorI18n::tr()`, QML `i18n()`/`i18nc()`). SVG `<desc>` elements in `icons/**` count too, since screen readers announce them.

`README.md` is deliberately OUT of scope, along with the other developer-facing repo docs (`CLAUDE.md`, `docs/**`, `tools/**/README.md`). The README uses em-dashes structurally throughout and pulling it under this rule would need a full punctuation rewrite first. Do not "fix" README em-dashes to satisfy the bullets below.

- NEVER use an em-dash (`—`, or the `—` escape) to splice clauses or tack on an appositive. Write two sentences, or join with a plain word (and, with, where, so, because).
- NEVER use a clause-splicing semicolon to join two independent clauses. Split into sentences or use "and". Semicolons inside backticked code, and semicolons separating genuine comma-bearing list items, are fine.
- NEVER use a spaced hyphen (` - `) as a stand-in dash. Rewrite the sentence.
- NEVER use a dramatic "Label: payload" colon for effect. The Keep-a-Changelog `**Term**: description` lead-in and real field labels are fine.
- AVOID rule-of-three triads and "not just X, but Y" constructions used for flourish.
- A literal typographic separator between two nouns is acceptable (e.g. the `%1 — %2` Layout/Zone display format) and so are settings-path breadcrumbs (e.g. `Settings → Snapping`).
- These rules do NOT apply to code comments, log/`qCWarning` messages, or other non-user-facing text.

## Settings

### Architecture
- `ISettings` interface → `Settings` class → `IConfigBackend` (pluggable, default: JSON → `~/.config/plasmazones/config.json`)
- `ConfigDefaults` for all default values; the old `.kcfg` schema files were removed from the repo
- Editor settings: separate, in `EditorController` (separate process)

### Adding a Setting
1. `src/config/configdefaults.h` — static default accessor + `xxxKey()` accessor for the config key string
2. `src/core/interfaces/isettings.h` — signal in ISettings
3. `src/config/settings.h` — Q_PROPERTY + getter + setter + member
4. The matching `src/config/settings/*.cpp` — setter (check changed, emit), load/save/reset using `ConfigDefaults::xxx()`. Setters live in that directory split by concern (`setters.cpp`, `shortcuts.cpp`, `storescalars.cpp`, `scrolling.cpp`, `triggers.cpp`, `perscreen.cpp`, `disable.cpp`, and so on), NOT in `src/config/settings.cpp`. Pick the file matching the setting's concern; `loadsave.cpp` holds the load/save/reset arms. Note three different files in the tree are named `settings.cpp` (`src/config/`, `src/daemon/overlayservice/`, `src/editor/controller/`), so always use the full path.

### Config Key Strings
- ALL config group names and key strings MUST use `ConfigDefaults::` accessors — never inline `QStringLiteral("...")`
- Group accessors: `ConfigDefaults::snappingBehaviorGroup()`, key accessors: `ConfigDefaults::enabledKey()`
- v2 groups use dot-paths mirroring the UI hierarchy (e.g. `"Snapping.Behavior.ZoneSpan"`)
- Key accessors are generic (e.g. `enabledKey()`, `triggersKey()`) — the group context disambiguates

### No Ad-Hoc Backwards Compatibility
- NEVER add migration code for individual renamed keys or deprecated settings within the same schema version
- If a setting is renamed or restructured within a version, just use the new key — old values are silently dropped
- Users get the default value if their config doesn't have the current key; this is acceptable
- NEVER write empty strings to "clear" obsolete keys on save
- NEVER read from a fallback/legacy group when the primary group is empty
- Rationale: ad-hoc migration code is write-once, test-forever complexity that rots and never gets removed

### Schema Version Migrations (ConfigSchemaVersion bumps)
- Schema version migrations (`migrateV1ToV2`, etc.) are the ONE exception — they live in `configmigration.cpp`
- Each version bump gets exactly one migration function + one `MigrationStep` registry entry
- Migration functions transform the entire JSON root in-place and stamp the new `_version`
- v1 group/key accessors in `configkeys.h` (prefixed `v1*`) exist ONLY for migration code readability
- The migration chain runs automatically via `ensureJsonConfig()` on startup
- NEVER add per-key fallback reads outside of migration functions — that's ad-hoc migration

### Shortcuts
- `PhosphorShortcuts::IBackend` (KGlobalAccel / XDG Portal / D-Bus fallback) — never use KGlobalAccel directly
- Register via `ShortcutManager`; dynamic updates via settings signals

## Build & Test

On macOS, use Docker (KDE/Qt6 deps are Linux-only):

```bash
# First build the image (once)
docker build -t plasmazones-build .

# Build + test (default runs ctest)
docker run --rm -v "$PWD":/src plasmazones-build

# Build + test with verbose output
docker run --rm -v "$PWD":/src plasmazones-build ctest --output-on-failure
```

On Linux (native):

```bash
# Configure. BUILD_TESTING defaults to OFF, so a build dir configured without
# it produces NO tests and ctest then reports "No tests were found" — which
# reads like success. Pass it explicitly or the "always run tests" rule above
# silently runs nothing. BUILD_TOOLS=ON adds shader-render and friends.
#
# TEST-TIME DEPENDENCY: the shader_validate_animations gate shells out to
# `glslangValidator` (or the newer `glslang`; either name works) to compile the
# compositor-only animation packs, and HARD-FAILS when neither is on PATH
# rather than skipping. Install your distro's glslang package before running
# ctest. Not needed to build, and not needed with BUILD_TESTING=OFF.
#
# BUILD_PHOSPHOR_SHELL also defaults to OFF, and it gates the whole Phosphor
# shell tier: libs/phosphor-shell*, the bar, control center, launcher, power
# and popout libraries, their demos, and their tests. Configure without it and
# none of that is built, so ctest passes without ever running those suites.
# Pass it when working on anything under the shell tier. It requires
# USE_KDE_FRAMEWORKS=ON (the default), since those libraries need KF6 Kirigami
# for icon rendering and have no Qt-only fallback.
cmake -B build -DBUILD_TESTING=ON -DBUILD_PHOSPHOR_SHELL=ON

# Build
cmake --build build --parallel $(nproc)

# Test
ctest --test-dir build --output-on-failure

# Lint (pre-commit hooks handle clang-format + qmlformat)
```

- CMake with `CMAKE_AUTOMOC/AUTORCC/AUTOUIC ON`
- `qt_add_qml_module()` — ALL QML files must be listed (missing = runtime "not a type" error)
- `cmake -DUSE_KDE_FRAMEWORKS=ON` (default) or `OFF` for portable Qt-only build
- KF6 deps when ON: `KCMUtils`, `GlobalAccel`, `ColorScheme` (the KWin effect's KColorScheme); optional: `Activities`
- Pluggable backends: `IConfigBackend`, `PhosphorShortcuts::IBackend`, `IWallpaperProvider`
- Standalone settings app (`plasmazones-settings`) + minimal KCM launcher

### Directory Structure
```
src/core/        — Domain models (Zone, Layout, ScreenManager)
src/daemon/      — Background service; hosts the three placement engines
src/editor/      — Layout editor (zone layouts + scrolling templates)
src/settings/    — Standalone settings app
src/shell/       — Shell process entry point (hosts the OSD / picker / selector surfaces, whose QML lives in src/ui)
src/ui/          — Shared QML controls, including the OSD, picker and selector content
src/dbus/        — D-Bus adaptors
src/config/      — Configuration backends
src/common/      — Cross-target helpers
src/shared/      — Code shared between daemon and apps
src/shaderpreview/  — Shader preview host
src/shadervalidate/ — plasmazones-shader-validate pack validator
libs/            — phosphor-* component libraries (LGPL; see License above)
kcm/             — System Settings module
kwin-effect/     — KWin effect (C++)
tests/           — Unit tests (Qt Test)
tools/           — Developer tools (shader-render); built with -DBUILD_TOOLS=ON
data/layouts/    — Default layout templates (JSON) — snapping
data/algorithms/ — Bundled Luau tiling algorithms — tiling
data/scrolling-templates/ — Bundled strip templates — scrolling
data/animations/ — Window animation shader packs
data/overlays/   — Zone overlay shader packs
data/surface/    — Window/shell decoration packs
data/curves/     — Animation easing curves
data/schemas/    — JSON schemas for the bundled data assets
```
Not exhaustive: `scripts/`, `packaging/`, `translations/`, `dbus/`, `icons/` and `extern/` also exist at the top level.

## Testing
- Qt Test: `QTEST_MAIN`, `QCOMPARE`, `QVERIFY`
- Test behavior, not implementation; mock D-Bus for daemon tests
- Edge cases: empty zones, overlapping zones, invalid coordinates

## Security
- NEVER hardcode API keys, secrets, or credentials
- NEVER commit .env files or files containing secrets
- Validate user input at system boundaries
- Sanitize file paths to prevent directory traversal

## D-Bus
- XML interface files → `qt6_add_dbus_adaptor()`
- `QDBusConnection::sessionBus()`; keep methods simple; `QVariantMap` for complex data

## Git
- Conventional commits: `feat:`, `fix:`, `refactor:`, `docs:`
- Atomic commits; don't commit build artifacts; SPDX headers required

## Concurrency: 1 MESSAGE = ALL RELATED OPERATIONS
- All operations MUST be concurrent/parallel in a single message
- Use Claude Code's subagent tool for spawning agents, not just MCP. That tool is
  now named `Agent`; "Task tool" throughout this section is its former name and
  means the same thing.
- ALWAYS batch ALL todos in ONE TodoWrite call (5-10+ minimum)
- ALWAYS spawn ALL agents in ONE message with full instructions via Task tool
- ALWAYS batch INDEPENDENT file reads/writes/edits in ONE message
- ALWAYS batch INDEPENDENT Bash commands in ONE message
- "Independent" is the operative word, and it is what keeps this section from
  contradicting "ALWAYS read a file before editing it" above. A read and the
  edit that depends on it cannot go in the same message; batch the reads, then
  batch the edits.

## Swarm Orchestration
- MUST initialize the swarm using CLI tools when starting complex tasks
- MUST spawn concurrent agents using Claude Code's Task tool
- Never use CLI tools alone for execution — Task tool agents do the actual work
- MUST call CLI tools AND Task tool in ONE message for complex work

### 3-Tier Model Routing (ADR-026)

| Tier | Handler | Latency | Cost | Use Cases |
|------|---------|---------|------|-----------|
| **1** | Agent Booster (WASM) | <1ms | $0 | Simple transforms (var→const, add types) — Skip LLM |
| **2** | Haiku | ~500ms | $0.0002 | Simple tasks, low complexity (<30%) |
| **3** | Sonnet/Opus | 2-5s | $0.003-0.015 | Complex reasoning, architecture, security (>30%) |

- Always check for `[AGENT_BOOSTER_AVAILABLE]` or `[TASK_MODEL_RECOMMENDATION]` before spawning agents
- Use Edit tool directly when `[AGENT_BOOSTER_AVAILABLE]`

## Swarm Configuration & Anti-Drift
- ALWAYS use hierarchical topology for coding swarms
- Keep maxAgents at 6-8 for tight coordination
- Use specialized strategy for clear role boundaries
- Use `raft` consensus for hive-mind (leader maintains authoritative state)
- Run frequent checkpoints via `post-task` hooks
- Keep shared memory namespace for all agents

### Project Config
- **Topology**: hierarchical-mesh
- **Max Agents**: 15
- **Memory**: hybrid
- **HNSW**: Enabled
- **Neural**: Enabled

## Swarm Execution Rules
- ALWAYS use `run_in_background: true` for all agent Task calls
- ALWAYS put ALL agent Task calls in ONE message for parallel execution
- After spawning, STOP — do NOT add more tool calls or check status
- Never poll TaskOutput or check swarm status — trust agents to return
- When agent results arrive, review ALL results before proceeding

## V3 CLI Commands

### Core Commands

| Command | Subcommands | Description |
|---------|-------------|-------------|
| `init` | 4 | Project initialization |
| `agent` | 8 | Agent lifecycle management |
| `swarm` | 6 | Multi-agent swarm coordination |
| `memory` | 11 | AgentDB memory with HNSW search |
| `task` | 6 | Task creation and lifecycle |
| `session` | 7 | Session state management |
| `hooks` | 17 | Self-learning hooks + 12 workers |
| `hive-mind` | 6 | Byzantine fault-tolerant consensus |

### Quick CLI Examples

```bash
npx @claude-flow/cli@latest init --wizard
npx @claude-flow/cli@latest agent spawn -t coder --name my-coder
npx @claude-flow/cli@latest swarm init --v3-mode
npx @claude-flow/cli@latest memory search --query "authentication patterns"
npx @claude-flow/cli@latest doctor --fix
```

## Available Agents (60+ Types)

### Core Development
`coder`, `reviewer`, `tester`, `planner`, `researcher`

### Specialized
`security-architect`, `security-auditor`, `memory-specialist`, `performance-engineer`

### Swarm Coordination
`hierarchical-coordinator`, `mesh-coordinator`, `adaptive-coordinator`

### GitHub & Repository
`pr-manager`, `code-review-swarm`, `issue-tracker`, `release-manager`

### SPARC Methodology
`sparc-coord`, `sparc-coder`, `specification`, `pseudocode`, `architecture`

## Memory Commands Reference

```bash
# Store (REQUIRED: --key, --value; OPTIONAL: --namespace, --ttl, --tags)
npx @claude-flow/cli@latest memory store --key "pattern-auth" --value "JWT with refresh" --namespace patterns

# Search (REQUIRED: --query; OPTIONAL: --namespace, --limit, --threshold)
npx @claude-flow/cli@latest memory search --query "authentication patterns"

# List (OPTIONAL: --namespace, --limit)
npx @claude-flow/cli@latest memory list --namespace patterns --limit 10

# Retrieve (REQUIRED: --key; OPTIONAL: --namespace)
npx @claude-flow/cli@latest memory retrieve --key "pattern-auth" --namespace patterns
```

## Quick Setup

```bash
claude mcp add claude-flow -- npx -y @claude-flow/cli@latest
npx @claude-flow/cli@latest daemon start
npx @claude-flow/cli@latest doctor --fix
```

## Claude Code vs CLI Tools
- Claude Code's Task tool handles ALL execution: agents, file ops, code generation, git
- CLI tools handle coordination via Bash: swarm init, memory, hooks, routing
- NEVER use CLI tools as a substitute for Task tool agents

## Key Pitfalls
- Never copy QObjects — Never hardcode colors/spacing — Never use indices for zones
- Never emit without checking value changed — Never use raw string literals with Qt6
- Keep files under 1000 lines (15% grace, hard ceiling 1150) — Keep QML for UI, C++ for logic

## Support
- Documentation: https://phosphor-works.github.io/plasmazones/
- Repository: https://github.com/fuddlesworth/PlasmaZones
- Issues: https://github.com/fuddlesworth/PlasmaZones/issues
