// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "algorithmservice.h"

#include "algorithmscaffold.h"

#include "../config/settings.h"
#include "../core/constants.h"
#include "../core/logging.h"
#include "../phosphor_i18n.h"

#include <PhosphorLayoutApi/LayoutId.h>
#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/AutotileConstants.h>
#include <PhosphorTiles/ITileAlgorithmRegistry.h>
#include <PhosphorTiles/LuauTileAlgorithm.h>
#include <PhosphorTiles/ScriptedAlgorithmLoader.h>
#include <PhosphorTiles/TilingAlgorithm.h>
#include <PhosphorTiles/TilingState.h>

#include <QDate>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLatin1Char>
#include <QLatin1String>
#include <QLoggingCategory>
#include <QRect>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringLiteral>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QVector>

namespace PlasmaZones {

namespace {

QString userAlgorithmsDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QLatin1Char('/')
        + ScriptedAlgorithmSubdir + QLatin1Char('/');
}

QString findUniqueAlgorithmPath(const QString& dir, const QString& baseName)
{
    QString path = dir + baseName + QStringLiteral(".luau");
    if (!QFile::exists(path))
        return path;
    for (int i = 1; i <= 999; ++i) {
        path = dir + baseName + QStringLiteral("-") + QString::number(i) + QStringLiteral(".luau");
        if (!QFile::exists(path))
            return path;
    }
    return QString();
}

/// Write @p content to @p destPath via QSaveFile, so the file appears at its
/// final path only once it is complete. The user algorithms directory is
/// watched by the daemon's loader in another process: a plain QFile write
/// would expose the empty file it creates before the content lands. Takes
/// bytes rather than a QString so an export can pass a source file through
/// unchanged instead of round-tripping it. Discards its temporary on any
/// failure, leaving @p destPath untouched.
bool writeAlgorithmFile(const QString& destPath, const QByteArray& content)
{
    QSaveFile out(destPath);
    if (!out.open(QIODevice::WriteOnly)) {
        qCWarning(PlasmaZones::lcCore) << "Failed to open algorithm file for writing:" << destPath;
        return false;
    }
    if (out.write(content) != content.size() || !out.commit()) {
        qCWarning(PlasmaZones::lcCore) << "Failed to write algorithm content:" << destPath;
        return false;
    }
    return true;
}

} // anonymous namespace

