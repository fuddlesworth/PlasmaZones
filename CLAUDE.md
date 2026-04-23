# PlasmaZones — Claude Code Configuration
# KDE/Plasma Window Tiling — Qt6/C++20/QML/Kirigami

## Project
PlasmaZones: window tiling + zone management for KDE Plasma. Qt6, KF6, Kirigami, C++20, Wayland-only.

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
- SPDX headers on ALL files: `// SPDX-FileCopyrightText: 2026 fuddlesworth`
- License identifier depends on the tree:
  - **App / daemon / editor / settings / KCM / tests** (`src/**`, `kcm/**`, `kwin/**`, `tests/**`): `GPL-3.0-or-later`
  - **Reusable libraries** (`libs/phosphor-*/**`): `LGPL-2.1-or-later`
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
- Keep files under 800 lines
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
- Service-oriented: `ILayoutService`, `ZoneManager`, `SnappingService`; DI via constructor
- Business logic in C++, UI in QML; controllers bridge via `Q_PROPERTY`
- Zone IDs everywhere, never indices
- JSON persistence in `~/.local/share/plasmazones/layouts/` with relative geometry (0.0–1.0)
- Wayland only (custom layer-shell QPA plugin for overlays); XWayland windows handled within Wayland session

## i18n
- C++: `PzI18n::tr()` — NEVER `KLocalizedString`/`i18n()`/`i18nc()` in C++
- QML: `i18n()` / `i18nc()` (via `PzLocalizedContext`)
- Extract: `cmake --build build --target update-ts`

## Settings

### Architecture
- `ISettings` interface → `Settings` class → `IConfigBackend` (pluggable, default: JSON → `~/.config/plasmazones/config.json`)
- `ConfigDefaults` for all default values; `.kcfg` schema is KCM-only
- Editor settings: separate, in `EditorController` (separate process)

### Adding a Setting
1. `configdefaults.h` — static default accessor + `xxxKey()` accessor for the config key string
2. `interfaces.h` — signal in ISettings
3. `settings.h` — Q_PROPERTY + getter + setter + member
4. `settings.cpp` — setter (check changed, emit), load/save/reset using `ConfigDefaults::xxx()`

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
- `IShortcutBackend` (KGlobalAccel / XDG Portal / D-Bus fallback) — never use KGlobalAccel directly
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
# Build
cmake --build build --parallel $(nproc)

# Test
cd build && ctest --output-on-failure

# Lint (pre-commit hooks handle clang-format + qmlformat)
```

- CMake with `CMAKE_AUTOMOC/AUTORCC/AUTOUIC ON`
- `qt6_add_qml_module()` — ALL QML files must be listed (missing = runtime "not a type" error)
- `cmake -DUSE_KDE_FRAMEWORKS=ON` (default) or `OFF` for portable Qt-only build
- KF6 deps when ON: `KCMUtils`, `GlobalAccel`; optional: `Activities`
- Pluggable backends: `IConfigBackend`, `IShortcutBackend`, `IWallpaperProvider`
- Standalone settings app (`plasmazones-settings`) + minimal KCM launcher

### Directory Structure
```
src/core/        — Domain models (Zone, Layout, ScreenManager)
src/daemon/      — Background service
src/editor/      — Layout editor
src/settings/    — Standalone settings app
src/dbus/        — D-Bus adaptors
src/config/      — Configuration backends
src/autotile/    — Tiling algorithms (built-in + scripted JS)
kcm/             — System Settings module
kwin/            — KWin script integration
tests/           — Unit tests (Qt Test)
data/layouts/    — Default layout templates (JSON)
data/algorithms/ — Bundled JS tiling algorithms
```

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
- Use Claude Code's Task tool for spawning agents, not just MCP
- ALWAYS batch ALL todos in ONE TodoWrite call (5-10+ minimum)
- ALWAYS spawn ALL agents in ONE message with full instructions via Task tool
- ALWAYS batch ALL file reads/writes/edits in ONE message
- ALWAYS batch ALL Bash commands in ONE message

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
- Keep files under 800 lines — Keep QML for UI, C++ for logic

## Support
- Documentation: https://github.com/ruvnet/claude-flow
- Issues: https://github.com/ruvnet/claude-flow/issues
