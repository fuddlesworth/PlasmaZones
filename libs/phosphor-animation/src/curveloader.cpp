// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/CurveLoader.h>

#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/detail/BatchedSink.h>

#include <PhosphorFsLoader/DirectoryLoader.h>
#include <PhosphorFsLoader/JsonEnvelopeValidator.h>
#include <PhosphorFsLoader/ParsedEntry.h>

#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QJsonValue>
#include <QLatin1String>
#include <QLoggingCategory>
#include <QUuid>

#include <algorithm>

namespace PhosphorAnimation {

namespace {
Q_LOGGING_CATEGORY(lcCurveLoader, "phosphoranimation.curveloader")

/// Generate a unique-per-instance owner tag. UUIDs are never reused
/// within a process, so sequential loaders (tests, plugin reloads,
/// embedded shells) never inherit authority over a prior loader's
/// unclaimed registry entries — even when a new loader reuses the
/// same heap slot. Prefixed `"curveloader-"` for debug clarity (matches
/// the `"profileloader-"` separator used by `ProfileLoader`; a colon
/// is fragile in serialised contexts like log filters and dbus paths).
QString makeCurveLoaderOwnerTag()
{
    return QStringLiteral("curveloader-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
}
} // namespace

namespace detail {
/// Per-key payload shipped through the `ParsedEntry::payload` std::any.
/// Free-standing (rather than nested in `Sink`) so it can name the
/// `BatchedSink<CurvePayload>` template instantiation in `Sink`'s base-
/// class clause without the C++ "incomplete enclosing class" issue.
/// Lives in `detail` (external linkage) rather than the anonymous
/// namespace so the compiler doesn't warn about
/// `CurveLoader::Sink` having an internal-linkage base.
struct CurvePayload
{
    QString displayName;
    std::shared_ptr<const Curve> curve;
    /// Captured wire-format string used to build `curve`. Persisted
    /// through the payload so the BatchedSink can diff against the
    /// previous commit and skip re-registering unchanged entries.
    /// `Curve::toString()` is the canonical Curve serialization, so
    /// two parses of the same JSON file produce the same wire string —
    /// the diff stays stable across reloads even if the JSON factory
    /// internals change.
    QString wireFormat;
};
} // namespace detail

/**
 * The curve-specific sink — knows the curve JSON schema, knows how to
 * turn it into a `shared_ptr<const Curve>`, and knows how to register /
 * unregister entries against `CurveRegistry`. The directory walk,
 * watching, and debounce live in `PhosphorFsLoader::DirectoryLoader`;
 * the diff / change-flag bookkeeping lives in `detail::BatchedSink`.
 */
class CurveLoader::Sink : public detail::BatchedSink<detail::CurvePayload>
{
public:
    using Payload = detail::CurvePayload;

    Sink(CurveRegistry& reg, QString owner)
        : detail::BatchedSink<Payload>(lcCurveLoader(), std::move(owner))
        , registry(&reg)
    {
    }

    CurveRegistry* registry; ///< pinned at ctor — no per-call mutation
    QHash<QString, CurveLoader::Entry> entries; ///< name → entry (for entries() introspection)

