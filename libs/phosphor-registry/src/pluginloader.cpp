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
// Hot-reload of a modified .so works by loading the new .so under
// the same id (the QLibrary's filename is the absolute path which
// hasn't changed unless the user renamed the file). QLibrary load()
// is refcounted; calling it twice on the same path bumps the count.
// The "old factory" stays in memory but is no longer in the
// registry; new widgets come from the new factory. Old widgets
// created by the prior factory keep working because their .so
// (same path, same mapping) is still in.
//
// Phase 5 will revisit this with a usage-refcount + safe-unload
// design when the plugin sandbox lands.
struct PluginLoader::LoadedPlugin
{
    Manifest manifest;
    std::unique_ptr<QLibrary> library;
    std::shared_ptr<IBarWidgetFactory> factory;
    // mtime of the .so at load time. On rescan, an mtime shift
    // triggers a reload (new factory replaces the registered one;
    // the QLibrary stays in m_pinnedLibraries below to keep the
    // mapping valid for any widgets still alive from the old
    // factory).
    qint64 loadedSoMtime = 0;
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
    QObject::connect(m_watcher.get(), &PhosphorFsLoader::WatchedDirectorySet::rescanCompleted, this,
                     &PluginLoader::rescanCompleted, Qt::DirectConnection);
}

PluginLoader::~PluginLoader()
{
    // Drop the watcher first so no rescan races our teardown.
    m_watcher.reset();
    // Unregister every plugin from the Registry. Factory shared_ptr
    // refs may still be held by surfaces; the registry's drop is
    // ordering-only. QLibrary mappings persist via the LoadedPlugin
    // entries until they go out of scope below.
    if (m_registry) {
        for (auto it = m_plugins.begin(); it != m_plugins.end(); ++it) {
            m_registry->unregisterFactory(it.key());
        }
    }
    m_plugins.clear();
    // Pinned libraries from prior reloads — drop them too. Same
    // ordering: registry was already cleared so no surface can have
    // gotten a fresh factory from a since-reloaded plugin in the
    // window between the loop above and this clear.
    m_pinnedLibraries.clear();
}

QString PluginLoader::pluginRoot() const
{
    return m_pluginRoot;
}

void PluginLoader::scanAndLoad()
{
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
    return resolveDefaultPluginRootImpl();
}

QString PluginLoader::resolveDefaultPluginRootImpl() const
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return QDir(base).filePath(QStringLiteral("phosphor/plugins"));
}

bool PluginLoader::ensurePluginRootExists() const
{
    QDir dir(m_pluginRoot);
    if (dir.exists()) {
        return true;
    }
    if (!QDir().mkpath(m_pluginRoot)) {
        qWarning() << "PluginLoader: failed to create plugin root" << m_pluginRoot;
        return false;
    }
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

            const auto existing = m_plugins.constFind(m.id);
            if (existing == m_plugins.constEnd()) {
                loadPluginFromDir(pluginDir);
                continue;
            }

            // Existing plugin — check if the .so mtime shifted
            // (hot-reload trigger).
            QDir pluginDirObj(pluginDir);
            const QStringList soFiles =
                pluginDirObj.entryList(QStringList() << QStringLiteral("*.so"), QDir::Files | QDir::Readable);
            if (soFiles.isEmpty()) {
                continue;
            }
            const QString candidate = pluginDirObj.absoluteFilePath(soFiles.first());
            const qint64 candidateMtime = QFileInfo(candidate).lastModified().toMSecsSinceEpoch();
            if (candidateMtime != existing->get()->loadedSoMtime) {
                // .so changed on disk. Pin the old library so its
                // mapping survives, drop the old factory from the
                // registry, and load the new one in its place.
                m_pinnedLibraries.push_back(std::move(existing->get()->library));
                if (m_registry) {
                    m_registry->unregisterFactory(m.id);
                }
                m_plugins.remove(m.id);
                Q_EMIT pluginUnloaded(m.id);
                loadPluginFromDir(pluginDir);
            }
        }
    }

    // Anything in m_plugins not in discoveredIds was removed from
    // disk — unregister it from the registry. The QLibrary stays
    // pinned so existing widgets from the now-gone factory keep
    // working until the bar tears them down.
    const QList<QString> currentIds = m_plugins.keys();
    for (const QString& pluginId : currentIds) {
        if (!discoveredIds.contains(pluginId)) {
            auto entry = m_plugins.value(pluginId);
            m_pinnedLibraries.push_back(std::move(entry->library));
            m_plugins.remove(pluginId);
            if (m_registry) {
                m_registry->unregisterFactory(pluginId);
            }
            Q_EMIT pluginUnloaded(pluginId);
        }
    }

    return watchedFiles;
}

