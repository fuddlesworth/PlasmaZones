// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Layout + algorithm CRUD methods for SettingsController.
//
// All methods here are members of PlasmaZones::SettingsController and use its
// private state. They live in a separate translation unit from
// settingscontroller.cpp but compile into the same `plasmazones-settings`
// executable. No API changes.

#include "settingscontroller.h"

#include "common/layoutpreviewserialize.h"
#include "config/configdefaults.h"
#include "core/utils/geometryutils.h"
#include "core/platform/logging.h"
#include "core/utils/utils.h"
#include "phosphor_i18n.h"
#include "settings/rules/ruleauthoring.h"
#include "settings/utils/dbusutils.h"

// Most of the dependency graph (Settings, PhosphorZones layouts, daemon
// D-Bus helpers, page controllers) is reached transitively through
// settingscontroller.h. Only headers used directly here are listed.
#include <PhosphorLayoutApi/LayoutId.h>
#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include <PhosphorZones/LayoutComputeService.h>
#include <PhosphorZones/ScrollingTemplate.h>
#include <PhosphorZones/ScrollingTemplateStore.h>

#include <QDBusMessage>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>

namespace PlasmaZones {

namespace {
/// Upper bound on a scrolling-template file this app will read into memory.
/// Matches the profile-tree bound in the settings D-Bus adaptor.
constexpr qint64 kMaxTemplateFileBytes = 64 * 1024;
} // namespace

// ═══════════════════════════════════════════════════════════════════════════════
// PhosphorZones::Layout management (D-Bus to daemon, no KCM PhosphorZones::LayoutRegistry class needed)
// ═══════════════════════════════════════════════════════════════════════════════

void SettingsController::scheduleLayoutLoad()
{
    m_layoutLoadTimer.start();
}

// m_pendingDaemonLayoutCalls counts the getLayoutList round-trips in flight.
// Non-zero holds back the local-path layout view, which is built from
// LayoutPreview and carries no daemon-side enrichment (hasSystemOrigin /
// hiddenFromSelector / defaultOrder / allow-lists). Publishing it mid-flight
// stripped that off every entry for the length of the round trip, so a
// hidden/auto-assign toggle visibly reverted on the card just toggled and every
// listing page rebuilt its whole model twice per mutation.
//
// This function increments it BEFORE reloading the local registry, so the gate
// covers that reload's synchronous emit as well as any landing before the reply.
// A COUNT, not a flag: the debounce is 50 ms and a reply costs the daemon a full
// rescan, so a burst readily puts two calls in flight, and a flag would be
// cleared by the first reply while the second was still pending. Decremented at
// the reply lambda's entry, ahead of any early-return. Startup's direct
// loadLayouts() runs ungated.
//
// m_withheldLocalLayouts is the local view withheld under that gate, adopted by
// the last reply when the daemon turns out to be unreachable, so a failed round
// trip does not leave the page painted from before the change. Engaged (not
// merely non-empty) marks a withheld view, so a genuine wipe to zero layouts is
// still adopted. Any SUCCESSFUL reply clears it: enriched data supersedes it,
// and a later error must not downgrade the page back to it.
void SettingsController::loadLayoutsAsync()
{
    // Force-reload the in-process PhosphorZones::LayoutRegistry from disk
    // before reading. The registry watches nothing — an explicit
    // loadLayouts() is the only thing that rescans disk — so this call is
    // what carries a daemon-side layout write into the local preview path.
    // Every D-Bus layout signal that triggers loadLayoutsAsync (layoutCreated
    // / layoutDeleted / layoutChanged / layoutPropertyChanged /
    // layoutListChanged — see
    // settingscontroller_dbuswire.cpp::wireDaemonSubscriptions) relies on it.
    // It is not a redundant backup for a file watcher.
    // Count this call in BEFORE the local reload, not after: loadLayouts()
    // drives the PhosphorZones::LayoutRegistry::layoutsChanged lambda wired in
    // settingscontroller.cpp's ctor synchronously, and that lambda's view of the
    // world is the in-process composite, which carries none of the daemon-side
    // enrichment (hasSystemOrigin / hiddenFromSelector / defaultOrder /
    // allow-lists) the reply below brings back. Publishing it first painted
    // every card without its enrichment for the length of the round trip — a
    // hidden or auto-assign toggle visibly snapped back before it took — and
    // made every listing page tear down and rebuild its entire model twice for
    // one mutation. The lambda holds that view in m_withheldLocalLayouts
    // instead, and the last reply in flight adopts it only if the daemon turns
    // out to be unreachable.
    ++m_pendingDaemonLayoutCalls;
    // Whatever an earlier cycle held back is superseded by the reload below,
    // which re-stashes the freshly-scanned view through the same lambda.
    m_withheldLocalLayouts.reset();
    if (m_localLayoutManager) {
        m_localLayoutManager->loadLayouts();
    }

    // Pick up the daemon-side enrichment the local composite can't know about.
    // On reply the enriched list replaces m_layouts. If the call errors and no
    // newer one is in flight, the local view withheld above is adopted rather
    // than leaving the page as it was before the change.
    auto* watcher = new QDBusPendingCallWatcher(
        PhosphorProtocol::ClientHelpers::asyncCall(PhosphorProtocol::Service::Interface::LayoutRegistry,
                                                   QStringLiteral("getLayoutList")),
        this);

    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        // Count this reply out first, before any early-return, so the local
        // path resumes emitting once nothing is in flight. Guarded rather than
        // a bare decrement: a stray second finished() must not drive the count
        // negative and un-gate the local path while a call is still pending.
        if (m_pendingDaemonLayoutCalls > 0) {
            --m_pendingDaemonLayoutCalls;
        }
        const bool lastInFlight = m_pendingDaemonLayoutCalls == 0;

        QDBusPendingReply<QStringList> reply = *w;
        if (reply.isError()) {
            if (!lastInFlight) {
                // A newer getLayoutList is still in flight and owns the refresh.
                // Publishing the withheld local view here would strip the
                // enrichment off every card for the rest of that round trip,
                // which is the whole failure this withholding exists to avoid.
                qCWarning(lcCore) << "Failed to load layouts (D-Bus):" << reply.error().message()
                                  << "— a newer request is still in flight, leaving the refresh to it.";
                return;
            }
            qCWarning(lcCore) << "Failed to load layouts (D-Bus):" << reply.error().message()
                              << "— falling back to the local manual-layout previews.";
            // Adopt the withheld local view. It is the only refresh the user
            // gets on this path, and it is newer than m_layouts by definition:
            // the reload that produced it is what this call was answering. It
            // is absent when a successful reply already published enriched data
            // for this burst, in which case the page is current and there is
            // nothing to fall back to.
            std::optional<QVariantList> withheldLocal;
            withheldLocal.swap(m_withheldLocalLayouts);
            if (withheldLocal && m_layouts != *withheldLocal) {
                m_layouts = std::move(*withheldLocal);
            }
            // Drop any pending select-after-create id so a future successful
            // reload doesn't emit layoutAdded() for a stale id that the user
            // has already navigated away from (or that was deleted from
            // another session).
            m_pendingSelectLayoutId.clear();
            // Emit unconditionally, even when the withheld view matched what we
            // already had: local emits that landed inside the gate window were
            // held back too (see settingscontroller.cpp's connect lambda), and
            // without this the page waits for the next disk change to refresh.
            Q_EMIT layoutsChanged();
            return;
        }

        // Enriched data supersedes anything the local path held back, so a
        // later error in this burst cannot downgrade the page to the
        // un-enriched view.
        m_withheldLocalLayouts.reset();

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
        // monitor overview, picker dialogs) to recompute. The
        // pending-select block below runs regardless of this guard —
        // a reveal must fire even when the reply matched.
        //
        // Safe even when a local emit was withheld during the round trip: the
        // daemon is the authority on the layout list, and every trigger of this
        // reload is either a daemon broadcast or a mutation that went through
        // the daemon, so a reply identical to m_layouts means nothing changed
        // for the user to see. Publishing the withheld view instead would only
        // repaint the same entries minus their enrichment.
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
//
// Loads the on-disk layouts via an in-process LayoutRegistry +
// ZonesLayoutSource so QML preview paths render even when the daemon is down
// (early launch, crash). Paints directly at startup, before the first
// getLayoutList goes out; after that loadLayoutsAsync holds it back
// (m_withheldLocalLayouts) and publishes it only when the daemon cannot
// answer, because it lacks the enrichment described below.
//
// Returns the projection produced by PlasmaZones::toVariantMap. That is the
// SAME projection, key for key, that the D-Bus side emits via toJson — the two
// differ only in container type (QVariantMap vs QJsonObject). What differs is
// the daemon's LayoutAdaptor::getLayoutList, which adds an enrichment layer on
// top (hasSystemOrigin / hiddenFromSelector / defaultOrder / allow-lists) from
// Layout state that LayoutPreview does not carry. So the list this returns is a
// strict SUBSET of the D-Bus list: any consumer reading an enrichment-only key
// off these previews gets `undefined`, not `false`. See
// src/common/layoutpreviewserialize.h.
//
// @note Autotile preview-parameter drift: the local AlgorithmRegistry is
// independent of the daemon's (see m_localAlgorithmRegistry), so daemon-side
// tuning (master count, split ratio, per-algorithm settings) does NOT propagate
// here — fallback previews render with built-in defaults. When the daemon is
// up, D-Bus carries the tuned previews; the fallback is only a "daemon is down"
// safety net.
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
    // Clamp client-side to the daemon's cap so the pending-select name matches
    // what the daemon actually stores (it silently truncates at this length).
    sanitizedName = clampName(sanitizedName);

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
    if (m_pendingSelectLayoutId == layoutId) {
        m_pendingSelectLayoutId.clear();
    }
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
        } else {
            // A refusal carried in a ReplyMessage minted nothing; a stale
            // create's id must not stay armed (same reasoning as the error
            // branch below).
            m_pendingSelectLayoutId.clear();
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

void SettingsController::onScrollingTemplatesChanged()
{
    if (m_localTemplateStore) {
        m_localTemplateStore->loadTemplates();
    }
    scheduleLayoutLoad();
}

void SettingsController::importScrollingTemplate(const QString& filePath)
{
    // Any exit below without a fresh mint must not leave an earlier create's
    // reveal id armed (duplicateLayout's shape), so drop it up front.
    m_pendingSelectLayoutId.clear();
    const QString safe = Utils::sanitizeIOPath(filePath);
    if (safe.isEmpty()) {
        qCWarning(lcCore) << "importScrollingTemplate: refusing unsafe path" << filePath;
        Q_EMIT layoutOperationFailed(PhosphorI18n::tr("That file path is not allowed."));
        return;
    }
    // A fifo or device node reports size 0, so the byte bound below would
    // pass and readAll() would block or grow unboundedly on the UI thread;
    // only regular files are templates.
    if (!QFileInfo(safe).isFile()) {
        qCWarning(lcCore) << "importScrollingTemplate: not a regular file" << safe;
        Q_EMIT layoutOperationFailed(PhosphorI18n::tr("That file is not a scrolling template this app can read."));
        return;
    }
    QFile file(safe);
    if (!file.open(QIODevice::ReadOnly)) {
        Q_EMIT layoutOperationFailed(PhosphorI18n::tr("Could not read the template file."));
        return;
    }
    // Bound the read before readAll() pulls the whole file into memory: this
    // path takes an arbitrary user-picked file, and a template is a small JSON
    // document (the same 64 KiB bound the settings adaptor puts on a profile
    // tree). Anything larger is not a template, so it fails as a bad file.
    if (file.size() > kMaxTemplateFileBytes) {
        qCWarning(lcCore) << "importScrollingTemplate: refusing oversized file" << safe << file.size();
        Q_EMIT layoutOperationFailed(PhosphorI18n::tr("That file is not a scrolling template this app can read."));
        return;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        Q_EMIT layoutOperationFailed(PhosphorI18n::tr("That file is not a scrolling template this app can read."));
        return;
    }
    QVariantMap data = doc.object().toVariantMap();
    // Imports always mint a fresh id: keeping a file's id would silently
    // OVERWRITE an existing template that happens to share it (or shadow a
    // bundled one), which is never what "import" means.
    data.remove(PhosphorZones::TemplateJsonKeys::Id);
    // Route through the daemon-first save so the authoritative store
    // persists it; validation failures toast through the shared path.
    // Stash the minted id so the refresh that follows selects the import in
    // the list, the way importLayout and duplicateLayout do.
    const QString newId = saveScrollingTemplate(data);
    if (!newId.isEmpty()) {
        m_pendingSelectLayoutId = newId;
    }
}

void SettingsController::exportScrollingTemplate(const QString& templateId, const QString& filePath)
{
    // An empty id is a caller bug and stays a bare return; a missing store
    // is an initialization failure worth a trace, the same reasoning
    // openScrollingTemplateFile documents.
    if (templateId.isEmpty()) {
        return;
    }
    if (!m_localTemplateStore) {
        qCWarning(lcCore) << "exportScrollingTemplate: no local template store";
        return;
    }
    const QString safe = Utils::sanitizeIOPath(filePath);
    if (safe.isEmpty()) {
        qCWarning(lcCore) << "exportScrollingTemplate: refusing unsafe path" << filePath;
        Q_EMIT layoutOperationFailed(PhosphorI18n::tr("That export path is not allowed."));
        return;
    }
    const PhosphorZones::ScrollingTemplate templ = m_localTemplateStore->templateById(QUuid::fromString(templateId));
    if (!templ.isValid()) {
        Q_EMIT layoutOperationFailed(PhosphorI18n::tr("That template no longer exists."));
        return;
    }
    // Written locally from the read view (the export payload is exactly the
    // persisted schema); no daemon round-trip needed for a read.
    QSaveFile out(safe);
    if (!out.open(QIODevice::WriteOnly)) {
        Q_EMIT layoutOperationFailed(
            PhosphorI18n::tr("Could not write the export. Check that the folder is writable."));
        return;
    }
    out.write(QJsonDocument(templ.toJson()).toJson(QJsonDocument::Indented));
    if (!out.commit()) {
        Q_EMIT layoutOperationFailed(
            PhosphorI18n::tr("Could not write the export. Check that the folder is writable."));
    }
}

void SettingsController::openScrollingTemplatesFolder()
{
    const QString path = PhosphorZones::ScrollingTemplateStore::userTemplateDirectory();
    QDir dir(path);
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void SettingsController::openScrollingTemplateFile(const QString& templateId)
{
    if (!m_localTemplateStore) {
        qCWarning(lcCore) << "openScrollingTemplateFile: no local template store";
        return;
    }
    const PhosphorZones::ScrollingTemplate templ = m_localTemplateStore->templateById(QUuid::fromString(templateId));
    if (!templ.isValid() || templ.sourcePath.isEmpty()) {
        // Both misses are silent no-ops in the UI, so log them: an unknown id
        // means the local read view is behind the daemon's store, and an empty
        // sourcePath means the entry was never loaded from a file.
        qCWarning(lcCore) << "openScrollingTemplateFile: no file for" << templateId << "(valid:" << templ.isValid()
                          << "path:" << templ.sourcePath << ")";
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(templ.sourcePath));
}

QString SettingsController::saveScrollingTemplate(const QVariantMap& templateData)
{
    // Daemon-first like every layout mutation; the local store refreshes on
    // the scrollingTemplatesChanged subscription.
    const QString payload =
        QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(templateData)).toJson(QJsonDocument::Compact));
    QDBusMessage reply = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                                QStringLiteral("saveScrollingTemplate"), {payload});
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()
        || reply.arguments().first().toString().isEmpty()) {
        const QString error = reply.type() == QDBusMessage::ErrorMessage
            ? reply.errorMessage()
            : PhosphorI18n::tr("The daemon refused the template. Check that it has a name.");
        qCWarning(lcCore) << "saveScrollingTemplate failed:" << error;
        Q_EMIT layoutOperationFailed(PhosphorI18n::tr("Could not save the template: %1").arg(error));
        return {};
    }
    const QString savedId = reply.arguments().first().toString();
    scheduleLayoutLoad();
    return savedId;
}

