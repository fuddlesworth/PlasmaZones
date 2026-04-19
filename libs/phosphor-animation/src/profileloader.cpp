// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/ProfileLoader.h>

#include <PhosphorAnimation/PhosphorProfileRegistry.h>

#include <QDir>
#include <QFile>
#include <QFileSystemWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QStandardPaths>

namespace PhosphorAnimation {

namespace {
Q_LOGGING_CATEGORY(lcProfileLoader, "phosphoranimation.profileloader")
} // namespace

ProfileLoader::ProfileLoader(QObject* parent)
    : QObject(parent)
{
    m_debounceTimer.setSingleShot(true);
    m_debounceTimer.setInterval(50);
    connect(&m_debounceTimer, &QTimer::timeout, this, &ProfileLoader::rescanAll);
}

ProfileLoader::~ProfileLoader() = default;

int ProfileLoader::loadFromDirectory(const QString& directory, PhosphorProfileRegistry& registry, LiveReload liveReload)
{
    m_registry = &registry;
    if (!m_directories.contains(directory)) {
        m_directories.append(directory);
    }
    if (liveReload == LiveReload::On) {
        m_liveReloadEnabled = true;
        installWatcherIfNeeded();
        if (m_watcher && !m_watcher->directories().contains(directory)) {
            if (QDir(directory).exists()) {
                m_watcher->addPath(directory);
            }
        }
    }

    const int countBefore = m_entries.size();
    rescanDirectory(directory);
    return m_entries.size() - countBefore;
}

int ProfileLoader::loadFromDirectories(const QStringList& directories, PhosphorProfileRegistry& registry,
                                       LiveReload liveReload)
{
    int total = 0;
    for (const QString& dir : directories) {
        total += loadFromDirectory(dir, registry, liveReload);
    }
    return total;
}

int ProfileLoader::loadLibraryBuiltins(PhosphorProfileRegistry& registry)
{
    const QStringList dirs =
        QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, QStringLiteral("phosphor-animation/profiles"),
                                  QStandardPaths::LocateDirectory);
    if (dirs.isEmpty()) {
        return 0;
    }
    return loadFromDirectories(dirs, registry, LiveReload::Off);
}

void ProfileLoader::requestRescan()
{
    m_debounceTimer.start();
}

int ProfileLoader::registeredCount() const
{
    return m_entries.size();
}

QList<ProfileLoader::Entry> ProfileLoader::entries() const
{
    return m_entries.values();
}

std::optional<std::pair<ProfileLoader::Entry, Profile>> ProfileLoader::parseFile(const QString& filePath) const
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(lcProfileLoader) << "Skipping unreadable file" << filePath << ":" << file.errorString();
        return std::nullopt;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qCWarning(lcProfileLoader) << "Skipping malformed JSON" << filePath << ":" << parseError.errorString();
        return std::nullopt;
    }
    if (!doc.isObject()) {
        qCWarning(lcProfileLoader) << "Skipping non-object root JSON in" << filePath;
        return std::nullopt;
    }
    QJsonObject obj = doc.object();
    const QString path = obj.value(QLatin1String("name")).toString();
    if (path.isEmpty()) {
        qCWarning(lcProfileLoader) << "Skipping" << filePath << ": missing required 'name' field";
        return std::nullopt;
    }

    // Profile::fromJson reads the remaining fields (curve / duration /
    // minDistance / sequenceMode / staggerInterval / presetName). We
    // strip 'name' first so it doesn't leak into presetName — the two
    // concepts are distinct (registry path vs. user-assigned preset
    // label).
    obj.remove(QLatin1String("name"));
    const Profile profile = Profile::fromJson(obj);

    Entry entry;
    entry.path = path;
    entry.sourcePath = filePath;
    return std::make_pair(std::move(entry), profile);
}

void ProfileLoader::rescanDirectory(const QString& directory)
{
    if (!m_registry) {
        qCWarning(lcProfileLoader) << "rescanDirectory: no registry bound";
        return;
    }
    QDir dir(directory);
    if (!dir.exists()) {
        qCDebug(lcProfileLoader) << "Directory does not exist:" << directory;
        return;
    }
    const QStringList files = dir.entryList({QStringLiteral("*.json")}, QDir::Files);
    for (const QString& file : files) {
        const QString fullPath = dir.absoluteFilePath(file);
        auto parsed = parseFile(fullPath);
        if (!parsed) {
            continue;
        }
        auto& [entry, profile] = *parsed;
        const QString path = entry.path;

        if (auto existing = m_entries.find(path); existing != m_entries.end()) {
            entry.systemSourcePath = existing->sourcePath;
        }

        m_registry->registerProfile(path, profile);
        m_entries.insert(path, std::move(entry));
    }
}

void ProfileLoader::rescanAll()
{
    if (!m_registry) {
        return;
    }
    for (const QString& dir : m_directories) {
        rescanDirectory(dir);
    }
    Q_EMIT profilesChanged();
}

void ProfileLoader::installWatcherIfNeeded()
{
    if (m_watcher) {
        return;
    }
    m_watcher = new QFileSystemWatcher(this);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, [this](const QString&) {
        requestRescan();
    });
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, [this](const QString&) {
        requestRescan();
    });
}

} // namespace PhosphorAnimation
