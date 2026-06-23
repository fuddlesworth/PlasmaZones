<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-registry

> Generic factory-by-name registry plus a plugin loader for the five
> UI extension seams the phosphor-shell exposes: bar widgets, control
> center tiles, launcher providers, OSDs, and desktop widgets.

## Responsibility

Phosphor already ships several hand-rolled registries
(`phosphor-layout-api/ILayoutSourceFactory`, `phosphor-tiles/AlgorithmRegistry`,
`phosphor-shaders/ShaderRegistry`, `phosphor-animation/AnimationShaderRegistry`).
Each one was designed independently for its own domain. `phosphor-registry`
generalises the pattern into a single template that the shell instantiates
once per UI seam.

- **One `Registry<T>` template, five interface specialisations.** The shell
  owns one `Registry<IBarWidgetFactory>`, one `Registry<IControlCenterTileFactory>`,
  and so on. Each is independent. Tests and the demos instantiate their
  own local registries with fakes.
- **Built-ins register explicitly.** The shell's composition root calls
  `registry.registerFactory(...)` for every built-in. No static-init magic,
  no global pending list. Same pattern for plugins (the `PluginLoader`
  calls the same `registerFactory`).
- **Plugins are .so + `manifest.json` bundles** under
  `~/.local/share/phosphor/plugins/<id>/`. The `PluginLoader` scans the
  directory, validates each manifest's ABI version, loads the `.so` via
  `QLibrary`, resolves a fixed C entry point, and registers the returned
  factory.
- **Hot-reload on disk change.** A `WatchedDirectorySet` (from
  phosphor-fsloader) watches the plugin root with a debounced
  rescan. Added plugin directories load, and removed directories
  unregister. In-place `.so` replacement (writing new bytes to the
  same path) is NOT honoured today because POSIX `dlopen`
  refcounts loads by path. Reloading the same path returns the
  prior mapping. Plugin authors iterating in development rename the
  plugin directory or restart the process. Removed plugins keep
  their `.so` mapping pinned for the loader's lifetime so widgets
  the old factory created keep working until they destruct
  naturally. A versioned-path scheme plus refcounted safe-unload
  is a future addition alongside the sandbox.
- **Capabilities are advisory for now.** Each factory declares a
  `capabilities()` list (also mirrored into the plugin manifest). An
  enforcement layer is a future addition.

## Key types

| Type | Purpose |
|------|---------|
| `PhosphorRegistry::Registry<T>` | Template registry. Stores factories of type `T` keyed by `T::id()`. Instance-per-composition-root. Emits `factoryRegistered` / `factoryUnregistered` via its `RegistryNotifier`. |
| `PhosphorRegistry::RegistryNotifier` | QObject signal carrier the template owns. The template itself can't be Q_OBJECT, so signals route through this. Reached via `registry.notifier()`. |
| `PhosphorRegistry::IFactoryBase` | Common base for the five interface families. Declares `id()`, `displayName()`, `capabilities()`. |
| `PhosphorRegistry::IBarWidgetFactory` | Factory for one top-bar widget (clock, workspaces, tray, etc.). Adds `createWidget(QQmlEngine*, QObject*)`. |
| `PhosphorRegistry::IControlCenterTileFactory` | Factory for one tile inside the Control Center popout. Adds `createTile(QQmlEngine*, QObject*)`. **No consumer yet**, reserved for a future control-center surface. |
| `PhosphorRegistry::ILauncherProviderFactory` | Factory for a launcher query provider (apps, calculator, etc.). Adds `createProvider(QObject*)`. **No consumer yet**, reserved for a future launcher surface. |
| `PhosphorRegistry::IOSDFactory` | Factory for an on-screen display (volume / mic / brightness). Adds `createOSD(QQmlEngine*, QObject*)`. **No consumer yet**, reserved for a future OSD surface. |
| `PhosphorRegistry::IDesktopWidgetFactory` | Factory for a desktop card / widget. Adds `createWidget(QQmlEngine*, QObject*)`. **No consumer yet**, reserved for a future desktop-widget surface. |
| `PhosphorRegistry::Manifest` | Plain-old-data mirror of `manifest.json`. `Manifest::parse` reads the file, and `parseObject` exists for tests. Rejects ABI mismatch, missing fields, id / directory mismatch. |
| `PhosphorRegistry::PluginLoader` | Scans a plugin root, loads each plugin's `.so` + manifest, registers in a `Registry<IBarWidgetFactory>`. Hot-reloads on filesystem change. |

