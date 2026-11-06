// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorRegistry/PluginCatalog.h>

#include <PhosphorFsLoader/IScanStrategy.h>
#include <PhosphorFsLoader/WatchedDirectorySet.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonValue>
#include <QLoggingCategory>
#include <QPluginLoader>
#include <QPointer>
#include <QRegularExpression>
#include <QStandardPaths>

#include <algorithm>

namespace PhosphorRegistry {

namespace {
Q_LOGGING_CATEGORY(lcPluginCatalog, "phosphorregistry.plugincatalog")

// Plugin files CONSIDERED per scan, summed over every search path. A plugin
// costs a metadata read, and no plausible install has four digits of them.
constexpr int DefaultMaxFilesPerScan = 1000;

const QLatin1String IidKey("IID");
const QLatin1String MetaDataKey("MetaData");
const QLatin1String IdKey("Id");
const QLatin1String ApiVersionKey("ApiVersion");

bool isValidPluginId(const QString& id)
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]*$"));
    return pattern.match(id).hasMatch();
}

// The write-bit half of the StrictModes rule OpenSSH and sudo apply to files
// they trust: a group- or world-writable library, or one in such a directory,
// could be swapped for other code between the scan and the load. Ownership is
// not checked: placing a foreign-owned file here already needs the write
// access this check refuses.
bool isGroupOrWorldWritable(const QString& path)
{
    return QFileInfo(path).permissions().testAnyFlags(QFileDevice::WriteGroup | QFileDevice::WriteOther);
}

QString logPrefix(const PluginSeam& seam)
{
    return QStringLiteral("PluginCatalog(%1):").arg(seam.name);
}

QString originName(PluginOrigin origin)
{
    switch (origin) {
    case PluginOrigin::Development:
        return QStringLiteral("development");
    case PluginOrigin::User:
        return QStringLiteral("user");
    case PluginOrigin::System:
        return QStringLiteral("system");
    }
    return QString();
}
} // namespace

// Keeps a loaded plugin's QPluginLoader for the catalog's lifetime. The
// loader's destructor does not unload the library, and the catalog never calls
// unload(): an instance, or an object it created, may still be in use anywhere.
struct PluginCatalog::Loaded
{
    PluginMetadata metadata;
    std::unique_ptr<QPluginLoader> loader;
    QPointer<QObject> instance;
};

class PluginCatalog::ScanStrategy : public PhosphorFsLoader::IScanStrategy
{
public:
    explicit ScanStrategy(PluginCatalog* owner)
        : m_owner(owner)
    {
    }

    QStringList performScan(const QStringList& directoriesInScanOrder) override
    {
        return m_owner->performScan(directoriesInScanOrder);
    }

private:
    PluginCatalog* m_owner = nullptr;
};

PluginCatalog::PluginCatalog(PluginSeam seam, QObject* parent)
    : QObject(parent)
    , m_seam(std::move(seam))
    , m_maxFilesPerScan(DefaultMaxFilesPerScan)
    , m_strategy(std::make_unique<ScanStrategy>(this))
    , m_watcher(std::make_unique<PhosphorFsLoader::WatchedDirectorySet>(*m_strategy))
{
    connect(m_watcher.get(), &PhosphorFsLoader::WatchedDirectorySet::rescanCompleted, this,
            &PluginCatalog::scanCompleted);
}

PluginCatalog::~PluginCatalog() = default;

QList<PluginSearchPath> PluginCatalog::defaultSearchPaths(const QString& seamName)
{
    QList<PluginSearchPath> paths;
    QSet<QString> seen;
    const auto add = [&paths, &seen](const QString& directory, PluginOrigin origin) {
        const QString clean = QDir::cleanPath(directory);
        if (clean.isEmpty() || seen.contains(clean)) {
            return;
        }
        seen.insert(clean);
        paths.append({clean, origin});
    };

    const QString development = qEnvironmentVariable(DevelopmentPathVariable);
    for (const QString& root : development.split(QLatin1Char(':'), Qt::SkipEmptyParts)) {
        add(root + QLatin1Char('/') + seamName, PluginOrigin::Development);
    }

    const QString dataHome = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (!dataHome.isEmpty()) {
        add(dataHome + QStringLiteral("/phosphor-works/plugins/") + seamName, PluginOrigin::User);
    }

    const QStringList libraryPaths = QCoreApplication::libraryPaths();
    for (const QString& libraryPath : libraryPaths) {
        add(libraryPath + QStringLiteral("/phosphor-works/") + seamName, PluginOrigin::System);
    }
    return paths;
}