AlgorithmService::AlgorithmService(Settings* settings, PhosphorTiles::AlgorithmRegistry* registry,
                                   PhosphorTiles::ScriptedAlgorithmLoader* loader, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_registry(registry)
    , m_loader(loader)
{
    Q_ASSERT(m_settings);
    Q_ASSERT(m_registry);
    Q_ASSERT(m_loader);

    // When scripted algorithms change (hot-reload), notify UI consumers.
    // Forwarded to SettingsController via a signal-to-signal connect in its
    // constructor so QML's availableAlgorithmsChanged binding stays stable.
    connect(m_loader, &PhosphorTiles::ScriptedAlgorithmLoader::algorithmsChanged, this,
            &AlgorithmService::availableAlgorithmsChanged);
}

AlgorithmService::~AlgorithmService()
{
    // Disconnect all pending algorithm registration watchers. The registry
    // is owned by SettingsController (m_localAlgorithmRegistry) and is
    // destroyed after this destructor body runs thanks to the reverse-order
    // member-destruction invariant (AlgorithmService unique_ptr declared
    // AFTER the registry unique_ptr in SettingsController). Eager disconnect
    // keeps the window between here and ~unique_ptr closed so any in-flight
    // watcher signal queued mid-destruction cannot fire into a
    // half-destructed service.
    for (auto it = m_algorithmWatchers.cbegin(); it != m_algorithmWatchers.cend(); ++it) {
        disconnect(it.value().connection);
    }
    m_algorithmWatchers.clear();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Algorithm helpers
// ═══════════════════════════════════════════════════════════════════════════════

QVariantList AlgorithmService::availableAlgorithms() const
{
    QVariantList algorithms;
    for (const QString& id : m_registry->availableAlgorithms()) {
        PhosphorTiles::TilingAlgorithm* algo = m_registry->algorithm(id);
        if (algo) {
            QVariantMap algoMap;
            algoMap[QLatin1String("id")] = id;
            algoMap[QLatin1String("name")] = algo->name();
            algoMap[QLatin1String("description")] = algo->description();
            algoMap[QLatin1String("defaultMaxWindows")] = algo->defaultMaxWindows();
            algoMap[QLatin1String("supportsSplitRatio")] = algo->supportsSplitRatio();
            algoMap[QLatin1String("supportsMasterCount")] = algo->supportsMasterCount();
            algoMap[QLatin1String("defaultSplitRatio")] = algo->defaultSplitRatio();
            algoMap[QLatin1String("producesOverlappingZones")] = algo->producesOverlappingZones();
            algoMap[QLatin1String("zoneNumberDisplay")] = algo->zoneNumberDisplay();
            algoMap[QLatin1String("centerLayout")] = algo->centerLayout();

            // Expose whether this algorithm declares custom parameters.
            // The full definitions are retrieved via customParamsForAlgorithm().
            algoMap[QLatin1String("supportsCustomParams")] = algo->supportsCustomParams();

            algorithms.append(algoMap);
        }
    }
    return algorithms;
}

QVariantList AlgorithmService::generateAlgorithmPreview(const QString& algorithmId, int windowCount, double splitRatio,
                                                        int masterCount, const QVariantMap& customParams) const
{
    PhosphorTiles::TilingAlgorithm* algo = m_registry->algorithm(algorithmId);
    if (!algo) {
        return {};
    }

    // Custom-param-aware preview: the settings KCM's preview passes the live
    // master/split values and the live custom-param map as explicit inputs, so
    // the diagram is a pure function of what the UI shows. That path diverges
    // from previewFromAlgorithm (which reads the registry's configured params),
    // so we build the TilingParams explicitly here. The relative-space
    // projection mirrors LayoutPreview's contract (0..1 against the shared
    // preview canvas size).
    const int previewSize = PhosphorTiles::AlgorithmRegistry::PreviewCanvasSize;
    const QRect previewRect(0, 0, previewSize, previewSize);

    PhosphorTiles::TilingState state(QStringLiteral("preview"));
    state.setMasterCount(masterCount);
    state.setSplitRatio(splitRatio);

    const int count = qMax(1, windowCount);
    PhosphorTiles::TilingParams params = PhosphorTiles::TilingParams::forPreview(count, previewRect, &state);
    params.customParams = customParams;

    const QVector<QRect> zones = algo->calculateZones(params);

    QVariantList result;
    result.reserve(zones.size());
    for (int i = 0; i < zones.size(); ++i) {
        QVariantMap relGeo;
        relGeo[QLatin1String("x")] = static_cast<qreal>(zones[i].x()) / previewSize;
        relGeo[QLatin1String("y")] = static_cast<qreal>(zones[i].y()) / previewSize;
        relGeo[QLatin1String("width")] = static_cast<qreal>(zones[i].width()) / previewSize;
        relGeo[QLatin1String("height")] = static_cast<qreal>(zones[i].height()) / previewSize;

        QVariantMap zoneMap;
        zoneMap[QLatin1String("zoneNumber")] = i + 1;
        zoneMap[QLatin1String("relativeGeometry")] = relGeo;
        result.append(zoneMap);
    }
    return result;
}

QVariantList AlgorithmService::generateAlgorithmDefaultPreview(const QString& algorithmId) const
{
    PhosphorTiles::TilingAlgorithm* algo = m_registry->algorithm(algorithmId);
    if (!algo) {
        return {};
    }
    // Default thumbnails reflect any custom params the user has saved for the
    // algorithm (the live KCM preview supplies its own map; this caller reads
    // the persisted one).
    QVariantMap savedCustom;
    const QVariant algoEntry = m_settings->autotilePerAlgorithmSettings().value(algorithmId);
    if (algoEntry.isValid()) {
        savedCustom = algoEntry.toMap().value(PhosphorTiles::AutotileJsonKeys::CustomParams).toMap();
    }
    return generateAlgorithmPreview(algorithmId, algo->defaultMaxWindows(), algo->defaultSplitRatio(),
                                    PhosphorTiles::AutotileDefaults::DefaultMasterCount, savedCustom);
}

void AlgorithmService::openAlgorithmsFolder()
{
    const QString path = userAlgorithmsDir();
    QDir dir(path);
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

bool AlgorithmService::importAlgorithm(const QString& filePath)
{
    if (filePath.isEmpty())
        return false;

    const QFileInfo source(filePath);
    if (!source.exists() || !source.isFile())
        return false;

    // The loader only registers `<basename>.luau` files whose basename matches
    // [A-Za-z0-9_-]+; importing anything else would copy a file the loader
    // silently ignores, leaving the user with a success toast for an algorithm
    // that never appears. Reject up front with a clear message instead.
    if (source.suffix().compare(QLatin1String("luau"), Qt::CaseInsensitive) != 0) {
        Q_EMIT algorithmOperationFailed(PhosphorI18n::tr("Only Luau algorithm files (.luau) can be imported."));
        return false;
    }
    static const QRegularExpression validBaseName(QStringLiteral("^[A-Za-z0-9_-]+$"));
    if (!validBaseName.match(source.completeBaseName()).hasMatch()) {
        Q_EMIT algorithmOperationFailed(
            PhosphorI18n::tr("Algorithm file names may contain only letters, digits, hyphens, and underscores."));
        return false;
    }

    const QString destDir = userAlgorithmsDir();
    QDir dir(destDir);
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }

    // Land as "<validated-basename>.luau" (lower-case suffix) so the loader's
    // case-sensitive *.luau glob picks it up regardless of the source suffix case.
    QString destPath = destDir + source.completeBaseName() + QStringLiteral(".luau");

    // Same file (or a symlink to it) already in place: no-op success.
    const QFileInfo destInfo(destPath);
    if (destInfo.exists() && source.canonicalFilePath() == destInfo.canonicalFilePath()) {
        return true;
    }
    // A different file owns the name: pick a unique "<name>-N.luau" rather than
    // clobbering it (no destructive remove-then-copy; matches duplicate/create).
    if (destInfo.exists()) {
        destPath = findUniqueAlgorithmPath(destDir, source.completeBaseName());
        if (destPath.isEmpty()) {
            Q_EMIT algorithmOperationFailed(
                PhosphorI18n::tr("Too many algorithms share this name. Remove some and try again."));
            return false;
        }
    }

    if (!QFile::copy(filePath, destPath)) {
        // Report failure — the QML caller treats a false return as a silent no-op.
        Q_EMIT algorithmOperationFailed(
            PhosphorI18n::tr("Could not copy the algorithm file. Check available disk space and permissions."));
        return false;
    }
    // The loader's QFileSystemWatcher picks up the new file and refreshes the list.
    // No id-keyed watch here: an import registers under its own metadata id (not
    // necessarily its filename), so a basename watch would false-error.
    return true;
}

QString AlgorithmService::scriptedFilePath(const QString& algorithmId) const
{
    if (algorithmId.isEmpty())
        return QString();
    PhosphorTiles::TilingAlgorithm* algo = m_registry->algorithm(algorithmId);
    if (!algo)
        return QString();
    auto* scripted = qobject_cast<PhosphorTiles::LuauTileAlgorithm*>(algo);
    if (!scripted)
        return QString();
    const QString path = scripted->filePath();
    if (path.isEmpty() || !QFile::exists(path))
        return QString();
    return path;
}

void AlgorithmService::cancelAlgorithmWatcher(const QString& expectedId)
{
    auto it = m_algorithmWatchers.find(expectedId);
    if (it != m_algorithmWatchers.end()) {
        disconnect(it.value().connection);
        m_algorithmWatchers.erase(it);
    }
}

void AlgorithmService::watchForAlgorithmRegistration(const QString& expectedId)
{
    // Cancel any existing watcher for this ID to prevent stacking
    cancelAlgorithmWatcher(expectedId);

    const quint64 generation = ++m_watcherGeneration;
    const QMetaObject::Connection conn = connect(m_registry, &PhosphorTiles::AlgorithmRegistry::algorithmRegistered,
                                                 this, [this, expectedId](const QString& registeredId) {
                                                     if (registeredId != expectedId) {
                                                         return;
                                                     }
                                                     auto it = m_algorithmWatchers.find(expectedId);
                                                     if (it != m_algorithmWatchers.end()) {
                                                         disconnect(it.value().connection);
                                                         m_algorithmWatchers.erase(it);
                                                     }
                                                     Q_EMIT algorithmCreated(expectedId);
                                                 });
    m_algorithmWatchers.insert(expectedId, {conn, generation});
    // The context object (this) ensures the lambda is not invoked if AlgorithmService
    // is destroyed before the timer fires — QTimer::singleShot with a context guarantees this.
    // The captured generation pins the timer to THIS watcher: if the watcher
    // resolves and a new one is created for the same id within the window,
    // the stale timer must not tear the newcomer down.
    QTimer::singleShot(10000, this, [this, expectedId, generation]() {
        auto it = m_algorithmWatchers.find(expectedId);
        if (it == m_algorithmWatchers.end() || it.value().generation != generation) {
            return;
        }
        disconnect(it.value().connection);
        m_algorithmWatchers.erase(it);
        qCWarning(PlasmaZones::lcCore) << "Algorithm registration timed out for:" << expectedId;
        Q_EMIT algorithmOperationFailed(
            PhosphorI18n::tr("Algorithm was created but not picked up by the registry. "
                             "Try refreshing or restarting the application."));
    });
}

void AlgorithmService::openAlgorithm(const QString& algorithmId)
{
    // Try registry first (works for already-registered algorithms)
    const QString registryPath = scriptedFilePath(algorithmId);
    if (!registryPath.isEmpty()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(registryPath));
        return;
    }

    // Fallback: try user algorithms dir directly (works right after creation
    // before the registry has picked up the file via QFileSystemWatcher).
    // Uses algorithmId as filename — valid for createNewAlgorithm (returns filename)
    // and duplicateAlgorithm (watches for the filename-based ID). Validate the id
    // as a bare basename before interpolating it into a path, so a stray id with
    // separators / ".." can never escape the user dir (defence in depth — every
    // producer already sanitizes).
    static const QRegularExpression safeBaseName(QStringLiteral("^[A-Za-z0-9_-]+$"));
    if (!safeBaseName.match(algorithmId).hasMatch()) {
        qCWarning(PlasmaZones::lcCore) << "Cannot open algorithm — unsafe id (not a bare basename):" << algorithmId;
        return;
    }
    const QString userPath = userAlgorithmsDir() + algorithmId + QStringLiteral(".luau");
    if (QFile::exists(userPath)) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(userPath));
        return;
    }

    qCWarning(PlasmaZones::lcCore) << "Cannot open algorithm — file not found for:" << algorithmId
                                   << "(checked registry and user dir:" << userAlgorithmsDir() << ")";
}

