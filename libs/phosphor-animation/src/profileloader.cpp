// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/ProfileLoader.h>

#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/PhosphorProfileRegistry.h>
#include <PhosphorAnimation/detail/BatchedSink.h>

#include <PhosphorFsLoader/DirectoryLoader.h>
#include <PhosphorFsLoader/JsonEnvelopeValidator.h>
#include <PhosphorFsLoader/ParsedEntry.h>

#include <QDir>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QUuid>

#include <algorithm>

namespace PhosphorAnimation {

namespace {
Q_LOGGING_CATEGORY(lcProfileLoader, "phosphoranimation.profileloader")
} // namespace

class ProfileLoader::Sink : public detail::BatchedSink<Profile>
{
public:
    Sink(PhosphorProfileRegistry& reg, CurveRegistry& curveReg, QString owner)
        : detail::BatchedSink<Profile>(lcProfileLoader(), std::move(owner))
        , registry(&reg)
        , curveRegistry(&curveReg)
    {
    }

    PhosphorProfileRegistry* registry; ///< pinned at ctor
    CurveRegistry* curveRegistry; ///< pinned at ctor — used by parseFile
    QHash<QString, ProfileLoader::Entry> entries; ///< path → entry

    std::optional<PhosphorFsLoader::ParsedEntry> parseFile(const QString& filePath) override
    {
        // Common envelope checks (read, parse, root-is-object,
        // non-empty `name`, name-matches-filename) live in the shared
        // helper. The helper strips `name` from the returned root, so
        // it can't leak into Profile::presetName — the two concepts
        // are distinct (registry path vs. user-assigned preset label).
        auto envelope = PhosphorFsLoader::validateJsonEnvelope(filePath, lcProfileLoader());
        if (!envelope) {
            return std::nullopt;
        }

        // Profile::fromJson reads the remaining fields (curve / duration /
        // minDistance / sequenceMode / staggerInterval / presetName).
        const Profile profile = Profile::fromJson(envelope->root, *curveRegistry);

        PhosphorFsLoader::ParsedEntry parsed;
        parsed.key = envelope->name;
        parsed.sourcePath = filePath;
        parsed.payload = profile;
        return parsed;
    }

protected:
    /// Profile equality is the schema-level value comparison defined
    /// by `Profile::operator==`. The diff suppresses no-op rescans
    /// (re-parse of files whose contents didn't change) so the
    /// consumer-facing `profilesChanged` signal mirrors actual change.
    bool payloadEqual(const Profile& a, const Profile& b) const override
    {
        return a == b;
    }

    void commitToRegistry(const QStringList& removedKeys, const QHash<QString, Profile>& currentMap,
                          const QStringList& changedOrAddedKeys) override
    {
        Q_ASSERT(registry);
        Q_UNUSED(removedKeys);
        Q_UNUSED(changedOrAddedKeys);

        // Single reloadFromOwner → one profilesReloaded signal regardless
        // of how many files changed (decision W: coalesce multiple
        // filesystem events into one consumer-visible batch). The
        // partitioning ensures daemon-direct entries at other paths
        // survive this rescan — `reloadFromOwner` only touches keys in
        // this loader's owner partition, so the daemon's settings-fanned
        // profiles, another loader's entries, etc. are left alone.
        // Bulk-replace handles additions and removals atomically; the
        // base's `removedKeys` and `changedOrAddedKeys` are redundant
        // here and intentionally unused.
        registry->reloadFromOwner(ownerTag(), currentMap);
    }

    void onTrackEntry(const QString& key, const Profile& payload, const QString& sourcePath,
                      const QString& systemSourcePath) override
    {
        Q_UNUSED(payload);
        ProfileLoader::Entry e;
        e.path = key;
        e.sourcePath = sourcePath;
        e.systemSourcePath = systemSourcePath;
        entries.insert(key, std::move(e));
    }

    void onUntrackEntry(const QString& key) override
    {
        entries.remove(key);
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
    , m_loader(std::make_unique<PhosphorFsLoader::DirectoryLoader>(*m_sink))
{
    // Gate `profilesChanged` on the per-batch change flag — same
    // contract as CurveLoader::curvesChanged. DirectoryLoader emits
    // entriesChanged on every rescan, but ProfileLoader consumers only
    // care when the tracked set or a Profile's value actually changed.
    connect(m_loader.get(), &PhosphorFsLoader::DirectoryLoader::entriesChanged, this, [this]() {
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
        m_sink->registry->clearOwner(m_sink->ownerTag());
    }
}

int ProfileLoader::loadFromDirectory(const QString& directory, LiveReload liveReload)
{
    return m_loader->loadFromDirectory(directory, liveReload);
}

int ProfileLoader::loadFromDirectories(const QStringList& directories, LiveReload liveReload,
                                       PhosphorFsLoader::RegistrationOrder order)
{
    return m_loader->loadFromDirectories(directories, liveReload, order);
}

int ProfileLoader::loadLibraryBuiltins(LiveReload liveReload)
{
    // Use the install-prefix directory baked in at build time via
    // PHOSPHORANIMATION_INSTALL_DATADIR. Namespacing under the library's
    // own `phosphor-animation/profiles` subdir means a consumer's
    // user-local `~/.local/share/<consumer>/profiles` pack is NEVER
    // accidentally pulled into the library's built-in load — the old
    // XDG-based `locateAll(GenericDataLocation, ...)` had the reverse
    // property where a user placing files under
    // `~/.local/share/phosphor-animation/profiles` would silently
    // shadow the library's immutable pack.
    //
    // When the macro is absent (sub-project builds that did not
    // propagate the datadir), fall back to a no-op — the caller's
    // consumer-namespaced directories are still loaded via the
    // `loadFromDirectory[ies]` entry points.
#ifdef PHOSPHORANIMATION_INSTALL_DATADIR
    const QString dir = QStringLiteral(PHOSPHORANIMATION_INSTALL_DATADIR "/profiles");
    if (!QDir(dir).exists()) {
        return 0;
    }
    return loadFromDirectory(dir, liveReload);
#else
    Q_UNUSED(liveReload);
    return 0;
#endif
}

QString ProfileLoader::ownerTag() const
{
    return m_sink ? m_sink->ownerTag() : QString();
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
