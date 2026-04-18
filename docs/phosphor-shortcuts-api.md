# PhosphorShortcuts — API Design

**Status:** design / scaffolding
**Version:** 0.1.0 (pre-release, API unstable)
**License:** LGPL-2.1-or-later (shared library) — consumers under any GPL-compatible licence

## Goals

PhosphorShortcuts is a **domain-free** global shortcut library for Qt6 Wayland
applications. It packages the three mainstream binding strategies behind one
interface:

1. **KGlobalAccel** — KDE Plasma native (requires KF6::GlobalAccel).
2. **XDG Desktop Portal GlobalShortcuts** — Hyprland, GNOME 48+, KDE.
3. **D-Bus trigger** — fallback for Sway, COSMIC, and any compositor that binds
   keys to `dbus-send` calls natively.

The library also defines an extension point (`INativeGrabber`) for a future
standalone Phosphor WM / compositor that performs its own key grabs via
`libinput` or a `wlr-keyboard` listener.

### Non-goals

- **No domain semantics.** The library knows about *keys* and *triggers*; it
  does not know about windows, zones, layouts, or tiling. Consumers layer their
  own action model on top.
- **No QAction in the public API.** `QAction` is kept as an implementation
  detail of the KGlobalAccel backend (that API demands it). Consumers address
  shortcuts by stable string id.
- **No config persistence.** The library never reads or writes config. Consumers
  feed in `QKeySequence` values from wherever they like (`PhosphorConfig`,
  hand-rolled JSON, compiled-in defaults).

## Namespace & headers

```cpp
namespace Phosphor::Shortcuts { /* ... */ }

#include <PhosphorShortcuts/PhosphorShortcuts.h>  // umbrella
#include <PhosphorShortcuts/IBackend.h>
#include <PhosphorShortcuts/Registry.h>
#include <PhosphorShortcuts/Factory.h>
```

CMake:

```cmake
find_package(PhosphorShortcuts 0.1 REQUIRED)
target_link_libraries(mytarget PRIVATE PhosphorShortcuts::PhosphorShortcuts)
```

## Core types

### `IBackend` — pluggable backend interface

```cpp
class IBackend : public QObject {
    Q_OBJECT
public:
    virtual void registerShortcut(const QString& id,
                                  const QKeySequence& defaultSeq,
                                  const QKeySequence& currentSeq,
                                  const QString& description) = 0;
    virtual void updateShortcut(const QString& id,
                                const QKeySequence& newTrigger) = 0;
    virtual void unregisterShortcut(const QString& id) = 0;  // immediate, NOT batched
    virtual void flush() = 0;                // batch-commit queued register/update ops

Q_SIGNALS:
    void activated(QString id);              // key pressed — the ONLY input signal
    void ready();                            // flush() completed
};
```

`registerShortcut` takes both the compiled-in default and the user's current
value as separate arguments. This lets KGlobalAccel record the right value
in `setDefaultShortcut` (so "Reset to default" in System Settings actually
resets to the factory default) while still grabbing whatever the user has
customised via `setShortcut`.

Implementation notes per backend:

| Backend | Behaviour |
|---------|-----------|
| KGlobalAccelBackend | Wraps a private `QHash<QString, QAction*>`. The QAction is internal; consumers never see it. `defaultSeq` → `setDefaultShortcut` (only when the compiled-in default actually changes — tracked internally to avoid redundant kglobalshortcutsrc writes), `currentSeq` → `setShortcut`. |
| PortalShortcutBackend | Uses `BindShortcuts(o, a(sa{sv}), s, a{sv})`. Sends `defaultSeq` as `preferred_trigger` — the compositor treats it as advisory and assigns the actual key. `currentSeq` is supplied by the Registry but the Portal backend doesn't forward it: Portal-based compositors expose their own key-rebinding UI and we cannot override the user's choice from the client side. Pass whatever you read from config as `currentSeq` — it's used transparently on KGlobalAccel and harmless on Portal. |
| DBusTriggerBackend | Ignores both sequences; exposes `TriggerAction(id)` at `/org/Phosphor/Shortcuts`. Users bind keybinds compositor-side. |

