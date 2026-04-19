// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/ProfileLoader.h>

#include <PhosphorAnimation/PhosphorProfileRegistry.h>

#include <PhosphorJsonLoader/DirectoryLoader.h>
#include <PhosphorJsonLoader/IDirectoryLoaderSink.h>
#include <PhosphorJsonLoader/ParsedEntry.h>

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QStandardPaths>

namespace PhosphorAnimation {

namespace {
Q_LOGGING_CATEGORY(lcProfileLoader, "phosphoranimation.profileloader")
} // namespace

class ProfileLoader::Sink : public PhosphorJsonLoader::IDirectoryLoaderSink
{
public:
    PhosphorProfileRegistry* registry = nullptr;
    QHash<QString, ProfileLoader::Entry> entries; ///< path → entry

    std::optional<PhosphorJsonLoader::ParsedEntry> parseFile(const QString& filePath) override
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

        PhosphorJsonLoader::ParsedEntry parsed;
        parsed.key = path;
        parsed.sourcePath = filePath;
        parsed.payload = profile;
        return parsed;
    }

    void commitBatch(const QStringList& removedKeys,
                     const QList<PhosphorJsonLoader::ParsedEntry>& currentEntries) override
    {
        if (!registry) {
            qCWarning(lcProfileLoader) << "commitBatch: registry not set";
            return;
        }

        // Build the full replacement map from the current-entries list
        // (the loader already applied user-wins-collision, so this
        // map is the authoritative post-scan shape). Missing entries
        // correspond to the removedKeys list — reloadAll handles both
        // additions and removals in one atomic swap.
        QHash<QString, Profile> map;
        map.reserve(currentEntries.size());
        for (const auto& p : currentEntries) {
            const Profile* profile = std::any_cast<Profile>(&p.payload);
            if (!profile) {
                qCWarning(lcProfileLoader) << "commitBatch: payload type-mismatch for" << p.key;
                continue;
            }
            map.insert(p.key, *profile);
        }

        // Single reloadAll → one profilesReloaded signal regardless of
        // how many files changed (decision W: coalesce multiple filesystem
        // events into one consumer-visible batch).
        registry->reloadAll(map);

        // Mirror the loader's tracked set in our own Entry map for
        // entries() introspection.
        for (const QString& key : removedKeys) {
            entries.remove(key);
        }
        for (const auto& p : currentEntries) {
            ProfileLoader::Entry e;
            e.path = p.key;
            e.sourcePath = p.sourcePath;
            e.systemSourcePath = p.systemSourcePath;
            entries.insert(p.key, std::move(e));
        }
    }
};

ProfileLoader::ProfileLoader(QObject* parent)
    : QObject(parent)
    , m_sink(std::make_unique<Sink>())
    , m_loader(std::make_unique<PhosphorJsonLoader::DirectoryLoader>(m_sink.get()))
{
    connect(m_loader.get(), &PhosphorJsonLoader::DirectoryLoader::entriesChanged, this,
            &ProfileLoader::profilesChanged);
}

ProfileLoader::~ProfileLoader() = default;

int ProfileLoader::loadFromDirectory(const QString& directory, PhosphorProfileRegistry& registry, LiveReload liveReload)
{
    m_sink->registry = &registry;
    return m_loader->loadFromDirectory(directory, liveReload);
}

int ProfileLoader::loadFromDirectories(const QStringList& directories, PhosphorProfileRegistry& registry,
                                       LiveReload liveReload)
{
    m_sink->registry = &registry;
    return m_loader->loadFromDirectories(directories, liveReload);
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
    m_loader->requestRescan();
}

int ProfileLoader::registeredCount() const
{
    return m_loader->registeredCount();
}

QList<ProfileLoader::Entry> ProfileLoader::entries() const
{
    return m_sink->entries.values();
}

} // namespace PhosphorAnimation