void AlgorithmService::openLayoutFile(const QString& layoutId)
{
    if (layoutId.isEmpty())
        return;
    // PhosphorZones::Layout files use UUID without braces as the filename
    const QUuid uuid(layoutId);
    if (uuid.isNull()) {
        qCDebug(PlasmaZones::lcCore) << "openLayoutFile: not a valid UUID layout ID:" << layoutId;
        return;
    }
    const QString bareId = uuid.toString(QUuid::WithoutBraces);
    const QString filename = bareId + QStringLiteral(".json");
    // Search user dir first, then all system dirs
    const QString located =
        QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("plasmazones/layouts/") + filename);
    if (located.isEmpty()) {
        qCWarning(PlasmaZones::lcCore) << "Layout file not found:" << filename;
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(located));
}

bool AlgorithmService::deleteAlgorithm(const QString& algorithmId)
{
    if (algorithmId.isEmpty()) {
        Q_EMIT algorithmOperationFailed(PhosphorI18n::tr("No algorithm is selected to delete."));
        return false;
    }

    PhosphorTiles::TilingAlgorithm* algo = m_registry->algorithm(algorithmId);
    if (!algo || !algo->isUserScript()) {
        qCWarning(PlasmaZones::lcCore) << "Cannot delete algorithm — not a user script:" << algorithmId;
        Q_EMIT algorithmOperationFailed(PhosphorI18n::tr("Only user-created algorithms can be deleted."));
        return false;
    }

    const QString filePath = scriptedFilePath(algorithmId);
    if (filePath.isEmpty()) {
        qCWarning(PlasmaZones::lcCore) << "Algorithm file not found for:" << algorithmId;
        Q_EMIT algorithmOperationFailed(PhosphorI18n::tr("Algorithm file not found."));
        return false;
    }

    // Only allow deleting from the user algorithms directory (canonicalize to defeat symlinks).
    // If the user dir doesn't exist yet, canonicalFilePath() returns empty — guard against
    // that becoming "/" which would match any absolute path.
    const QString rawUserDir = QFileInfo(userAlgorithmsDir()).canonicalFilePath();
    const QString userDir = rawUserDir + QLatin1Char('/');
    const QString canonicalPath = QFileInfo(filePath).canonicalFilePath();
    if (rawUserDir.isEmpty() || canonicalPath.isEmpty() || !canonicalPath.startsWith(userDir)) {
        qCWarning(PlasmaZones::lcCore) << "Refusing to delete non-user algorithm file:" << filePath
                                       << "userDir=" << rawUserDir << "canonical=" << canonicalPath;
        Q_EMIT algorithmOperationFailed(rawUserDir.isEmpty()
                                            ? PhosphorI18n::tr("The user algorithms directory does not exist.")
                                            : PhosphorI18n::tr("That file is outside the user algorithms directory."));
        return false;
    }

    // Cancel any pending registration watcher for this algorithm — otherwise
    // the 10s timeout fires algorithmOperationFailed for a deliberately deleted file.
    cancelAlgorithmWatcher(algorithmId);

    // Use the canonical path for removal to ensure we delete the actual file,
    // not a symlink pointing into the user dir.
    const bool ok = QFile::remove(canonicalPath);
    if (!ok) {
        qCWarning(PlasmaZones::lcCore) << "Failed to delete algorithm file:" << canonicalPath;
        Q_EMIT algorithmOperationFailed(PhosphorI18n::tr("Could not delete algorithm file. Check file permissions."));
    }
    // QFileSystemWatcher will pick up the deletion and trigger a refresh
    return ok;
}

