# PlasmaZones — Claude Code Configuration
# KDE/Plasma Window Placement — Qt6/C++20/QML/Kirigami

## Project
PlasmaZones: window snapping, tiling and scrolling for KDE Plasma. Qt6, KF6, Kirigami, C++20, Wayland-only.

### Placement Modes
Three mutually exclusive modes. Each screen runs exactly one, resolved per (screen, desktop, activity):
- **Snapping** — drag a window with a modifier held, drop it into a user-drawn zone. Engine: `phosphor/libs/phosphor-snap-engine`. Artifacts: layouts (`plasmazones/data/layouts`, user copies in `~/.local/share/plasmazones/layouts/`).
- **Tiling** — windows place themselves via a scripted algorithm. Engine: `phosphor/libs/phosphor-tile-engine` running Luau through `phosphor-tiles` / `phosphor-scripting`. Artifacts: algorithms (`plasmazones/data/algorithms/*.luau`).
- **Scrolling** — windows form columns on an endless strip, modeled on niri. Engine: `phosphor/libs/phosphor-scroll-engine`. Artifacts: templates (`plasmazones/data/scrolling-templates`).

Shared placement policy lives in `phosphor/libs/phosphor-engine`. A verdict from one mode never gates another — see the float-is-per-mode invariant, written up at `phosphor/libs/phosphor-engine/include/PhosphorEngine/WindowPlacement.h` (each engine keeps its own float slot and state, independent of the others). When adding a cross-cutting feature, check whether all three modes need an arm before calling it done.

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
- NEVER save to the root folder — use the directories below
- Source, tests and data live INSIDE a tier, not at the root. There is no
  top-level `/src`, `/tests`, `/config` or `/examples`:
  - `plasmazones/{src,tests,data}` for the app tier
  - `phosphor/libs/<lib>/{src,include,tests}` for a tier-1 library
  - `phosphor-shell-libs/{libs/<lib>,examples}` for the shell libraries and their demos
  - `phosphor-shell/{src,shell,tests}` for the shell binary and its bundled QML
- Use `/docs` for documentation and markdown files
- Use `/scripts` for repo-level checks and dev harnesses
- The full layout is the Directory Structure block further down; it is the
  authoritative one.

