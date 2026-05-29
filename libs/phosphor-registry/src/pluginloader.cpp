// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorRegistry/PluginLoader.h>

#include <PhosphorFsLoader/IScanStrategy.h>
#include <PhosphorFsLoader/WatchedDirectorySet.h>

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QLibrary>
#include <QObject>
#include <QSet>
#include <QStandardPaths>

namespace PhosphorRegistry {

// Live state for one loaded plugin. The QLibrary holds the .so
// mapped for the PluginLoader's lifetime — Phase 1.3 deliberately
// does NOT unload .so files during the process lifetime to avoid the
// "vtable in a freshly-unmapped .so" crash class when an old widget
// from a since-replaced factory is still parented somewhere. Unload
// just unregisters from the Registry; the QLibrary mapping survives
// until PluginLoader::~PluginLoader.
//
// In-place .so REPLACEMENT (writing new bytes to the same path) is
// NOT a supported hot-reload path in Phase 1.3. The reason: POSIX
// dlopen refcounts loads by path. After scheduleUnload pins the old
// QLibrary, a subsequent QLibrary::load() on the same path returns
// the same mapping — so an "mtime changed" loop would re-register
// the OLD code, not the new bytes. Supported hot-reload is
// add/remove of plugin DIRECTORIES (or rename + re-add), not
// in-place .so writes. Plugin authors iterating in development can
// rename their plugin directory or restart the demo.
//
// Phase 5's sandbox work will revisit with a versioned-path scheme
// (copy the .so to .so.<hash> before loading) so in-place edits can
// be honoured without re-using the prior dlopen mapping.
struct PluginLoader::LoadedPlugin
{
    Manifest manifest;
    std::unique_ptr<QLibrary> library;
    std::shared_ptr<IBarWidgetFactory> factory;
};

// Bridge between WatchedDirectorySet's IScanStrategy interface and
// PluginLoader's per-cycle scan logic. Mirrors how
// ScriptedAlgorithmLoader nests a JsScanStrategy inside itself.
class PluginLoader::ScanStrategyImpl : public PhosphorFsLoader::IScanStrategy
{
public:
    explicit ScanStrategyImpl(PluginLoader* owner)
        : m_owner(owner)
    {
    }

    QStringList performScan(const QStringList& directoriesInScanOrder) override
    {
        return m_owner->performScanCycle(directoriesInScanOrder);
    }

private:
    PluginLoader* m_owner = nullptr;
};

PluginLoader::PluginLoader(Registry<IBarWidgetFactory>* registry, const QString& pluginRoot, QObject* parent)
    : QObject(parent)
    , m_registry(registry)
    , m_pluginRoot(pluginRoot.isEmpty() ? resolveDefaultPluginRoot() : pluginRoot)
{
    if (!m_registry) {
        qFatal("PluginLoader: registry must not be null");
    }
    m_strategy = std::make_unique<ScanStrategyImpl>(this);
    m_watcher = std::make_unique<PhosphorFsLoader::WatchedDirectorySet>(*m_strategy);
    // Qt::AutoConnection (the default) is correct here: both ends
    // live on the same thread (GUI thread per the class contract),
    // which AutoConnection resolves to direct-call semantics
    // already. Explicit Qt::DirectConnection adds no safety and
    // would silently lie about cross-thread support if either end
    // moves threads in the future.
    QObject::connect(m_watcher.get(), &PhosphorFsLoader::WatchedDirectorySet::rescanCompleted, this,
                     &PluginLoader::rescanCompleted);
}

PluginLoader::~PluginLoader()
{
    // Drop the watcher first so no rescan races our teardown.
    m_watcher.reset();
    // Snapshot the id list BEFORE iterating: unregisterFactory
    // fires the registry's factoryUnregistered signal, and a slot
    // wired to it (e.g. the demo controller) is fully entitled to
    // mutate Registry / PluginLoader state in response. Iterating
    // m_plugins directly while signals can rebound into us is a
    // QHash-iterator-invalidation hazard. m_registry is non-null by
    // construction (qFatal in the ctor on null), so no runtime guard
    // here — caller-lifetime is the contract.
    const QList<QString> ids = m_plugins.keys();
    for (const QString& id : ids) {
        m_registry->unregisterFactory(id);
    }
    m_plugins.clear();
    // Pinned libraries from prior removals — drop them too. Same
    // ordering: registry was already cleared so no surface can have
    // gotten a factory from a since-pinned plugin in the window
    // between the loop above and this clear.
    m_pinnedLibraries.clear();
}

QString PluginLoader::pluginRoot() const
{
    return m_pluginRoot;
}

void PluginLoader::scanAndLoad()
{
    if (!m_watcher) {
        // Symmetric with rescanNow's guard. m_watcher is constructed
        // unconditionally in the ctor and reset only by the dtor, so
        // the only realistic null window is "scanAndLoad called from
        // a slot during teardown after ~PluginLoader did m_watcher.reset()
        // but before the QObject parent's children list is fully
        // unwound". Cheap to guard, expensive to debug if missed.
        return;
    }
    if (!ensurePluginRootExists()) {
        return;
    }
    m_watcher->registerDirectory(m_pluginRoot, PhosphorFsLoader::LiveReload::On);
}

void PluginLoader::rescanNow()
{
    if (!m_watcher) {
        return;
    }
    m_watcher->rescanNow();
}

QStringList PluginLoader::loadedPluginIds() const
{
    return m_plugins.keys();
}

int PluginLoader::liveWidgetCount(const QString& pluginId) const
{
    // Phase 1.3 does not track per-widget refcount — the unload
    // contract is "registry-drop only, .so stays mapped for the
    // process lifetime" so the count is moot. The accessor stays
    // in the public API as a Phase-5 hook; today it returns -1 to
    // signal "untracked" so tests don't accidentally assert on a
    // refcount value Phase 1.3 doesn't maintain.
    Q_UNUSED(pluginId);
    return -1;
}

QString PluginLoader::resolveDefaultPluginRoot() const
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return QDir(base).filePath(QStringLiteral("phosphor/plugins"));
}