void SettingsController::deleteScrollingTemplate(const QString& templateId)
{
    // Template ids ride the same pending-select slot as layouts, so mirror
    // deleteLayout: deleting a just-imported/just-duplicated template must
    // not leave its reveal armed.
    if (m_pendingSelectLayoutId == templateId) {
        m_pendingSelectLayoutId.clear();
    }
    QDBusMessage reply = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                                QStringLiteral("deleteScrollingTemplate"), {templateId});
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qCWarning(lcCore) << "deleteScrollingTemplate failed:" << reply.errorMessage();
        Q_EMIT layoutOperationFailed(PhosphorI18n::tr("Could not delete the template: %1").arg(reply.errorMessage()));
    } else if (reply.arguments().isEmpty() || !reply.arguments().first().toBool()) {
        // The daemon answers false for a refusal it can name (a bundled
        // template, an unwritable user directory). That is a ReplyMessage, not
        // an ErrorMessage, so it needs its own branch or it reads as success —
        // the same shape exportLayout uses below.
        qCWarning(lcCore) << "deleteScrollingTemplate: daemon refused" << templateId;
        Q_EMIT layoutOperationFailed(PhosphorI18n::tr("Could not delete the template."));
    }
    scheduleLayoutLoad();
}

void SettingsController::duplicateScrollingTemplate(const QString& templateId)
{
    QDBusMessage reply = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                                QStringLiteral("duplicateScrollingTemplate"), {templateId});
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qCWarning(lcCore) << "duplicateScrollingTemplate failed:" << reply.errorMessage();
        Q_EMIT layoutOperationFailed(
            PhosphorI18n::tr("Could not duplicate the template: %1").arg(reply.errorMessage()));
        // Clear so a stale create's reply cannot land a layoutAdded() for an
        // id this failed duplicate never produced, matching duplicateLayout.
        m_pendingSelectLayoutId.clear();
    } else if (reply.arguments().isEmpty() || reply.arguments().first().toString().isEmpty()) {
        // An empty id is how the daemon reports a refusal it can name (an id
        // that no longer resolves, an unwritable user directory) — same
        // ReplyMessage-carrying-a-rejection shape saveScrollingTemplate checks
        // for above.
        qCWarning(lcCore) << "duplicateScrollingTemplate: daemon refused" << templateId;
        Q_EMIT layoutOperationFailed(PhosphorI18n::tr("Could not duplicate the template."));
        // Same stale-reveal reasoning as the ErrorMessage branch above: a
        // refused duplicate minted nothing, so nothing may stay armed.
        m_pendingSelectLayoutId.clear();
    } else {
        // Select the copy once the refresh lands, the way duplicateLayout does.
        m_pendingSelectLayoutId = reply.arguments().first().toString();
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

// The three editor launchers used to be one-way calls, which made every
// failure invisible: a stopped daemon silently discards the message, and a
// missing or broken editor binary died inside the daemon with only a journal
// line. The launch verbs now answer whether the process was spawned, so the
// launchers read the reply and toast the one failure the user can act on.
void SettingsController::launchEditorViaDaemon(const QString& method, const QVariantList& args)
{
    const QDBusMessage reply =
        DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry), method, args);
    if (reply.type() != QDBusMessage::ReplyMessage) {
        Q_EMIT layoutOperationFailed(PhosphorI18n::tr("Could not open the editor. The daemon may not be running."));
        return;
    }
    if (!reply.arguments().value(0).toBool()) {
        // The daemon answered and refused: the editor binary could not be
        // started, or the id failed the daemon-side validation.
        Q_EMIT layoutOperationFailed(PhosphorI18n::tr("Could not open the editor."));
    }
}