#### Unregister semantics (Portal caveat)

`unregisterShortcut` / `Registry::unbind` releases the grab cleanly on
KGlobalAccel (`removeAllShortcuts` on the internal QAction) and on
DBusTrigger (object path is re-available). **On the Portal backend the
release is partial**: the XDG `GlobalShortcuts` interface has no per-id
unbind — `BindShortcuts` is additive by spec, and the only way to drop a
grab is to close the entire session. `PortalBackend::unregisterShortcut`
clears the local registration so `onActivated` drops the event, but the
key stays routed to the session until `PortalBackend` is destroyed.

Consumers that need genuinely transient grabs (e.g. bind on drag start,
release on drag end) on Portal-based compositors should bind the
shortcut once at app startup and gate activation via a boolean inside
the callback rather than rely on bind/unbind cycles. The Registry
accepts a `persistent=false` flag on `bind()` so the adhoc id is
excluded from `bindings(persistentOnly=true)` enumeration regardless of
which backend is active.

### `Registry` — consumer-facing facade

```cpp
class Registry : public QObject {
    Q_OBJECT
public:
    struct Binding {
        QString id;
        QKeySequence defaultSeq;
        QKeySequence currentSeq;
        QString description;
    };

    explicit Registry(IBackend* backend, QObject* parent = nullptr);

    // Register once at startup. Optional callback fires in addition to the triggered() signal.
    void bind(const QString& id,
              const QKeySequence& defaultSeq,
              const QString& description = {},
              std::function<void()> callback = {});

    // Change the active binding (e.g. from a settings change). Takes effect after flush().
    void rebind(const QString& id, const QKeySequence& seq);

    void unbind(const QString& id);
    void flush();

    QKeySequence shortcut(const QString& id) const;
    QVector<Binding> bindings() const;

Q_SIGNALS:
    void triggered(QString id);              // fan-out dispatch (matches IBackend::activated)
    void ready();                            // forwarded from backend
};
```

Contract details worth knowing:

- `bind()` is safe to call multiple times for the same id. The second call
  updates `defaultSeq`, `description`, and `callback` in place but **preserves
  `currentSeq`** — any prior `rebind()` (e.g. from user config) is kept.
- `bind()` with an empty `defaultSeq` is stored but NOT registered with the
  backend until a subsequent `rebind()` supplies a non-empty sequence. This
  matches `rebind()`'s empty-seq → `unbind()` routing and keeps the
  stale-Wayland-grab hazard from PlasmaZones discussion #155 unreachable
  from either entry point.
- `rebind()` with an empty `QKeySequence` routes through `unbind()` rather
  than leaving an empty sequence registered.
- `flush()` picks between `IBackend::registerShortcut` and
  `IBackend::updateShortcut` per id by comparing the current default / current
  sequences against the last values pushed to the backend. First flush after
  bind → `registerShortcut`. Subsequent `defaultSeq` change → another
  `registerShortcut` so the backend can refresh its "reset to default" target
  (KGlobalAccel's `setDefaultShortcut`, Portal's `preferred_trigger`) —
  backend implementations handle repeated `registerShortcut` idempotently.
  Subsequent `currentSeq`-only change → `updateShortcut`.
- **Description changes are local-only.** The `updateShortcut` signature has
  no description argument; a `bind()` that updates only the description
  reaches the Registry (visible via `bindings()`) but does not roundtrip
  through the backend. Callers that need user-facing description edits
  should unbind + bind.
- `unbind()` applies immediately — it is NOT batched until the next flush.
  Backends' unregister paths all run synchronously or with trivial state,
  and a deferred release would be surprising for a "release this grab now"
  API. See the Portal caveat above for what "release" means on that backend.
- `persistent=false` on `bind()` marks a binding as transient for
  `bindings(persistentOnly=true)` enumeration. It does not affect backend
  behaviour — the grab is still registered normally.