const PluginSeam& PluginCatalog::seam() const
{
    return m_seam;
}

void PluginCatalog::setSearchPaths(const QList<PluginSearchPath>& paths)
{
    if (m_registered) {
        qCWarning(lcPluginCatalog) << "PluginCatalog(" << m_seam.name
                                   << "): setSearchPaths after scan() is ignored; the paths are fixed by then";
        return;
    }
    m_searchPaths.clear();
    QSet<QString> seen;
    for (const PluginSearchPath& path : paths) {
        const QString clean = QDir::cleanPath(path.directory);
        if (clean.isEmpty() || seen.contains(clean)) {
            continue;
        }
        seen.insert(clean);
        m_searchPaths.append({clean, path.origin});
    }
}

QList<PluginSearchPath> PluginCatalog::searchPaths() const
{
    return m_searchPaths;
}

void PluginCatalog::setLiveReload(bool enabled)
{
    if (m_registered) {
        qCWarning(lcPluginCatalog) << "PluginCatalog(" << m_seam.name
                                   << "): setLiveReload after scan() is ignored; the watch is set up by then";
        return;
    }
    m_liveReload = enabled;
}

void PluginCatalog::scan()
{
    if (m_registered) {
        m_watcher->rescanNow();
        return;
    }
    m_registered = true;
    if (m_searchPaths.isEmpty()) {
        m_searchPaths = defaultSearchPaths(m_seam.name);
    }
    QStringList directories;
    directories.reserve(m_searchPaths.size());
    for (const PluginSearchPath& path : std::as_const(m_searchPaths)) {
        directories.append(path.directory);
    }
    if (directories.isEmpty()) {
        Q_EMIT scanCompleted();
        return;
    }
    // registerDirectories runs the first scan synchronously and emits
    // rescanCompleted, which is forwarded as scanCompleted.
    m_watcher->registerDirectories(directories,
                                   m_liveReload ? PhosphorFsLoader::LiveReload::On : PhosphorFsLoader::LiveReload::Off,
                                   PhosphorFsLoader::RegistrationOrder::HighestPriorityFirst);
}

QList<PluginMetadata> PluginCatalog::plugins() const
{
    QList<PluginMetadata> list = m_plugins.values();
    std::sort(list.begin(), list.end(), [](const PluginMetadata& a, const PluginMetadata& b) {
        return a.id < b.id;
    });
    return list;
}

std::optional<PluginMetadata> PluginCatalog::plugin(const QString& id) const
{
    const auto it = m_plugins.constFind(id);
    if (it == m_plugins.cend()) {
        return std::nullopt;
    }
    return *it;
}

QList<RefusedPlugin> PluginCatalog::refused() const
{
    return m_refused;
}

bool PluginCatalog::isLoaded(const QString& id) const
{
    return m_loaded.contains(id);
}

void PluginCatalog::setMaxFilesPerScanForTest(int cap)
{
    m_maxFilesPerScan = cap > 0 ? cap : DefaultMaxFilesPerScan;
}

