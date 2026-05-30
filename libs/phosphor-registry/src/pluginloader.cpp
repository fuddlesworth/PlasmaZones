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
// dlopen refcounts loads by path. After the unload path pins the
// old QLibrary in m_pinnedLibraries, a subsequent QLibrary::load()
// on the same path returns the same mapping — so an "mtime changed"
// loop would re-register the OLD code, not the new bytes. Supported
// hot-reload is add/remove of plugin DIRECTORIES (or rename + re-add),
// not in-place .so writes. Plugin authors iterating in development
// can rename their plugin directory or restart the demo.
//
// Phase 5's sandbox work will revisit with a versioned-path scheme
// (copy the .so to .so.<hash> before loading) so in-place edits can
// be honoured without re-using the prior dlopen mapping.
struct PluginLoader::LoadedPlugin
{
    // Member ORDER is load-bearing — do NOT reorder.
    //
    // Members destruct in reverse declaration order, so on ~LoadedPlugin
    // the factory shared_ptr drops FIRST (running the factory's vtable-
    // backed destructor while the .so is still mapped), then the
    // QLibrary unique_ptr drops. If the QLibrary were declared after
    // the factory, the unmap would race the factory's vtable lookup
    // and crash. The destructorWithoutPriorRescanDoesNotCrash test
    // pins this ordering; a future field-reorder sweep would silently
    // break process-shutdown safety.
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
    // QHash-iterator-invalidation hazard. (m_registry assumed non-
    // null — see the class invariant on the field.)
    //
    // Route through unloadPlugin with shouldPin == false: we don't
    // need to preserve the QLibrary mappings here (the whole process
    // is winding down; pinned libraries have nothing to outlive) AND
    // we don't emit pluginUnloaded (the QObject signal infrastructure
    // is being torn down — subscribers can rely on
    // factoryUnregistered from the Registry, which IS still safe
    // to fire). The performScanCycle removal path uses shouldPin ==
    // true for the opposite reason: surviving widgets need their
    // vtable mappings preserved.
    const QList<QString> ids = m_plugins.keys();
    for (const QString& id : ids) {
        unloadPlugin(id, /*shouldPin=*/false);
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
    // Idempotency: a second scanAndLoad must NOT call
    // registerDirectory again on the same root. WatchedDirectorySet's
    // contract is "register the directory once; subsequent
    // registrations are unspecified" — at best a no-op, at worst
    // a transient unwatched window if a future revision treats
    // re-register as drop+re-add. The almost-certain caller intent
    // ("rescan now please") is what rescanNow() does, so route
    // there. The guard latches on first successful registerDirectory
    // — if ensurePluginRootExists fails (mkpath rejected), the
    // latch stays false so a follow-up scanAndLoad after the
    // operator fixes the root retries registration cleanly.
    if (m_initialScanDone) {
        rescanNow();
        return;
    }
    if (!ensurePluginRootExists()) {
        return;
    }
    m_watcher->registerDirectory(m_pluginRoot, PhosphorFsLoader::LiveReload::On);
    m_initialScanDone = true;
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
    if (base.isEmpty()) {
        // No writable GenericDataLocation — XDG_DATA_HOME and HOME are
        // both unset (unusual but possible in stripped containers /
        // headless CI). Returning a relative "phosphor/plugins" would
        // resolve against CWD at scan time, almost never what the shell
        // wants. Return an empty sentinel; ensurePluginRootExists will
        // refuse to mkpath an empty string and the loader stays inert
        // rather than silently scanning CWD.
        qWarning().noquote() << "PluginLoader: GenericDataLocation is empty (no XDG_DATA_HOME or HOME); "
                                "plugin discovery disabled. Pass an explicit pluginRoot to the ctor.";
        return QString();
    }
    return QDir(base).filePath(QStringLiteral("phosphor/plugins"));
}

bool PluginLoader::ensurePluginRootExists() const
{
    // Warn-once latch. Without it, every rescanNow() (the watcher's
    // timer, the demo's reload button) re-logs the same "failed to
    // create plugin root" line and floods the journal.
    //
    // Plain bool: m_pluginRoot is set in the ctor and never mutated,
    // so a single latch covers the only path that can ever reach
    // this function. The latch is cleared the moment this instance
    // manages to create (or finds) the directory, so a recoverable
    // failure resets cleanly and the next failure re-logs.
    //
    // Per-instance retention: the bool is a member, so its lifetime
    // is tied to the PluginLoader instance. The shell only constructs
    // one PluginLoader per session in practice — multiple instances
    // sharing a broken root would each log once, which is fine.
    //
    // Thread-safety: unguarded — PluginLoader is GUI-thread-only per
    // the class contract. Promote to an atomic + update the class
    // docs in lockstep if any caller ever moves off-thread.

    if (m_pluginRoot.isEmpty()) {
        // resolveDefaultPluginRoot returned empty (no GenericDataLocation)
        // — refuse cleanly rather than mkpath an empty string (which
        // resolves to "" → CWD). The empty-path warning was already
        // logged at construction time; stay silent here on every rescan.
        return false;
    }
    QDir dir(m_pluginRoot);
    if (dir.exists()) {
        m_loggedPluginRootFailure = false;
        return true;
    }
    if (!QDir().mkpath(m_pluginRoot)) {
        if (!m_loggedPluginRootFailure) {
            qWarning() << "PluginLoader: failed to create plugin root" << m_pluginRoot
                       << "(subsequent rescans silently retry)";
            m_loggedPluginRootFailure = true;
        }
        return false;
    }
    m_loggedPluginRootFailure = false;
    return true;
}

QStringList PluginLoader::performScanCycle(const QStringList& directoriesInScanOrder)
{
    // For each plugin root, enumerate immediate subdirectories. A
    // subdirectory is a plugin candidate iff it contains a
    // manifest.json. Plugins live under exactly one directory per
    // scan; XDG layering is not relevant for the bundle case.
    // QHash rather than QSet so we can name the first-seen directory
    // when warning about the second-and-later colliding plugins (the
    // QSet version dropped the first path's identity on the floor).
    QHash<QString, QString> discoveredIds; // id → first-seen pluginDir this cycle
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
            // Duplicate-id detection within this rescan cycle.
            //
            // PHASE-X: this branch is UNREACHABLE today because
            // Manifest::parse enforces that the plugin's directory
            // basename equals its manifest id, and two sibling
            // directories cannot share a basename on any sane
            // filesystem. The check matters once PluginLoader scans
            // MULTIPLE roots (XDG layering: /usr/share/phosphor/plugins/foo
            // AND ~/.local/share/phosphor/plugins/foo can both ship
            // manifest id "foo"), at which point performScanCycle
            // iterates a non-singleton `directoriesInScanOrder`.
            //
            // IMPORTANT for the Phase-X implementer: the "first wins"
            // tie-break this branch encodes is INVERTED relative to
            // standard XDG override semantics. IScanStrategy's contract
            // (see phosphor-fsloader/IScanStrategy.h) hands
            // directoriesInScanOrder in [lowest-priority, ...,
            // highest-priority] order, and consumers are expected to
            // reverse-iterate so the user's ~/.local override wins
            // over the system /usr/share entry. The current forward
            // iteration with first-wins would silently drop the
            // user's override — that is wrong for any XDG-shaped
            // layering. Either reverse the outer loop and keep
            // first-wins, OR keep forward iteration and switch this
            // branch to last-wins (overwrite the existing entry +
            // unload the prior). Verify against the sibling pattern
            // in phosphor-fsloader's MetadataPackScanStrategy /
            // DirectoryLoader before shipping multi-root.
            //
            // The warning names both directories so a triager can
            // spot the collision instead of guessing.
            const auto existing = discoveredIds.constFind(m.id);
            if (existing != discoveredIds.constEnd()) {
                qWarning().noquote() << "PluginLoader: duplicate manifest id" << m.id << "in" << pluginDir
                                     << "— already loaded from" << existing.value() << "this rescan; skipping";
                continue;
            }
            discoveredIds.insert(m.id, pluginDir);

            // Already loaded from a previous rescan? Phase 1.3 does NOT
            // honour in-place .so replacement (see LoadedPlugin's
            // design note for the POSIX-dlopen-refcount rationale).
            // The supported hot-reload path is directory add/remove.
            // Subsequent rescans of an unchanged plugin are no-ops.
            if (m_plugins.contains(m.id)) {
                continue;
            }
            // Hand the already-parsed Manifest through to avoid a
            // second Manifest::parse pass inside loadPluginFromDir
            // — the file has not changed on disk between the parse
            // above and the load call below, so re-reading it would
            // just duplicate work (and risk a transient parse-
            // failure-on-second-read flake if the file is being
            // edited concurrently).
            loadPluginFromDir(pluginDir, m);
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
        // shouldPin == true: surviving widgets produced by the now-
        // gone factory need the .so to stay mapped until their
        // parents tear them down (see LoadedPlugin's design note for
        // the vtable-in-unmapped-.so crash class this defends
        // against). unloadPlugin also fires pluginUnloaded here.
        unloadPlugin(pluginId, /*shouldPin=*/true);
    }

