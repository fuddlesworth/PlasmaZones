// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Layout + algorithm CRUD methods for SettingsController.
//
// Split out of settingscontroller.cpp to keep that file under the
// 800-line cap (see CLAUDE.md). All methods here are members of
// PlasmaZones::SettingsController and use its private state — they
// live in a separate translation unit but compile into the same
// `plasmazones-settings` executable. No API changes.

#include "settingscontroller.h"

#include "../common/layoutpreviewserialize.h"
#include "../config/configdefaults.h"
#include "../core/geometryutils.h"
#include "../core/logging.h"
#include "../core/utils.h"
#include "../phosphor_i18n.h"
#include "dbusutils.h"

// Most of the dependency graph (Settings, PhosphorZones layouts, daemon
// D-Bus helpers, page controllers) is reached transitively through
// settingscontroller.h. Only headers used directly here are listed.
#include <PhosphorLayoutApi/LayoutId.h>
#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include <PhosphorZones/LayoutComputeService.h>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QUrl>

namespace PlasmaZones {

// ═══════════════════════════════════════════════════════════════════════════════
// PhosphorZones::Layout management (D-Bus to daemon, no KCM PhosphorZones::LayoutRegistry class needed)
// ═══════════════════════════════════════════════════════════════════════════════

void SettingsController::scheduleLayoutLoad()
{
    m_layoutLoadTimer.start();
}

void SettingsController::loadLayoutsAsync()
{
    // Force-reload the in-process PhosphorZones::LayoutRegistry from disk before reading.
    // The LayoutManager's QFileSystemWatcher catches most disk changes,
    // but Qt's QFSW has known misses on cross-process atomic-rename
    // writes (the daemon writes layouts via QSaveFile, which creates a
    // new inode the watcher may not bind to in time). Belt-and-suspenders:
    // every D-Bus layout signal that triggers loadLayoutsAsync (layoutCreated
    // / layoutDeleted / layoutChanged / layoutPropertyChanged /
    // layoutListChanged — see the connect block in the ctor) ALSO forces
    // an explicit reload here, so the local-source preview path stays
    // strictly in sync with the daemon's view regardless of which file-
    // event path fires first.
    if (m_localLayoutManager) {
        m_localLayoutManager->loadLayouts();
    }

    // Step 1: instant paint from the in-process composite source is handled
    // by the ctor-wired PhosphorZones::LayoutRegistry::layoutsChanged lambda (see ~line 180
    // — it calls recalcLocalLayouts() + swaps m_layouts from localLayoutPreviews()
    // and emits layoutsChanged). loadLayouts() above triggers that signal
    // synchronously when the disk contents actually changed, so the instant-paint
    // path runs without a duplicate recalc/emit here.

    // Step 2: async D-Bus call to pick up daemon-side enrichment
    // (hasSystemOrigin / hiddenFromSelector / defaultOrder / allow-lists)
    // that the local composite can't know about. On reply the enriched
    // list replaces m_layouts; if the call errors we keep the local
    // previews from Step 1 visible rather than blanking the page.
    // Gate the local-path layoutsChanged emit (see the ctor-wired lambda
    // on PhosphorZones::LayoutRegistry::layoutsChanged). The reply lambda clears this
    // unconditionally so any subsequent local-only refresh (daemon down)
    // emits as usual.
    m_awaitingDaemonLayouts = true;
    auto* watcher = new QDBusPendingCallWatcher(
        PhosphorProtocol::ClientHelpers::asyncCall(PhosphorProtocol::Service::Interface::LayoutRegistry,
                                                   QStringLiteral("getLayoutList")),
        this);

    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        // Clear the gate first so any local-path emit that arrives after
        // an error reply (or after a successful one) runs normally.
        m_awaitingDaemonLayouts = false;

        QDBusPendingReply<QStringList> reply = *w;
        if (reply.isError()) {
            qCWarning(lcCore) << "Failed to load layouts (D-Bus):" << reply.error().message()
                              << "— keeping local manual-layout previews from Step 1.";
            // Drop any pending select-after-create id so a future successful
            // reload doesn't emit layoutAdded() for a stale id that the user
            // has already navigated away from (or that was deleted from
            // another session).
            m_pendingSelectLayoutId.clear();
            // Re-emit the local view: the file-watcher emit that ran
            // while m_awaitingDaemonLayouts was true was suppressed
            // (see settingscontroller.cpp's connect lambda). Without
            // this re-emit, an immediate user-visible refresh after a
            // daemon error waits for the next disk change.
            Q_EMIT layoutsChanged();
            return;
        }

        QVariantList newLayouts;
        const QStringList layoutJsonList = reply.value();
        for (const QString& layoutJson : layoutJsonList) {
            QJsonDocument doc = QJsonDocument::fromJson(layoutJson.toUtf8());
            if (!doc.isNull() && doc.isObject()) {
                newLayouts.append(doc.object().toVariantMap());
            }
        }

        SettingsController::sortMergedLayoutList(newLayouts);
        // Skip the swap+emit when the daemon reply matches what we
        // already have. Five debounced D-Bus signals
        // (layoutCreated/Deleted/Changed/PropertyChanged/ListChanged)
        // coalesce into one reload here, and identical payloads
        // otherwise force every QML binding (LayoutComboBox model,
        // monitor overview, picker dialogs) to recompute. Skipping
        // the emit also avoids re-firing the pending-select path
        // for an already-loaded id.
        if (m_layouts != newLayouts) {
            m_layouts = newLayouts;
            Q_EMIT layoutsChanged();
        }

        // Emit pending select after model is populated
        if (!m_pendingSelectLayoutId.isEmpty()) {
            QString id = m_pendingSelectLayoutId;
            m_pendingSelectLayoutId.clear();
            Q_EMIT layoutAdded(id);
        }
    });
}