- `ready()` is emitted after the backend acknowledges the batch. For
  async backends (Portal) it waits on the D-Bus reply; for synchronous
  backends (KGlobalAccel, DBusTrigger) it fires immediately.
- Activation order: on every backend activation, `Registry` invokes the
  per-binding callback first, then emits `triggered()`. Consumers that
  mutate state in the callback and read it from a `triggered` slot can
  rely on this ordering.

Consumers pick one of two patterns:

- **Fan-out via signal**: connect a single slot to `Registry::triggered` and
  dispatch on `id`. Good for UIs that want a centralised action router.
- **Per-binding callback**: pass `callback` to `bind()`. Good for direct
  hooking without a central dispatcher.

Both fire on every activation; use whichever suits the caller.

### Ad-hoc / transient shortcuts

Consumers that bind a shortcut for the duration of a UI state (e.g. an
Escape cancel grab tied to an active drag) can simply pair `bind()` on
entry with `unbind()` on exit.

For subsystems that need a transient grab without taking a hard dependency
on the concrete shortcut-manager type, the library exposes
`Phosphor::Shortcuts::IAdhocRegistrar` — a pure-abstract two-method
interface (`registerAdhocShortcut` / `unregisterAdhocShortcut`) that the
consumer's manager implements. Subsystems hold an `IAdhocRegistrar*` and
call through it; the underlying Registry stays private to the manager.

```cpp
class WindowDragAdaptor {
    Phosphor::Shortcuts::IAdhocRegistrar* m_registrar = nullptr;
    void onDragStart() {
        m_registrar->registerAdhocShortcut("cancel_overlay", QKeySequence(Qt::Key_Escape),
                                           tr("Cancel"), [this]{ cancelDrag(); });
    }
    void onDragEnd() { m_registrar->unregisterAdhocShortcut("cancel_overlay"); }
};
```

The PlasmaZones daemon's `ShortcutManager` implements `IAdhocRegistrar`
and refuses adhoc registrations during the initial settings-driven batch
(would race the Portal `BindShortcuts` Response). Other consumers may
implement different bind-flow semantics.

One KGlobalAccel-specific caveat: each `unbind()` → `unregisterShortcut()`
on that backend runs `KGlobalAccel::removeAllShortcuts(action)`, which wipes
the persistent entry from `kglobalshortcutsrc`. For a genuinely ephemeral
id this is correct (no stale grab between sessions), but it also means a
user can't customise the key for that id via System Settings — their
customisation is erased on the next `unbind()`. Registry-level entries
that stay bound for the lifetime of the app (the common case) are not
affected; the destructor deliberately does NOT call `removeAllShortcuts`.
If a consumer needs user-customisable bindings for a transient id, keep
the Registry entry bound for the process lifetime and toggle the callback
instead of re-binding.

### Portal Response handling

`PortalBackend` subscribes once to
`org.freedesktop.portal.Request::Response` at construction time with **no
path filter** and demultiplexes signals inside the slot by comparing
`QDBusMessage::path()` against the tracked Request paths for the in-flight
`CreateSession` and `BindShortcuts` requests. This closes a race the spec
explicitly permits: a portal may emit `Response` as soon as the Request
object is created, which can happen before the caller's async-call reply
lands — and if the portal picks a Request path format that differs from
the spec's example convention, a pre-subscribe-on-predicted-path approach
would miss the signal entirely. The path-less subscription never misses;
path matching happens once the signal is already in hand.

The tradeoff is that our slot is invoked for every Response signal the
portal emits to our bus connection, including replays and Responses for
previously-superseded Requests. Each unknown-path signal is dropped with
a `qCDebug` log — cheap, and easier to diagnose than a silent hang.

### `Factory` — runtime backend selection

```cpp
enum class BackendHint {
    Auto,              // detect (default)
    KGlobalAccel,
    Portal,
    DBusTrigger,
    Native,            // future — returns nullptr until an INativeGrabber backend lands
};

std::unique_ptr<IBackend> createBackend(BackendHint hint = BackendHint::Auto,
                                        QObject* parent = nullptr);
```

Auto-detection order (unchanged from the current PlasmaZones logic):