    return watchedFiles;
}

bool PluginLoader::unloadPlugin(const QString& id, bool shouldPin)
{
    // Shared unload routine: both the dtor (shouldPin == false, no
    // emit) and the per-rescan removal path (shouldPin == true,
    // emit pluginUnloaded) route through here so the hash-erase →
    // optionally-pin → unregister sequence can't drift between them.
    const auto it = m_plugins.constFind(id);
    if (it == m_plugins.constEnd()) {
        // A previously-fired pluginUnloaded slot rebounded into a
        // path that removed this entry. Skip it rather than
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
        qDebug() << "PluginLoader: plugin" << id << "vanished mid-unload (rebound from prior signal slot); skipping";
        return false;
    }
    // Move ownership out of the hash BEFORE any signal fires so a
    // re-entrant slot can't observe the entry in two states.
    std::shared_ptr<LoadedPlugin> entry = it.value();
    m_plugins.remove(id);
    if (shouldPin) {
        // Critical ordering: move the QLibrary into m_pinnedLibraries
        // BEFORE the LoadedPlugin's shared_ptr last ref drops (and
        // runs the factory's deleter). The factory's destructor and
        // any of its vtable dispatch live inside the .so; if the
        // QLibrary were unmapped first, that destructor call would
        // jump into freed memory and segfault.
        m_pinnedLibraries.push_back(std::move(entry->library));
    }
    m_registry->unregisterFactory(id);
    if (shouldPin) {
        Q_EMIT pluginUnloaded(id);
    }
    return true;
}

