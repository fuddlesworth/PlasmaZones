// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/CurveLoader.h>

#include <PhosphorAnimation/CurveRegistry.h>

#include <PhosphorJsonLoader/DirectoryLoader.h>
#include <PhosphorJsonLoader/IDirectoryLoaderSink.h>
#include <PhosphorJsonLoader/ParsedEntry.h>

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLoggingCategory>
#include <QStandardPaths>

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
    };

    explicit Sink(CurveRegistry& reg)
        : registry(&reg)
    {
    }

    CurveRegistry* registry; ///< pinned at ctor — no per-call mutation
    QHash<QString, CurveLoader::Entry> entries; ///< name → entry (for entries() introspection)

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

        Payload payload{obj.value(QLatin1String("displayName")).toString(), std::move(curve)};

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

        // Purge entries whose backing files are no longer on disk.
        // CurveRegistry::registerFactory is replace-semantic, so a
        // future registration can re-use the key — but the current
        // caller wants the key fully unregistered until then.
        for (const QString& key : removedKeys) {
            registry->unregisterFactory(key);
            entries.remove(key);
        }

        // Install the current set. If a key is already registered
        // with the identical factory, re-registering replaces it —
        // this is the intended behaviour (the factory lambda captures
        // the curve by shared_ptr, so a shape-identical re-parse
        // produces an equivalent ownership graph).
        for (const auto& p : currentEntries) {
            const auto payload = std::any_cast<Payload>(&p.payload);
            if (!payload) {
                qCWarning(lcCurveLoader) << "commitBatch: payload type-mismatch for" << p.key;
                continue;
            }
            const auto curve = payload->curve;
            registry->registerFactory(p.key, [curve](const QString&, const QString&) {
                return curve;
            });
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
    , m_loader(std::make_unique<PhosphorJsonLoader::DirectoryLoader>(m_sink.get()))
{
    connect(m_loader.get(), &PhosphorJsonLoader::DirectoryLoader::entriesChanged, this, &CurveLoader::curvesChanged);
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
    return m_sink->entries.values();
}

} // namespace PhosphorAnimation
