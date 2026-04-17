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
                                  const QKeySequence& preferredTrigger,
                                  const QString& description) = 0;
    virtual void updateShortcut(const QString& id,
                                const QKeySequence& newTrigger) = 0;
    virtual void unregisterShortcut(const QString& id) = 0;
    virtual void flush() = 0;                // batch commit queued ops

Q_SIGNALS:
    void activated(QString id);              // key pressed — the ONLY input signal
    void ready();                            // flush() completed
};
```

Implementation notes per backend:

| Backend | Behaviour |
|---------|-----------|
| KGlobalAccelBackend | Wraps a private `QHash<QString, QAction*>`. The QAction is internal; consumers never see it. |
| PortalShortcutBackend | Uses `BindShortcuts(o, a(sa{sv}), s, a{sv})`. `preferredTrigger` is advisory — the compositor assigns the actual key. |
| DBusTriggerBackend | Ignores `preferredTrigger` entirely; exposes `TriggerAction(id)` at `/org/Phosphor/Shortcuts`. Users bind keybinds compositor-side. |

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
    void triggered(const QString& id);       // fan-out dispatch
    void ready();                            // forwarded from backend
};
```

Consumers pick one of two patterns:

- **Fan-out via signal**: connect a single slot to `Registry::triggered` and
  dispatch on `id`. Good for UIs that want a centralised action router.
- **Per-binding callback**: pass `callback` to `bind()`. Good for direct
  hooking without a central dispatcher.

Both fire on every activation; use whichever suits the caller.

### `Factory` — runtime backend selection

```cpp
enum class BackendHint {
    Auto,              // detect (default)
    KGlobalAccel,
    Portal,
    DBusTrigger,
    Native,            // future — falls back to DBusTrigger for now
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
