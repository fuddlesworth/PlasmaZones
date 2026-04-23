// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/CurveLoader.h>

#include <PhosphorAnimation/CurveRegistry.h>

#include <PhosphorJsonLoader/DirectoryLoader.h>
#include <PhosphorJsonLoader/IDirectoryLoaderSink.h>
#include <PhosphorJsonLoader/ParsedEntry.h>

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLoggingCategory>
#include <QStandardPaths>

#include <algorithm>

namespace PhosphorAnimation {

namespace {
Q_LOGGING_CATEGORY(lcCurveLoader, "phosphoranimation.curveloader")
} // namespace

/**
 * The curve-specific sink — knows the curve JSON schema, knows how to
 * turn it into a `shared_ptr<const Curve>`, and knows how to register /
 * unregister entries against `CurveRegistry`. The directory walk,
 * watching, and debounce live in `PhosphorJsonLoader::DirectoryLoader`.
 */
class CurveLoader::Sink : public PhosphorJsonLoader::IDirectoryLoaderSink
{
public:
    /// Per-key payload shipped through the `ParsedEntry::payload` std::any.
    struct Payload
    {
        QString displayName;
        std::shared_ptr<const Curve> curve;
        /// Captured wire-format string used to build `curve`. Persisted
        /// through the payload so `commitBatch` can diff against the
        /// previous commit and skip re-registering unchanged entries.
        QString wireFormat;
    };

    explicit Sink(CurveRegistry& reg)
        : registry(&reg)
    {
    }

    CurveRegistry* registry; ///< pinned at ctor — no per-call mutation
    QHash<QString, CurveLoader::Entry> entries; ///< name → entry (for entries() introspection)
    /// Wire-format snapshot per successfully committed key, used by
    /// commitBatch to skip re-registration of keys whose parsed curve
    /// is byte-identical to the previous scan. Empty after teardown —
    /// removedKeys are evicted here first so a later re-add produces
    /// a miss and actually registers.
    QHash<QString, QString> lastCommittedWireFormat;
    /// Set by commitBatch whenever the tracked curve set changes —
    /// either a key was removed, a new key was registered, or an
    /// existing key's wire-format differs from the last commit. Read
    /// by CurveLoader::onEntriesChanged to decide whether to emit
    /// `curvesChanged`. Re-set to false before every commit; `false`
    /// at end-of-batch means a no-op rescan (re-parse of files whose
    /// contents didn't change) and the consumer signal is suppressed.
    bool lastBatchChanged = false;

    std::optional<PhosphorJsonLoader::ParsedEntry> parseFile(const QString& filePath) override
    {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) {
            qCWarning(lcCurveLoader) << "Skipping unreadable file" << filePath << ":" << file.errorString();
            return std::nullopt;
        }
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            qCWarning(lcCurveLoader) << "Skipping malformed JSON" << filePath << ":" << parseError.errorString();
            return std::nullopt;
        }
        if (!doc.isObject()) {
            qCWarning(lcCurveLoader) << "Skipping non-object root JSON in" << filePath;
            return std::nullopt;
        }
        const QJsonObject obj = doc.object();

        const QString name = obj.value(QLatin1String("name")).toString();
        if (name.isEmpty()) {
            qCWarning(lcCurveLoader) << "Skipping" << filePath << ": missing required 'name' field";
            return std::nullopt;
        }

        // The filename (without extension) is the user's ergonomic
        // handle on the curve; the `name` field is what actually gets
        // registered. If the two diverge — typically because the user
        // copied `widget.fade.json → custom.json` and forgot to rename
        // the inner field — the result is a curve that shadows the
        // original under the inner-name key while the file on disk
        // suggests a different identity. Reject up front with a clear
        // diagnostic naming both sides.
        const QString basename = QFileInfo(filePath).completeBaseName();
        if (name != basename) {
            qCWarning(lcCurveLoader).nospace()
                << "Skipping " << filePath << ": curve name '" << name << "' does not match filename '" << basename
                << "' — rejecting to avoid silent shadowing";
            return std::nullopt;
        }

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