## License
- SPDX headers on every file whose format supports comments: `// SPDX-FileCopyrightText: 2026 fuddlesworth`. Data assets in formats with no comment syntax are exempt, which in practice means `plasmazones/data/**/*.json` and the `manifest.json.in` fixtures under `phosphor/libs/phosphor-registry/tests/` — never add a header to those, it makes the file invalid.
- License identifier depends on the tree:
  - **App tiers** (`plasmazones/**` except its `data/` trees, `phosphor-shell/**`, `phosphor-shell-libs/examples/**`, `scripts/**`): `GPL-3.0-or-later`
  - **Reusable libraries, including their own tests** (`phosphor/libs/phosphor-*/**` and `phosphor-shell-libs/libs/phosphor-*/**`, which subsumes each library's `tests/`): `LGPL-2.1-or-later`
  - A library's own `tests/` follow the library (LGPL), NOT the app-tier GPL rule: test code that links and ships inside an LGPL lib must not taint that lib's build tree with GPL. The GPL `plasmazones/tests/**` rule means only the top-level app test tree.
  - **Bundled animation and pointer shader packs** (`plasmazones/data/animations/**` and `plasmazones/data/pointer/**` shader source: `.frag`, `.vert`, `.glsl`): `LGPL-2.1-or-later` for PlasmaZones-original shaders, so a third-party pack or tool can build on them. The exception is incorporated upstream copyleft. A shader that copies verbatim or ports GPL-3.0 upstream code (the Burn-My-Windows ports and their `shared/bmw_compat.glsl` shim, the niri `honeycomb` port) MUST stay `GPL-3.0-or-later` and carries a second `SPDX-FileCopyrightText` crediting the upstream author, because PlasmaZones is not the copyright holder of those bodies and cannot relicense them. The license follows the incorporated content and is never a per-directory blanket, so a pack's `.frag` and `.vert` may legitimately differ (a GPL-derived `.frag` beside a PlasmaZones-original LGPL `.vert`). Ports of permissively-licensed upstreams such as the MIT gl-transitions `desktop-*` frags may be LGPL. Generated editor aids like `p_generated.glsl` are gitignored and carry no SPDX header. (`plasmazones/data/overlays/**` is currently GPL and `plasmazones/data/surface/**` is mixed. Neither has been normalized, so follow the existing header in those trees.)
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
- `PLASMAZONES_EXPORT` on public API classes in `plasmazones/src/**`, where `plasmazones_rendering` and `plasmazones_shaderpreview` each carry their own. A phosphor library uses its OWN `PHOSPHOR<LIB>_EXPORT` (55 distinct macros: 37 under `phosphor/libs/`, 18 under `phosphor-shell-libs/libs/`). Writing `PLASMAZONES_EXPORT` into a phosphor lib header names an undefined macro.
- Keep files under 1000 lines, with a 15% grace (hard ceiling 1150). Under 1000 is the target; 1000–1150 is tolerated and not a review finding on its own. Past 1150, split by concern.
- The ceiling binds NEW files and files being substantially rewritten. Around 61 existing files are already over it (the largest are `plasmazones/kwin-effect/plasmazoneseffect/plasmazoneseffect.h`, `plasmazones/kwin-effect/tilinghandler/tiling.cpp` and `plasmazones/tests/unit/helpers/StubSettings.h`); those are grandfathered. Do not raise an existing overrun as a review finding on its own, and do not split one as a drive-by. Growing one further, or adding a new file over the ceiling, is a finding.
- Input validation at system boundaries

### Qt6 String Literals (CRITICAL)
- `QLatin1String()` for JSON keys and string comparisons
- `QStringLiteral()` for constants, MIME types, paths
- NEVER use raw `"string"` with QString/QJsonObject. The top-level CMakeLists defines `QT_NO_CAST_FROM_ASCII`, so this is a compile error, not a convention

### QUuid Convention
- `toString()` (with braces) everywhere — EXCEPT filesystem paths use `WithoutBraces`

## QML Style
- Qt Quick 6, Kirigami, QtQuick.Controls/Layouts
- Components/files: `PascalCase.qml` — IDs/props/functions: `camelCase`
- Prefer bindings over JS assignments; typed properties over `var`; `required property` for mandatory props
- Use `Kirigami.Theme` for colors, `Kirigami.Units` for spacing — never hardcode
- Zone IDs (QUuid), never indices — `Accessible.name` on interactive elements
- A `PascalCase.js` library must declare `.pragma library` within the first 128
  BYTES. `qt_add_qml_module` scans only that far, and past it Qt emits an author
  warning claiming the file is re-evaluated per importing document. Machine-checked
  by the `js-pragma` rule.

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
User-facing strings MUST read like plain, human-written prose with no LLM tics. This applies to every surface a user reads: `description`/`name` fields in `plasmazones/data/**/*.json` (animation, shader, layout, and scrolling-template metadata), `plasmazones/data/whatsnew.json` highlights, `plasmazones/data/algorithms/*.luau` `description` fields, `CHANGELOG.md` entries, the `.desktop` `Name`/`GenericName`/`Comment` fields, AppStream `.metainfo.xml` summaries and descriptions, packaging descriptions (`packaging/**` pkgdesc / Summary / %description / Debian Description / Nix meta), and every translatable string (`PhosphorI18n::tr()`, QML `i18n()`/`i18nc()`). SVG `<desc>` elements in `plasmazones/icons/**` count too, since screen readers announce them.

`README.md` is deliberately OUT of scope, along with the other developer-facing repo docs (`CLAUDE.md`, `AGENTS.md`, `docs/**`, `plasmazones/tools/**/README.md`). The README uses em-dashes structurally throughout and pulling it under this rule would need a full punctuation rewrite first. Do not "fix" README em-dashes to satisfy the bullets below.

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
Use the `pz-add-setting` skill, which carries the full worked example. Summary:

1. `plasmazones/src/config/configdefaults_<area>.h` — static default accessor (plus `constexpr` Min/Max for a clamped numeric). `configdefaults.h` is split by area (`_appearance`, `_gaps`, `_limits`, `_screens`, `_scrolling`, `_scrolling_behavior`, `_scrolling_shortcuts`, `_shaders`).
2. `plasmazones/src/config/configkeys.h` — group and `xxxKey()` accessors, if new (`configkeys_scrolling.h` for a scrolling group). NOT `configdefaults.h`, which declares none of them; the call is still spelled `ConfigDefaults::` because ConfigDefaults inherits the chain.
3. `plasmazones/src/config/settingsschema*.cpp` — register the `{key, default, QMetaType, description, coercion}` KeyDef in its group. **The store takes its default, type and clamping from the schema, not from the getter.** Skip this and the setting silently reads back as the type-default. The description field is user-facing prose and is held to the plain-prose rules below.
4. `plasmazones/src/core/interfaces/isettings.h` — signal in ISettings.
5. `plasmazones/src/config/settings.h` — Q_PROPERTY + getter + setter declarations (`override`). **No member variable.**
6. The matching `plasmazones/src/config/settings/*.cpp` — store-backed getter (`m_store->read<T>(group, key)`) and setter. There is **no** load/save/reset arm to write; persistence goes through the store. Setters live in that directory split by concern (`setters.cpp`, `shortcuts.cpp`, `storescalars.cpp`, `scrolling.cpp`, `triggers.cpp`, `perscreen.cpp`, `disable.cpp`, `uienums.cpp`, and so on), NOT in `plasmazones/src/config/settings.cpp`. Note three different files in the tree are named `settings.cpp` (`plasmazones/src/config/`, `plasmazones/src/daemon/overlayservice/`, `plasmazones/src/editor/controller/`), so always use the full path.

An unclamped setter compares, early-returns, writes, then emits. A **clamped** setter must write first and compare after, because the schema's coercion runs on the write and the stored value may differ from the value passed in.

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

## Skills
In-repo skills under `.claude/skills/` (symlinked into `.agents/skills/`). Invoke them rather than reconstructing the procedure:
- `pz-build` — configure, build and test. Carries the two `OFF`-by-default flags that make a suite silently run nothing, the warning-vs-error grep, the ctest D-Bus isolation, and the non-unity gate.
- `pz-add-setting` — the six files a setting touches, in the store-backed shape.
- `pz-verify-live` — nested-KWin harness for verifying placement and effect changes against a real compositor.
- `code-audit` — multi-pass audit-and-fix loop.
- `shader-theme` — build a cohesive shader theme.

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
# TEST-TIME DEPENDENCY: the shader_validate_animations and
# shader_validate_pointer gates shell out to `glslangValidator` (or the newer
# `glslang`; either name works) to compile every animation and pointer pack for
# the compositor's classic-GL branch, and HARD-FAIL when neither is on PATH
# rather than skipping. Install your distro's glslang package before running
# ctest. Not needed to build, and not needed with BUILD_TESTING=OFF.
#
# BUILD_PHOSPHOR_SHELL also defaults to OFF, and it gates the whole Phosphor
# shell tier: phosphor-shell-libs/libs/phosphor-shell*, the bar, control center, launcher, power
# and popout libraries, their demos, and their tests. Configure without it and
# none of that is built, so ctest passes without ever running those suites.
# Pass it when working on anything under the shell tier. It requires
# USE_KDE_FRAMEWORKS=ON (the default), since those libraries need KF6 Kirigami
# for icon rendering and have no Qt-only fallback.
cmake -B build -DBUILD_TESTING=ON -DBUILD_PHOSPHOR_SHELL=ON

# Build
cmake --build build --parallel 6   # literal 6, not $(nproc): see the pz-build skill

# Test
ctest --test-dir build --output-on-failure

# Lint (pre-commit hooks handle clang-format + qmlformat)

# Conventions. Machine-checks the rules in this file that are decidable by
# inspection: SPDX headers, the GPL-3 app / LGPL-2.1 libs split, the file-size
# ceiling (growth-only, baselined in scripts/oversize-baseline.json),
# PhosphorI18n::tr() over i18n() in C++, ConfigDefaults:: accessors over inline
# config paths, `.pragma library` inside Qt's 128-byte window in QML .js
# libraries (see QML Style), and the plain-prose rules on the user-facing
# strings it can reach. Stdlib only.
#
# The prose rule reaches data JSON, tr()/i18n(), settings-schema descriptions,
# .desktop, AppStream, packaging and algorithm .luau. It does NOT reach
# CHANGELOG.md entries or icon SVG <desc>, which are in the rule below but
# stay review-only.
# Also runs on pre-commit (staged files) and in CI (whole tree).
python3 scripts/check-conventions.py
python3 scripts/check-conventions.py --list-rules
```

### Per-tier builds with moon
The repo is a [moon](https://moonrepo.dev) workspace. CMake still does every compile; moon adds the tier graph on top: one command per tier, dependency ordering, and affected-only runs locally (CI still drives CMake directly). The four tiers are the projects `phosphor`, `phosphor-shell-libs`, `phosphor-shell` and `plasmazones`, plus `repo` for the whole-tree checks. Install moon from the AUR (`moon-bin`) or with `proto install moon`.

```bash
moon run plasmazones:build          # build that tier and its upstream tiers (configure runs first each time)
moon run phosphor:test              # ctest --test-dir build/phosphor
moon run :test --affected           # every tier touched by the working-tree diff
moon run repo:check                 # conventions + JSON schema gates
moon run repo:install               # whole-tree install (needs root at the default prefix)
DESTDIR=/tmp/stage moon run repo:install   # staged install, no root
moon run plasmazones:build-release  # same, from the release preset into build-release/
moon run phosphor:test-release      # ctest --test-dir build-release/phosphor
moon query projects --affected      # which tiers a change reaches
```

Configurations come from `CMakePresets.json`: `debug` configures into `build/`, `release` into `build-release/`, `relwithdebinfo` into `build-relwithdebinfo/`, and every preset turns tests, the shell and the tools on. The moon tasks are the debug ones by default and each has a `-release` twin. Plain CMake users get the same trees with `cmake --preset release && cmake --build --preset release`.

How it maps onto CMake (see `.moon/tasks/cmake.yml`): every tier task runs from the workspace root against the build directory its preset names. `build` invokes the tier's aggregate target, `<tier>-tier`, declared by `phosphor_tier_target()` at the end of each tier CMakeLists; `test` runs ctest scoped to `build/<tier>`. Because the build directory is shared, moon does not cache build outputs and ccache remains the compile cache. A tier gets its own build directory, and with it a moon-cached output, once it can be configured standalone against an installed upstream tier.

Install is a whole-tree verb on the `repo` project, not a per-tier one, because CMake cannot install a subset here: none of the install rules declares a `COMPONENT`, so `cmake --install build --component <tier>` would install nothing. Per-tier install verbs need every rule tagged with a component first. Follow an install with `moon run repo:post-install` to refresh the KDE service cache, the same step `make post-install` runs.

Known tier inversion: `phosphor-shell` links `plasmazones_rendering`, `plasmazones_shared_qml` and `plasmazones_shared_qmlplugin` from the plasmazones tier, so its `moon.yml` lists `plasmazones` as a dependency and `.moon/workspace.yml` turns layer enforcement off. Moving those three targets into a phosphor library is necessary but not sufficient: a second inversion, tier-1 tests reading tier-4 data under `plasmazones/data/`, has to go too before layer enforcement can be re-enabled. See `.moon/workspace.yml`.

- CMake with `CMAKE_AUTOMOC/AUTORCC/AUTOUIC ON`
- `qt_add_qml_module()` — ALL QML files must be listed (missing = runtime "not a type" error)
- `cmake -DUSE_KDE_FRAMEWORKS=ON` (default) or `OFF` for portable Qt-only build
- KF6 deps when ON: `KCMUtils`, `GlobalAccel`, `ColorScheme` (the KWin effect's KColorScheme); optional: `Activities`
- Pluggable backends: `IConfigBackend`, `PhosphorShortcuts::IBackend`, `IWallpaperProvider`
- Standalone settings app (`plasmazones-settings`) + minimal KCM launcher

### Directory Structure
The tree is four product tiers plus repo-level support directories. Each tier has its own `CMakeLists.txt` (entered from the root one) and its own `moon.yml` project.
```
phosphor/                — tier 1: core LGPL libraries
  libs/phosphor-*/       — engines, rendering, layer-shell, animation, config, ...
  data/schemas/          — JSON schemas the libraries compile in
  extern/                — vendored Luau + valijson tarballs
phosphor-shell-libs/     — tier 2: shell libraries (BUILD_PHOSPHOR_SHELL)
  libs/phosphor-*/       — theme, popout, ipc, phosphor-shell*, phosphor-service-*
  examples/              — demos and CLI acceptance harnesses
phosphor-shell/          — tier 3: the shell binary (BUILD_PHOSPHOR_SHELL)
  src/                   — shell process controllers and transports
  cli/phosphorctl/       — typed IPC CLI
  shell/                 — bundled shell QML tree (installed as user-editable copy)
  tests/                 — shell unit tests
plasmazones/             — tier 4: PlasmaZones
  src/core/              — Domain models (Zone, Layout, ScreenManager)
  src/daemon/            — Background service; hosts the three placement engines
  src/editor/            — Layout editor (zone layouts + scrolling templates)
  src/settings/          — Standalone settings app
  src/ui/                — Shared QML controls (OSD, picker and selector content)
  src/dbus/              — D-Bus adaptors
  src/config/            — Configuration backends
  src/common/            — Cross-target helpers
  src/shared/            — Code shared between daemon and apps
  src/shaderpreview/     — Shader preview host
  src/shadervalidate/    — plasmazones-shader-validate pack validator
  kcm/                   — System Settings module
  kwin-effect/           — KWin effect (C++)
  tests/                 — Unit tests (Qt Test)
  tools/                 — Developer tools (shader-render); built with -DBUILD_TOOLS=ON
  data/layouts/          — Default layout templates (JSON) — snapping
  data/algorithms/       — Bundled Luau tiling algorithms — tiling
  data/scrolling-templates/ — Bundled strip templates — scrolling
  data/animations/       — Window animation shader packs
  data/overlays/         — Zone overlay shader packs
  data/surface/          — Window/shell decoration packs
  data/pointer/          — Pointer shader packs
  data/curves/           — Animation easing curves
  data/schemas/          — Schemas for plasmazones-only documents (whatsnew, scrolling templates)
cmake/                   — Shared CMake modules
scripts/                 — Repo-level checks and dev harnesses
.moon/                   — moon workspace config (see Build & Test)
```
Not exhaustive: `scripts/`, `packaging/` and `docs/` sit at the root; `plasmazones/translations/`, `plasmazones/dbus/`, `plasmazones/icons/`, `plasmazones/scripts/` (the installed support script) and `phosphor/extern/` live inside their tiers. `phosphor-shell/` also carries its own `icons/` and `scripts/`.

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
- ALWAYS batch ALL todos in ONE TodoWrite call (5-10+ minimum)
- ALWAYS spawn ALL agents in ONE message with full instructions via the `Agent` tool
- ALWAYS batch INDEPENDENT file reads/writes/edits in ONE message
- ALWAYS batch INDEPENDENT Bash commands in ONE message
- "Independent" is the operative word, and it is what keeps this section from
  contradicting "ALWAYS read a file before editing it" above. A read and the
  edit that depends on it cannot go in the same message; batch the reads, then
  batch the edits.

## Key Pitfalls
- Never copy QObjects — Never hardcode colors/spacing — Never use indices for zones
- Never emit without checking value changed — Never use raw string literals with Qt6
- Keep files under 1000 lines (15% grace, hard ceiling 1150) — Keep QML for UI, C++ for logic

## Support
- Documentation: https://phosphor-works.github.io/plasmazones/
- Repository: https://github.com/fuddlesworth/PlasmaZones
- Issues: https://github.com/fuddlesworth/PlasmaZones/issues