QObject* PluginCatalog::load(const QString& id, QString* errorString)
{
    const auto fail = [errorString](const QString& message) -> QObject* {
        if (errorString) {
            *errorString = message;
        }
        return nullptr;
    };

    if (const auto loaded = m_loaded.constFind(id); loaded != m_loaded.cend()) {
        return (*loaded)->instance.data();
    }
    const auto found = m_plugins.constFind(id);
    if (found == m_plugins.cend()) {
        return fail(QStringLiteral("no %1 plugin with id %2").arg(m_seam.name, id));
    }
    const PluginMetadata metadata = *found;

    // The file may have changed since the scan read its metadata. Check it
    // again right before it is mapped.
    auto loader = std::make_unique<QPluginLoader>(metadata.filePath);
    const QJsonObject current = loader->metaData();
    if (current.value(IidKey).toString() != m_seam.iid
        || current.value(MetaDataKey).toObject().value(IdKey).toString() != id
        || current.value(MetaDataKey).toObject().value(ApiVersionKey).toInt(-1) != m_seam.apiVersion) {
        return fail(QStringLiteral("%1 changed since it was scanned and no longer matches plugin %2")
                        .arg(metadata.filePath, id));
    }
    QObject* const instance = loader->instance();
    if (!instance) {
        return fail(QStringLiteral("could not load %1: %2").arg(metadata.filePath, loader->errorString()));
    }

    auto entry = std::make_shared<Loaded>();
    entry->metadata = metadata;
    entry->loader = std::move(loader);
    entry->instance = instance;
    m_loaded.insert(id, entry);
    qCInfo(lcPluginCatalog).noquote() << logPrefix(m_seam) << "loaded" << id << "from" << metadata.filePath;
    return instance;
}

PluginOrigin PluginCatalog::originOf(const QString& directory) const
{
    const QString clean = QDir::cleanPath(directory);
    for (const PluginSearchPath& path : m_searchPaths) {
        if (path.directory == clean) {
            return path.origin;
        }
    }
    return PluginOrigin::System;
}

bool PluginCatalog::shouldWarn(const QString& key)
{
    if (m_warned.contains(key)) {
        return false;
    }
    m_warned.insert(key);
    return true;
}

