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
#include <QSet>
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
//   6. The plugin .so must be linked against a libstdc++ ABI
//      compatible with the loader's. The loader wraps the returned
//      factory in a shared_ptr whose deleter compiles `delete p;`
//      against the LOADER's `operator delete`, so the factory
//      must have been allocated via the SAME `operator new` pair.
//      In practice this means both binaries must link against
//      the same libstdc++ (or libc++) implementation; mixing
//      stdlibs across the boundary is undefined behaviour even
//      if the C++ standard versions match.
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
    // Registry). Useful for diagnostic UIs. Order is unspecified
    // (QHash iteration) and not stable across rescans or Qt versions;
    // sort if you need deterministic display, the way the BarController
    // example in the README does.
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
    //
    // Re-entry contract: slots wired to this signal MAY call
    // rescanNow() (the const-find guard in unloadPlugin makes that
    // safe), but doing so amplifies stack depth linearly with the
    // size of the current removal batch — each remaining-to-unload
    // plugin re-enters performScanCycle through the slot. For a
    // desktop shell session this depth is bounded by the user's
    // plugin churn (<100 typically), well under any sane stack
    // budget; a slot that calls rescanNow unconditionally on every
    // unload should add its own re-entry latch if the plugin
    // population is unbounded.
    void pluginUnloaded(const QString& id);