## Typical use

**C++ shell composition root**: register built-ins explicitly, then arm the
plugin loader for any third-party plugins.

```cpp
#include <PhosphorRegistry/PhosphorRegistry.h>

using namespace PhosphorRegistry;

Registry<IBarWidgetFactory> barRegistry;

// Built-ins: explicit register
barRegistry.registerFactory(std::make_shared<ClockFactory>());
barRegistry.registerFactory(std::make_shared<WorkspacesFactory>());
barRegistry.registerFactory(std::make_shared<MediaFactory>());

// Plugins (loaded asynchronously into the same registry):
PluginLoader loader(&barRegistry);  // defaults to ~/.local/share/phosphor/plugins/
loader.scanAndLoad();

QObject::connect(barRegistry.notifier(), &RegistryNotifier::factoryRegistered,
                 [&](const QString& id) { qDebug() << "registered" << id; });
```

**Glue layer**: the shell wraps the registry in a `QObject` controller
that exposes the factory ids as a `Q_PROPERTY(QStringList)` and the
widget construction as a `Q_INVOKABLE`. The library stays QML-free so
consumers own their own facade. This controller pattern is what both
phosphor-registry demos use. The snippets below name it `BarController`
to match the production-shell shape (a top-bar widget controller).
`examples/phosphor-registry-demo/Main.qml` wires the same pattern under
the context-property name `demoController` to keep the demo's QML
self-describing.

```cpp
// barcontroller.h
class BarController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList factoryIds READ factoryIds NOTIFY factoryIdsChanged)
public:
    explicit BarController(QObject* parent = nullptr);
    void setEngine(QQmlEngine* engine); // call once from main()
    Q_INVOKABLE QQuickItem* createWidgetFor(const QString& id, QQuickItem* parent);
    [[nodiscard]] QStringList factoryIds() const;
Q_SIGNALS:
    void factoryIdsChanged();
private:
    QPointer<QQmlEngine> m_engine;
    PhosphorRegistry::Registry<PhosphorRegistry::IBarWidgetFactory> m_registry;
};

// barcontroller.cpp: forward the registry's signals as one factoryIdsChanged
QObject::connect(m_registry.notifier(),
                 &PhosphorRegistry::RegistryNotifier::factoryRegistered,
                 this, &BarController::factoryIdsChanged);
QObject::connect(m_registry.notifier(),
                 &PhosphorRegistry::RegistryNotifier::factoryUnregistered,
                 this, &BarController::factoryIdsChanged);

QStringList BarController::factoryIds() const
{
    // Registry::ids() returns registration (insertion) order, which is
    // already deterministic. We sort here only to present an alphabetical
    // bar layout (a real shell would persist a user-configured order; the
    // demos sort alphabetically for simplicity).
    QStringList ids = m_registry.ids();
    ids.sort();
    return ids;
}

QQuickItem* BarController::createWidgetFor(const QString& id, QQuickItem* parent)
{
    if (!m_engine) return nullptr;
    auto factory = m_registry.factory(id);
    if (!factory) return nullptr;
    return factory->createWidget(m_engine.data(), parent);
}
```

**Bar QML**: a `Repeater` drives `factoryIds` and parents each widget
under its own delegate `Item`. Repeater destroys old delegates on
model change, and QObject's parent-cascade takes the C++-owned widgets
with them (no manual `child.destroy()`, which fails on C++-owned
QQuickItems with "indestructible object" errors).

```qml
Row {
    spacing: 8

    Repeater {
        model: barController ? barController.factoryIds : []

        delegate: Item {
            id: slot
            required property string modelData
            property Item widget: null

            implicitWidth: widget ? widget.implicitWidth : 0
            implicitHeight: widget ? widget.implicitHeight : 0

            Component.onCompleted: {
                slot.widget = barController.createWidgetFor(slot.modelData, slot)
            }
        }
    }
}
```

