<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-registry

> Generic factory-by-name registry plus a plugin loader for the five
> UI extension seams the phosphor-shell exposes: bar widgets, control
> center tiles, launcher providers, OSDs, and desktop widgets.

## Responsibility

PlasmaZones already ships several hand-rolled registries
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
  phosphor-fsloader) watches the plugin root with a 50 ms debounced
  rescan. Added plugin directories load; removed directories
  unregister. In-place `.so` replacement (writing new bytes to the
  same path) is NOT honoured in Phase 1.3 because POSIX `dlopen`
  refcounts loads by path — reloading the same path returns the
  prior mapping. Plugin authors iterating in development rename the
  plugin directory or restart the process. Removed plugins keep
  their `.so` mapping pinned for the loader's lifetime so widgets
  the old factory created keep working until they destruct
  naturally; Phase 5 will add a versioned-path scheme + refcounted
  safe-unload alongside the sandbox.
- **Capabilities are advisory in Phase 1.3.** Each factory declares a
  `capabilities()` list (also mirrored into the plugin manifest); the
  enforcement layer lands in Phase 5.

## Key types

| Type | Purpose |
|------|---------|
| `PhosphorRegistry::Registry<T>` | Template registry. Stores factories of type `T` keyed by `T::id()`. Instance-per-composition-root. Emits `factoryRegistered` / `factoryUnregistered` via its `RegistryNotifier`. |
| `PhosphorRegistry::RegistryNotifier` | QObject signal carrier the template owns. The template itself can't be Q_OBJECT; signals route through this. Reached via `registry.notifier()`. |
| `PhosphorRegistry::IFactoryBase` | Common base for the five interface families. Declares `id()`, `displayName()`, `capabilities()`. |
| `PhosphorRegistry::IBarWidgetFactory` | Factory for one top-bar widget (clock, workspaces, tray, etc.). Adds `createWidget(QQmlEngine*, QObject*)`. |
| `PhosphorRegistry::IControlCenterTileFactory` | Factory for one tile inside the Control Center popout. Adds `createTile(QQmlEngine*, QObject*)`. **No consumer in Phase 1.3** — surface lands in Phase 4.4. |
| `PhosphorRegistry::ILauncherProviderFactory` | Factory for a launcher query provider (apps, calculator, etc.). Adds `createProvider(QObject*)`. **No consumer in Phase 1.3** — surface lands in Phase 4.2. |
| `PhosphorRegistry::IOSDFactory` | Factory for an on-screen display (volume / mic / brightness). Adds `createOSD(QQmlEngine*, QObject*)`. **No consumer in Phase 1.3** — surface lands in Phase 3.3. |
| `PhosphorRegistry::IDesktopWidgetFactory` | Factory for a desktop card / widget. Adds `createWidget(QQmlEngine*, QObject*)`. **No consumer in Phase 1.3** — surface lands in Phase 4.4 / 5. |
| `PhosphorRegistry::Manifest` | Plain-old-data mirror of `manifest.json`. `Manifest::parse` reads the file; `parseObject` exists for tests. Rejects ABI mismatch, missing fields, id / directory mismatch. |
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

**The bar QML** binds to a `QAbstractListModel` exposing the registry's ids;
on each `factoryRegistered` / `factoryUnregistered` the model emits a row
change and the bar layout refreshes. The model lives in the shell's
glue layer (Phase 4.1), not in this library — the library stays
QML-free so consumers own their own facade.

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
// in my-plugin/manifest.json — same directory as the .so
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
  factory's `id()` against the manifest's `id` field; mismatch is a
  refused load with a clear log message. This prevents a renamed
  plugin from quietly taking over another's slot.
- **Unload is registry-drop only in Phase 1.3.** Removing a plugin from
  disk unregisters its factory from the registry but pins the `QLibrary`
  mapping for the loader's lifetime. Widgets the now-gone plugin
  produced keep working until they destruct naturally. Phase 5's
  sandbox revisits this with refcounted safe-unload.
- **ABI mismatch rejected at load.** Manifests declaring `"abi"` other
  than `PhosphorRegistry::kPluginAbiVersion` (currently `1`) are refused
  with a clear log message. Bumping the ABI version is reserved for
  changes to the C entry point signature, the factory vtable layout,
  or the manifest schema.
- **Capabilities are advisory.** The `capabilities()` list ships in
  every factory and every manifest, but the loader does not enforce
  them in Phase 1.3. Phase 5's sandbox uses the same metadata slot.

## Dependencies

- `QtCore`, `QtGui`, `QtQml`. The library does not link `QtQuick`;
  consumers that build widgets do.
- `phosphor-fsloader` (private link): `WatchedDirectorySet` +
  `IScanStrategy` drive the plugin loader's hot-reload path.

## See also

- `examples/phosphor-registry-demo/` — toy bar with two built-in widgets
  registered explicitly. Proves the registry seam.
- `examples/phosphor-registry-plugin-demo/` — same toy bar plus a third
  widget loaded from a separate `.so`. Proves the plugin ABI and
  hot-reload.
- `libs/phosphor-layout-api/ILayoutSourceFactory` — the pre-existing
  registry pattern this library generalises.
- `docs/phosphor-shell-design/04-implementation-plan.md` Phase 1.3 —
  this library's roadmap entry.