void SettingsController::editLayout(const QString& layoutId)
{
    launchEditorViaDaemon(QStringLiteral("openEditorForLayoutOnScreen"), {layoutId, QString()});
}

void SettingsController::editLayoutOnScreen(const QString& layoutId, const QString& screenId)
{
    if (layoutId.isEmpty() || screenId.isEmpty()) {
        return;
    }
    launchEditorViaDaemon(QStringLiteral("openEditorForLayoutOnScreen"), {layoutId, screenId});
}

void SettingsController::editScrollingTemplate(const QString& templateId)
{
    launchEditorViaDaemon(QStringLiteral("openEditorForScrollingTemplate"), {templateId});
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

void SettingsController::importLayout(const QString& filePath)
{
    const QString safe = Utils::sanitizeIOPath(filePath);
    if (safe.isEmpty()) {
        qCWarning(lcCore) << "importLayout: refusing unsafe path" << filePath;
        Q_EMIT layoutOperationFailed(PhosphorI18n::tr("That file path is not allowed."));
        return;
    }
    QDBusMessage reply = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                                QStringLiteral("importLayout"), {safe});
    if (reply.type() == QDBusMessage::ErrorMessage) {
        // The transport itself failed: the daemon is not running, or the method
        // is missing. The daemon's own rejections do NOT arrive here (it has no
        // QDBusContext and never sends an error reply) — they come back as an
        // empty id below.
        qCWarning(lcCore) << "importLayout failed:" << reply.errorMessage();
        Q_EMIT layoutOperationFailed(PhosphorI18n::tr("Failed to import layout: %1").arg(reply.errorMessage()));
    } else if (reply.arguments().isEmpty() || reply.arguments().first().toString().isEmpty()) {
        // An empty id is how the daemon reports every rejection it can name: a
        // file that is not there, not readable, not JSON, or not a layout. It is
        // a ReplyMessage, so without this branch the page just refreshes and the
        // user is left to work out that nothing was imported.
        qCWarning(lcCore) << "importLayout: daemon rejected" << safe;
        Q_EMIT layoutOperationFailed(PhosphorI18n::tr("That file is not a layout this app can read."));
    } else {
        m_pendingSelectLayoutId = reply.arguments().first().toString();
    }
    scheduleLayoutLoad();
}