bool PluginLoader::ensurePluginRootExists() const
{
    // Per-path warn-once latch. Without it, every rescanNow() (the
    // watcher's timer, the demo's reload button) re-logs the same
    // "failed to create plugin root" line and floods the journal.
    // Per-path so a future re-config of m_pluginRoot to a different
    // path still surfaces a fresh failure the first time. Keyed on
    // the actual root string; QSet not QHash because we only need
    // membership.
    //
    // Cross-instance retention: the latch is a function-scope static,
    // so the set persists across every PluginLoader instance in the
    // process for the program lifetime. That is intentional. Two
    // PluginLoader instances sharing the same broken plugin root
    // (test setups, multi-window shells) should only log the failure
    // once total, not once per instance. The set entry is dropped
    // the moment any instance manages to create the directory (the
    // remove() at the top of this function), so a recoverable
    // failure resets the latch cleanly.
    //
    // We deliberately did not promote this to a per-instance member.
    // Per-instance would log once-per-instance instead of once-per-
    // process-per-root, which is louder without being more
    // diagnostic for the common case (the shell only constructs one
    // PluginLoader per session).
    //
    // Thread-safety: this set is intentionally NOT guarded by a
    // mutex. The PluginLoader contract (documented on the class) is
    // GUI-thread-only: ensurePluginRootExists is called only from
    // performScanCycle (via the watcher's rescan tick) and from
    // scanAndLoad, both of which are on the GUI thread. A
    // multi-thread caller would violate the class contract before
    // it racily mutated this set; a lock here would mask that misuse
    // rather than catch it. If a future refactor moves any
    // PluginLoader operation off the GUI thread, this set MUST be
    // promoted to either a QMutex-guarded structure or an instance
    // member (whichever the new threading model supports), and the
    // class contract docs MUST be updated in lockstep.
    static QSet<QString> s_logged;

    QDir dir(m_pluginRoot);
    if (dir.exists()) {
        s_logged.remove(m_pluginRoot);
        return true;
    }
    if (!QDir().mkpath(m_pluginRoot)) {
        if (!s_logged.contains(m_pluginRoot)) {
            qWarning() << "PluginLoader: failed to create plugin root" << m_pluginRoot
                       << "(subsequent rescans silently retry)";
            s_logged.insert(m_pluginRoot);
        }
        return false;
    }
    s_logged.remove(m_pluginRoot);
    return true;
}