// ── Daemon-independent layout previews (PhosphorZones::ILayoutSource) ───────
// See header doc for why these exist. Both helpers route through the shared
// toVariantMap so settings + editor + future consumers emit the
// same QML-compatible shape (drop-in replacement for the legacy m_layouts
// produced by LayoutAdaptor::getLayoutList).

QVariantList SettingsController::localLayoutPreviews() const
{
    QVariantList list;
    if (!m_localSources.composite()) {
        return list;
    }
    const auto previews = m_localSources.composite()->availableLayouts();
    list.reserve(previews.size());
    for (const auto& preview : previews) {
        list.append(toVariantMap(preview));
    }
    return list;
}

void SettingsController::recalcLocalLayouts()
{
    if (!m_localLayoutManager) {
        return;
    }
    QScreen* primary = Utils::primaryScreen();
    if (!primary) {
        return;
    }
    for (PhosphorZones::Layout* layout : m_localLayoutManager->layouts()) {
        if (!layout) {
            continue;
        }
        // Settings app is a separate process without a daemon ScreenManager — pass
        // nullptr and accept the Qt-availableGeometry fallback (this preview code
        // path doesn't need VS-aware sub-regions).
        PhosphorZones::LayoutComputeService::recalculateSync(
            layout, GeometryUtils::effectiveScreenGeometry(nullptr, layout, primary));
    }
}

QVariantMap SettingsController::localLayoutPreview(const QString& id, int windowCount)
{
    if (id.isEmpty() || !m_localSources.composite()) {
        return {};
    }
    const auto preview = m_localSources.composite()->previewAt(id, windowCount);
    if (preview.id.isEmpty()) {
        return {};
    }
    return toVariantMap(preview);
}

void SettingsController::createNewLayout()
{
    createNewLayout(PhosphorI18n::tr("New Layout"), QStringLiteral("custom"), -1, true);
}

bool SettingsController::createNewLayout(const QString& name, const QString& type, int aspectRatioClass,
                                         bool openInEditor)
{
    QString sanitizedName = name.trimmed();
    if (sanitizedName.isEmpty())
        sanitizedName = PhosphorI18n::tr("New Layout");

    const QString layoutType = type.isEmpty() ? QStringLiteral("custom") : type;

    QDBusMessage reply = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                                QStringLiteral("createLayout"), {sanitizedName, layoutType});

    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
        QString newLayoutId = reply.arguments().first().toString();
        if (!newLayoutId.isEmpty()) {
            if (aspectRatioClass >= 0) {
                QDBusMessage arReply = DaemonDBus::callDaemon(
                    QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                    QStringLiteral("setLayoutAspectRatioClass"), {newLayoutId, aspectRatioClass});
                if (arReply.type() == QDBusMessage::ErrorMessage) {
                    qCWarning(lcCore) << "setLayoutAspectRatioClass failed:" << arReply.errorMessage();
                    // Surface to QML — the layout was created but its aspect-ratio
                    // class wasn't applied. Mirror the other layout-mutation
                    // paths (setLayoutHidden / setLayoutAutoAssign / standalone
                    // setLayoutAspectRatio) which all emit layoutOperationFailed
                    // on partial failure.
                    Q_EMIT layoutOperationFailed(PhosphorI18n::tr("Layout created, but aspect-ratio class could not be "
                                                                  "applied: %1")
                                                     .arg(arReply.errorMessage()));
                }
            }
            if (openInEditor) {
                editLayout(newLayoutId);
            }
            m_pendingSelectLayoutId = newLayoutId;
            scheduleLayoutLoad();
            return true;
        }
        // Daemon returned a reply but with an empty layout ID
        Q_EMIT layoutOperationFailed(
            PhosphorI18n::tr("Could not create the layout. The daemon returned an empty layout ID."));
        scheduleLayoutLoad();
        return false;
    }
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qCWarning(lcCore) << "createNewLayout failed:" << reply.errorMessage();
        Q_EMIT layoutOperationFailed(reply.errorMessage());
    } else {
        Q_EMIT layoutOperationFailed(PhosphorI18n::tr("Could not create the layout. The daemon may not be running."));
    }
    // Still refresh — the daemon may have partially processed the request
    scheduleLayoutLoad();
    return false;
}