void SettingsController::exportLayout(const QString& layoutId, const QString& filePath)
{
    if (layoutId.isEmpty())
        return;
    const QString safe = Utils::sanitizeIOPath(filePath);
    if (safe.isEmpty()) {
        qCWarning(lcCore) << "exportLayout: refusing unsafe path" << filePath;
        Q_EMIT layoutOperationFailed(PhosphorI18n::tr("That export path is not allowed."));
        return;
    }
    // A replying call, not sendOneWay. The write happens in the daemon, so a
    // one-way send has nowhere to put "the folder is not writable" and the file
    // picker just closed on a user who is owed an answer. Sent async through a
    // QDBusPendingCallWatcher (same idiom as loadLayoutsAsync above) so the UI
    // thread never stalls on the round-trip; the watcher lambda evaluates the
    // reply and surfaces the same failure toasts a blocking call would.
    auto* watcher = new QDBusPendingCallWatcher(
        PhosphorProtocol::ClientHelpers::asyncCall(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                                   QStringLiteral("exportLayout"), {layoutId, safe}),
        this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, safe](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        // Evaluate the raw QDBusMessage rather than a typed QDBusPendingReply<bool>
        // so the branch structure matches the daemon's actual reply shapes:
        // transport/daemon errors arrive as an ErrorMessage; a rejection the
        // daemon can name arrives as a ReplyMessage carrying false.
        const QDBusMessage reply = w->reply();
        if (reply.type() == QDBusMessage::ErrorMessage) {
            qCWarning(lcCore) << "exportLayout failed:" << reply.errorMessage();
            Q_EMIT layoutOperationFailed(PhosphorI18n::tr("Failed to export layout: %1").arg(reply.errorMessage()));
            return;
        }
        // The daemon answers false for a destination it could not open, write or
        // commit. That is a ReplyMessage, not an ErrorMessage, so it needs its own
        // branch or it reads as success.
        if (reply.arguments().isEmpty() || !reply.arguments().first().toBool()) {
            qCWarning(lcCore) << "exportLayout: daemon could not write" << safe;
            Q_EMIT layoutOperationFailed(
                PhosphorI18n::tr("Could not write the export. Check that the folder is writable."));
        }
    });
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