1. No session bus → `DBusTriggerBackend`.
2. `org.kde.kglobalaccel` registered AND KF6 support compiled in →
   `KGlobalAccelBackend`.
3. `org.freedesktop.portal.GlobalShortcuts` advertised on
   `org.freedesktop.portal.Desktop` → `PortalShortcutBackend`.
4. Otherwise → `DBusTriggerBackend`.

## Integration patterns

### PlasmaZones daemon

After migration, `ShortcutManager` collapses from ~420 lines to ~120:

```cpp
m_backend  = Phosphor::Shortcuts::createBackend();
m_registry = new Phosphor::Shortcuts::Registry(m_backend.get(), this);

struct Entry { QStringView id; QKeySequence def; QStringView desc; void (ShortcutManager::*signal)(); };
static constexpr Entry k[] = {
    { u"pz.move-window-left",  QKeySequence("Meta+Shift+Left"),  u"Move window left",  &ShortcutManager::moveWindowLeftRequested },
    /* …one row per shortcut… */
};

for (const auto& e : k) {
    m_registry->bind(e.id.toString(), e.def, e.desc.toString(),
                     [this, s = e.signal]{ (this->*s)(); });
}
m_registry->flush();

connect(m_settings, &Settings::moveWindowLeftShortcutChanged,
        this, [this]{ m_registry->rebind("pz.move-window-left", m_settings->moveWindowLeftShortcut()); m_registry->flush(); });
```

All 40+ `setup*` / `update*` method pairs become table rows.

### Future standalone Phosphor WM

```cpp
// Compositor boot path
auto grabber = std::make_unique<PhosphorWM::LibInputGrabber>(session);
auto backend = std::make_unique<Phosphor::Shortcuts::NativeBackend>(std::move(grabber));
auto registry = std::make_unique<Phosphor::Shortcuts::Registry>(backend.get());

registry->bind("wm.focus-next",   QKeySequence("Mod+Tab"), "Focus next window",
               [&]{ wm.focusNext(); });
registry->bind("wm.close-window", QKeySequence("Mod+q"),   "Close window",
               [&]{ wm.closeFocused(); });
```

Same `Registry`, same `bind()` / `triggered()` contract — just a different
`IBackend` under the hood.

## Future extensions

Reserved for later, not part of the 0.1 scaffold:

### `INativeGrabber` — for standalone WM use

```cpp
class INativeGrabber {
public:
    virtual ~INativeGrabber() = default;
    virtual bool grab(const QString& id, const QKeySequence& seq) = 0;
    virtual void release(const QString& id) = 0;
    virtual QObject* activationNotifier() = 0;   // emits `activated(QString id)`
};
```

A `NativeBackend` adapts any `INativeGrabber` (libinput, wlr-keyboard, XDG
input-method, …) into the standard `IBackend` interface.

### Shortcut scopes

Not every shortcut is global. A future revision will add:

```cpp
enum class Scope {
    Global,     // always active
    Overlay,    // only when a client overlay surface has focus
    Modal,      // only while a specific modal is open
};
```

Scopes route through the same backend but gate activation at the registry
layer.

### Chord sequences

Emacs-style `Ctrl+x Ctrl+s`. Handled entirely in `Registry`; backends still
grab a single leader key.

### Mouse-button triggers

The `QKeySequence` domain already covers this — documenting it as supported
once the portal / kglobalaccel paths are verified.

## Versioning & stability

`0.x` — API may break between minor versions.
`1.0` — API frozen; subsequent minor versions additive-only. Target for 1.0 is
once the PlasmaZones migration ships and one non-PlasmaZones consumer has used
the library in anger.

## Dependencies

- Qt 6.6+ (`Core`, `Gui`, `DBus`)
- **Optional** KF6::GlobalAccel (build flag `PHOSPHORSHORTCUTS_USE_KGLOBALACCEL=ON`,
  default ON if KF6 is found)
- C++20

No link against any PlasmaZones target. No link against `PhosphorConfig`,
`PhosphorZones`, or any other Phosphor library.