The bar refreshes automatically when plugins load or unload because
`factoryIdsChanged` re-evaluates the Repeater's `model` binding. See
`examples/phosphor-registry-demo/` for the in-process variant and
`examples/phosphor-registry-plugin-demo/` for the same pattern with a
third widget loaded from a separate `.so`.

**Plugin author side**: a plugin is a `.so` exporting one extern "C"
entry point alongside a `manifest.json`.

```cpp
// in my-plugin/myplugin.cpp
#include <PhosphorRegistry/IBarWidgetFactory.h>

class MyPluginFactory : public PhosphorRegistry::IBarWidgetFactory { /* ... */ };

extern "C" Q_DECL_EXPORT PhosphorRegistry::IBarWidgetFactory*
phosphor_registry_create_factory()
{
    return new MyPluginFactory();
}
```

```json
// in my-plugin/manifest.json: same directory as the .so
{
    "id": "my-plugin",
    "displayName": "My Plugin",
    "abi": 1,
    "capabilities": ["bar.widget"]
}
```

The plugin directory's basename must equal the manifest's `id`. The
loader enforces this so on-disk layout and registry keys stay aligned.

## Arbitration / lifecycle

- **Duplicate id rejected.** `registerFactory` of an id already in the
  registry is a no-op + qWarning (the first registration wins). The
  plugin loader hits the same path for any plugin whose id collides
  with an existing one.
- **Factory id must match manifest id.** The loader cross-checks the
  factory's `id()` against the manifest's `id` field, and a mismatch is a
  refused load with a clear log message. This prevents a renamed
  plugin from quietly taking over another's slot.
- **Symlinks are not followed.** Plugin subdirectories and `.so` files
  are enumerated with `QDir::NoSymLinks`, so a symlinked entry pointing
  outside the (user-writable) plugin root cannot smuggle a plugin tree
  or shared object past the basename-equals-id containment rule.
- **Group/world-writable plugins refused.** Before `dlopen`, the loader
  refuses any `.so` (or plugin directory) that is group- or
  world-writable, the same StrictModes discipline OpenSSH and sudo
  apply to files they trust. A permissive mode would let another local
  process overwrite the code the shell is about to run. Signature /
  origin verification is reserved for a future sandbox.
- **Unload is registry-drop only.** Removing a plugin from
  disk unregisters its factory from the registry but pins the `QLibrary`
  mapping for the loader's lifetime. Widgets the now-gone plugin
  produced keep working until they destruct naturally. A future
  sandbox revisits this with refcounted safe-unload.
- **ABI mismatch rejected at load.** Manifests declaring `"abi"` other
  than `PhosphorRegistry::PluginAbiVersion` (currently `1`) are refused
  with a clear log message. Bumping the ABI version is reserved for
  changes to the C entry point signature, the factory vtable layout,
  or the manifest schema.
- **Capabilities are advisory.** The `capabilities()` list ships in
  every factory and every manifest, but the loader does not enforce
  them yet. A future sandbox uses the same metadata slot.

## Dependencies

- `QtCore`, `QtQml`. The library does not link `QtGui` or `QtQuick`.
  Consumers that build widgets do.
- `phosphor-fsloader` (PUBLIC link): `WatchedDirectorySet` + `IScanStrategy`
  back both the plugin loader's hot-reload path and the header-only
  `MetadataPackLoader<T>` template, which exposes fsloader types in its public
  interface, so the dependency is transitive (PUBLIC) for consumers.

## See also

- `examples/phosphor-registry-demo/`: toy bar with two built-in widgets
  registered explicitly. Proves the registry seam.
- `examples/phosphor-registry-plugin-demo/`: same toy bar plus a third
  widget loaded from a separate `.so`. Proves the plugin ABI and
  hot-reload.
- `libs/phosphor-layout-api/ILayoutSourceFactory`: the pre-existing
  registry pattern this library generalises.
