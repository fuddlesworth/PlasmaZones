// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <PhosphorRegistry/IBarWidgetFactory.h>
#include <PhosphorRegistry/Manifest.h>
#include <PhosphorRegistry/Registry.h>
#include <PhosphorRegistry/phosphorregistry_export.h>

#include <QHash>
#include <QLibrary>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QtCore/qtclasshelpermacros.h>

#include <memory>
#include <vector>

namespace PhosphorFsLoader {
class WatchedDirectorySet;
}

namespace PhosphorRegistry {

// Fixed factory-creation entry point a plugin .so must export. The
// loader resolves this symbol via QLibrary::resolve and calls it
// exactly once per .so load.
//
// ABI contract (the plugin author MUST honour all of these):
//   1. The returned pointer must be a fresh heap allocation
//      (`new MyFactory()`). The loader takes ownership and deletes
//      via std::shared_ptr's deleter. Returning a pointer to a
//      static / stack object is undefined behaviour.
//   2. Returning nullptr is a recoverable failure — the loader
//      logs a warning, unloads the .so, and skips the plugin
//      directory.
//   3. The returned factory's id() must equal the manifest's id
//      field (case-sensitive); the loader rejects mismatches.
//   4. The function must not throw. Exceptions crossing the C ABI
//      boundary are undefined behaviour. Catch internally and
//      return nullptr if construction can fail.
//   5. The function must be safe to call once per .so mapping.
//      The loader never calls it twice on the same QLibrary; a
//      hot-reload involves a fresh QLibrary instance pointing at
//      a different path.
//
// Phase 1.3 wires this entry point for IBarWidgetFactory only. When
// the four other surfaces (control-center tile, launcher provider,
// OSD, desktop widget) get demos in Phase 4, the entry point will
// be parameterised by a "kind" argument so a single plugin can
// export factories for multiple seams. For now: one .so, one
// IBarWidgetFactory.
constexpr const char* PluginEntryPointSymbol = "phosphor_registry_create_factory";
using PluginFactoryEntry = PhosphorRegistry::IBarWidgetFactory* (*)();

// Discovers plugin directories under a configurable root, loads
// each into a Registry<IBarWidgetFactory>, and hot-reloads on
// filesystem change. Built on phosphor-fsloader's
// WatchedDirectorySet (same infrastructure that powers
// ScriptedAlgorithmLoader and the shader registries).
//
// Lifetime contract
//   - The PluginLoader does not own the Registry; the caller does.
//     The Registry must outlive the PluginLoader, since every
//     plugin's unload calls Registry::unregisterFactory.
//   - The PluginLoader owns the .so + the factory shared_ptr for
//     every loaded plugin. On removal (plugin directory disappears
//     between rescans), the .so is moved into m_pinnedLibraries
//     and kept mapped for the PluginLoader's lifetime so that any
//     still-parented widget produced by the now-gone factory keeps
//     working until its parent tears it down. This is the Phase-1.3
//     trade-off discussed in pluginloader.cpp: simple to implement,
//     "no .so ever unmapped during the process lifetime" is the
//     price, Phase 5's sandbox will replace it with a proper
//     refcount-gated unload.
//
// Hot-reload contract
//   - Add: new plugin directory under the root is loaded on the next
//     rescan cycle.
//   - Remove: plugin directory disappearing is honoured — the factory
//     is unregistered from the Registry and the .so pinned.
//   - In-place .so edit: NOT honoured in Phase 1.3. POSIX dlopen
//     refcounts loads by path; reloading the same path returns the
//     prior mapping. Plugin authors iterating in development should
//     rename the plugin directory or restart the process; Phase 5's
//     versioned-path scheme retires this limitation.
//
// Thread affinity
//   - GUI thread only. Mirrors WatchedDirectorySet's contract.
class PHOSPHORREGISTRY_EXPORT PluginLoader : public QObject
{
    Q_OBJECT
public:
    // registry must outlive the PluginLoader. pluginRoot is the
    // directory the loader scans for plugin subdirectories. Default
    // (empty pluginRoot) resolves to
    // ${GenericDataLocation}/phosphor/plugins/. Callers (CI, tests)
    // pass an explicit root to bypass XDG.
    explicit PluginLoader(Registry<IBarWidgetFactory>* registry, const QString& pluginRoot = QString(),
                          QObject* parent = nullptr);
    ~PluginLoader() override;
    Q_DISABLE_COPY_MOVE(PluginLoader)

