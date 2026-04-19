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

    CurveRegistry* registry = nullptr;
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
        QString wireFormat;
        if (typeId == QLatin1String("spring")) {
            const qreal omega = params.value(QLatin1String("omega")).toDouble(12.0);
            const qreal zeta = params.value(QLatin1String("zeta")).toDouble(0.8);
            wireFormat = QStringLiteral("spring:%1,%2").arg(omega).arg(zeta);
        } else if (typeId == QLatin1String("cubic-bezier")) {
            const qreal x1 = params.value(QLatin1String("x1")).toDouble(0.33);
            const qreal y1 = params.value(QLatin1String("y1")).toDouble(1.0);
            const qreal x2 = params.value(QLatin1String("x2")).toDouble(0.68);
            const qreal y2 = params.value(QLatin1String("y2")).toDouble(1.0);
            wireFormat = QStringLiteral("%1,%2,%3,%4").arg(x1).arg(y1).arg(x2).arg(y2);
        } else if (typeId.startsWith(QLatin1String("elastic-"))) {
            const qreal amplitude = params.value(QLatin1String("amplitude")).toDouble(1.0);
            const qreal period = params.value(QLatin1String("period")).toDouble(0.3);
            wireFormat = QStringLiteral("%1:%2,%3").arg(typeId).arg(amplitude).arg(period);
        } else if (typeId.startsWith(QLatin1String("bounce-"))) {
            const qreal amplitude = params.value(QLatin1String("amplitude")).toDouble(1.0);
            const int bounces = params.value(QLatin1String("bounces")).toInt(3);
            wireFormat = QStringLiteral("%1:%2,%3").arg(typeId).arg(amplitude).arg(bounces);
        } else {
            qCWarning(lcCurveLoader) << "Skipping" << filePath << ": unknown typeId" << typeId;
            return std::nullopt;
        }

        auto curve = CurveRegistry::instance().tryCreate(wireFormat);
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

CurveLoader::CurveLoader(QObject* parent)
    : QObject(parent)
    , m_sink(std::make_unique<Sink>())
    , m_loader(std::make_unique<PhosphorJsonLoader::DirectoryLoader>(m_sink.get()))
{
    connect(m_loader.get(), &PhosphorJsonLoader::DirectoryLoader::entriesChanged, this, &CurveLoader::curvesChanged);
}

CurveLoader::~CurveLoader() = default;

int CurveLoader::loadFromDirectory(const QString& directory, CurveRegistry& registry, LiveReload liveReload)
{
    m_sink->registry = &registry;
    return m_loader->loadFromDirectory(directory, liveReload);
}

int CurveLoader::loadFromDirectories(const QStringList& directories, CurveRegistry& registry, LiveReload liveReload)
{
    m_sink->registry = &registry;
    return m_loader->loadFromDirectories(directories, liveReload);
}

int CurveLoader::loadLibraryBuiltins(CurveRegistry& registry)
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
    return loadFromDirectories(dirs, registry, LiveReload::Off);
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
