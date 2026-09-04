<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: LGPL-2.1-or-later -->

# PhosphorShellLauncher

The launcher: a spotlight-style search over installed applications, open
windows, clipboard history, arithmetic and shell commands, per
[`docs/phosphor-shell-design/mockups/launcher-spotlight.svg`](../../docs/phosphor-shell-design/mockups/launcher-spotlight.svg).
Two targets in one directory: a C++ core library
(`PhosphorShellLauncher`) any host can link without the UI, and the
`Phosphor.Launcher` QML module that draws it, themed through
[`phosphor-theme`](../phosphor-theme/README.md) and built on the
[`phosphor-shell-widgets`](../phosphor-shell-widgets/README.md) atoms.

Phase 4.2 deliverable per
[`docs/phosphor-shell-design/04-implementation-plan.md`](../../docs/phosphor-shell-design/04-implementation-plan.md).

## The contract

`PhosphorRegistry::ILauncherProvider` (in
[`phosphor-registry`](../phosphor-registry/README.md)) is the seam. A
provider is a pure data source: `setQuery()` pushes the text,
`resultsChanged()` says `results()` would now answer differently, and
`activate(id, Primary | Alternate)` performs a row's action. It owns no
UI. Providers are created through `ILauncherProviderFactory`, which
Phase 1.3 shipped with a bare `QObject*` return and 4.2 sharpened to the
concrete type.

## Core library

| Component             | Role                                                                                                   |
|-----------------------|--------------------------------------------------------------------------------------------------------|
| `FuzzyMatcher`        | A port of fzf's default `FuzzyMatchV2` scorer with fzf's constants and bonus table, so two candidates rank here as they rank in fzf. `perfectScore()` lets an exact-answer provider place itself above any fuzzy hit. |
| `DesktopEntry`        | Hand-parsed Desktop Entry group: locale fallback, list values, `\s` escapes, `TryExec`, `Exec` quoting and field-code stripping. `DesktopEntryScanner` walks the applications directories first-wins per id. |
| `LauncherModel`       | The one model the surface binds: every provider's rows for the query, ranked within a provider, providers ordered by their best row, filterable to one. Owns no providers; the host `addProvider()`s each. |
| `AppsProvider`        | Installed applications, rescanned when an applications directory changes. Name matches outrank keyword and generic-name matches for the same text. |
| `CalculatorProvider`  | A recursive-descent evaluator (`+ - * / % ^`, parentheses, `sqrt`, `abs`), not a JS engine. A bare number is not a calculation. Enter copies the answer. |
| `CommandProvider`     | Runs the typed line through `/bin/sh -c`. Offered only when the first word resolves on `PATH`; scores at the floor so an app always outranks it. Alt+Enter runs it in a terminal. |
| `ClipboardProvider`   | History from `phosphor-service-clipboard`, reached through its `history` model and `copy`/`remove` by name so this library does not link the service. Rows are addressed by timestamp, not index. |
| `WindowsProvider`     | Open windows, duck-typed over any model with a `toplevel` role whose objects have `title`, `appId` and `activate()`. Keeps the Wayland stack out of this library; inert on a compositor without foreign-toplevel. |

Not built: an emoji provider. There is no emoji dataset in the tree and
bundling one is a data decision (a subset, or a full CLDR-annotated set).

## The surface

`Launcher` renders into whatever it is parented to and owns no window,
like the other Phase 3/4 surfaces. The shell opens it as a screen-centred
popout; the demo puts it in a plain window. It takes a `results` model
and reads nothing else.

Keyboard, all from the search field so focus never leaves it: Up/Down
move the selection, Return runs the primary action, Alt+Return the
alternate when the row offers one, Tab and Shift+Tab cycle the provider
filter, Escape emits `dismissed`. A refused activation keeps the surface
open.

```qml
import Phosphor.Launcher

Launcher {
    anchors.centerIn: parent
    results: LauncherResults   // the host's LauncherModel
    onActivated: close()
    onDismissed: close()
}
```

## Tests

`tests/` is Qt Test over the core (matcher orderings and the exact
perfect score, parser and scanner over a fixture tree, the pure-logic
providers' gates and ranking (apps, calculator, command), the two
model-backed providers against fakes of the sources they duck-type
(clipboard, windows), the model's grouping/ordering/filter/activation) plus a
QtQuickTest harness for the surface's keyboard contract against a fake
model. Every claim with a mutation-shaped failure mode was verified by
mutation: disabling the matcher's run-bonus inheritance, the scanner's
first-directory-wins, the model's best-row ordering, the calculator's
bare-number gate, the command PATH gate and the apps secondary-field
penalty each fail the test written for it.

## Dependencies

- Qt6 >= 6.6 Core for the core library, plus Gui privately for the one
  clipboard call the calculator makes. Qml / Quick / `QtQuick.Shapes`
  (`Qt6::QuickShapes`) and KF6 Kirigami for the QML module.
- `phosphor-registry` (`ILauncherProvider`, `ILauncherProviderFactory`) as a
  public dependency: the provider contract appears in this library's own
  public headers.
- `phosphor-theme` (`Phosphor.Theme`) for tokens and Motion, and
  `phosphor-shell-widgets` (`Phosphor.Widgets`) for the field, pill and row
  chrome. In-tree builds link their QML plugins automatically.
- At runtime, `xdg-terminal-exec` (or `$TERMINAL`) for the terminal legs of
  the apps and command providers. Absent, those activations warn and refuse
  rather than failing the launcher.

## Status

Phase 4.2: in the tree. The core library, the five providers, the ranked
model and the spotlight surface are all present, with six Qt Test suites and
a QtQuickTest harness. The clipboard and windows providers are model-backed
and have no suites of their own yet.

Built only with `-DBUILD_PHOSPHOR_SHELL=ON`, which is off by default.
The acceptance demo is `examples/phosphor-launcher-demo/`, a plain window
that hosts the surface over the real providers, so it lists the running
session's applications, clipboard and PATH.
