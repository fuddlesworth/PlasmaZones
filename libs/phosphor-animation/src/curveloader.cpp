// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/CurveLoader.h>

#include <PhosphorAnimation/CurveRegistry.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLoggingCategory>
#include <QStandardPaths>

namespace PhosphorAnimation {

namespace {
Q_LOGGING_CATEGORY(lcCurveLoader, "phosphoranimation.curveloader")
} // namespace

CurveLoader::CurveLoader(QObject* parent)
    : QObject(parent)
{
    // Single-shot debounce. Coalesces multiple filesystem events that
    // land in the same 50 ms window (a text editor's save-temp-rename
    // dance can fire three events; we want one rescan).
    m_debounceTimer.setSingleShot(true);
    m_debounceTimer.setInterval(50);
    connect(&m_debounceTimer, &QTimer::timeout, this, &CurveLoader::rescanAll);
}

CurveLoader::~CurveLoader() = default;

int CurveLoader::loadFromDirectory(const QString& directory, CurveRegistry& registry, LiveReload liveReload)
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
    const int countDelta = m_entries.size() - countBefore;
    return countDelta;
}

int CurveLoader::loadFromDirectories(const QStringList& directories, CurveRegistry& registry, LiveReload liveReload)
{
    int total = 0;
    for (const QString& dir : directories) {
        total += loadFromDirectory(dir, registry, liveReload);
    }
    return total;
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
    // Start the debounce timer; actual rescan runs on timeout. Safe to
    // call multiple times in rapid succession — the single-shot timer
    // coalesces into one fire.
    m_debounceTimer.start();
}

int CurveLoader::registeredCount() const
{
    return m_entries.size();
}

QList<CurveLoader::Entry> CurveLoader::entries() const
{
    return m_entries.values();
}

std::optional<std::pair<CurveLoader::Entry, std::shared_ptr<const Curve>>>
CurveLoader::parseFile(const QString& filePath) const
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
    // route through CurveRegistry. This reuses the existing fromString
    // plumbing rather than reimplementing per-typeId JSON parsing —
    // the wire format is already the canonical parameter shape for
    // every built-in curve type (decision V: no new curve classes
    // through JSON, just parameter tuning).
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

    Entry entry;
    entry.name = name;
    entry.displayName = obj.value(QLatin1String("displayName")).toString();
    entry.sourcePath = filePath;
    return std::make_pair(std::move(entry), std::move(curve));
}

void CurveLoader::rescanDirectory(const QString& directory)
{
    if (!m_registry) {
        qCWarning(lcCurveLoader) << "rescanDirectory: no registry bound";
        return;
    }

    QDir dir(directory);
    if (!dir.exists()) {
        qCDebug(lcCurveLoader) << "Directory does not exist:" << directory;
        return;
    }
    const QStringList files = dir.entryList({QStringLiteral("*.json")}, QDir::Files);
    for (const QString& file : files) {
        const QString fullPath = dir.absoluteFilePath(file);
        auto parsed = parseFile(fullPath);
        if (!parsed) {
            continue;
        }
        auto& [entry, curve] = *parsed;
        const QString name = entry.name;

        // Collision policy (decision X): the later-scanned entry wins,
        // and the displaced entry's sourcePath is preserved on the new
        // one as `systemSourcePath` — so `unregisterUserCurves` (future
        // sub-commit) can restore the system original. `loadFromDirectories`
        // passes dirs in system-first / user-last order, so "later" ==
        // "user" == "wins."
        if (auto existing = m_entries.find(name); existing != m_entries.end()) {
            entry.systemSourcePath = existing->sourcePath;
        }

        // Register with CurveRegistry. Capture the curve in a factory
        // lambda keyed on the user's name — subsequent CurveRegistry::
        // create("<name>") returns this instance.
        m_registry->registerFactory(name, [curve](const QString&, const QString&) {
            return curve;
        });
        m_entries.insert(name, std::move(entry));
    }
}

void CurveLoader::rescanAll()
{
    if (!m_registry) {
        return;
    }
    const int countBefore = m_entries.size();
    for (const QString& dir : m_directories) {
        rescanDirectory(dir);
    }
    // Fire the change signal unconditionally on rescan — consumers
    // cannot cheaply diff the set from outside, and re-resolving a
    // handful of curves is negligible vs. missing a genuine edit.
    Q_UNUSED(countBefore);
    Q_EMIT curvesChanged();
}

void CurveLoader::installWatcherIfNeeded()
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