private:
    // Tracks one loaded plugin's state. Holds the QLibrary for
    // the .so and the factory the loader returned. See
    // pluginloader.cpp for the full unload-strategy rationale.
    struct LoadedPlugin;
    // Bridges WatchedDirectorySet's IScanStrategy callbacks into
    // performScanCycle. Nested private class — has access to
    // PluginLoader's privates by virtue of the enclosing-class rule,
    // no `friend` declaration needed.
    class ScanStrategyImpl;

    // Resolve the default plugin root when the ctor's pluginRoot
    // argument is empty. Always returns
    // ${GenericDataLocation}/phosphor/plugins/ (XDG-honouring via
    // QStandardPaths::writableLocation).
    [[nodiscard]] QString resolveDefaultPluginRoot() const;
    bool ensurePluginRootExists() const;

    // Warn-once-per-directory gate for every load-FAILURE path (missing
    // .so, invalid manifest, group/world-writable .so or dir, failed
    // dlopen / corrupt .so, missing entry point, null factory,
    // factory-id mismatch). Returns true at most once per pluginDir
    // until a successful load clears its latch, so a persistently-broken
    // plugin doesn't re-log on every debounced rescan. NOTE: the
    // "multiple .so files" advisory is NOT a failure (it proceeds to
    // load) and uses its own m_warnedMultiSoDirs latch instead.
    [[nodiscard]] bool shouldWarnForPluginDir(const QString& pluginDir);

    // Drive a single scan cycle. Called by the scan strategy after
    // WatchedDirectorySet finds the plugin subdirectories. The
    // strategy returns the manifest paths the WatchedDirectorySet
    // should keep per-file watches on.
    QStringList performScanCycle(const QStringList& directoriesInScanOrder);

    // The manifest is parsed by performScanCycle (which inspects
    // every candidate directory's manifest.json to gather the
    // discovered-id set) and then handed in here. Threading the
    // parsed Manifest through avoids re-running Manifest::parse on
    // the same file twice per discovery cycle — once during
    // enumeration and once during per-plugin load — which would
    // otherwise double the I/O cost of every scan.
    void loadPluginFromDir(const QString& pluginDir, const Manifest& manifest);

    /// Remove a plugin from m_plugins and unregister its factory.
    /// shouldPin == true preserves the QLibrary in m_pinnedLibraries
    /// so still-parented widgets keep their vtable mapping alive
    /// (the per-rescan removal path) AND emits pluginUnloaded for
    /// the same id; shouldPin == false skips both — the dtor case
    /// where the entire process is winding down and pinned libraries
    /// have nothing to outlive. Returns false if the id is no longer
    /// present (legitimate re-entry from a prior signal slot that
    /// rebounded into rescan logic); returns true on a successful
    /// unload. The id-already-gone case is logged at qDebug, not
    /// qWarning — see performScanCycle for the rationale.
    bool unloadPlugin(const QString& id, bool shouldPin);

    // Non-owning. Caller guarantees lifetime per the ctor contract.
    // Cannot use QPointer because Registry<T> is not a QObject (the
    // QObject-derived RegistryNotifier lives inside the template).
    //
    // Class invariant: m_registry is non-null after the ctor returns
    // (the ctor qFatals on null), so every use site below assumes
    // non-null without a runtime guard. If a future refactor adds a
    // path that can null this pointer post-construction, the
    // assumption (and the qFatal that backs it) must be revisited.
    Registry<IBarWidgetFactory>* m_registry = nullptr;
    QString m_pluginRoot;
    std::unique_ptr<ScanStrategyImpl> m_strategy;
    std::unique_ptr<PhosphorFsLoader::WatchedDirectorySet> m_watcher;
    // shared_ptr (not unique_ptr) because Qt 6's QHash still
    // requires the value type to be copy-constructible — the
    // internal Node<K,V> copy ctor used during rehash detach is
    // not move-only friendly even in Qt 6.11. The plugin map's
    // logical ownership stays exclusive (only the QHash holds a
    // ref in steady state); shared_ptr just unblocks the COW path.
    QHash<QString, std::shared_ptr<LoadedPlugin>> m_plugins;
    // QLibrary instances retained from prior unregisters. Pinned for
    // the PluginLoader's lifetime so old widgets whose vtables live
    // inside the .so keep working. ~PluginLoader releases each
    // QLibrary wrapper; the underlying dlopen mapping persists until
    // process exit (QLibrary's default destructor does NOT call
    // unload() — that's intentional here, keeping vtables alive for
    // any stragglers Qt has not yet deleted).
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
    // Warn-once latch for ensurePluginRootExists. Mutated from the
    // const ensurePluginRootExists via `mutable` since the logical
    // "did we already complain about this root?" state is not part
    // of the loader's observable contract — the public API
    // (loadedPluginIds, liveWidgetCount, pluginRoot) returns the
    // same values whether or not we have logged. Plain bool (not a
    // per-path QSet): m_pluginRoot is set in the ctor and never
    // mutated, so a single bool tracks the only path that can ever
    // need latching. See ensurePluginRootExists in pluginloader.cpp
    // for the GUI-thread-only thread-safety rationale.
    mutable bool m_loggedPluginRootFailure = false;
    // Directories already warned about this process. Keyed by plugin
    // directory path; an entry is removed when that directory loads
    // successfully. GUI-thread-only (same contract as the rest of the
    // loader), so it needs no synchronisation. See shouldWarnForPluginDir.
    QSet<QString> m_warnedPluginDirs;
    // Separate warn-once latch for the "multiple .so files" advisory.
    // Kept distinct from m_warnedPluginDirs so the advisory (emitted
    // before the load is attempted) cannot consume the shared
    // load-failure token and mask the actual failure reason when the
    // picked .so then fails to load. Cleared on a successful load.
    QSet<QString> m_warnedMultiSoDirs;
    // Idempotency guard for scanAndLoad. The first successful call
    // hands the plugin root to WatchedDirectorySet::registerDirectory,
    // which arms hot-reload + drives an initial scan. Subsequent
    // calls must NOT re-register the same directory — that would
    // either silently no-op (best case, wasted work) or, in a
    // future WatchedDirectorySet revision that treats re-register
    // as "drop + re-add", briefly disarm the watch. Route repeat
    // calls to rescanNow() so the caller gets the synchronous
    // rescan they almost certainly wanted without touching the
    // registration state.
    bool m_initialScanDone = false;
};

} // namespace PhosphorRegistry