// The algorithm bodies live in AlgorithmService. The SettingsController::*
// methods below are Q_INVOKABLE forwarders so QML's entry points stay stable.
// Most are one line. The three exceptions: the ones taking a path sanitize it
// first (that guard belongs on the controller, which is where QML hands the
// path in); openLayoutFile resolves the layout's real on-disk path through the
// local registry before falling back to the service; and algorithmIdFromLayoutId
// has no service body at all, being a pure LayoutId helper.

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
    const QString safe = Utils::sanitizeIOPath(filePath);
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
    // filename from the id (as AlgorithmService::openLayoutFile's
    // locate-by-UUID fallback does)
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
    const QString safe = Utils::sanitizeIOPath(destPath);
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

QVariantList SettingsController::activeLayoutMatchOptions() const
{
    // Every `layouts` entry (manual layouts and autotile entries pass through
    // unchanged), with the NATIVE template rows transformed: a template
    // entry's raw UUID is not an ActiveLayout value — the context resolvers
    // stamp the PREFIXED "scrolling:<uuid>" form — so each template row is
    // rewritten to that wire id with a "Template: …" label. Nothing here is
    // derived from manual layouts.
    //
    // The bare "scrolling:" sentinel leads the list. It is the value a
    // Scrolling context stamps when it resolves NO template, so without an
    // entry for it the documented `ActiveLayout Equals "scrolling:"` rule shape
    // is unauthorable in the picker. Head position rather than beside the
    // template rows: those sort in among the manual layouts by name, so there
    // is no contiguous template block to sit next to, and index 0 is the one
    // slot that does not shift as templates are added and renamed.
    QVariantList out;
    out.reserve(m_layouts.size() + 1);
    QVariantMap sentinel;
    sentinel[QStringLiteral("id")] = QString(PhosphorLayout::LayoutId::ScrollingId);
    sentinel[QStringLiteral("displayName")] = PhosphorI18n::tr("Scrolling (no template)");
    out.append(sentinel);
    for (const QVariant& lv : m_layouts) {
        const QVariantMap m = lv.toMap();
        const QString id = m.value(QStringLiteral("id")).toString();
        if (m.value(QStringLiteral("isScrollingTemplate")).toBool()) {
            QVariantMap derived;
            derived[QStringLiteral("id")] = PhosphorLayout::LayoutId::makeScrollingId(id);
            derived[QStringLiteral("displayName")] =
                RuleAuthoring::templateDisplayLabel(m.value(QStringLiteral("displayName")).toString());
            out.append(derived);
            continue;
        }
        out.append(lv);
    }
    return out;
}
} // namespace PlasmaZones