        // Build the wire-format string from the typeId + parameters and
        // route through CurveRegistry. Decision V: no new curve classes
        // through JSON, just parameter tuning on existing types.
        //
        // Each branch validates its own parameter ranges before
        // delegating to `tryCreate`. Validation at the library boundary
        // catches negative stiffness / period / amplitude /
        // out-of-unit-range bezier control points up front, giving the
        // author a clear "value X is out of range" message instead of
        // the opaque "CurveRegistry could not build" downstream warning
        // that a bad parameter would otherwise produce via
        // `tryCreate`'s generic parser.
        const auto reject = [&](const QString& field, auto value, const QString& constraint) {
            qCWarning(lcCurveLoader).nospace() << "Skipping " << filePath << ": " << typeId << " parameter '" << field
                                               << "' = " << value << " — " << constraint;
        };
        QString wireFormat;
        if (typeId == QLatin1String("spring")) {
            const qreal omega = params.value(QLatin1String("omega")).toDouble(12.0);
            const qreal zeta = params.value(QLatin1String("zeta")).toDouble(0.8);
            if (!(omega > 0.0)) {
                reject(QStringLiteral("omega"), omega, QStringLiteral("must be > 0 (angular frequency)"));
                return std::nullopt;
            }
            if (zeta < 0.0) {
                reject(QStringLiteral("zeta"), zeta, QStringLiteral("must be >= 0 (damping ratio)"));
                return std::nullopt;
            }
            wireFormat = QStringLiteral("spring:%1,%2").arg(omega).arg(zeta);
        } else if (typeId == QLatin1String("cubic-bezier")) {
            const qreal x1 = params.value(QLatin1String("x1")).toDouble(0.33);
            const qreal y1 = params.value(QLatin1String("y1")).toDouble(1.0);
            const qreal x2 = params.value(QLatin1String("x2")).toDouble(0.68);
            const qreal y2 = params.value(QLatin1String("y2")).toDouble(1.0);
            // Cubic Bezier control points: x must stay in [0,1] (CSS
            // spec + QEasingCurve constraint). y can legitimately exceed
            // the unit range — that's how overshoot curves work — so we
            // deliberately DO NOT bound y1/y2.
            if (x1 < 0.0 || x1 > 1.0) {
                reject(QStringLiteral("x1"), x1, QStringLiteral("must be in [0, 1] (control-point x)"));
                return std::nullopt;
            }
            if (x2 < 0.0 || x2 > 1.0) {
                reject(QStringLiteral("x2"), x2, QStringLiteral("must be in [0, 1] (control-point x)"));
                return std::nullopt;
            }
            wireFormat = QStringLiteral("%1,%2,%3,%4").arg(x1).arg(y1).arg(x2).arg(y2);
        } else if (typeId.startsWith(QLatin1String("elastic-"))) {
            const qreal amplitude = params.value(QLatin1String("amplitude")).toDouble(1.0);
            const qreal period = params.value(QLatin1String("period")).toDouble(0.3);
            if (!(amplitude > 0.0)) {
                reject(QStringLiteral("amplitude"), amplitude, QStringLiteral("must be > 0"));
                return std::nullopt;
            }
            if (!(period > 0.0)) {
                reject(QStringLiteral("period"), period, QStringLiteral("must be > 0"));
                return std::nullopt;
            }
            wireFormat = QStringLiteral("%1:%2,%3").arg(typeId).arg(amplitude).arg(period);
        } else if (typeId.startsWith(QLatin1String("bounce-"))) {
            const qreal amplitude = params.value(QLatin1String("amplitude")).toDouble(1.0);
            const int bounces = params.value(QLatin1String("bounces")).toInt(3);
            if (!(amplitude > 0.0)) {
                reject(QStringLiteral("amplitude"), amplitude, QStringLiteral("must be > 0"));
                return std::nullopt;
            }
            if (bounces < 1) {
                reject(QStringLiteral("bounces"), bounces, QStringLiteral("must be >= 1"));
                return std::nullopt;
            }
            wireFormat = QStringLiteral("%1:%2,%3").arg(typeId).arg(amplitude).arg(bounces);
        } else {
            qCWarning(lcCurveLoader) << "Skipping" << filePath << ": unknown typeId" << typeId;
            return std::nullopt;
        }

        auto curve = registry->tryCreate(wireFormat);
        if (!curve) {
            qCWarning(lcCurveLoader) << "Skipping" << filePath << ": CurveRegistry could not build" << wireFormat;
            return std::nullopt;
        }

        Payload payload{obj.value(QLatin1String("displayName")).toString(), std::move(curve), wireFormat};