void SettingsController::deleteLayout(const QString& layoutId)
{
    // Drop any pending select-after-create id — if the user deletes
    // a layout that was just created (and is still in the
    // create→reply→select pipeline), the trailing layoutAdded() emit
    // would name an id the user just removed.
    if (m_pendingSelectLayoutId == layoutId)
        m_pendingSelectLayoutId.clear();
    QDBusMessage reply = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                                QStringLiteral("deleteLayout"), {layoutId});
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qCWarning(lcCore) << "deleteLayout failed:" << reply.errorMessage();
        Q_EMIT layoutOperationFailed(PhosphorI18n::tr("Could not delete layout: %1").arg(reply.errorMessage()));
    }
    scheduleLayoutLoad();
}

void SettingsController::duplicateLayout(const QString& layoutId)
{
    QDBusMessage reply = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                                QStringLiteral("duplicateLayout"), {layoutId});
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
        QString newId = reply.arguments().first().toString();
        if (!newId.isEmpty()) {
            m_pendingSelectLayoutId = newId;
        }
    } else if (reply.type() == QDBusMessage::ErrorMessage) {
        qCWarning(lcCore) << "duplicateLayout failed:" << reply.errorMessage();
        Q_EMIT layoutOperationFailed(PhosphorI18n::tr("Could not duplicate layout: %1").arg(reply.errorMessage()));
        // Clear so a stale create's reply doesn't accidentally land
        // a layoutAdded() for an id this failed duplicate never
        // produced — the user has no other context for the emit.
        m_pendingSelectLayoutId.clear();
    }
    scheduleLayoutLoad();
}

QVariantMap SettingsController::physicalScreenResolution(const QString& screenId) const
{
    QVariantMap result;
    QScreen* screen = PhosphorScreens::ScreenIdentity::findByIdOrName(screenId);
    if (screen) {
        result[QStringLiteral("width")] = screen->geometry().width();
        result[QStringLiteral("height")] = screen->geometry().height();
    }
    return result;
}

void SettingsController::editLayout(const QString& layoutId)
{
    PhosphorProtocol::ClientHelpers::sendOneWay(PhosphorProtocol::Service::Interface::LayoutRegistry,
                                                QStringLiteral("openEditorForLayoutOnScreen"), {layoutId, QString()});
}

void SettingsController::editLayoutOnScreen(const QString& layoutId, const QString& screenId)
{
    if (layoutId.isEmpty() || screenId.isEmpty())
        return;
    PhosphorProtocol::ClientHelpers::sendOneWay(PhosphorProtocol::Service::Interface::LayoutRegistry,
                                                QStringLiteral("openEditorForLayoutOnScreen"), {layoutId, screenId});
}

