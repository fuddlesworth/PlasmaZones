<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-control

> Reusable Qt6/QML/Kirigami settings-app framework: the window chrome
> (sidebar, breadcrumbs, footer, page host) plus the orchestration
> (`PageController`, `PageRegistry`, `StagingDomain`, `ApplicationController`)
> that any Kirigami settings app builds on, so each consumer writes only its
> own page controllers and QML pages.

## Responsibility

A settings app on this framework declares a set of pages, each backed by a
staging domain that holds the user's pending edits, and the shell handles the
rest: sidebar navigation, breadcrumbs, a global dirty flag, batched
Apply/Discard across every domain, deep-link reveal, and global search. The
library provides:

- **Staging domains.** `StagingDomain` is the unit of staged work:
  `isDirty()`, `apply()`, `discard()`, `resetToDefaults()`, plus async
  completion signals. `PageController` is a domain that also has a QML page and
  a sidebar row. Headless domains hold cross-cutting state (e.g. per-screen
  assignment maps) with no sidebar entry.
- **A page catalogue.** `PageRegistry` holds the page tree (top-level entries
  plus drill-down or collapsible children), keyed by stable page id, and drives
  the sidebar via serialized `Entry` views.
- **Top-level orchestration.** `ApplicationController` owns the registry and the
  registered domains, recomputes the global dirty flag, drives `applyAll()` /
  `discardAll()` as one transaction (sync or async), dispatches per-page
  `resetToDefaults()`, and latches deep-link anchors for `PageHost` to reveal.
- **Global search.** `SearchController` builds a ranked result index from the
  page registry (Page entries), authored anchors (`addEntry()`), and dynamic
  content (`ISearchProvider`). `SearchRanker` is the pure, headless,
  typo-tolerant scorer behind it.
- **The QML chrome.** `org.phosphor.control` ships `SettingsAppWindow`,
  `Sidebar`, `Breadcrumbs`, `UnsavedChangesFooter`, `PageHost`,
  `DiscardChangesDialog`, and `AboutPageShell`, wired to the controllers above.
- **Glue.** `DBusBridge` for talking to a daemon, and `LocalizedContext` for
  bare-word `i18n()` in QML on Qt-only (non-KF6) builds.

## Key types

| Type | Purpose |
|------|---------|
| `PhosphorControl::ApplicationController` | Top-level orchestrator: owns the registry + domains, global dirty flag, batched `applyAllAsync` / `discardAllAsync`, current-page selection, deep-link anchor latch |
| `PhosphorControl::StagingDomain`         | Abstract unit of staged changes: `isDirty()`, `apply()`, `discard()`, `resetToDefaults()`, with `applyResult` / `discardResult` async signals |
| `PhosphorControl::PageController`         | A `StagingDomain` that also has a QML page and a sidebar row, with a stable, globally-unique page id |
| `PhosphorControl::PageRegistry`           | Catalogue of registered pages as a tree (`Entry` per page); read-only after startup, supports dynamic add but not removal |
| `PhosphorControl::SearchController`       | Global settings search: ranked results from page registry, authored anchors, and registered providers |
| `PhosphorControl::SearchRanker`           | Pure, stateless, typo-tolerant scoring + ranking; headless and Qt-free beyond `QString` |
| `PhosphorControl::ISearchProvider`        | App-implemented source of dynamic search entries (rules, shaders, layouts, …) |
| `PhosphorControl::SearchEntry`            | One searchable item (Page / Section / Setting / Entity), addressed as `"pageId"` or `"pageId#anchor"` |
| `PhosphorControl::DBusBridge`             | Configured endpoint(s) for daemon calls: sync `call()` / async `asyncCall()`, with explicit-interface variants |
| `PhosphorControl::LocalizedContext`       | QML i18n context routing `i18n()`/`i18nc()`/`i18np()` through Qt translation, for Qt-only consumers without KF6 |

## Typical use