        PhosphorJsonLoader::ParsedEntry parsed;
        parsed.key = name;
        parsed.sourcePath = filePath;
        parsed.payload = std::move(payload);
        return parsed;
    }

    void commitBatch(const QStringList& removedKeys,
                     const QList<PhosphorJsonLoader::ParsedEntry>& currentEntries) override
    {
        if (!registry) {
            qCWarning(lcCurveLoader) << "commitBatch: registry not set";
            return;
        }

        // Reset the per-batch "anything actually changed" flag. Any
        // remove/add/content-change below flips it to true, which
        // gates the consumer-facing `curvesChanged` signal.
        lastBatchChanged = false;

        // Purge entries whose backing files are no longer on disk.
        // CurveRegistry::registerFactory is replace-semantic, so a
        // future registration can re-use the key — but the current
        // caller wants the key fully unregistered until then.
        //
        // Gate `lastBatchChanged` on the actual snapshot delete: a
        // removedKeys entry whose key never made it into our snapshot
        // (parsed and rejected on a prior pass, then deleted from disk)
        // produced no externally-visible registration, so deleting it
        // doesn't change anything a consumer would see. Only count it as
        // a batch-change if we genuinely lose tracked state.
        for (const QString& key : removedKeys) {
            const bool hadEntry = entries.remove(key) > 0;
            const bool hadWire = lastCommittedWireFormat.remove(key) > 0;
            if (hadEntry || hadWire) {
                registry->unregisterFactory(key);
                lastBatchChanged = true;
            }
        }

        // Install the current set. Skip re-registering entries whose
        // wire-format is byte-identical to the previous commit — the
        // curve they'd produce is equivalent, and the extra mutex
        // round-trip through CurveRegistry is pointless churn. Mirrors
        // the shape of PhosphorProfileRegistry::reloadFromOwner, which
        // has the same "only touch entries that actually changed"
        // property.
        for (const auto& p : currentEntries) {
            const auto payload = std::any_cast<Payload>(&p.payload);
            if (!payload) {
                qCWarning(lcCurveLoader) << "commitBatch: payload type-mismatch for" << p.key;
                continue;
            }

            const auto existingIt = lastCommittedWireFormat.constFind(p.key);
            const bool unchanged = existingIt != lastCommittedWireFormat.constEnd()
                && *existingIt == payload->wireFormat && entries.contains(p.key);
            if (!unchanged) {
                const auto curve = payload->curve;
                // registerFactory returns true when it replaced an
                // existing registration. That's expected on every
                // content change — we log at debug only. A false
                // return with a NEW key is also fine (fresh insert).
                // The error case is registerFactory rejecting the
                // input (empty typeId / null factory) — should never
                // happen here since parseFile enforces a non-empty
                // name and builds a non-null curve, but check
                // defensively so future refactors can't silently lose
                // registrations.
                registry->registerFactory(p.key, [curve](const QString&, const QString&) {
                    return curve;
                });
                if (!registry->has(p.key)) {
                    qCWarning(lcCurveLoader) << "commitBatch: registerFactory silently rejected" << p.key;
                    continue;
                }
                lastCommittedWireFormat.insert(p.key, payload->wireFormat);
                lastBatchChanged = true;
            }

            CurveLoader::Entry trackedEntry;
            trackedEntry.name = p.key;
            trackedEntry.displayName = payload->displayName;
            trackedEntry.sourcePath = p.sourcePath;
            trackedEntry.systemSourcePath = p.systemSourcePath;
            entries.insert(p.key, std::move(trackedEntry));
        }
    }
};

CurveLoader::CurveLoader(CurveRegistry& registry, QObject* parent)
    : QObject(parent)
    , m_sink(std::make_unique<Sink>(registry))
    , m_loader(std::make_unique<PhosphorJsonLoader::DirectoryLoader>(*m_sink))
{
    // Gate `curvesChanged` on the per-batch change flag. DirectoryLoader
    // emits `entriesChanged` unconditionally on every rescan (tests and
    // debug tooling rely on that), but consumers of CurveLoader only
    // care when the tracked curve set or one of its wire-formats
    // actually changed — re-parsing the same files a second time (e.g.
    // a `requestRescan()` with no content change) is invisible at this
    // layer. Matches the header's documented contract.
    connect(m_loader.get(), &PhosphorJsonLoader::DirectoryLoader::entriesChanged, this, [this]() {
        if (m_sink->lastBatchChanged) {
            Q_EMIT curvesChanged();
        }
    });
}

CurveLoader::~CurveLoader() = default;

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
    // Discover the library's own bundled pack via QStandardPaths against
    // the phosphor-animation org/app. Today the library ships no built-in
    // JSON curves (the spring/cubic-bezier factories registered at runtime
    // cover the full surface); the lookup stays in place so future
    // curve packs can drop in without an API change here.
    //
    // Intentionally NOT namespacing under a consumer string — this is
    // the library's OWN pack, consumer-agnostic by construction.
    const QStringList dirs =
        QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, QStringLiteral("phosphor-animation/curves"),
                                  QStandardPaths::LocateDirectory);
    if (dirs.isEmpty()) {
        return 0;
    }
    return loadFromDirectories(dirs, liveReload);
}

void CurveLoader::requestRescan()
{
    m_loader->requestRescan();
}

int CurveLoader::registeredCount() const
{
    return m_loader->registeredCount();
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