    std::optional<PhosphorFsLoader::ParsedEntry> parseFile(const QString& filePath) override
    {
        // Common envelope checks (read, parse, root-is-object,
        // non-empty `name`, name-matches-filename) live in the shared
        // helper. On success the helper hands back the parsed root with
        // `name` removed, so the schema-specific code below only deals
        // with curve-typed fields.
        auto envelope = PhosphorFsLoader::validateJsonEnvelope(filePath, lcCurveLoader());
        if (!envelope) {
            return std::nullopt;
        }
        const QJsonObject& obj = envelope->root;
        const QString& name = envelope->name;

        // Reject user-authored files whose `name` collides with a
        // built-in typeId. CurveRegistry::registerFactory is
        // replace-semantic, so letting one through would permanently
        // wipe the built-in factory (e.g. `spring`) from the registry
        // until process restart. The user picked the name explicitly,
        // so surface the conflict instead of silently prefixing.
        if (CurveRegistry::isBuiltinTypeId(name)) {
            qCWarning(lcCurveLoader).nospace() << "Skipping " << filePath << ": user curve name '" << name
                                               << "' conflicts with builtin — use a different name";
            return std::nullopt;
        }

        const QString typeId = obj.value(QLatin1String("typeId")).toString();
        if (typeId.isEmpty()) {
            qCWarning(lcCurveLoader) << "Skipping" << filePath << ": missing required 'typeId' field";
            return std::nullopt;
        }
        const QJsonObject params = obj.value(QLatin1String("parameters")).toObject();

        // Delegate parameter-shape validation to the CurveRegistry's
        // JsonFactory for `typeId`. Keeps the registry as the sole
        // schema authority — third-party curves that registered a JSON
        // factory become JSON-loadable automatically, and the built-in
        // spring / cubic-bezier / elastic / bounce shapes live in
        // exactly one place (curveregistry.cpp's registerBuiltins).
        //
        // A null return means either:
        //   - unknown typeId (no factory registered), or
        //   - known typeId whose JSON factory rejected the parameters
        //     (validation failure — factory emits its own qCWarning).
        auto curve = registry->tryCreateFromJson(typeId, params);
        if (!curve) {
            qCWarning(lcCurveLoader) << "Skipping" << filePath << ": typeId" << typeId
                                     << "unknown or parameters rejected (see curve registry log)";
            return std::nullopt;
        }

        const QString wireFormat = curve->toString();

        Payload payload{obj.value(QLatin1String("displayName")).toString(), std::move(curve), wireFormat};

        PhosphorFsLoader::ParsedEntry parsed;
        parsed.key = name;
        parsed.sourcePath = filePath;
        parsed.payload = std::move(payload);
        return parsed;
    }

protected:
    /// Curve payloads are equal when their canonical wire-format
    /// strings match. Comparing wire-formats (rather than the
    /// `shared_ptr<const Curve>` target) means two reloads of the same
    /// JSON file produce equal payloads — separately allocated curve
    /// instances with identical parameters compare equal, which is the
    /// behaviour the diff needs to suppress no-op re-registers.
    bool payloadEqual(const Payload& a, const Payload& b) const override
    {
        return a.wireFormat == b.wireFormat;
    }

    void commitToRegistry(const QStringList& removedKeys, const QHash<QString, Payload>& currentMap,
                          const QStringList& changedOrAddedKeys) override
    {
        if (!registry) {
            qCWarning(lcCurveLoader) << "commitToRegistry: registry not set";
            return;
        }

        // Purge entries whose backing files are no longer on disk.
        // CurveRegistry::registerFactory is replace-semantic, so a
        // future registration can re-use the key — but the current
        // caller wants the key fully unregistered until then. We own
        // each factory under our `ownerTag`, so calling
        // `unregisterFactory` here only evicts entries we ourselves
        // registered.
        for (const QString& key : removedKeys) {
            registry->unregisterFactory(key);
        }

        // Re-register only the entries the BatchedSink flagged as
        // changed-or-added. Skipping unchanged entries avoids a
        // pointless mutex round-trip through CurveRegistry on every
        // rescan (mirrors the shape of
        // PhosphorProfileRegistry::reloadFromOwner's diff suppression).
        for (const QString& key : changedOrAddedKeys) {
            const auto it = currentMap.constFind(key);
            if (it == currentMap.constEnd()) {
                continue; // defensive: BatchedSink always pairs them
            }
            const auto curve = it->curve;
            // Trust parseFile's preconditions: `name` is non-empty
            // (envelope-validator guard) and `curve` is non-null
            // (tryCreateFromJson guard above). The previous post-
            // register `has()` check was a race (concurrent
            // `unregisterByOwner` on this loader's tag could erase the
            // just-registered entry between the register and the
            // has() lookup, producing a spurious warning + skipped
            // wireFormat snapshot → forced re-registration on next
            // rescan). Drop the check; the single source of truth is
            // parseFile's preconditions. — Phase 1c fix preserved.
            //
            // Pass the owner tag so the dtor's
            // `unregisterByOwner(ownerTag)` only evicts what we
            // installed, preserving other loaders' and direct
            // `registerFactory` callers' entries.
            registry->registerFactory(
                key,
                [curve](const QString&, const QString&) {
                    return curve;
                },
                ownerTag());
        }
    }

    void onTrackEntry(const QString& key, const Payload& payload, const QString& sourcePath,
                      const QString& systemSourcePath) override
    {
        CurveLoader::Entry trackedEntry;
        trackedEntry.name = key;
        trackedEntry.displayName = payload.displayName;
        trackedEntry.sourcePath = sourcePath;
        trackedEntry.systemSourcePath = systemSourcePath;
        entries.insert(key, std::move(trackedEntry));
    }