A consumer subclasses `ApplicationController` to declare its pages, subclasses
`PageController` for each page, and points the QML `SettingsAppWindow` at the
controller. Titles are translated by the caller, because the library
deliberately provides no translation context for app strings.

```cpp
#include <PhosphorControl/ApplicationController.h>
#include <PhosphorControl/PageController.h>

using namespace PhosphorControl;

// Each page stages its own edits.
class GeneralPage : public PageController
{
public:
    explicit GeneralPage(QObject* parent = nullptr)
        : PageController(QStringLiteral("general"), parent) {}

    bool isDirty() const override { return m_dirty; }
    void apply() override   { /* persist */ Q_EMIT applyResult(true, {}); }
    void discard() override { /* reload  */ Q_EMIT discardResult(true, {}); }
};

// The app wires its pages up at construction.
class MySettings : public ApplicationController
{
public:
    explicit MySettings(QObject* parent = nullptr)
        : ApplicationController(parent)
    {
        auto* general = new GeneralPage(this);
        registerPage(general, {}, MySettings::tr("General"),
                     QUrl(QStringLiteral("qrc:/qt/qml/MyApp/qml/GeneralPage.qml")));
    }
};
```

```qml
// Main.qml — the chrome binds to the controller exposed from C++.
import org.phosphor.control

SettingsAppWindow {
    controller: appController   // your ApplicationController subclass
}
```

The shell drives the dirty/Apply lifecycle from the domains' signals:
`UnsavedChangesFooter` binds to `ApplicationController::dirty` /
`applying` / `discarding`, so a synchronous page just emits
`applyResult(true, {})` at the end of `apply()` and the footer swaps
"Save" → "Saving…" automatically.

See `examples/minimal/` for a complete two-page app (General + About).

## Design notes

- **Staging is the contract, not persistence.** `ApplicationController` only
  invokes `apply()` / `discard()` on domains whose `isDirty()` is true, so
  implementations must not rely on side effects that need to run while clean
  (timestamp stamping, notifications). Every domain must emit `applyResult` /
  `discardResult` exactly once per batch. The async driver's pending counter
  parks the chrome's "Saving…" state until it lands or the 60 s timeout fires.
- **Boundary with phosphor-config and phosphor-shortcuts.** This library owns no
  configuration store and no shortcut backend. Consumers wire their own
  `ISettings` against [`phosphor-config`](../phosphor-config/README.md) and pull
  global shortcuts from [`phosphor-shortcuts`](../phosphor-shortcuts/README.md).
- **Headless-friendly C++ core.** The `PhosphorControl` library links `Qt6::Gui`
  privately and avoids `Qt6::Quick`, so headless tests can construct
  `ApplicationController` / `PageController` / `StagingDomain` without a QML
  engine. The QML module is a separate target.
- **Search is layered.** `SearchRanker` is pure and unit-testable on its own
  (best-tier-per-field scoring, weighted title > keywords > subtitle, edit
  distance only as a "did you mean" fallback). `SearchController` composes it
  with the page registry and providers. The app supplies translated strings, and
  the index stores them verbatim.
- **STATIC QML module in-tree, SHARED on opt-in.** `org.phosphor.control` builds
  STATIC for in-tree consumers (linked directly). Out-of-tree
  `find_package(PhosphorControl)` consumers need the SHARED runtime plugin,
  gated behind `-DPHOSPHOR_CONTROL_QML_INSTALL=ON` (mirrors phosphor-animation).

## Dependencies

- `QtCore`, `QtDBus`, `QtGui` (private), `QtQml`. The QML module adds `QtQuick`
- [`phosphor-animation`](../phosphor-animation/README.md) — the QML chrome
  imports `org.phosphor.animation` for the spring-physics motion profiles
  (panel fade, accordion expand/collapse, tint, pulse)

## See also

- [`phosphor-config`](../phosphor-config/README.md) — the configuration store an
  app's `ISettings` implementation is wired against. This library does not
  duplicate that contract.
- [`phosphor-shortcuts`](../phosphor-shortcuts/README.md) — global-shortcut
  backends, kept out of this library on purpose.
