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
// loader resolves this symbol via QPluginLoader::resolve and calls
// it once per load. The returned factory's lifetime belongs to the
// loader (kept alive in m_plugins until unload).
//
// Phase 1.3 wires this entry point for IBarWidgetFactory only. When
// the four other surfaces (control-center tile, launcher provider,
// OSD, desktop widget) get demos in Phase 4, the entry point will
// be parameterised by a "kind" argument so a single plugin can
// export factories for multiple seams. For now: one .so, one
// IBarWidgetFactory.
constexpr const char* kPluginEntryPointSymbol = "phosphor_registry_create_factory";
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
//     every loaded plugin. Unload is gated on usage refcount: if a
//     widget produced by this plugin is still alive, the unload is
//     deferred until the last widget destructs. This avoids the
//     "destroy a vtable that live objects still point at" crash
//     class.
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

    // Fired when a plugin is unloaded from the registry. May be
    // delayed past the rescan that triggered the unload if widgets
    // produced by the plugin are still alive — emitted only when
    // the refcount actually drops to zero and the unload completes.
    void pluginUnloaded(const QString& id);

private:
    // Tracks one loaded plugin's state. Holds the QPluginLoader for
    // the .so, the factory the loader returned, and the live-widget
    // refcount that gates unload.
    struct LoadedPlugin;
    class ScanStrategyImpl;

    // Test seam: override the IScanStrategy callbacks for headless
    // unit tests. Not part of the public API.
    friend class ScanStrategyImpl;

    QString resolveDefaultPluginRoot() const;
    QString resolveDefaultPluginRootImpl() const;
    bool ensurePluginRootExists() const;

    // Drive a single scan cycle. Called by the scan strategy after
    // WatchedDirectorySet finds the plugin subdirectories. The
    // strategy returns the manifest paths the WatchedDirectorySet
    // should keep per-file watches on.
    QStringList performScanCycle(const QStringList& directoriesInScanOrder);

    void loadPluginFromDir(const QString& pluginDir);
    void scheduleUnload(const QString& pluginId);
    void completeUnloadIfQuiesced(const QString& pluginId);
    void onWidgetCreated(const QString& pluginId, QObject* widget);
    void onWidgetDestroyed(const QString& pluginId);

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
    // QLibrary instances retained from prior reloads / unregisters.
    // Pinned for the PluginLoader's lifetime so old widgets whose
    // vtables live inside the .so keep working. Drains at
    // ~PluginLoader. See pluginloader.cpp for the Phase-1.3 vs
    // Phase-5 trade-off discussion.
    std::vector<std::unique_ptr<QLibrary>> m_pinnedLibraries;
};

} // namespace PhosphorRegistry