void SettingsController::openLayoutsFolder()
{
    const QString path = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QLatin1Char('/')
        + ConfigDefaults::layoutsSubdir();
    QDir dir(path);
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

// Path validation for import/export dialogs. Promoted out of an
// anonymous namespace and into a static member so the cross-TU
// `settingscontroller_session.cpp` (import/export-all-settings) can
// reuse the same defence-in-depth path sanitiser.
//
// User-driven calls come from QFileDialog (already canonical), but the
// methods are Q_INVOKABLE so a compromised page resource — or a future
// `--page` CLI flag — could pass arbitrary strings. Defence-in-depth:
//   * Reject paths containing NUL (POSIX path corruption / D-Bus
//     marshalling break).
//   * Reject paths beginning with `~` (untilde resolution is QML's
//     responsibility, not ours).
//   * Reject relative-traversal segments AFTER cleanPath so
//     `/home/x/../../etc/passwd` doesn't survive normalisation, AND
//     also reject leading-`..` shapes (`../foo`, plain `..`) that
//     cleanPath preserves verbatim — those are filesystem-root-escape
//     attempts that the QFileDialog path never produces.
// Returns the lexically cleaned absolute path, or empty on rejection. Caller
// logs. Symlinks are NOT resolved, so a caller needing a containment check
// must canonicalize the result itself (see AlgorithmService::deleteAlgorithm).
QString SettingsController::sanitizeIOPath(const QString& raw)
{
    if (raw.isEmpty() || raw.contains(QLatin1Char('\0'))) {
        return {};
    }
    if (raw.startsWith(QLatin1Char('~'))) {
        return {};
    }
    const QString clean = QDir::cleanPath(raw);
    // cleanPath resolves `..`; reject if the cleaned form still contains
    // `..` as a segment (which means the path started outside any
    // resolvable filesystem root, e.g. relative `../../etc/passwd`).
    if (clean.contains(QStringLiteral("/../")) || clean.endsWith(QStringLiteral("/.."))) {
        return {};
    }
    // cleanPath doesn't strip leading-`..` shapes (`..`, `../foo`,
    // `../../bar`) — they're still relative-traversal escape attempts
    // even after canonicalisation.
    if (clean == QStringLiteral("..") || clean.startsWith(QStringLiteral("../"))) {
        return {};
    }
    // Require absolute paths. The Q_INVOKABLE entry points are reached
    // from QFileDialog (which always yields an absolute URL) and from
    // the test suite; relative paths shouldn't surface here. A
    // compromised QML page or a future `--page` CLI flag passing a
    // bare `report.json` would otherwise resolve relative to the
    // settings-app cwd and read/write outside the user's intent.
    if (!clean.startsWith(QLatin1Char('/'))) {
        return {};
    }
    return clean;
}

void SettingsController::importLayout(const QString& filePath)
{
    const QString safe = sanitizeIOPath(filePath);
    if (safe.isEmpty()) {
        qCWarning(lcCore) << "importLayout: refusing unsafe path" << filePath;
        Q_EMIT layoutOperationFailed(PhosphorI18n::tr("That file path is not allowed."));
        return;
    }
    QDBusMessage reply = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                                QStringLiteral("importLayout"), {safe});
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
        QString newLayoutId = reply.arguments().first().toString();
        if (!newLayoutId.isEmpty()) {
            m_pendingSelectLayoutId = newLayoutId;
        }
    } else if (reply.type() == QDBusMessage::ErrorMessage) {
        // Surface the daemon's rejection (corrupt JSON, permission denied,
        // layout-id collision, etc.) — without this branch the page
        // silently refreshes and the user has no feedback, mirroring the
        // pattern that Pass-4 hardening already applied to setLayoutHidden
        // / setLayoutAutoAssign / setLayoutAspectRatio.
        qCWarning(lcCore) << "importLayout failed:" << reply.errorMessage();
        Q_EMIT layoutOperationFailed(PhosphorI18n::tr("Failed to import layout: %1").arg(reply.errorMessage()));
    }
    scheduleLayoutLoad();
}

void SettingsController::exportLayout(const QString& layoutId, const QString& filePath)
{
    if (layoutId.isEmpty())
        return;
    const QString safe = sanitizeIOPath(filePath);
    if (safe.isEmpty()) {
        qCWarning(lcCore) << "exportLayout: refusing unsafe path" << filePath;
        Q_EMIT layoutOperationFailed(PhosphorI18n::tr("That export path is not allowed."));
        return;
    }
    PhosphorProtocol::ClientHelpers::sendOneWay(PhosphorProtocol::Service::Interface::LayoutRegistry,
                                                QStringLiteral("exportLayout"), {layoutId, safe});
}

void SettingsController::setLayoutHidden(const QString& layoutId, bool hidden)
{
    if (layoutId.isEmpty())
        return;
    QDBusMessage reply = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                                QStringLiteral("setLayoutHidden"), {layoutId, hidden});
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qCWarning(lcCore) << "setLayoutHidden failed for" << layoutId << ":" << reply.errorMessage();
        Q_EMIT layoutOperationFailed(
            PhosphorI18n::tr("Failed to update layout visibility: %1").arg(reply.errorMessage()));
    }
    scheduleLayoutLoad();
}

void SettingsController::setLayoutAutoAssign(const QString& layoutId, bool enabled)
{
    if (layoutId.isEmpty())
        return;
    QDBusMessage reply = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                                QStringLiteral("setLayoutAutoAssign"), {layoutId, enabled});
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qCWarning(lcCore) << "setLayoutAutoAssign failed for" << layoutId << ":" << reply.errorMessage();
        Q_EMIT layoutOperationFailed(PhosphorI18n::tr("Failed to update auto-assign: %1").arg(reply.errorMessage()));
    }
    scheduleLayoutLoad();
}

