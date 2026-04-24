// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/ProfileLoader.h>

#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/PhosphorProfileRegistry.h>

#include <PhosphorJsonLoader/DirectoryLoader.h>
#include <PhosphorJsonLoader/IDirectoryLoaderSink.h>
#include <PhosphorJsonLoader/ParsedEntry.h>

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>

namespace PhosphorAnimation {

namespace {
Q_LOGGING_CATEGORY(lcProfileLoader, "phosphoranimation.profileloader")
} // namespace

class ProfileLoader::Sink : public PhosphorJsonLoader::IDirectoryLoaderSink
{
public:
    Sink(PhosphorProfileRegistry& reg, CurveRegistry& curveReg, QString owner)
        : registry(&reg)
        , curveRegistry(&curveReg)
        , ownerTag(std::move(owner))
    {
    }

    PhosphorProfileRegistry* registry; ///< pinned at ctor
    CurveRegistry* curveRegistry; ///< pinned at ctor — used by parseFile
    QString ownerTag; ///< stable per-instance tag for partitioned reload
    QHash<QString, ProfileLoader::Entry> entries; ///< path → entry
    /// Snapshot of the last-committed profiles by path, used by
    /// commitBatch to decide whether the consumer-facing
    /// `profilesChanged` signal should fire. Populated after every
    /// commit; cleared on removedKeys. A rescan that produces an
    /// identical set is a no-op and suppresses the signal.
    QHash<QString, Profile> lastCommittedProfiles;
    /// Set by commitBatch whenever the tracked profile set actually
    /// changed (a path added / removed / or an existing path's Profile
    /// value differs). Read by ProfileLoader's entriesChanged adapter
    /// to decide whether to emit `profilesChanged`.
    bool lastBatchChanged = false;

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

        // Reject mismatched filename / name. Users who copy an
        // existing profile file and forget to rename the inner `name`
        // would otherwise silently register under the original path
        // (shadowing a system entry they had no intention to replace).
        // Clear diagnostic naming both sides so the user can fix
        // whichever side is wrong.
        const QString basename = QFileInfo(filePath).completeBaseName();
        if (path != basename) {
            qCWarning(lcProfileLoader).nospace()
                << "Skipping " << filePath << ": profile name '" << path << "' does not match filename '" << basename
                << "' — rejecting to avoid silent shadowing";
            return std::nullopt;
        }

        // Profile::fromJson reads the remaining fields (curve / duration /
        // minDistance / sequenceMode / staggerInterval / presetName). We
        // strip 'name' first so it doesn't leak into presetName — the two
        // concepts are distinct (registry path vs. user-assigned preset
        // label).
        obj.remove(QLatin1String("name"));
        const Profile profile = Profile::fromJson(obj, *curveRegistry);

        PhosphorJsonLoader::ParsedEntry parsed;
        parsed.key = path;
        parsed.sourcePath = filePath;
        parsed.payload = profile;
        return parsed;
    }

    void commitBatch(const QStringList& removedKeys,
                     const QList<PhosphorJsonLoader::ParsedEntry>& currentEntries) override
    {
        Q_ASSERT(registry);

        // Reset the per-batch change flag. Any add / remove / value-diff
        // below flips it to true, which gates the consumer-facing
        // `profilesChanged` signal in ProfileLoader.
        lastBatchChanged = false;

        // Build the full replacement map from the current-entries list
        // (the loader already applied user-wins-collision, so this
        // map is the authoritative post-scan shape). Missing entries
        // correspond to the removedKeys list — reloadFromOwner handles
        // both additions and removals in one atomic swap, BUT crucially
        // leaves entries owned by other sources (the daemon's
        // settings-fanned profiles, another loader, etc.) untouched.
        // Using reloadAll here would wipe those on every rescan.
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

        // Diff against the previous commit to decide whether the
        // consumer signal fires. The registry's own reloadFromOwner
        // is also diff-gated internally, but we need our own flag so
        // the consumer's `profilesChanged` mirrors that behaviour —
        // DirectoryLoader always fires entriesChanged on every rescan.
        for (const QString& key : removedKeys) {
            if (lastCommittedProfiles.remove(key) > 0) {
                lastBatchChanged = true;
            }
        }
        for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
            auto existingIt = lastCommittedProfiles.constFind(it.key());
            if (existingIt == lastCommittedProfiles.constEnd() || !(*existingIt == it.value())) {
                lastCommittedProfiles.insert(it.key(), it.value());
                lastBatchChanged = true;
            }
        }