QStringList PluginLoader::performScanCycle(const QStringList& directoriesInScanOrder)
{
    // For each plugin root, enumerate immediate subdirectories. A
    // subdirectory is a plugin candidate iff it contains a
    // manifest.json. Plugins live under exactly one directory per
    // scan; XDG layering is not relevant for the bundle case.
    QSet<QString> discoveredIds;
    QStringList watchedFiles; // returned to the watcher for per-file re-arm

    for (const QString& root : directoriesInScanOrder) {
        QDir rootDir(root);
        if (!rootDir.exists()) {
            continue;
        }
        const QStringList subdirs = rootDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString& subdir : subdirs) {
            const QString pluginDir = rootDir.absoluteFilePath(subdir);
            const QString manifestPath = QDir(pluginDir).absoluteFilePath(QStringLiteral("manifest.json"));
            if (!QFileInfo::exists(manifestPath)) {
                continue;
            }
            watchedFiles.append(manifestPath);
            const Manifest m = Manifest::parse(manifestPath, pluginDir);
            if (!m.isValid) {
                qWarning().noquote() << "PluginLoader: refusing" << pluginDir << "—" << m.parseError;
                continue;
            }
            discoveredIds.insert(m.id);

            // Already loaded? Phase 1.3 does NOT honour in-place .so
            // replacement (see LoadedPlugin's design note for the
            // POSIX-dlopen-refcount rationale). The supported hot-
            // reload path is directory add/remove. Subsequent
            // rescans of an unchanged plugin are no-ops.
            if (m_plugins.contains(m.id)) {
                continue;
            }
            loadPluginFromDir(pluginDir);
        }
    }

    // Anything in m_plugins not in discoveredIds was removed from
    // disk — unregister it from the registry. The QLibrary stays
    // pinned so existing widgets from the now-gone factory keep
    // working until the bar tears them down.
    //
    // Critical ordering: move the QLibrary into m_pinnedLibraries
    // BEFORE the LoadedPlugin's shared_ptr last ref drops (and runs
    // the factory's deleter). The factory's destructor and any of
    // its vtable dispatch live inside the .so; if the QLibrary were
    // unmapped first, that destructor call would jump into freed
    // memory and segfault. Future refactors of this block MUST
    // preserve the library-move-before-factory-destroy ordering.
    //
    // Re-entry-safety: pluginUnloaded (line emit below) and the
    // registry's factoryUnregistered (via unregisterFactory above)
    // are both wired to user slots in the demo controller. Those
    // slots can mutate m_plugins (e.g. call rescanNow or a follow-
    // up unregister). Since we iterate a SNAPSHOT id list, a slot
    // can race us by removing m_plugins[pluginId] before this
    // iteration body sees it. Take ownership BEFORE any signal
    // fires by erasing the QHash entry first, and tolerate the
    // entry already being gone with a checked constFind early-
    // continue.
    const QList<QString> currentIds = m_plugins.keys();
    for (const QString& pluginId : currentIds) {
        if (discoveredIds.contains(pluginId)) {
            continue;
        }
        const auto it = m_plugins.constFind(pluginId);
        if (it == m_plugins.constEnd()) {
            // A previously-fired pluginUnloaded slot rebounded into
            // a path that removed this entry. Skip it rather than
            // dereffing a default-constructed shared_ptr. This is
            // legitimate re-entry handling, not an error condition,
            // so log at qDebug — qWarning would fire on the happy
            // path every time a slot wired to pluginUnloaded calls
            // rescanNow() and flood the journal.
            //
            // We don't also check `!it.value()`: loadPluginFromDir is
            // the only code path that inserts into m_plugins, and it
            // always stores a freshly-constructed std::make_shared
            // (never null). A null-entry guard here would be dead
            // defensive code masking a contract violation rather than
            // catching one — if a future insert path landed a null
            // shared_ptr we'd want a crash, not a silent skip.
            qDebug() << "PluginLoader: plugin" << pluginId
                     << "vanished mid-unload (rebound from prior signal slot); skipping";
            continue;
        }
        // Move ownership out of the hash BEFORE any signal fires so
        // a re-entrant slot can't observe the entry in two states.
        std::shared_ptr<LoadedPlugin> entry = it.value();
        m_plugins.remove(pluginId);
        m_pinnedLibraries.push_back(std::move(entry->library));
        m_registry->unregisterFactory(pluginId);
        Q_EMIT pluginUnloaded(pluginId);
    }

    return watchedFiles;
}