void SettingsController::setLayoutAspectRatio(const QString& layoutId, int aspectRatioClass)
{
    if (layoutId.isEmpty())
        return;
    QDBusMessage reply =
        DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                               QStringLiteral("setLayoutAspectRatioClass"), {layoutId, aspectRatioClass});
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qCWarning(lcCore) << "setLayoutAspectRatio failed for" << layoutId << ":" << reply.errorMessage();
        Q_EMIT layoutOperationFailed(PhosphorI18n::tr("Failed to update aspect ratio: %1").arg(reply.errorMessage()));
    }
    scheduleLayoutLoad();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Algorithm helpers
// ═══════════════════════════════════════════════════════════════════════════════

// All bodies moved to AlgorithmService; SettingsController::* methods below
// are 1-line Q_INVOKABLE forwarders so QML's entry points stay stable.

QVariantList SettingsController::availableAlgorithms() const
{
    return m_algorithmService->availableAlgorithms();
}

QVariantList SettingsController::generateAlgorithmPreview(const QString& algorithmId, int windowCount,
                                                          double splitRatio, int masterCount,
                                                          const QVariantMap& customParams) const
{
    return m_algorithmService->generateAlgorithmPreview(algorithmId, windowCount, splitRatio, masterCount,
                                                        customParams);
}

QVariantList SettingsController::generateAlgorithmDefaultPreview(const QString& algorithmId) const
{
    return m_algorithmService->generateAlgorithmDefaultPreview(algorithmId);
}

void SettingsController::openAlgorithmsFolder()
{
    m_algorithmService->openAlgorithmsFolder();
}

bool SettingsController::importAlgorithm(const QString& filePath)
{
    const QString safe = sanitizeIOPath(filePath);
    if (safe.isEmpty()) {
        qCWarning(lcCore) << "importAlgorithm: refusing unsafe path" << filePath;
        Q_EMIT algorithmOperationFailed(PhosphorI18n::tr("That file path is not allowed."));
        return false;
    }
    return m_algorithmService->importAlgorithm(safe);
}

QString SettingsController::algorithmIdFromLayoutId(const QString& layoutId)
{
    return PhosphorLayout::LayoutId::isAutotile(layoutId) ? PhosphorLayout::LayoutId::extractAlgorithmId(layoutId)
                                                          : layoutId;
}

void SettingsController::openAlgorithm(const QString& algorithmId)
{
    m_algorithmService->openAlgorithm(algorithmId);
}

void SettingsController::openLayoutFile(const QString& layoutId)
{
    // Resolve the layout's real on-disk path via the registry. Bundled
    // layouts ship under human-readable filenames (fibonacci.json,
    // grid-3x2.json, ...), not UUID-named files, so reconstructing the
    // filename from the id (as openLayoutFile's locate-by-UUID fallback does)
    // never finds them. Layout::sourcePath() is the authoritative path set
    // when the registry loaded the file, and is correct for both bundled and
    // user layouts.
    const QUuid uuid(layoutId);
    if (!uuid.isNull() && m_localLayoutManager) {
        if (PhosphorZones::Layout* layout = m_localLayoutManager->layoutById(uuid)) {
            const QString path = layout->sourcePath();
            if (!path.isEmpty() && QFile::exists(path)) {
                QDesktopServices::openUrl(QUrl::fromLocalFile(path));
                return;
            }
        }
    }

    // Fallback: UUID-named lookup for a freshly created user layout not yet
    // reflected in the in-process registry.
    m_algorithmService->openLayoutFile(layoutId);
}

bool SettingsController::deleteAlgorithm(const QString& algorithmId)
{
    return m_algorithmService->deleteAlgorithm(algorithmId);
}

bool SettingsController::duplicateAlgorithm(const QString& algorithmId)
{
    return m_algorithmService->duplicateAlgorithm(algorithmId);
}

bool SettingsController::exportAlgorithm(const QString& algorithmId, const QString& destPath)
{
    const QString safe = sanitizeIOPath(destPath);
    if (safe.isEmpty()) {
        qCWarning(lcCore) << "exportAlgorithm: refusing unsafe path" << destPath;
        Q_EMIT algorithmOperationFailed(PhosphorI18n::tr("That export path is not allowed."));
        return false;
    }
    return m_algorithmService->exportAlgorithm(algorithmId, safe);
}

QString SettingsController::createNewAlgorithm(const QString& name, const QString& baseTemplate,
                                               const QVariantMap& capabilities)
{
    return m_algorithmService->createNewAlgorithm(name, baseTemplate, capabilities);
}
} // namespace PlasmaZones