        // Single reloadFromOwner → one profilesReloaded signal regardless
        // of how many files changed (decision W: coalesce multiple
        // filesystem events into one consumer-visible batch). The
        // partitioning ensures daemon-direct entries at other paths
        // survive this rescan.
        registry->reloadFromOwner(ownerTag, map);

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

namespace {
/// Generate a unique-per-instance owner tag when the caller didn't
/// specify one. Uses a fresh UUID rather than the object's `this`
/// pointer — address reuse across sequential loader construction
/// (RAII-scoped loaders in unit tests, plugin reloads that tear down
/// and rebuild a loader at the same heap slot) would otherwise let the
/// new loader briefly inherit authority over the prior loader's
/// unclaimed partition entries before its first commitBatch runs.
/// UUIDs are never reused within a process.
QString defaultOwnerTag()
{
    return QStringLiteral("profileloader-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
}
} // namespace

ProfileLoader::ProfileLoader(PhosphorProfileRegistry& registry, CurveRegistry& curveRegistry, const QString& ownerTag,
                             QObject* parent)
    : QObject(parent)
    , m_sink(std::make_unique<Sink>(registry, curveRegistry, ownerTag.isEmpty() ? defaultOwnerTag() : ownerTag))
    , m_loader(std::make_unique<PhosphorJsonLoader::DirectoryLoader>(*m_sink))
{
    // Gate `profilesChanged` on the per-batch change flag — same
    // contract as CurveLoader::curvesChanged. DirectoryLoader emits
    // entriesChanged on every rescan, but ProfileLoader consumers only
    // care when the tracked set or a Profile's value actually changed.
    connect(m_loader.get(), &PhosphorJsonLoader::DirectoryLoader::entriesChanged, this, [this]() {
        if (m_sink->lastBatchChanged) {
            Q_EMIT profilesChanged();
        }
    });
}

ProfileLoader::~ProfileLoader()
{
    // Clean up any registry entries we own so a process hosting multiple
    // sequential loaders (tests, especially) doesn't accumulate ghosts
    // from destroyed loaders.
    if (m_sink && m_sink->registry) {
        m_sink->registry->clearOwner(m_sink->ownerTag);
    }
}

int ProfileLoader::loadFromDirectory(const QString& directory, LiveReload liveReload)
{
    return m_loader->loadFromDirectory(directory, liveReload);
}

int ProfileLoader::loadFromDirectories(const QStringList& directories, LiveReload liveReload)
{
    return m_loader->loadFromDirectories(directories, liveReload);
}

int ProfileLoader::loadLibraryBuiltins(LiveReload liveReload)
{
    const QStringList dirs =
        QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, QStringLiteral("phosphor-animation/profiles"),
                                  QStandardPaths::LocateDirectory);
    if (dirs.isEmpty()) {
        return 0;
    }
    return loadFromDirectories(dirs, liveReload);
}

QString ProfileLoader::ownerTag() const
{
    return m_sink ? m_sink->ownerTag : QString();
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
    // Sort by path for deterministic ordering — same rationale as
    // DirectoryLoader::entries(). QHash iteration order is randomised
    // in Qt6.
    QList<ProfileLoader::Entry> sorted = m_sink->entries.values();
    std::sort(sorted.begin(), sorted.end(), [](const Entry& a, const Entry& b) {
        return a.path < b.path;
    });
    return sorted;
}

bool ProfileLoader::hasPath(const QString& path) const
{
    return m_sink->entries.contains(path);
}

} // namespace PhosphorAnimation