bool AlgorithmService::duplicateAlgorithm(const QString& algorithmId)
{
    const QString sourcePath = scriptedFilePath(algorithmId);
    if (sourcePath.isEmpty()) {
        Q_EMIT algorithmOperationFailed(PhosphorI18n::tr("The algorithm file could not be found."));
        return false;
    }

    PhosphorTiles::TilingAlgorithm* algo = m_registry->algorithm(algorithmId);
    if (!algo) {
        Q_EMIT algorithmOperationFailed(PhosphorI18n::tr("That algorithm is no longer registered."));
        return false;
    }

    const QString destDir = userAlgorithmsDir();
    QDir dir(destDir);
    if (!dir.exists())
        dir.mkpath(QStringLiteral("."));

    // Generate unique filename: <source>-copy.luau, <source>-copy-1.luau, etc.
    // Derived from the source FILE name, not the registry id: the loader
    // validates basenames but takes metadata ids verbatim, so an id is not
    // guaranteed to be a safe path component.
    const QString baseName = QFileInfo(sourcePath).completeBaseName() + QStringLiteral("-copy");
    const QString destPath = findUniqueAlgorithmPath(destDir, baseName);
    if (destPath.isEmpty()) {
        qCWarning(PlasmaZones::lcCore) << "Could not find unique filename for duplicate:" << baseName;
        Q_EMIT algorithmOperationFailed(
            PhosphorI18n::tr("Too many copies of this algorithm already exist. "
                             "Rename or delete some before duplicating."));
        return false;
    }

    // Canonicalize source path to follow symlinks and ensure we read the actual file
    const QString canonicalSource = QFileInfo(sourcePath).canonicalFilePath();
    if (canonicalSource.isEmpty()) {
        Q_EMIT algorithmOperationFailed(PhosphorI18n::tr("The algorithm file path could not be resolved."));
        return false;
    }

    // Read source, update metadata, write copy
    QFile sourceFile(canonicalSource);
    if (!sourceFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        Q_EMIT algorithmOperationFailed(PhosphorI18n::tr("Could not read source algorithm file."));
        return false;
    }
    QString content = QString::fromUtf8(sourceFile.readAll());
    // readAll() returns what it managed to read: without this check a mid-file
    // failure would be rewritten and registered as a truncated algorithm (the
    // metadata regexes below sit near the top of the file and would still hit).
    if (sourceFile.error() != QFileDevice::NoError) {
        qCWarning(PlasmaZones::lcCore) << "Failed to read source algorithm file:" << canonicalSource
                                       << sourceFile.errorString();
        Q_EMIT algorithmOperationFailed(PhosphorI18n::tr("Could not read source algorithm file."));
        return false;
    }
    sourceFile.close();

    // Update name and id in the copy's metadata object — strip all existing " (Copy)" suffixes to avoid accumulation.
    // Sanitize before stripping so the strip sees the text that will actually
    // be written: sanitization can materialize a trailing " (Copy)" (e.g. a
    // newline before "(Copy)" becomes a space) that stripping afterwards
    // would miss, and the suffix appended below must never be re-mangled.
    const QString newFilename = QFileInfo(destPath).completeBaseName();
    QString baseCopyName = AlgorithmScaffold::sanitizeMetadataString(algo->name());
    while (baseCopyName.endsWith(QLatin1String(" (Copy)")))
        baseCopyName.chop(7);
    const QString newName = baseCopyName + QStringLiteral(" (Copy)");
    // Replace the first name and id values inside the metadata table.
    // Anchored to line start to avoid matching inside algorithm body strings.
    // Capture leading whitespace so the replacement preserves indentation.
    //
    // These patterns only match double-quoted values on metadata-object lines
    // (the form every bundled template uses). Single-quoted, JSDoc-style, or
    // property-shorthand metadata is unsupported — if either match fails, the
    // duplicate would otherwise be written with the original name + id,
    // colliding with the source in the registry. Bail out instead.
    static const QRegularExpression nameRe(QStringLiteral(R"(^(\s*)name\s*=\s*"[^"]*")"),
                                           QRegularExpression::MultilineOption);
    static const QRegularExpression idRe(QStringLiteral(R"(^(\s*)id\s*=\s*"[^"]*")"),
                                         QRegularExpression::MultilineOption);
    const QRegularExpressionMatch nameMatch = nameRe.match(content);
    const QRegularExpressionMatch idMatch = idRe.match(content);
    if (!nameMatch.hasMatch() || !idMatch.hasMatch()) {
        qCWarning(PlasmaZones::lcCore) << "duplicateAlgorithm: could not locate metadata name/id in" << canonicalSource
                                       << "(nameMatch=" << nameMatch.hasMatch() << "idMatch=" << idMatch.hasMatch()
                                       << ")";
        Q_EMIT algorithmOperationFailed(
            PhosphorI18n::tr("Could not duplicate the algorithm. Its metadata format is not recognised. "
                             "Expected `name = \"...\"` and `id = \"...\"` on separate lines."));
        return false;
    }
    content.replace(nameMatch.capturedStart(), nameMatch.capturedLength(),
                    nameMatch.captured(1) + QStringLiteral("name = \"") + newName + QStringLiteral("\""));
    // idMatch captured positions may have shifted after the name replacement;
    // re-run id match over the updated content so we don't corrupt bytes.
    const QRegularExpressionMatch idMatch2 = idRe.match(content);
    if (!idMatch2.hasMatch()) {
        qCWarning(PlasmaZones::lcCore) << "duplicateAlgorithm: id match disappeared after name replacement";
        Q_EMIT algorithmOperationFailed(PhosphorI18n::tr("Could not rewrite the algorithm's metadata."));
        return false;
    }
    content.replace(idMatch2.capturedStart(), idMatch2.capturedLength(),
                    idMatch2.captured(1) + QStringLiteral("id = \"") + newFilename + QStringLiteral("\""));

    if (!writeAlgorithmFile(destPath, content.toUtf8())) {
        Q_EMIT algorithmOperationFailed(
            PhosphorI18n::tr("Could not write duplicate algorithm file. Check disk space and permissions."));
        return false;
    }

    // Watch for registry pickup and emit algorithmCreated (issue #2: duplicate didn't fire signal)
    watchForAlgorithmRegistration(newFilename);
    return true;
}

bool AlgorithmService::exportAlgorithm(const QString& algorithmId, const QString& destPath)
{
    if (destPath.isEmpty()) {
        Q_EMIT algorithmOperationFailed(PhosphorI18n::tr("No export destination specified."));
        return false;
    }

    const QString sourcePath = scriptedFilePath(algorithmId);
    if (sourcePath.isEmpty()) {
        Q_EMIT algorithmOperationFailed(PhosphorI18n::tr("The algorithm file could not be found."));
        return false;
    }

    // Atomic replace via QSaveFile: it writes to a uniquely named temporary
    // in the destination directory and renames over the target on commit,
    // so a pre-existing destination is never lost on failure and no fixed
    // ".tmp"/".bak" sibling files are touched.
    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly)) {
        Q_EMIT algorithmOperationFailed(PhosphorI18n::tr("Could not read the algorithm file for export."));
        return false;
    }
    const QByteArray contents = source.readAll();
    // readAll() returns what it managed to read, so a mid-file failure would
    // otherwise export a silently truncated script.
    if (source.error() != QFileDevice::NoError) {
        qCWarning(PlasmaZones::lcCore) << "Failed to read algorithm file for export:" << sourcePath
                                       << source.errorString();
        Q_EMIT algorithmOperationFailed(PhosphorI18n::tr("Could not read the algorithm file for export."));
        return false;
    }
    source.close();

    if (!writeAlgorithmFile(destPath, contents)) {
        Q_EMIT algorithmOperationFailed(PhosphorI18n::tr("Could not write to export destination."));
        return false;
    }
    return true;
}