    // The actual plugin root the loader is using (the resolved path
    // after XDG fallback). Convenient for diagnostics + test
    // assertions.
    [[nodiscard]] QString pluginRoot() const;

    // Run a synchronous initial scan + load. After this returns,
    // every valid plugin found under pluginRoot is registered with
    // the supplied Registry. Hot-reload (WatchedDirectorySet) is
    // armed after this call.
    void scanAndLoad();

    // Force a synchronous rescan. Used by tests + the demo's
    // "Reload" button. Behaves the same as a watcher-driven rescan
    // (additions / removals / .so changes processed identically).
    void rescanNow();

    // Currently-loaded plugin ids (the same ids registered with the
    // Registry). Useful for diagnostic UIs.
    [[nodiscard]] QStringList loadedPluginIds() const;

    // Number of live widgets produced by the named plugin. Phase
    // 1.3 returns -1 ("untracked") — the .so stays mapped for the
    // process lifetime, so refcounted unload adds no safety yet.
    // Phase 5's sandbox work will wire this to a real count.
    [[nodiscard]] int liveWidgetCount(const QString& pluginId) const;

Q_SIGNALS:
    // Fired after every rescan cycle, regardless of whether the
    // plugin set changed. Mirrors WatchedDirectorySet::rescanCompleted's
    // unconditional-emit contract.
    void rescanCompleted();

    // Fired when a plugin is loaded into the registry.
    void pluginLoaded(const QString& id);

    // Fired when a plugin is unloaded from the registry (i.e. its
    // directory was removed from disk between rescans). The .so
    // remains mapped — see the class-level Lifetime contract — so
    // widgets the plugin produced before removal stay valid until
    // their parents tear them down.
    void pluginUnloaded(const QString& id);

private:
    // Tracks one loaded plugin's state. Holds the QLibrary for
    // the .so and the factory the loader returned. See
    // pluginloader.cpp for the full unload-strategy rationale.
    struct LoadedPlugin;
    class ScanStrategyImpl;

    // Test seam: override the IScanStrategy callbacks for headless
    // unit tests. Not part of the public API.
    friend class ScanStrategyImpl;

    // Resolve the default plugin root when the ctor's pluginRoot
    // argument is empty. Always returns
    // ${GenericDataLocation}/phosphor/plugins/ (XDG-honouring via
    // QStandardPaths::writableLocation).
    [[nodiscard]] QString resolveDefaultPluginRoot() const;
    bool ensurePluginRootExists() const;

    // Drive a single scan cycle. Called by the scan strategy after
    // WatchedDirectorySet finds the plugin subdirectories. The
    // strategy returns the manifest paths the WatchedDirectorySet
    // should keep per-file watches on.
    QStringList performScanCycle(const QStringList& directoriesInScanOrder);

    void loadPluginFromDir(const QString& pluginDir);

    // Non-owning. Caller guarantees lifetime per the ctor contract.
    // Cannot use QPointer because Registry<T> is not a QObject (the
    // QObject-derived RegistryNotifier lives inside the template).
    Registry<IBarWidgetFactory>* m_registry = nullptr;
    QString m_pluginRoot;
    std::unique_ptr<ScanStrategyImpl> m_strategy;
    std::unique_ptr<PhosphorFsLoader::WatchedDirectorySet> m_watcher;
    // shared_ptr (not unique_ptr) because QHash's internal node
    // detach machinery requires the value type to be
    // copy-constructible. The plugin map's logical ownership stays
    // exclusive — only the QHash holds a ref in steady state — but
    // shared_ptr unblocks the COW path.
    QHash<QString, std::shared_ptr<LoadedPlugin>> m_plugins;
    // QLibrary instances retained from prior unregisters. Pinned for
    // the PluginLoader's lifetime so old widgets whose vtables live
    // inside the .so keep working. Drains at ~PluginLoader.
    //
    // Known bounded leak: this vector grows monotonically across the
    // process lifetime — each plugin removed at runtime adds one
    // QLibrary mapping that stays until shutdown. For a desktop
    // shell session the upper bound is "user-driven plugin churn,"
    // typically <100 over a day, well within memory budget. The
    // Phase-5 sandbox replaces this with a versioned-path scheme
    // and refcount-gated unload. See pluginloader.cpp for the full
    // Phase-1.3 vs Phase-5 rationale.
    std::vector<std::unique_ptr<QLibrary>> m_pinnedLibraries;
};

} // namespace PhosphorRegistry
