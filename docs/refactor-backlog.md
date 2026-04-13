# Refactor backlog

Deferred refactors identified during the PR #314 single-instance work. Each
entry is scoped large enough to be its own PR and design discussion. They
were explicitly not touched in the PR #314 fix so the immediate bug work
could land without scope creep.

## 1. Extract `EditorLaunchController` from `EditorController`

**Status**: deferred
**Origin**: PR #314 review item (6)
**Files in play**: `src/editor/EditorController.{h,cpp}`,
`src/editor/controller/layout.cpp`, `src/editor/main.cpp`

### Problem

`EditorController` has accumulated unrelated responsibilities:

- Zone editing domain (zones, selection, undo, templates, snapping)
- Layout lifecycle (create/load/save, dirty tracking)
- Shader state + daemon queries
- Clipboard handling
- KConfig persistence (`loadEditorSettings` / `saveEditorSettings`)
- Per-layout gap / padding / visibility overrides
- **D-Bus single-instance lifecycle** (`registerDBusService`, `handleLaunchRequest`, `applyLaunchArgs`, `m_singleInstance` ownership)
- **Window management** (`showFullScreenOnTargetScreen`, screen switching, VS/physical plan)

The single-instance + window management concerns have nothing to do with
zone editing. They're there because the controller is already the QObject
exported over D-Bus and the QML context property.

### Goal

Split out `EditorLaunchController` (or similar) that owns:

- `SingleInstanceService` lifecycle
- `handleLaunchRequest` D-Bus slot
- `applyLaunchArgs` translation from CLI → controller mutations
- Reference to the real `EditorController` and to the primary `QQuickWindow`

`main.cpp` would construct `EditorLaunchController(&editorController)` and
the launch controller would be the one exported at `/EditorApp`. Zone
editing work stays on `EditorController`.

### Trade-offs