void PluginLoader::loadPluginFromDir(const QString& pluginDir)
{
    QDir dir(pluginDir);
    const QStringList soFiles = dir.entryList(QStringList() << QStringLiteral("*.so"), QDir::Files | QDir::Readable);
    if (soFiles.isEmpty()) {
        qWarning() << "PluginLoader: no .so under" << pluginDir;
        return;
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
    auto entryFn = reinterpret_cast<PluginFactoryEntry>(library->resolve(kPluginEntryPointSymbol));
    if (!entryFn) {
        qWarning().noquote() << "PluginLoader: plugin" << libraryPath << "missing entry point"
                             << kPluginEntryPointSymbol;
        library->unload();
        return;
    }
    IBarWidgetFactory* rawFactory = entryFn();
    if (!rawFactory) {
        qWarning().noquote() << "PluginLoader: entry point returned null for" << libraryPath;
        library->unload();
        return;
    }
    if (rawFactory->id() != manifest.id) {
        qWarning().noquote() << "PluginLoader: factory id" << rawFactory->id() << "does not match manifest id"
                             << manifest.id;
        delete rawFactory;
        library->unload();
        return;
    }

    auto entry_record = std::make_shared<LoadedPlugin>();
    entry_record->manifest = manifest;
    entry_record->library = std::move(library);
    entry_record->factory.reset(rawFactory, [](IBarWidgetFactory* p) {
        delete p;
    });
    entry_record->loadedSoMtime = QFileInfo(libraryPath).lastModified().toMSecsSinceEpoch();

    const QString pluginId = manifest.id;
    m_plugins.insert(pluginId, entry_record);
    if (m_registry) {
        m_registry->registerFactory(m_plugins.value(pluginId)->factory);
    }

    Q_EMIT pluginLoaded(pluginId);
}

void PluginLoader::scheduleUnload(const QString& pluginId)
{
    // Phase 1.3: synchronous unload (registry drop only; .so pinned).
    auto it = m_plugins.find(pluginId);
    if (it == m_plugins.end()) {
        return;
    }
    auto entry = std::move(*it);
    m_pinnedLibraries.push_back(std::move(entry->library));
    m_plugins.erase(it);
    if (m_registry) {
        m_registry->unregisterFactory(pluginId);
    }
    Q_EMIT pluginUnloaded(pluginId);
}

void PluginLoader::completeUnloadIfQuiesced(const QString& pluginId)
{
    // No-op in Phase 1.3. The Phase-5 sandbox will reintroduce
    // refcount-gated completion here.
    Q_UNUSED(pluginId);
}

void PluginLoader::onWidgetCreated(const QString& pluginId, QObject* widget)
{
    // Phase 1.3: widget tracking is not wired (the .so stays pinned
    // for process lifetime, so per-widget refcount adds no safety).
    // Phase 5 will hook this up via a proxy factory that intercepts
    // createWidget calls.
    Q_UNUSED(pluginId);
    Q_UNUSED(widget);
}

void PluginLoader::onWidgetDestroyed(const QString& pluginId)
{
    Q_UNUSED(pluginId);
}

} // namespace PhosphorRegistry