QString AlgorithmService::createNewAlgorithm(const QString& name, const QString& baseTemplate,
                                             const QVariantMap& capabilities)
{
    // Sanitize name to a filename: lowercase, replace non-alphanumeric (except hyphens) with
    // hyphens, collapse multiple hyphens, then strip the whole leading run of
    // digits and hyphens in one pass (so the result can start with neither,
    // e.g. "12-3col" cannot resurface a leading digit) plus trailing hyphens
    QString filename = name.trimmed().toLower();
    static const QRegularExpression nonAlnum(QStringLiteral("[^a-z0-9-]"));
    filename.replace(nonAlnum, QStringLiteral("-"));
    static const QRegularExpression multiHyphen(QStringLiteral("-{2,}"));
    filename.replace(multiHyphen, QStringLiteral("-"));
    static const QRegularExpression leadDigitsHyphens(QStringLiteral("^[0-9-]+"));
    filename.replace(leadDigitsHyphens, QString());
    static const QRegularExpression trailHyphens(QStringLiteral("-+$"));
    filename.replace(trailHyphens, QString());
    if (filename.isEmpty())
        filename = QStringLiteral("untitled-algorithm");

    // Build destination path
    const QString destDir = userAlgorithmsDir();
    QDir dir(destDir);
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }

    const QString destPath = findUniqueAlgorithmPath(destDir, filename);
    if (destPath.isEmpty()) {
        qCWarning(PlasmaZones::lcCore) << "Could not find unique filename for algorithm:" << filename
                                       << "— all 999 slots exhausted";
        Q_EMIT algorithmOperationFailed(
            PhosphorI18n::tr("Too many algorithms already share this name. "
                             "Rename or delete some before creating another."));
        return QString();
    }
    // Update filename to match the final path (may have -N suffix)
    filename = QFileInfo(destPath).completeBaseName();

    // Build Luau content
    QString content;

    // SPDX header — use current year and a placeholder author. A template
    // copy takes only the copyright line and inherits the template's own
    // license; the blank scaffold is our own code, so it takes both lines.
    const int currentYear = QDate::currentDate().year();
    const QString copyrightLine =
        QStringLiteral("-- SPDX-FileCopyrightText: ") + QString::number(currentYear) + QStringLiteral(" <your name>");
    const QString blankHeader = copyrightLine + QStringLiteral("\n-- SPDX-License-Identifier: GPL-3.0-or-later\n");

    // Display name — strip newlines/quotes to prevent injection.
    const QString sanitizedDisplayName = AlgorithmScaffold::sanitizeMetadataString(name.trimmed());

    // Start from a base template by reusing its module locals + tile and
    // patching in the new owner's copyright + name/id. The template's other
    // metadata fields (capability flags, defaults, customParams) are kept
    // verbatim — the template's code depends on them. A template that cannot
    // be used is a hard failure: silently substituting the blank scaffold
    // would hand the user something very different from what they picked.
    if (baseTemplate != QLatin1String("blank") && !baseTemplate.isEmpty()) {
        // Defence in depth, mirroring openAlgorithm: a bare basename can
        // never escape the algorithms subdir.
        static const QRegularExpression safeTemplateName(QStringLiteral("^[A-Za-z0-9_-]+$"));
        if (!safeTemplateName.match(baseTemplate).hasMatch()) {
            qCWarning(PlasmaZones::lcCore)
                << "createNewAlgorithm: unsafe template id (not a bare basename):" << baseTemplate;
            Q_EMIT algorithmOperationFailed(
                PhosphorI18n::tr("The selected template could not be used. Pick another template or start blank."));
            return QString();
        }

        // locate() checks the user's writable data dir first, so a user
        // algorithm file named after a bundled template id deliberately
        // shadows it here — the registry and the wizard's preview resolve
        // the same override, keeping what the user saw and what gets copied
        // consistent.
        const QString templateFile =
            QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                                   ScriptedAlgorithmSubdir + QLatin1Char('/') + baseTemplate + QStringLiteral(".luau"));

        if (templateFile.isEmpty()) {
            qCWarning(PlasmaZones::lcCore)
                << "createNewAlgorithm: template file for" << baseTemplate << "not found in any data location";
            Q_EMIT algorithmOperationFailed(
                PhosphorI18n::tr("The selected template could not be found. Pick another template or start blank."));
            return QString();
        }

        QFile file(templateFile);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qCWarning(PlasmaZones::lcCore) << "createNewAlgorithm: could not read template file:" << templateFile;
            Q_EMIT algorithmOperationFailed(
                PhosphorI18n::tr("The selected template could not be read. Pick another template or start blank."));
            return QString();
        }
        const QString templateContent = QString::fromUtf8(file.readAll());
        if (file.error() != QFileDevice::NoError) {
            qCWarning(PlasmaZones::lcCore)
                << "createNewAlgorithm: failed to read template file:" << templateFile << file.errorString();
            Q_EMIT algorithmOperationFailed(
                PhosphorI18n::tr("The selected template could not be read. Pick another template or start blank."));
            return QString();
        }
        file.close();

        content = AlgorithmScaffold::spliceTemplate(templateContent, copyrightLine, sanitizedDisplayName, filename);
        if (content.isEmpty()) {
            qCWarning(PlasmaZones::lcCore) << "createNewAlgorithm: template" << baseTemplate
                                           << "has an unrecognized metadata shape at" << templateFile;
            Q_EMIT algorithmOperationFailed(
                PhosphorI18n::tr("The selected template could not be used. Pick another template or start blank."));
            return QString();
        }
    } else {
        // Blank scaffold: a self-contained pluau.algorithm module the user
        // edits. The wizard's capability toggles only apply here.
        AlgorithmScaffold::Capabilities caps;
        caps.masterCount = capabilities.value(QStringLiteral("supportsMasterCount")).toBool();
        caps.splitRatio = capabilities.value(QStringLiteral("supportsSplitRatio")).toBool();
        caps.overlappingZones = capabilities.value(QStringLiteral("producesOverlappingZones")).toBool();
        caps.memory = capabilities.value(QStringLiteral("supportsMemory")).toBool();
        caps.scriptState = capabilities.value(QStringLiteral("supportsScriptState")).toBool();
        caps.singleWindow = capabilities.value(QStringLiteral("supportsSingleWindow")).toBool();
        caps.retileOnFocus = capabilities.value(QStringLiteral("retileOnFocus")).toBool();
        content = AlgorithmScaffold::buildBlankScaffold(blankHeader, sanitizedDisplayName, filename, caps);
    }

    if (!writeAlgorithmFile(destPath, content.toUtf8())) {
        Q_EMIT algorithmOperationFailed(
            PhosphorI18n::tr("Could not write algorithm file. Check disk space and permissions."));
        return QString();
    }

    watchForAlgorithmRegistration(filename);
    return filename;
}

} // namespace PlasmaZones