void PluginLoader::loadPluginFromDir(const QString& pluginDir, const Manifest& parsedManifest)
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

    // performScanCycle has already validated the manifest before
    // reaching us — the only caller — so an invalid one here would
    // be a contract violation by a future second caller. Guard
    // anyway so a stray invalid Manifest can't load a .so against
    // a placeholder id. The libraryPath field is the one piece
    // performScanCycle doesn't know about (it discovers the .so
    // here), so populate it onto a local copy before storing.
    if (!parsedManifest.isValid) {
        qWarning().noquote() << "PluginLoader: refusing" << pluginDir
                             << "— manifest invalid:" << parsedManifest.parseError;
        return;
    }
    Manifest manifest = parsedManifest;
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
        // QLibrary::unload() can return false (refcount held elsewhere,
        // page-unmap failure). Surface the unload-failure case so a
        // triager seeing a stuck plugin can correlate it with a follow-up
        // load attempt that still finds the old code mapped in.
        if (!library->unload()) {
            qDebug().noquote() << "PluginLoader: unload() returned false for" << libraryPath;
        }
        return;
    }
    // Wrap entryFn()'s raw pointer into the shared_ptr ON THE SAME
    // statement so an exception escaping the deleter binding never
    // strands the allocation. The deleter lambda is compiled into the
    // loader's TU so `delete p;` runs against the loader's
    // `operator delete` — the plugin must allocate the factory with
    // the SAME libstdc++ ABI the loader sees. The shared_ptr also
    // gives every early-exit branch below automatic cleanup without a
    // per-branch raw `delete`.
    std::shared_ptr<IBarWidgetFactory> factory(entryFn(), [](IBarWidgetFactory* p) {
        delete p;
    });
    if (!factory) {
        qWarning().noquote() << "PluginLoader: entry point returned null for" << libraryPath;
        if (!library->unload()) {
            qDebug().noquote() << "PluginLoader: unload() returned false for" << libraryPath;
        }
        return;
    }
    if (factory->id() != manifest.id) {
        // Anchor the warning to the .so path so a triager scanning a
        // log full of plugin failures can correlate factory-id
        // mismatches with the offending file. The factory's own id
        // string may be empty if the plugin author left it blank,
        // making the libraryPath the only stable identifier.
        qWarning().noquote() << "PluginLoader: factory id" << factory->id() << "does not match manifest id"
                             << manifest.id << "from" << libraryPath;
        factory.reset(); // runs custom deleter before .so unmap
        if (!library->unload()) {
            qDebug().noquote() << "PluginLoader: unload() returned false for" << libraryPath;
        }
        return;
    }

    auto entryRecord = std::make_shared<LoadedPlugin>();
    entryRecord->manifest = manifest;
    entryRecord->library = std::move(library);
    entryRecord->factory = factory;

    const QString pluginId = manifest.id;
    m_plugins.insert(pluginId, entryRecord);
    m_registry->registerFactory(factory);

    Q_EMIT pluginLoaded(pluginId);
}

} // namespace PhosphorRegistry