    void onUntrackEntry(const QString& key) override
    {
        entries.remove(key);
    }
};

CurveLoader::CurveLoader(CurveRegistry& registry, QObject* parent)
    : QObject(parent)
    , m_sink(std::make_unique<Sink>(registry, makeCurveLoaderOwnerTag()))
    , m_loader(std::make_unique<PhosphorFsLoader::DirectoryLoader>(*m_sink))
{
    // Gate `curvesChanged` on the per-batch change flag. DirectoryLoader
    // emits `entriesChanged` unconditionally on every rescan (tests and
    // debug tooling rely on that), but consumers of CurveLoader only
    // care when the tracked curve set or one of its wire-formats
    // actually changed — re-parsing the same files a second time (e.g.
    // a `requestRescan()` with no content change) is invisible at this
    // layer. Matches the header's documented contract.
    connect(m_loader.get(), &PhosphorFsLoader::DirectoryLoader::entriesChanged, this, [this]() {
        if (m_sink->lastBatchChanged) {
            Q_EMIT curvesChanged();
        }
    });
}

CurveLoader::~CurveLoader()
{
    // Bulk-remove every factory this loader registered under its owner
    // tag — mirrors `ProfileLoader::~ProfileLoader` issuing
    // `clearOwner(ownerTag)` against PhosphorProfileRegistry.
    //
    // Owner-tagged partitioning means another `CurveLoader` (or any
    // direct `registerFactory` caller with a different tag) that
    // happens to share a typeId is preserved — the previous per-key
    // unregister loop would have evicted the other registrant's entry
    // silently. Each removal emits an info log line in CurveRegistry
    // so rollback is observable in traces.
    if (m_sink && m_sink->registry && !m_sink->ownerTag().isEmpty()) {
        m_sink->registry->unregisterByOwner(m_sink->ownerTag());
    }
}

int CurveLoader::loadFromDirectory(const QString& directory, LiveReload liveReload)
{
    return m_loader->loadFromDirectory(directory, liveReload);
}

int CurveLoader::loadFromDirectories(const QStringList& directories, LiveReload liveReload)
{
    return m_loader->loadFromDirectories(directories, liveReload);
}

int CurveLoader::loadLibraryBuiltins(LiveReload liveReload)
{
    // Use the install-prefix directory baked in at build time via
    // PHOSPHORANIMATION_INSTALL_DATADIR. Namespacing under the library's
    // own `phosphor-animation/curves` subdir means a consumer's
    // user-local `~/.local/share/<consumer>/curves` pack is NEVER
    // accidentally pulled into the library's built-in load (the old
    // XDG-based `locateAll(GenericDataLocation,
    // "phosphor-animation/curves", ...)` had the reverse property —
    // a user placing files under `~/.local/share/phosphor-animation/
    // curves` would silently shadow the library's immutable pack).
    //
    // When the macro is absent (e.g. a sub-project build that did not
    // propagate the datadir), fall back to a no-op — the caller's
    // consumer-namespaced directories are still loaded via the
    // `loadFromDirectory[ies]` entry points.
#ifdef PHOSPHORANIMATION_INSTALL_DATADIR
    const QString dir = QStringLiteral(PHOSPHORANIMATION_INSTALL_DATADIR "/curves");
    if (!QDir(dir).exists()) {
        return 0;
    }
    return loadFromDirectory(dir, liveReload);
#else
    Q_UNUSED(liveReload);
    return 0;
#endif
}

void CurveLoader::requestRescan()
{
    m_loader->requestRescan();
}

int CurveLoader::registeredCount() const
{
    return m_loader->registeredCount();
}

QString CurveLoader::ownerTag() const
{
    return m_sink ? m_sink->ownerTag() : QString();
}

QList<CurveLoader::Entry> CurveLoader::entries() const
{
    // Match DirectoryLoader::entries() — sort by key for deterministic
    // ordering across platforms and Qt versions. QHash iteration order
    // is intentionally randomised in Qt6, so without the sort snapshot
    // tests and diagnostic logs would differ between runs.
    QList<CurveLoader::Entry> sorted = m_sink->entries.values();
    std::sort(sorted.begin(), sorted.end(), [](const Entry& a, const Entry& b) {
        return a.name < b.name;
    });
    return sorted;
}

} // namespace PhosphorAnimation