QStringList PluginCatalog::performScan(const QStringList& directoriesLowestPriorityFirst)
{
    QHash<QString, PluginMetadata> found;
    QList<RefusedPlugin> refused;
    QSet<QString> raisedWarnings;
    QStringList watchedFiles;
    int considered = 0;
    bool truncated = false;

    const auto refuse = [&](const QString& filePath, const QString& pluginId, const QString& reason) {
        refused.append({filePath, pluginId, reason});
        const QString key = filePath + QLatin1Char('|') + reason;
        raisedWarnings.insert(key);
        if (shouldWarn(key)) {
            qCWarning(lcPluginCatalog).noquote() << logPrefix(m_seam) << "refusing" << filePath << "-" << reason;
        }
    };
    const auto note = [&](const QString& key, const QString& message) {
        raisedWarnings.insert(key);
        if (shouldWarn(key)) {
            qCInfo(lcPluginCatalog).noquote() << logPrefix(m_seam) << message;
        }
    };

    // Highest priority first, so the first plugin found for an id wins.
    for (auto dirIt = directoriesLowestPriorityFirst.crbegin(); dirIt != directoriesLowestPriorityFirst.crend();
         ++dirIt) {
        const QString directory = *dirIt;
        const QDir dir(directory);
        if (!dir.exists()) {
            continue;
        }
        const PluginOrigin origin = originOf(directory);
        // NoSymLinks: code loads only from a real file in a searched
        // directory, never through a link to somewhere else. QDir::Name makes
        // the order, and so the winner of an in-directory duplicate, the same
        // on every filesystem.
        const QStringList files = dir.entryList(QStringList{QStringLiteral("*.so")},
                                                QDir::Files | QDir::Readable | QDir::NoSymLinks, QDir::Name);
        QHash<QString, QString> idsInThisDirectory;
        for (const QString& fileName : files) {
            if (considered >= m_maxFilesPerScan) {
                truncated = true;
                break;
            }
            ++considered;
            const QString filePath = dir.absoluteFilePath(fileName);
            watchedFiles.append(filePath);

            if (isGroupOrWorldWritable(filePath) || isGroupOrWorldWritable(dir.absolutePath())) {
                refuse(filePath, QString(),
                       QStringLiteral("it or its directory is group- or world-writable (chmod go-w both)"));
                continue;
            }
            const QJsonObject pluginMetaData = QPluginLoader(filePath).metaData();
            if (pluginMetaData.isEmpty()) {
                refuse(filePath, QString(), QStringLiteral("not a Qt plugin"));
                continue;
            }
            const QString iid = pluginMetaData.value(IidKey).toString();
            if (iid != m_seam.iid) {
                refuse(filePath, QString(), QStringLiteral("its IID is %1, expected %2").arg(iid, m_seam.iid));
                continue;
            }
            const QJsonObject own = pluginMetaData.value(MetaDataKey).toObject();
            const QString id = own.value(IdKey).toString();
            if (!isValidPluginId(id)) {
                refuse(filePath, id, QStringLiteral("its metadata has no valid \"Id\""));
                continue;
            }
            const int apiVersion = own.value(ApiVersionKey).toInt(-1);
            if (apiVersion != m_seam.apiVersion) {
                refuse(filePath, id,
                       QStringLiteral("plugin %1 is built for ApiVersion %2, this build loads %3")
                           .arg(id, QString::number(apiVersion), QString::number(m_seam.apiVersion)));
                continue;
            }
            if (const auto twin = idsInThisDirectory.constFind(id); twin != idsInThisDirectory.cend()) {
                refuse(filePath, id, QStringLiteral("%1 in the same directory already has id %2").arg(*twin, id));
                continue;
            }
            idsInThisDirectory.insert(id, fileName);

            if (const auto winner = found.constFind(id); winner != found.cend()) {
                note(QStringLiteral("override|") + filePath,
                     QStringLiteral("%1 (%2) overrides %3 (%4) for plugin %5")
                         .arg(winner->filePath, originName(winner->origin), filePath, originName(origin), id));
                continue;
            }
            found.insert(id, PluginMetadata{id, apiVersion, filePath, origin, own});
        }
        if (truncated) {
            break;
        }
    }

    if (truncated) {
        // A scan cut short by the cap did not see every file, so a known
        // plugin it did not reach is not gone. Keep it rather than remove it.
        qCWarning(lcPluginCatalog) << "PluginCatalog(" << m_seam.name << "): stopped after" << m_maxFilesPerScan
                                   << "plugin files; plugins past that are not rescanned";
        for (auto it = m_plugins.cbegin(); it != m_plugins.cend(); ++it) {
            if (!found.contains(it.key())) {
                found.insert(it.key(), it.value());
            }
        }
    }

    // A loaded plugin keeps the code it was loaded from until the process
    // restarts: the dynamic linker keeps one mapping per path, and objects the
    // plugin created may be in use. Keep offering the metadata of what runs.
    for (auto it = m_loaded.cbegin(); it != m_loaded.cend(); ++it) {
        const auto now = found.find(it.key());
        if (now == found.end()) {
            continue;
        }
        if (now->filePath != it.value()->metadata.filePath) {
            note(QStringLiteral("loaded-changed|") + it.key() + QLatin1Char('|') + now->filePath,
                 QStringLiteral("plugin %1 now comes from %2, but %3 is already loaded; the change takes effect at "
                                "the next start")
                     .arg(it.key(), now->filePath, it.value()->metadata.filePath));
            *now = it.value()->metadata;
        }
    }

    std::sort(refused.begin(), refused.end(), [](const RefusedPlugin& a, const RefusedPlugin& b) {
        return a.filePath < b.filePath;
    });

    QStringList added;
    QStringList removed;
    for (auto it = found.cbegin(); it != found.cend(); ++it) {
        if (!m_plugins.contains(it.key())) {
            added.append(it.key());
        }
    }
    for (auto it = m_plugins.cbegin(); it != m_plugins.cend(); ++it) {
        if (!found.contains(it.key())) {
            removed.append(it.key());
        }
    }
    std::sort(added.begin(), added.end());
    std::sort(removed.begin(), removed.end());

    m_plugins = std::move(found);
    m_refused = std::move(refused);
    // A problem that went away forgets its warning, so it warns again if it
    // comes back.
    m_warned = std::move(raisedWarnings);

    for (const QString& id : std::as_const(removed)) {
        Q_EMIT pluginRemoved(id);
    }
    for (const QString& id : std::as_const(added)) {
        Q_EMIT pluginAdded(id);
    }
    return watchedFiles;
}

} // namespace PhosphorRegistry