void PluginLoader::loadPluginFromDir(const QString& pluginDir)
{
    QDir dir(pluginDir);
    // QDir::Name forces lexicographic sort so the "first" pick is
    // deterministic across runs and across filesystems with no
    // stable entry order (tmpfs, some FUSE mounts). Without an
    // explicit sort, QFileSystemEngine returns directory entries
    // in inode-allocation order on most ext4 setups, which makes
    // the choice of .so depend on the order the files were
    // created — a fragile invariant for tests and packaging.
    const QStringList soFiles =
        dir.entryList(QStringList() << QStringLiteral("*.so"), QDir::Files | QDir::Readable, QDir::Name);
    if (soFiles.isEmpty()) {
        qWarning() << "PluginLoader: no .so under" << pluginDir;
        return;
    }
    if (soFiles.size() > 1) {
        // Multiple .so files in one plugin directory is almost
        // certainly a packaging mistake. Surface it so the author
        // notices instead of silently picking the lexicographically
        // first match.
        qWarning().noquote() << "PluginLoader:" << pluginDir << "contains" << soFiles.size() << ".so files; picking"
                             << soFiles.first() << "(deterministic by lexicographic order)";
    }
    const QString libraryPath = dir.absoluteFilePath(soFiles.first());
    const QString manifestPath = dir.absoluteFilePath(QStringLiteral("manifest.json"));

    Manifest manifest = Manifest::parse(manifestPath, pluginDir);
    if (!manifest.isValid) {
        qWarning().noquote() << "PluginLoader: invalid manifest at" << manifestPath << "—" << manifest.parseError;
        return;
    }
    manifest.libraryPath = libraryPath;

    auto library = std::make_unique<QLibrary>(libraryPath);
    if (!library->load()) {
        qWarning().noquote() << "PluginLoader: failed to load" << libraryPath << "—" << library->errorString();
        return;
    }
    auto entryFn = reinterpret_cast<PluginFactoryEntry>(library->resolve(PluginEntryPointSymbol));
    if (!entryFn) {
        qWarning().noquote() << "PluginLoader: plugin" << libraryPath << "missing entry point"
                             << PluginEntryPointSymbol;
        library->unload();
        return;
    }
    IBarWidgetFactory* rawFactory = entryFn();
    if (!rawFactory) {
        qWarning().noquote() << "PluginLoader: entry point returned null for" << libraryPath;
        library->unload();
        return;
    }
    // Wrap the raw factory in a shared_ptr with a custom deleter
    // IMMEDIATELY. The deleter lambda is compiled into the loader's TU,
    // so the `delete p;` call uses the loader's `operator delete`. The
    // contract is therefore "plugin must allocate the factory with the
    // SAME operator new the loader sees" — in practice that means both
    // sides link against the same libstdc++ ABI. This shared_ptr is
    // about exception-safety + early-exit-path coverage (it runs the
    // deleter on every code path below, including the id-mismatch
    // branch), not about isolating cross-TU allocators.
    std::shared_ptr<IBarWidgetFactory> factory(rawFactory, [](IBarWidgetFactory* p) {
        delete p;
    });
    if (factory->id() != manifest.id) {
        // Anchor the warning to the .so path so a triager scanning a
        // log full of plugin failures can correlate factory-id
        // mismatches with the offending file. The factory's own id
        // string may be empty if the plugin author left it blank,
        // making the libraryPath the only stable identifier.
        qWarning().noquote() << "PluginLoader: factory id" << factory->id() << "does not match manifest id"
                             << manifest.id << "from" << libraryPath;
        factory.reset(); // runs custom deleter before .so unmap
        library->unload();
        return;
    }

    auto entryRecord = std::make_shared<LoadedPlugin>();
    entryRecord->manifest = manifest;
    entryRecord->library = std::move(library);
    entryRecord->factory = factory;

    const QString pluginId = manifest.id;
    m_plugins.insert(pluginId, entryRecord);
    // m_registry is non-null by construction (qFatal in the ctor on
    // null), so no runtime guard — caller-lifetime is the contract.
    m_registry->registerFactory(factory);

    Q_EMIT pluginLoaded(pluginId);
}

} // namespace PhosphorRegistry