- **Pro**: `EditorController.h` drops below the 800-line budget set in
  `CLAUDE.md` (it's currently over).
- **Pro**: clearer boundary for the D-Bus surface — anything Q_SCRIPTABLE
  lives on the launch controller, nothing leaks into the domain model.
- **Pro**: easier to unit-test launch-arg parsing independently of the
  full editor object graph.
- **Con**: introduces another object that QML doesn't need, so the context
  property plumbing in `main.cpp` gets one more step.
- **Con**: `showFullScreenOnTargetScreen` is called from QML
  (`Component.onCompleted` + `onTargetScreenChanged`), so it has to stay
  on `EditorController` unless we also route the QML callers through the
  launch controller. Probably fine — leave window management on the
  controller, move only the D-Bus/launch concerns.

### Rough plan

1. Create `src/editor/EditorLaunchController.{h,cpp}`.
2. Move `m_singleInstance`, `registerDBusService`, `handleLaunchRequest`,
   `applyLaunchArgs` to it. Give it a `EditorController*` constructor arg.
3. `main.cpp` constructs `EditorController` first, then
   `EditorLaunchController(&controller)`; the launch controller's
   `registerDBusService` is the one that main.cpp calls.
4. Update the `SingleInstanceService` export object from `EditorController*`
   to `EditorLaunchController*`.
5. Leave `showFullScreenOnTargetScreen` on `EditorController` — it's QML
   API, not D-Bus API.

---

## 2. Extract cursor → effective-screen resolution

**Status**: deferred
**Origin**: PR #314 review item (7)
**Files in play**: `src/editor/main.cpp`,
`src/editor/controller/layout.cpp`, potentially new
`src/core/screen_resolver.{h,cpp}` or `src/editor/helpers/ScreenResolver.{h,cpp}`

### Problem

`editor/main.cpp` hand-rolls a blocking D-Bus call to the daemon's
`getEffectiveScreenAt` to figure out which physical (or virtual) screen
the cursor is on:

```cpp
QDBusMessage msg = QDBusMessage::createMethodCall(
    QString::fromLatin1(DBus::ServiceName), QString::fromLatin1(DBus::ObjectPath),
    QString::fromLatin1(DBus::Interface::Screen), QStringLiteral("getEffectiveScreenAt"));
msg << cursorPos.x() << cursorPos.y();
QDBusMessage reply = QDBusConnection::sessionBus().call(msg, QDBus::Block, 2000);
// ... parse reply, fall back to QGuiApplication::screenAt ...
```

This is:

- **Bespoke**: nobody else in the codebase does cursor → effective-screen
  resolution this way. If the daemon's interface changes, main.cpp is the
  only caller that has to be updated.
- **Blocking in `main()`**: 2s timeout on every launch, including the
  fast-path forward case where the running instance would re-resolve
  anyway.
- **Non-reusable**: `EditorController::handleLaunchRequest` can't re-
  resolve when a forwarded launch arrives with an empty screen ID — it'd
  have to duplicate the logic.

### Goal

A `ScreenResolver` (or similar) helper with an interface like:

```cpp
namespace PlasmaZones {
class ScreenResolver {
public:
    /// Resolve the effective screen ID (virtual-screen aware) at the given
    /// cursor position. Falls back to the physical QScreen::name() if the
    /// daemon is unreachable. Never returns an empty string when at least
    /// one QScreen exists.
    static QString effectiveScreenAt(QPoint cursorPos, int daemonTimeoutMs = 2000);

    /// Convenience: resolve for the current cursor position.
    static QString effectiveScreenAtCursor(int daemonTimeoutMs = 2000);
};
}
```

`editor/main.cpp` calls `ScreenResolver::effectiveScreenAtCursor()` and
that's it. `EditorController::handleLaunchRequest`, if we want it to re-
resolve empty screen IDs from forwarded launches, can call the same helper.

### Trade-offs

- **Pro**: removes blocking daemon call from `main.cpp` hotpath (cleaner
  separation).
- **Pro**: enables running-instance re-resolution when a forward arrives
  with an empty screen ID — useful if the user crosses monitors between
  the first launch and a repeat press.
- **Con**: one more helper class for a single caller today. Borderline
  premature abstraction until a second caller exists (i.e. until we
  actually want the running-instance re-resolve).

### Rough plan

1. Create `src/core/screen_resolver.{h,cpp}` (in `core` because it's not
   editor-specific — the daemon interface it wraps is shared).
2. Move the daemon D-Bus call + fallback logic there.
3. `editor/main.cpp` cursor-screen block shrinks to one call.
4. (Optional follow-up) `handleLaunchRequest` re-resolves when the
   forwarded `screenId` is empty, using `QCursor::pos()` on the running
   instance's display — gives the user "press shortcut on any monitor,
   editor follows" behavior even without passing `--screen`.

---

## 3. Convert app-level D-Bus surfaces to XML-declared adaptors

**Status**: deferred
**Origin**: PR #314 review item (8)
**Files in play**: `src/editor/EditorController.{h,cpp}`,
`src/settings/settingscontroller.{h,cpp}`, new
`dbus/org.plasmazones.EditorApp.xml` + `dbus/org.plasmazones.SettingsApp.xml`,
`src/CMakeLists.txt`, `src/settings/CMakeLists.txt`

### Problem

The daemon's D-Bus surface (`LayoutManager`, `Screen`, `Settings`, etc.)
follows the project convention documented in `CLAUDE.md`:

> XML interface files → `qt6_add_dbus_adaptor()`

Each daemon interface is declared in an XML file under `dbus/`, and
CMake generates an adaptor class (`LayoutAdaptor`, `SettingsAdaptor`, …)
via `qt6_add_dbus_adaptor()`. The adaptor owns the Q_SCRIPTABLE methods
and forwards them to the real service object.

The app-level single-instance services (`EditorController` +
`SettingsController`) bypass this convention: they mark their own slots
Q_SCRIPTABLE and register themselves directly via
`QDBusConnection::ExportScriptableSlots`. This works but:

- The D-Bus interface isn't introspectable from a static XML file — tools
  like `qdbusviewer` or generated client-side proxies have to rely on
  introspection from a running instance.
- The domain object (`EditorController`) carries Q_SCRIPTABLE annotations
  it wouldn't otherwise need, mixing transport concerns into the domain.
- The convention in `CLAUDE.md` is violated, making it unclear which
  style future services should follow.

### Goal

Introduce `dbus/org.plasmazones.EditorApp.xml` and
`dbus/org.plasmazones.SettingsApp.xml`, add
`qt6_add_dbus_adaptor()` calls in the relevant CMakeLists, and have the
apps instantiate generated `EditorAppAdaptor` / `SettingsAppAdaptor`
classes that forward to the controllers.

### Trade-offs

- **Pro**: consistent with the daemon pattern. One way to add D-Bus
  surfaces in the project.
- **Pro**: XML interface files can be shipped as D-Bus introspection data
  and used by external tooling / clients to generate type-safe proxies.
- **Pro**: removes Q_SCRIPTABLE annotations from the controllers — the
  adaptors are the transport layer.
- **Con**: each new method needs an XML edit + a CMake re-run, which is
  heavier than just adding a Q_SCRIPTABLE slot.
- **Con**: the D-Bus surfaces here are tiny (2-3 methods each). The
  convention's payoff is strongest for big daemon-style interfaces, so
  this is more about consistency than practical benefit.

### Rough plan

1. Write `dbus/org.plasmazones.EditorApp.xml` describing
   `handleLaunchRequest(screenId, layoutId, createNew, preview)`.
2. Write `dbus/org.plasmazones.SettingsApp.xml` describing
   `setActivePage(page)`.
3. Add `qt6_add_dbus_adaptor()` calls to the editor + settings CMake
   targets.
4. Remove Q_SCRIPTABLE annotations from `EditorController::handleLaunchRequest`
   and `SettingsController::setActivePage`.
5. Pass the generated `EditorAppAdaptor(this)` / `SettingsAppAdaptor(this)`
   to `SingleInstanceService` as the export object instead of the
   controller itself.
6. Verify introspection matches the XML via `dbus-send --print-reply ...
   org.freedesktop.DBus.Introspectable.Introspect`.

### Why this lives in the backlog instead of the current PR

PR #314 was focused on fixing a bug (parallel startup race) and a round of
deduplication. Swapping `ExportScriptableSlots` for generated adaptors
across two apps is a separate concern, touches CMake for both targets,
and changes the public introspection surface — it should be reviewed on
its own merits rather than bundled with a fix.

---

## Not in this backlog

Intentionally excluded because they were completed in PR #314:

- `SingleInstanceService` RAII helper (**done**, `src/core/single_instance_service.{h,cpp}`)
- `EditorController::applyLaunchArgs` shared launch-arg entry point (**done**)
- `showFullScreenOnTargetScreen` VS / physical path unification (**done**)
- `m_primaryWindow` caching replacing `QGuiApplication::allWindows()` iteration (**done**, then removed with the focus-steal revert — see below)

Intentionally excluded because the approach doesn't work on Wayland:

- Any form of programmatic "bring the running instance's window to the
  front" (`forceBringToFront`, XDG activation token forwarding,
  `KWindowSystem::activateWindow` with a token). KWin refuses focus-steal
  for already-mapped fullscreen `xdg_toplevel`s from programmatic
  callers, and the XDG activation token path didn't pan out empirically.
  The current behavior is: a forwarded launch applies its args (layout
  switch / screen switch) but does not try to raise the window. The user
  has to focus the existing window themselves.
