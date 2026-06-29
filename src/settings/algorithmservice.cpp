// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "algorithmservice.h"

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
#include <QStandardPaths>
#include <QStringLiteral>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QVector>

namespace PlasmaZones {

namespace {

// Strip characters that would let a metadata display string break out of its
// Luau double-quoted literal, or inject a brace that desyncs the brace-depth
// template splicer in spliceLuauTemplate().
QString sanitizeLuauMetadataString(QString value)
{
    value.replace(QLatin1Char('\n'), QLatin1Char(' '));
    value.replace(QLatin1Char('\r'), QLatin1Char(' '));
    value.replace(QLatin1Char('\\'), QLatin1Char('/'));
    value.replace(QLatin1Char('"'), QLatin1Char('\''));
    return value;
}

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

/// Build a `metadata = { ... }` Luau table block (4-space indented, as it
/// appears inside `pluau.algorithm{ ... }`). Ends with `},` so it drops in ahead
/// of the tile field.
QString buildLuauMetadata(const QString& name, const QString& id, bool overlapping, bool masterCount, bool splitRatio,
                          bool memory)
{
    const auto b = [](bool v) {
        return v ? QStringLiteral("true") : QStringLiteral("false");
    };
    QString m;
    m += QStringLiteral("    metadata = {\n");
    m += QStringLiteral("        name = \"") + name + QStringLiteral("\",\n");
    m += QStringLiteral("        id = \"") + id + QStringLiteral("\",\n");
    m += QStringLiteral("        description = \"Custom tiling algorithm\",\n");
    m += QStringLiteral("        producesOverlappingZones = ") + b(overlapping) + QStringLiteral(",\n");
    m += QStringLiteral("        supportsMasterCount = ") + b(masterCount) + QStringLiteral(",\n");
    m += QStringLiteral("        supportsSplitRatio = ") + b(splitRatio) + QStringLiteral(",\n");
    m += QStringLiteral("        defaultSplitRatio = 0.5,\n");
    m += QStringLiteral("        defaultMaxWindows = 6,\n");
    m += QStringLiteral("        minimumWindows = 1,\n");
    m += QStringLiteral("        zoneNumberDisplay = \"all\",\n");
    m += QStringLiteral("        supportsMemory = ") + b(memory) + QStringLiteral(",\n");
    m += QStringLiteral("    },");
    return m;
}

/// Splice a bundled `.luau` template: replace its leading SPDX comment block
/// with @p newHeader and its `metadata = { ... }` table with @p metadataBlock,
/// preserving the module locals + tile function. Returns empty on a template
/// whose `metadata = {` block can't be located (caller falls back to a blank
/// scaffold). Brace-depth scan; matches the bundled templates' formatting (no
/// `{`/`}` inside metadata strings).
QString spliceLuauTemplate(const QString& templateContent, const QString& newHeader, const QString& metadataBlock)
{
    const QStringList lines = templateContent.split(QLatin1Char('\n'));

    // Skip the template's own leading comment / blank lines (its SPDX header).
    int firstCode = 0;
    while (firstCode < lines.size()) {
        const QString t = lines[firstCode].trimmed();
        if (t.isEmpty() || t.startsWith(QLatin1String("--"))) {
            ++firstCode;
            continue;
        }
        break;
    }

    static const QRegularExpression metaRe(QStringLiteral(R"(^\s*metadata\s*=\s*\{)"));
    int metaStart = -1;
    for (int i = firstCode; i < lines.size(); ++i) {
        if (metaRe.match(lines[i]).hasMatch()) {
            metaStart = i;
            break;
        }
    }
    if (metaStart < 0) {
        return QString();
    }

    int depth = 0;
    int metaEnd = -1;
    for (int i = metaStart; i < lines.size(); ++i) {
        for (const QChar c : lines[i]) {
            if (c == QLatin1Char('{')) {
                ++depth;
            } else if (c == QLatin1Char('}')) {
                --depth;
            }
        }
        if (depth == 0) {
            metaEnd = i;
            break;
        }
    }
    if (metaEnd < 0) {
        return QString();
    }

    QString out = newHeader + QStringLiteral("\n");
    for (int i = firstCode; i < metaStart; ++i) {
        out += lines[i] + QLatin1Char('\n');
    }
    out += metadataBlock + QLatin1Char('\n');
    for (int i = metaEnd + 1; i < lines.size(); ++i) {
        out += lines[i];
        if (i < lines.size() - 1) {
            out += QLatin1Char('\n');
        }
    }
    return out;
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
        disconnect(it.value());
    }
    m_algorithmWatchers.clear();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Algorithm helpers
// ═══════════════════════════════════════════════════════════════════════════════

QVariantList AlgorithmService::availableAlgorithms() const
{
    QVariantList algorithms;
    auto* registry = m_registry;
    for (const QString& id : registry->availableAlgorithms()) {
        PhosphorTiles::TilingAlgorithm* algo = registry->algorithm(id);
        if (algo) {
            QVariantMap algoMap;
            algoMap[QStringLiteral("id")] = id;
            algoMap[QStringLiteral("name")] = algo->name();
            algoMap[QStringLiteral("description")] = algo->description();
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
    auto* registry = m_registry;
    PhosphorTiles::TilingAlgorithm* algo = registry->algorithm(algorithmId);
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
    auto* registry = m_registry;
    PhosphorTiles::TilingAlgorithm* algo = registry->algorithm(algorithmId);
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
    const QString path = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QLatin1Char('/')
        + ScriptedAlgorithmSubdir;
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
    auto* registry = m_registry;
    PhosphorTiles::TilingAlgorithm* algo = registry->algorithm(algorithmId);
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
        disconnect(it.value());
        m_algorithmWatchers.erase(it);
    }
}

void AlgorithmService::watchForAlgorithmRegistration(const QString& expectedId)
{
    // Cancel any existing watcher for this ID to prevent stacking
    cancelAlgorithmWatcher(expectedId);

    m_algorithmWatchers.insert(expectedId,
                               connect(m_registry, &PhosphorTiles::AlgorithmRegistry::algorithmRegistered, this,
                                       [this, expectedId](const QString& registeredId) {
                                           if (registeredId != expectedId) {
                                               return;
                                           }
                                           auto it = m_algorithmWatchers.find(expectedId);
                                           if (it != m_algorithmWatchers.end()) {
                                               disconnect(it.value());
                                               m_algorithmWatchers.erase(it);
                                           }
                                           Q_EMIT algorithmCreated(expectedId);
                                       }));
    // The context object (this) ensures the lambda is not invoked if AlgorithmService
    // is destroyed before the timer fires — QTimer::singleShot with a context guarantees this.
    QTimer::singleShot(10000, this, [this, expectedId]() {
        auto it = m_algorithmWatchers.find(expectedId);
        if (it == m_algorithmWatchers.end()) {
            return;
        }
        disconnect(it.value());
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

    auto* registry = m_registry;
    PhosphorTiles::TilingAlgorithm* algo = registry->algorithm(algorithmId);
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

    auto* registry = m_registry;
    PhosphorTiles::TilingAlgorithm* algo = registry->algorithm(algorithmId);
    if (!algo) {
        Q_EMIT algorithmOperationFailed(PhosphorI18n::tr("That algorithm is no longer registered."));
        return false;
    }

    const QString destDir = userAlgorithmsDir();
    QDir dir(destDir);
    if (!dir.exists())
        dir.mkpath(QStringLiteral("."));

    // Generate unique filename: algorithmId-copy.luau, algorithmId-copy-2.luau, etc.
    const QString baseName = algorithmId + QStringLiteral("-copy");
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
    sourceFile.close();

    // Update name and id in the copy's metadata object — strip all existing " (Copy)" suffixes to avoid accumulation
    const QString newFilename = QFileInfo(destPath).completeBaseName();
    QString baseCopyName = algo->name();
    while (baseCopyName.endsWith(QLatin1String(" (Copy)")))
        baseCopyName.chop(7);
    // Sanitize to prevent metadata injection.
    const QString newName = sanitizeLuauMetadataString(baseCopyName + QStringLiteral(" (Copy)"));
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

    QFile destFile(destPath);
    if (!destFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qCWarning(PlasmaZones::lcCore) << "Failed to write duplicate algorithm file:" << destPath;
        Q_EMIT algorithmOperationFailed(
            PhosphorI18n::tr("Could not write duplicate algorithm file. Check disk space and permissions."));
        return false;
    }
    const QByteArray encoded = content.toUtf8();
    const qint64 written = destFile.write(encoded);
    destFile.close();
    if (written < 0 || written != encoded.size()) {
        qCWarning(PlasmaZones::lcCore) << "Failed to write duplicate algorithm content:" << destPath
                                       << "written=" << written << "expected=" << encoded.size();
        QFile::remove(destPath);
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

    // Atomic-ish replace: copy source → tmp, move existing dest → backup,
    // rename tmp → dest, delete backup on success. On any failure along
    // the way, restore the backup so the user's pre-existing file is never
    // lost. QFile::rename fails if dest exists, so we can't skip the
    // backup step on POSIX like POSIX rename(2) would.
    const QString tmpPath = destPath + QStringLiteral(".tmp");
    const QString backupPath = destPath + QStringLiteral(".bak");
    QFile::remove(tmpPath);
    QFile::remove(backupPath);

    if (!QFile::copy(sourcePath, tmpPath)) {
        Q_EMIT algorithmOperationFailed(PhosphorI18n::tr("Could not copy algorithm file for export."));
        return false;
    }

    const bool destExisted = QFile::exists(destPath);
    if (destExisted && !QFile::rename(destPath, backupPath)) {
        QFile::remove(tmpPath);
        Q_EMIT algorithmOperationFailed(PhosphorI18n::tr("Could not replace existing file at export destination."));
        return false;
    }

    if (!QFile::rename(tmpPath, destPath)) {
        // rename() fails across filesystems — fall back to copy+remove.
        if (!QFile::copy(tmpPath, destPath)) {
            // Restore the original file so the caller's dest is never lost.
            if (destExisted) {
                QFile::rename(backupPath, destPath);
            }
            QFile::remove(tmpPath);
            Q_EMIT algorithmOperationFailed(PhosphorI18n::tr("Could not write to export destination."));
            return false;
        }
        if (!QFile::remove(tmpPath)) {
            qCWarning(PlasmaZones::lcCore) << "Failed to clean up temporary export file:" << tmpPath;
        }
    }

    if (destExisted) {
        QFile::remove(backupPath);
    }
    return true;
}

QString AlgorithmService::createNewAlgorithm(const QString& name, const QString& baseTemplate, bool supportsMasterCount,
                                             bool supportsSplitRatio, bool producesOverlappingZones,
                                             bool supportsMemory)
{
    // Sanitize name to a filename: lowercase, replace non-alphanumeric (except hyphens) with
    // hyphens, collapse multiple hyphens, strip leading/trailing hyphens
    QString filename = name.trimmed().toLower();
    static const QRegularExpression nonAlnum(QStringLiteral("[^a-z0-9-]"));
    filename.replace(nonAlnum, QStringLiteral("-"));
    static const QRegularExpression multiHyphen(QStringLiteral("-{2,}"));
    filename.replace(multiHyphen, QStringLiteral("-"));
    static const QRegularExpression leadTrailHyphen(QStringLiteral("^-|-$"));
    filename.replace(leadTrailHyphen, QString());
    static const QRegularExpression leadDigits(QStringLiteral("^[0-9]+"));
    filename.replace(leadDigits, QString());
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

    // SPDX header — use current year and a placeholder author
    const int currentYear = QDate::currentDate().year();
    const QString header = QStringLiteral("-- SPDX-FileCopyrightText: ") + QString::number(currentYear)
        + QStringLiteral(" <your name>\n") + QStringLiteral("-- SPDX-License-Identifier: GPL-3.0-or-later\n");

    // Metadata table — strip newlines/quotes to prevent injection.
    const QString sanitizedDisplayName = sanitizeLuauMetadataString(name.trimmed());
    const QString metadataBlock = buildLuauMetadata(sanitizedDisplayName, filename, producesOverlappingZones,
                                                    supportsMasterCount, supportsSplitRatio, supportsMemory);

    // Start from a base template by reusing its module locals + tile and
    // swapping in our own SPDX header + metadata table.
    bool foundTemplate = false;
    if (baseTemplate != QLatin1String("blank") && !baseTemplate.isEmpty()) {
        const QString templateFile =
            QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                                   ScriptedAlgorithmSubdir + QLatin1Char('/') + baseTemplate + QStringLiteral(".luau"));

        if (!templateFile.isEmpty()) {
            QFile file(templateFile);
            if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                const QString templateContent = QString::fromUtf8(file.readAll());
                file.close();

                const QString spliced = spliceLuauTemplate(templateContent, header, metadataBlock);
                if (spliced.isEmpty()) {
                    qCWarning(PlasmaZones::lcCore)
                        << "createNewAlgorithm: template" << baseTemplate
                        << "has no recognisable `metadata = { ... }` block — using fallback body.";
                } else {
                    content = spliced;
                    foundTemplate = true;
                }
            } else {
                qCWarning(PlasmaZones::lcCore) << "createNewAlgorithm: could not read template file:" << templateFile;
            }
        }
    }

    if (!foundTemplate) {
        // Blank scaffold: a self-contained pluau.algorithm module the user edits.
        content = header + QStringLiteral("\nlocal pluau = pluau\n\nreturn pluau.algorithm {\n") + metadataBlock
            + QStringLiteral("\n\n") + QStringLiteral("    tile = function(ctx)\n")
            + QStringLiteral("        if ctx.windowCount <= 0 then return {} end\n")
            + QStringLiteral("        return pluau.fillArea(ctx.area, ctx.windowCount)\n")
            + QStringLiteral("    end,\n}\n");
    }

    // Write the file
    QFile outFile(destPath);
    if (!outFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qCWarning(PlasmaZones::lcCore) << "Failed to write algorithm file:" << destPath;
        Q_EMIT algorithmOperationFailed(
            PhosphorI18n::tr("Could not write algorithm file. Check disk space and permissions."));
        return QString();
    }
    const QByteArray encoded = content.toUtf8();
    const qint64 written = outFile.write(encoded);
    outFile.close();
    if (written < 0 || written != encoded.size()) {
        qCWarning(PlasmaZones::lcCore) << "Failed to write algorithm content:" << destPath << "written=" << written
                                       << "expected=" << encoded.size();
        QFile::remove(destPath);
        Q_EMIT algorithmOperationFailed(
            PhosphorI18n::tr("Could not write algorithm file. Check disk space and permissions."));
        return QString();
    }

    watchForAlgorithmRegistration(filename);
    return filename;
}

} // namespace PlasmaZones
