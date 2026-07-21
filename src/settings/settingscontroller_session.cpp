// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Session-state methods for SettingsController:
//   * Font enumeration helpers (Q_INVOKABLE getters consumed by QML).
//   * The QUrl → local-path helper QML hands file-dialog results through.
//   * Stage-assignment setters and quick-layout-slot accessors, plus the
//     screen-state / staged-assignment queries the assignments page reads back.
//   * Virtual-desktop + activity tracking (D-Bus signals from KWin /
//     ActivityManager and the daemon's screen-layout broadcast).
//   * Running-windows and physical-screen queries (used by the assignments
//     page).
//   * Config import / export (whole-file JSON dump and restore).
//   * Window-geometry persistence (QSettings entry under the
//     organization config file, NOT the main JSON).
//   * KZones import helpers.
//
// Per-screen overrides live in settingscontroller_perscreen.cpp and the
// ordering helpers in settingscontroller_ordering.cpp.
//
// All methods here are members of PlasmaZones::SettingsController and use its
// private state. Same class as settingscontroller.cpp, separate translation
// unit, no API change.

#include "settingscontroller.h"

#include "../config/configdefaults.h"
#include "../config/configmigration.h"
#include "../core/logging.h"
#include "../core/settings_interfaces.h"
#include "../core/utils.h"
#include "../phosphor_i18n.h"
#include "dbusutils.h"
#include "kzonesimporter.h"
#include "settingscontroller_pagekeys.h"
#include "virtualscreenutils.h"

#include <PhosphorLayoutApi/LayoutId.h>
#include <PhosphorZones/AssignmentEntry.h>
#include <PhosphorZones/ZoneJsonKeys.h>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRect>
#include <QScreen>
#include <QSettings>

namespace PlasmaZones {

QStringList SettingsController::fontStylesForFamily(const QString& family) const
{
    return QFontDatabase::styles(family);
}

int SettingsController::fontStyleWeight(const QString& family, const QString& style) const
{
    return QFontDatabase::weight(family, style);
}

bool SettingsController::fontStyleItalic(const QString& family, const QString& style) const
{
    return QFontDatabase::italic(family, style);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Assignment staging — the QML-callable atomic mode+layout staging entry point
// forwards to StagingService and flips the dirty flag; all state + flush logic
// lives in the service.
// ═══════════════════════════════════════════════════════════════════════════════

void SettingsController::stageAssignmentEntry(const QString& screenName, int virtualDesktop, const QString& activityId,
                                              int mode, const QString& snappingLayoutId,
                                              const QString& tilingAlgorithmId)
{
    m_staging.stageAssignmentEntry(screenName, virtualDesktop, activityId, mode, snappingLayoutId, tilingAlgorithmId);
    setNeedsSave(true);
}

void SettingsController::stageAssignmentClear(const QString& screenName, int virtualDesktop, const QString& activityId)
{
    m_staging.stageFullClear(screenName, virtualDesktop, activityId);
    setNeedsSave(true);
}

void SettingsController::removeStagedAssignment(const QString& screenName, int virtualDesktop,
                                                const QString& activityId)
{
    m_staging.removeStagedAssignment(screenName, virtualDesktop, activityId);
    // Same bookkeeping as stageAssignmentClear. Removing a staged entry can
    // leave the staging map empty, but dirtiness here is page-level, not a
    // count of staged entries: the user interacted with the page, and Apply
    // simply flushes whatever remains (possibly nothing) — the flush of an
    // empty map is a no-op. There is no unstage path in this controller
    // that flips needsSave back to false short of save()/load(), so
    // setNeedsSave(true) is the coherent mirror of the other staging
    // mutators.
    setNeedsSave(true);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Path helpers exposed to QML
// ═══════════════════════════════════════════════════════════════════════════════

QString SettingsController::urlToLocalFile(const QUrl& url) const
{
    // QUrl::toLocalFile handles percent-decoding, embedded query/fragment,
    // and uses the canonical scheme-strip path that QtDBus/QFile interop
    // already relies on. Replaces the ad-hoc regex pattern previously
    // duplicated in QML file-picker callbacks.
    return url.toLocalFile();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Quick layout slots (D-Bus to daemon)
// ═══════════════════════════════════════════════════════════════════════════════

QString SettingsController::getQuickLayoutSlot(int slotNumber) const
{
    if (slotNumber < 1 || slotNumber > kQuickLayoutSlotCount)
        return {};
    QString staged;
    if (m_staging.stagedSnappingQuickSlot(slotNumber, staged))
        return staged;
    // Snapping quick slots use wire mode 0 (AssignmentEntry::Snapping).
    QDBusMessage reply = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                                QStringLiteral("getQuickLayoutSlot"), {0, slotNumber});
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty())
        return reply.arguments().first().toString();
    return {};
}

void SettingsController::setQuickLayoutSlot(int slotNumber, const QString& layoutId)
{
    if (slotNumber < 1 || slotNumber > kQuickLayoutSlotCount)
        return;
    m_staging.stageSnappingQuickSlot(slotNumber, layoutId);
    setNeedsSave(true);
}

QString SettingsController::getQuickLayoutShortcut(int slotNumber) const
{
    if (slotNumber < 1 || slotNumber > kQuickLayoutSlotCount)
        return {};
    // Return the default shortcut string -- the standalone cannot query KGlobalAccel
    // since it doesn't link KF6::GlobalAccel. The shortcut is Meta+Alt+N.
    return QStringLiteral("Meta+Alt+%1").arg(slotNumber);
}

QString SettingsController::getTilingQuickLayoutSlot(int slotNumber) const
{
    if (slotNumber < 1 || slotNumber > kQuickLayoutSlotCount)
        return {};
    QString staged;
    if (m_staging.stagedTilingQuickSlot(slotNumber, staged))
        return staged;
    // Tiling quick slots use wire mode 1 (AssignmentEntry::Autotile). They are
    // daemon-backed (mode-keyed LayoutRegistry), same as snapping slots.
    QDBusMessage reply = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                                QStringLiteral("getQuickLayoutSlot"), {1, slotNumber});
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty())
        return reply.arguments().first().toString();
    return {};
}

void SettingsController::setTilingQuickLayoutSlot(int slotNumber, const QString& layoutId)
{
    if (slotNumber < 1 || slotNumber > kQuickLayoutSlotCount)
        return;
    m_staging.stageTilingQuickSlot(slotNumber, layoutId);
    setNeedsSave(true);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Virtual desktops / activities (D-Bus queries to daemon)
// ═══════════════════════════════════════════════════════════════════════════════

void SettingsController::refreshVirtualDesktops()
{
    QDBusMessage countReply = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                                     QStringLiteral("getVirtualDesktopCount"));
    if (countReply.type() == QDBusMessage::ReplyMessage && !countReply.arguments().isEmpty()) {
        m_virtualDesktopCount = countReply.arguments().first().toInt();
    } else if (countReply.type() == QDBusMessage::ErrorMessage) {
        qCWarning(lcCore) << "refreshVirtualDesktops: getVirtualDesktopCount D-Bus call failed:"
                          << countReply.errorMessage();
        // Mirror the refreshActivities pattern: reset to the single-
        // desktop default on error so QML doesn't render desktop indices
        // the daemon no longer enumerates.
        m_virtualDesktopCount = 1;
    }

    QDBusMessage namesReply = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                                     QStringLiteral("getVirtualDesktopNames"));
    if (namesReply.type() == QDBusMessage::ReplyMessage && !namesReply.arguments().isEmpty()) {
        m_virtualDesktopNames = namesReply.arguments().first().toStringList();
    } else if (namesReply.type() == QDBusMessage::ErrorMessage) {
        qCWarning(lcCore) << "refreshVirtualDesktops: getVirtualDesktopNames D-Bus call failed:"
                          << namesReply.errorMessage();
        m_virtualDesktopNames.clear();
    }
}

void SettingsController::refreshActivities()
{
    QDBusMessage availReply = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                                     QStringLiteral("isActivitiesAvailable"));
    if (availReply.type() == QDBusMessage::ReplyMessage && !availReply.arguments().isEmpty()) {
        m_activitiesAvailable = availReply.arguments().first().toBool();
    } else if (availReply.type() == QDBusMessage::ErrorMessage) {
        qCWarning(lcCore) << "refreshActivities: isActivitiesAvailable D-Bus call failed:" << availReply.errorMessage();
        // Treat a D-Bus error the same as an explicit "false" reply —
        // without this, m_activitiesAvailable kept its previous (likely
        // true) value, the function then entered the `if (true)` branch
        // below, each sub-call also errored, and m_activities /
        // m_currentActivity stayed stale. QML rendered activities the
        // daemon could no longer enumerate.
        m_activitiesAvailable = false;
    }

    if (m_activitiesAvailable) {
        QDBusMessage infoReply = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                                        QStringLiteral("getAllActivitiesInfo"));
        if (infoReply.type() == QDBusMessage::ReplyMessage && !infoReply.arguments().isEmpty()) {
            QString json = infoReply.arguments().first().toString();
            QJsonParseError parseErr;
            QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &parseErr);
            if (parseErr.error != QJsonParseError::NoError) {
                // Malformed payload — clear so QML stops rendering an
                // activity set we can no longer trust to match the
                // daemon's view. Silent-on-parse-error would keep
                // m_activities stale forever.
                qCWarning(lcCore) << "refreshActivities: getAllActivitiesInfo parse error:" << parseErr.errorString();
                m_activities.clear();
            } else if (doc.isArray()) {
                m_activities.clear();
                for (const auto& val : doc.array()) {
                    m_activities.append(val.toObject().toVariantMap());
                }
            } else {
                // Reply was a JSON document but not an array — same
                // shape mismatch as a parse error; treat as failure.
                qCWarning(lcCore) << "refreshActivities: getAllActivitiesInfo reply was not a JSON array";
                m_activities.clear();
            }
        } else if (infoReply.type() == QDBusMessage::ErrorMessage) {
            qCWarning(lcCore) << "refreshActivities: getAllActivitiesInfo D-Bus call failed:"
                              << infoReply.errorMessage();
            // Same rationale as the isActivitiesAvailable branch above:
            // a D-Bus failure leaves m_activities stale and QML
            // continues to render activities the daemon can no longer
            // enumerate. Clear so the view honestly reflects "we
            // couldn't talk to the daemon."
            m_activities.clear();
        }

        QDBusMessage currentReply = DaemonDBus::callDaemon(
            QString(PhosphorProtocol::Service::Interface::LayoutRegistry), QStringLiteral("getCurrentActivity"));
        if (currentReply.type() == QDBusMessage::ReplyMessage && !currentReply.arguments().isEmpty()) {
            m_currentActivity = currentReply.arguments().first().toString();
        } else if (currentReply.type() == QDBusMessage::ErrorMessage) {
            qCWarning(lcCore) << "refreshActivities: getCurrentActivity D-Bus call failed:"
                              << currentReply.errorMessage();
        }
    } else {
        // Activities subsystem went away (kactivities not running, plugin
        // disabled, etc.) — clear stale state so QML stops rendering
        // activities that no longer exist.
        m_activities.clear();
        m_currentActivity.clear();
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Virtual desktop / activity D-Bus signal handlers
// ═══════════════════════════════════════════════════════════════════════════════

void SettingsController::onVirtualDesktopsChanged()
{
    refreshVirtualDesktops();

    // Prune both per-mode disabled-desktop lists. This is driven by a
    // KDE virtual-desktop count change (external system event), not by
    // the user clicking on the active page — target the "overview" page
    // (where MonitorStatePage.qml owns the per-mode disabled lists) so
    // dirty state attaches to the right tab regardless of where the user
    // is currently looking. Pattern mirrors the rules handler in
    // settingscontroller.cpp.
    //
    // Wrap the SETTER calls (not just the trailing setNeedsSave) so the
    // synchronous NOTIFY emit from setDisabledDesktops() — which routes
    // through onSettingsPropertyChanged → setNeedsSave — finds the
    // external-edit target set to "overview" instead of whatever leaf
    // the user is currently viewing. Without this wrap, the dirty bit
    // attaches to the active page and the explicit setNeedsSave below
    // becomes a no-op for the intended target.
    bool prunedAny = false;
    {
        ExternalEditScope scope(*this, QStringLiteral("overview"));
        // Iterate every mode the (Mode, Family) table knows about so a
        // future mode (e.g. Scrolling) is automatically pruned when the
        // user removes a virtual desktop it referenced. The hand-maintained
        // {Snapping, Autotile} list here used to silently skip Scrolling.
        for (const auto mode : PhosphorZones::allModes()) {
            QStringList disabled = m_settings.disabledDesktops(mode);
            if (pruneDisabledDesktopEntries(disabled, m_virtualDesktopCount)) {
                m_settings.setDisabledDesktops(mode, disabled);
                prunedAny = true;
            }
        }
        if (prunedAny) {
            setNeedsSave(true);
        }
    }

    Q_EMIT virtualDesktopsChanged();
}

void SettingsController::onActivitiesChanged()
{
    refreshActivities();

    // Prune disabled-activity entries that reference removed activities.
    // External event (KDE activity change) — see onVirtualDesktopsChanged
    // above for the same wrap-the-setters rationale (the synchronous
    // NOTIFY emit re-routes dirty bookkeeping through the meta-object
    // loop, which needs m_externalEditStack already top-of-stack set to
    // "overview").
    bool prunedAny = false;
    {
        ExternalEditScope scope(*this, QStringLiteral("overview"));
        if (!m_activities.isEmpty()) {
            QSet<QString> validIds;
            for (const QVariant& v : std::as_const(m_activities)) {
                const QVariantMap map = v.toMap();
                const QString id = map.value(QStringLiteral("id")).toString();
                if (!id.isEmpty()) {
                    validIds.insert(id);
                }
            }
            // Iterate every mode the (Mode, Family) table knows about so a
            // future mode (e.g. Scrolling) is automatically pruned when the
            // user removes a KDE activity it referenced. Symmetric with the
            // desktop-prune loop in onVirtualDesktopsChanged above.
            for (const auto mode : PhosphorZones::allModes()) {
                QStringList disabledActs = m_settings.disabledActivities(mode);
                if (pruneDisabledActivityEntries(disabledActs, validIds)) {
                    m_settings.setDisabledActivities(mode, disabledActs);
                    prunedAny = true;
                }
            }
        }
        if (prunedAny) {
            setNeedsSave(true);
        }
    }

    Q_EMIT activitiesChanged();
}

void SettingsController::onScreenLayoutChanged(const QString& screenId, const QString& layoutId, int virtualDesktop)
{
    Q_UNUSED(screenId)
    Q_UNUSED(layoutId)
    Q_UNUSED(virtualDesktop)
    // External assignment change (hotkey, script, toggle) — refresh overview.
    // The daemon-side broadcast carries (screenId, layoutId, virtualDesktop)
    // but every current QML consumer reacts by re-querying full state, so
    // the no-arg signal is sufficient and keeps the public Q_SIGNAL surface
    // small. If a future consumer needs per-screen targeting, widen the
    // signal in lockstep with the QML side (single-signal source of truth).
    Q_EMIT screenLayoutChanged();
}

namespace {

// Drop a leading UTF-8 BOM from @p bytes. Qt's JSON parser treats a BOM as a
// syntax error rather than skipping it, so any bytes handed to
// QJsonDocument::fromJson have to come through here first. Shared by the import
// probe's head sniff and its whole-file parse so the two agree on where the
// content starts.
QByteArray withoutUtf8Bom(const QByteArray& bytes)
{
    static const QByteArray kUtf8Bom("\xEF\xBB\xBF", 3);
    return bytes.startsWith(kUtf8Bom) ? bytes.mid(kUtf8Bom.size()) : bytes;
}

// Parses the daemon's running-windows JSON payload into a QVariantList of
// {windowClass, appName, caption, desktopFile} maps ready for QML consumption. The
// synchronous getRunningWindows() predecessor was removed in Phase 6 of
// refactor/dbus-performance; only onRunningWindowsAvailable calls this now.
// Anonymous namespace keeps this TU-local — same convention as the
// settingscontroller_ordering.cpp helpers so unity-build batching can't
// produce a duplicate-symbol surprise.
QVariantList parseRunningWindowsJson(const QString& json)
{
    if (json.isEmpty()) {
        return {};
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        return {};
    }

    QVariantList result;
    const QJsonArray array = doc.array();
    result.reserve(array.size());
    for (const QJsonValue& value : array) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject obj = value.toObject();
        QVariantMap item;
        item[QStringLiteral("windowClass")] = obj[QLatin1String("windowClass")].toString();
        item[QStringLiteral("appName")] = obj[QLatin1String("appName")].toString();
        item[QStringLiteral("caption")] = obj[QLatin1String("caption")].toString();
        // `desktopFile` is forwarded verbatim — older effect builds that
        // don't include the field will produce an empty string, which the
        // WindowPickerDialog hides from its DesktopFile mode.
        item[QStringLiteral("desktopFile")] = obj[QLatin1String("desktopFile")].toString();
        result.append(item);
    }
    return result;
}

} // namespace

void SettingsController::requestRunningWindows()
{
    // Fire-and-forget: the daemon emits runningWindowsRequested to the
    // KWin effect, which answers via provideRunningWindows, which the
    // daemon fans out on runningWindowsAvailable — caught by our
    // onRunningWindowsAvailable slot. The UI thread never blocks.
    //
    // If the daemon isn't running, dispatching the call would still go
    // through D-Bus auto-start machinery (or fail outright) and the user
    // would see an empty list for the full timeout window with no
    // indication the request never made it out. Drop the cached list so
    // a subsequent cachedRunningWindows() read reflects the daemon-down
    // state (rather than a stale snapshot), then surface the empty-reply
    // via an immediate timeout so the QML page renders the requestTimedOut
    // placeholder state instead of a generic "no windows" message.
    if (!m_daemonController.isRunning()) {
        m_cachedRunningWindows.clear();
        m_runningWindowsTimeout.stop();
        Q_EMIT runningWindowsTimedOut();
        return;
    }
    // Start (or restart) the client-side timeout guard. Repeated calls
    // coalesce — the most recent deadline wins, matching the fire-and-
    // forget semantics on the daemon side.
    //
    // Invalidate the cached list so a subsequent timeout truly reflects
    // "no fresh data" — without this, QML readers would continue to see
    // the stale list and never realise the refresh attempt failed.
    m_cachedRunningWindows.clear();
    m_runningWindowsTimeout.start();
    DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::Settings),
                           QStringLiteral("requestRunningWindows"));
}

void SettingsController::onRunningWindowsAvailable(const QString& json)
{
    // Reply arrived — stop the timeout timer so a stale runningWindowsTimedOut()
    // doesn't fire after we've already served fresh data.
    m_runningWindowsTimeout.stop();
    m_cachedRunningWindows = parseRunningWindowsJson(json);
    Q_EMIT runningWindowsAvailable(m_cachedRunningWindows);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Config export/import
// ═══════════════════════════════════════════════════════════════════════════════

bool SettingsController::exportAllSettings(const QString& filePath)
{
    // Reachable from QML: urlToLocalFile yields an empty string for a URL that
    // is not a local file, so a remote destination lands here rather than being
    // a programmer error.
    if (filePath.isEmpty()) {
        Q_EMIT settingsTransferFailed(PhosphorI18n::tr("Settings can only be exported to a local file."));
        return false;
    }
    // Defence-in-depth: same sanitiser the per-layout import/export uses.
    // A QML or D-Bus caller passing a path with `..` traversal segments,
    // an embedded NUL, or a leading `~` is treated as a programmer error.
    const QString safeFilePath = Utils::sanitizeIOPath(filePath);
    if (safeFilePath.isEmpty()) {
        qCWarning(lcCore) << "exportAllSettings: refusing unsafe path" << filePath;
        Q_EMIT settingsTransferFailed(PhosphorI18n::tr("That export path is not allowed."));
        return false;
    }
    const QString configPath = PlasmaZones::ConfigDefaults::configFilePath();
    // Exporting onto the live config is refused: the write below would put the
    // current settings into the file the app is using, which is an implicit
    // Save of whatever the user has pending, and it would leave that file
    // disagreeing with the baseline per-page Discard reverts to. The file
    // picker can reach the config directory. Canonical paths, so a symlink or a
    // `.` segment pointing at the same file is caught too. canonicalFilePath()
    // is empty for a file that does not exist yet, which on a fresh profile is
    // exactly the live config path; in that case canonicalize the PARENT
    // directory and rejoin the filename, so `<symlink-to-config-dir>/config.json`
    // is still caught, and fall back to cleaned lexical paths only when the
    // parent does not exist either (nothing on disk to resolve).
    const QFileInfo destInfo(safeFilePath);
    bool destIsLiveConfig = false;
    if (destInfo.exists()) {
        destIsLiveConfig = destInfo.canonicalFilePath() == QFileInfo(configPath).canonicalFilePath();
    } else {
        const auto resolveThroughParent = [](const QFileInfo& info) -> QString {
            const QString parentCanonical = info.dir().canonicalPath();
            if (parentCanonical.isEmpty())
                return QDir::cleanPath(info.filePath());
            return parentCanonical + QLatin1Char('/') + info.fileName();
        };
        destIsLiveConfig = resolveThroughParent(destInfo) == resolveThroughParent(QFileInfo(configPath));
    }
    if (destIsLiveConfig) {
        qCWarning(PlasmaZones::lcCore) << "exportAllSettings: destination is the live config file, refusing"
                                       << safeFilePath;
        Q_EMIT settingsTransferFailed(
            PhosphorI18n::tr("That is the settings file this app is using. Export to a different file."));
        return false;
    }
    // Serialize the current settings straight to the destination rather than
    // copying the live config. The old form flushed with Settings::save()
    // first, so that the export carried what the user could see rather than the
    // last-saved snapshot — but save() commits to ~/.config and re-baselines,
    // so pressing Export persisted edits the user had never chosen to save and
    // left a later per-page Discard with nothing to revert to. exportTo writes
    // the same values (the setters already keep the backend's in-memory root
    // current) without touching the live file or the baseline, and it writes
    // atomically, so a failed export leaves whatever was at the destination
    // alone instead of unlinking it up front to make room.
    if (!m_settings.exportTo(safeFilePath)) {
        qCWarning(PlasmaZones::lcCore) << "Failed to export settings to:" << safeFilePath;
        Q_EMIT settingsTransferFailed(
            PhosphorI18n::tr("Could not write the export. Check that the folder is writable."));
        return false;
    }
    return true;
}

bool SettingsController::importAllSettings(const QString& filePath)
{
    // Same as the export side: an empty path here is a URL that is not a local
    // file, which a user can pick.
    if (filePath.isEmpty()) {
        Q_EMIT settingsTransferFailed(PhosphorI18n::tr("Settings can only be imported from a local file."));
        return false;
    }
    // Split rather than combined: a refused path and a file that is not there
    // are the same `false` to a caller but different words to a user, and the
    // log wants them apart too.
    const QString safeFilePath = Utils::sanitizeIOPath(filePath);
    if (safeFilePath.isEmpty()) {
        qCWarning(lcCore) << "importAllSettings: refusing unsafe path" << filePath;
        Q_EMIT settingsTransferFailed(PhosphorI18n::tr("That file path is not allowed."));
        return false;
    }
    if (!QFile::exists(safeFilePath)) {
        qCWarning(lcCore) << "importAllSettings: file not found" << filePath;
        Q_EMIT settingsTransferFailed(PhosphorI18n::tr("That settings file is no longer there."));
        return false;
    }

    const QString configPath = PlasmaZones::ConfigDefaults::configFilePath();

    // Importing the live config onto itself is a no-op, but the copy below
    // removes the destination first and would then find its source gone. The
    // backup restores it, so nothing is lost, yet the user is told the import
    // failed when there was simply nothing to do. Refuse it up front instead.
    const QFileInfo importInfo(safeFilePath);
    if (importInfo.canonicalFilePath() == QFileInfo(configPath).canonicalFilePath()) {
        qCWarning(PlasmaZones::lcCore) << "importAllSettings: source is the live config file, refusing" << safeFilePath;
        Q_EMIT settingsTransferFailed(
            PhosphorI18n::tr("Those are the settings this app is already using. Pick a different file to import."));
        return false;
    }

    // Detect if the imported file is legacy INI format (not JSON).
    // If so, run the migration converter to produce a JSON file.
    bool isLegacyIni = false;
    {
        QFile f(safeFilePath);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            // Read enough bytes to find the first non-whitespace character.
            // JSON files start with '{' (or '[' for arrays, though config is always an object).
            QByteArray head = f.peek(256);
            // Reject non-UTF-8 BOMs explicitly — a UTF-16/UTF-32 file would
            // not be valid JSON for our config format AND is not legacy INI
            // either; misclassifying it as INI would corrupt data on
            // migration. Caller gets the same "not legacy" path which then
            // fails JSON validation and aborts cleanly.
            const auto byte = [&head](int i) {
                return static_cast<unsigned char>(head.at(i));
            };
            if (head.size() >= 4 && byte(0) == 0x00 && byte(1) == 0x00 && byte(2) == 0xFE && byte(3) == 0xFF) {
                qCWarning(PlasmaZones::lcCore) << "Import file has UTF-32 BE BOM, refusing:" << filePath;
                Q_EMIT settingsTransferFailed(PhosphorI18n::tr("That is not a settings file this app can read."));
                return false;
            }
            if (head.size() >= 4 && byte(0) == 0xFF && byte(1) == 0xFE && byte(2) == 0x00 && byte(3) == 0x00) {
                qCWarning(PlasmaZones::lcCore) << "Import file has UTF-32 LE BOM, refusing:" << filePath;
                Q_EMIT settingsTransferFailed(PhosphorI18n::tr("That is not a settings file this app can read."));
                return false;
            }
            if (head.size() >= 2 && ((byte(0) == 0xFE && byte(1) == 0xFF) || (byte(0) == 0xFF && byte(1) == 0xFE))) {
                qCWarning(PlasmaZones::lcCore) << "Import file has UTF-16 BOM, refusing:" << filePath;
                Q_EMIT settingsTransferFailed(PhosphorI18n::tr("That is not a settings file this app can read."));
                return false;
            }
            // Skip UTF-8 BOM (EF BB BF) if present — trimmed() only strips ASCII whitespace.
            head = withoutUtf8Bom(head.trimmed()).trimmed();
            // A leading '[' is ambiguous: a legacy INI file starts with its
            // first "[Group]" header, but so does a JSON array. The array is
            // valid JSON yet never a settings file (the config root is always
            // an object), and feeding it to the INI migrator would produce a
            // misleading "older settings file" error. Disambiguate by parsing
            // the whole file: valid JSON here means it is not INI, so refuse
            // it with the accurate message; a parse failure means a real INI
            // section header and the migration path proceeds as before.
            if (!head.isEmpty() && head.at(0) == '[') {
                // Parse the body under the SAME normalization `head` got. peek()
                // left the read position at 0, so readAll() re-reads the BOM that
                // was stripped from `head` above, and Qt's JSON parser rejects a
                // leading BOM outright — a BOM'd JSON array would look like a
                // parse failure, fall through to isLegacyIni, and earn exactly the
                // misleading "older settings file" error this branch exists to
                // prevent.
                if (!QJsonDocument::fromJson(withoutUtf8Bom(f.readAll().trimmed())).isNull()) {
                    qCWarning(PlasmaZones::lcCore)
                        << "Import file is a JSON array, not a settings object, refusing:" << filePath;
                    Q_EMIT settingsTransferFailed(PhosphorI18n::tr("That is not a settings file this app can read."));
                    return false;
                }
            }
            isLegacyIni = !head.isEmpty() && head.at(0) != '{';
        }
    }

    // Backup current config
    const QString backupPath = configPath + QStringLiteral(".bak");
    if (QFile::exists(backupPath)) {
        QFile::remove(backupPath);
    }
    if (QFile::exists(configPath) && !QFile::copy(configPath, backupPath)) {
        qCWarning(PlasmaZones::lcCore) << "Failed to backup config to:" << backupPath;
        Q_EMIT settingsTransferFailed(
            PhosphorI18n::tr("Could not back up your current settings, so nothing was imported."));
        return false;
    }

    bool ok = false;
    if (isLegacyIni) {
        // Convert INI to JSON in-place using the migration module
        if (QFile::exists(configPath)) {
            QFile::remove(configPath);
        }
        ok = PlasmaZones::ConfigMigration::migrateIniToJson(safeFilePath, configPath);
        if (!ok) {
            qCWarning(PlasmaZones::lcCore) << "Failed to convert legacy INI file:" << safeFilePath;
            Q_EMIT settingsTransferFailed(PhosphorI18n::tr("Could not read that older settings file."));
        }
    } else {
        // Validate JSON before overwriting current config
        QFile importFile(safeFilePath);
        if (!importFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qCWarning(PlasmaZones::lcCore) << "Failed to open import file:" << safeFilePath;
            Q_EMIT settingsTransferFailed(PhosphorI18n::tr("Could not read that settings file."));
            ok = false;
        } else {
            QJsonParseError parseErr;
            QJsonDocument importDoc = QJsonDocument::fromJson(importFile.readAll(), &parseErr);
            if (parseErr.error != QJsonParseError::NoError || !importDoc.isObject()) {
                qCWarning(PlasmaZones::lcCore)
                    << "Invalid JSON in import file:" << safeFilePath << parseErr.errorString();
                Q_EMIT settingsTransferFailed(PhosphorI18n::tr("That is not a settings file this app can read."));
                ok = false;
            } else {
                // Valid JSON — copy to config path
                if (QFile::exists(configPath)) {
                    QFile::remove(configPath);
                }
                ok = QFile::copy(safeFilePath, configPath);
                if (!ok) {
                    qCWarning(PlasmaZones::lcCore) << "Failed to import settings from:" << safeFilePath;
                    // Says what failed, not what the restore below will do:
                    // that runs after this and can fail too.
                    Q_EMIT settingsTransferFailed(PhosphorI18n::tr("Could not replace your settings with that file."));
                }
            }
        }
    }

    if (!ok) {
        // Restore backup on failure. `QFile::rename` on Qt REFUSES to
        // overwrite an existing destination (regardless of POSIX rename(2)
        // atomicity at the syscall layer), so we must explicitly remove
        // configPath first. In the open-fail / JSON-parse-fail paths above
        // configPath was never touched, so it still exists — without this
        // pre-remove the rename silently fails and the user is left with
        // their original (pre-import) config plus a misleading "Failed to
        // restore" warning while the actual restore-from-backup is
        // unreachable. There's a one-syscall window between remove and
        // rename where configPath doesn't exist; that's preferable to the
        // silent-failure semantics of the previous form.
        if (QFile::exists(backupPath)) {
            // Both arms leave the user without their settings, which the
            // failure reported above does not cover: it says the import
            // failed, not that the restore behind it failed too and the only
            // copy is now the backup. Name the file, since recovering it by
            // hand is the one action left.
            if (QFile::exists(configPath) && !QFile::remove(configPath)) {
                qCWarning(PlasmaZones::lcCore)
                    << "Failed to remove configPath before restore. Backup remains at:" << backupPath;
                Q_EMIT settingsTransferFailed(
                    PhosphorI18n::tr("Your settings could not be put back. A copy is saved at %1.").arg(backupPath));
            } else if (!QFile::rename(backupPath, configPath)) {
                qCWarning(PlasmaZones::lcCore)
                    << "Failed to restore config from backup after failed import. Backup remains at:" << backupPath;
                Q_EMIT settingsTransferFailed(
                    PhosphorI18n::tr("Your settings could not be put back. A copy is saved at %1.").arg(backupPath));
            }
        }
    } else {
        // Clean up backup on success
        QFile::remove(backupPath);
        // Wrap the in-memory reload so property NOTIFY signals don't mark
        // pages dirty — the imported config is already on disk. Keep
        // m_loading=true through the page-controller revert calls below
        // too: revertPending() emits pendingChangesChanged synchronously
        // and revert() schedules a daemon fetch whose reply could
        // re-fire dirtyChanged before the trailing setNeedsSave(false)
        // runs. Both connect-side handlers (the
        // m_animationsPage::pendingChangesChanged and
        // m_rulesPage::dirtyChanged connect lambdas in
        // settingscontroller.cpp) early-return when m_loading is true.
        m_loading = true;
        m_settings.load();
        // Page controllers with their own on-disk staging surfaces
        // (animations / rules) must reload too — m_settings.load()
        // only refreshes settings.json-backed state. Without these the
        // imported config disagrees with what the page controllers still
        // hold in their in-memory snapshots.
        // Refused means an async discard still owns the snapshot map, so the
        // page is still holding pre-edit content for files the import just
        // rewrote. Do not force the session clean in that case: the import did
        // not fully land, so leave whatever dirty state the session already
        // carried rather than declaring it settled. (This only withholds the
        // clear; setNeedsSave(false) never marks anything dirty, and a later
        // Discard on the animation pages is gated by hasPendingChanges(), not
        // by m_dirtyPages membership.)
        const bool animationsReverted = !m_animationsPage || m_animationsPage->revertPending();
        if (!animationsReverted) {
            qCWarning(lcConfig) << "importConfig: animation snapshots are still staged after the revert (a discard is "
                                   "in flight, or a restore failed)";
            // The settings on disk are the imported ones, but the animation
            // page still holds pre-import snapshots. That is a partial result
            // the user has to know about: without this the import reports plain
            // success while the page shows something else.
            Q_EMIT settingsTransferFailed(
                PhosphorI18n::tr("Your settings were imported, but the animation pages still show the old ones. "
                                 "Reopen the settings window to see the imported values."));
            // Report not-fully-landed. The bool's only job is gating the General
            // page's success toast (see the settingsTransferFailed doc), and the
            // toast surface replaces whatever is in flight. Returning true here
            // would let the caller overwrite the reason just emitted, inside the
            // same JS statement, so the user would only ever read "Settings
            // imported" on the one path that exists to say otherwise.
            ok = false;
        }
        if (m_rulesPage) {
            m_rulesPage->revert();
        }
        m_loading = false;
        DaemonDBus::notifyReload();
        if (animationsReverted) {
            setNeedsSave(false);
        }
    }
    return ok;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Screen state query
// ═══════════════════════════════════════════════════════════════════════════════

QVariantList SettingsController::getScreenStates() const
{
    QDBusMessage reply = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                                QStringLiteral("getScreenStates"));
    if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty())
        return {};

    const QString json = reply.arguments().at(0).toString();
    if (json.isEmpty())
        return {};

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray())
        return {};

    QVariantList result;
    for (const QJsonValue& value : doc.array()) {
        if (value.isObject())
            result.append(value.toObject().toVariantMap());
    }
    return result;
}

QVariantMap SettingsController::getStagedAssignment(const QString& screenName, int virtualDesktop,
                                                    const QString& activityId) const
{
    auto* s = m_staging.stagedAssignmentFor(screenName, virtualDesktop, activityId);
    if (!s)
        return {};
    QVariantMap map;
    // A staged full clear carries no mode/layout fields (stageFullClear
    // resets them), so surface it explicitly — otherwise QML restore code
    // would see an empty map and fall back to the daemon's still-resolved
    // explicit values, hiding the pending clear.
    if (s->fullCleared) {
        map[QStringLiteral("fullCleared")] = true;
        return map;
    }
    if (s->snappingLayoutId.has_value())
        map[QStringLiteral("layoutId")] = *s->snappingLayoutId;
    if (s->tilingAlgorithmId.has_value()) {
        const QString& val = *s->tilingAlgorithmId;
        map[QStringLiteral("algorithmId")] =
            PhosphorLayout::LayoutId::isAutotile(val) ? PhosphorLayout::LayoutId::extractAlgorithmId(val) : val;
    }
    // Explicit mode takes priority (stageAssignmentEntry path)
    if (s->stagedMode.has_value()) {
        map[QStringLiteral("mode")] = *s->stagedMode;
    } else {
        // Infer mode from which fields are staged (per-field path)
        if (s->tilingAlgorithmId.has_value() && !s->tilingAlgorithmId->isEmpty())
            map[QStringLiteral("mode")] = 1;
        else if (s->snappingLayoutId.has_value() && !s->snappingLayoutId->isEmpty())
            map[QStringLiteral("mode")] = 0;
    }
    return map;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Per-screen autotile / snapping / zone-selector overrides live in
// settingscontroller_perscreen.cpp.
// ═══════════════════════════════════════════════════════════════════════════════

QVariantMap SettingsController::loadWindowGeometry() const
{
    QSettings settings;
    QVariantMap geo;
    int w = settings.value(ConfigDefaults::settingsAppWindowWidthKey(), 0).toInt();
    int h = settings.value(ConfigDefaults::settingsAppWindowHeightKey(), 0).toInt();
    int x = settings.value(ConfigDefaults::settingsAppWindowXKey()).toInt();
    int y = settings.value(ConfigDefaults::settingsAppWindowYKey()).toInt();
    bool hasPosition = settings.contains(ConfigDefaults::settingsAppWindowXKey());

    // Validate against available screen geometry
    if (w > 0 && h > 0) {
        QRect virtualGeo;
        for (auto* screen : QGuiApplication::screens())
            virtualGeo = virtualGeo.united(screen->availableGeometry());
        if (!virtualGeo.isEmpty()) {
            w = qMin(w, virtualGeo.width());
            h = qMin(h, virtualGeo.height());
            // Check if center of saved window is on any screen
            if (hasPosition && !virtualGeo.contains(QPoint(x + w / 2, y + h / 2))) {
                hasPosition = false; // off-screen, let WM place it
            }
        }
    }

    geo[ConfigDefaults::settingsAppWindowWidthKey()] = w;
    geo[ConfigDefaults::settingsAppWindowHeightKey()] = h;
    geo[ConfigDefaults::settingsAppWindowXKey()] = x;
    geo[ConfigDefaults::settingsAppWindowYKey()] = y;
    geo[QStringLiteral("hasPosition")] = hasPosition;
    return geo;
}

void SettingsController::saveWindowGeometry(int x, int y, int width, int height)
{
    QSettings settings;
    settings.setValue(ConfigDefaults::settingsAppWindowXKey(), x);
    settings.setValue(ConfigDefaults::settingsAppWindowYKey(), y);
    settings.setValue(ConfigDefaults::settingsAppWindowWidthKey(), width);
    settings.setValue(ConfigDefaults::settingsAppWindowHeightKey(), height);
}

// ═══════════════════════════════════════════════════════════════════════════════
// KZones Import — thin wrappers around kzonesimporter.{h,cpp}
// ═══════════════════════════════════════════════════════════════════════════════

bool SettingsController::hasKZonesConfig()
{
    return KZonesImporter::hasKZonesConfig();
}

int SettingsController::importFromKZones()
{
    const auto result = KZonesImporter::importFromKwinrc();
    if (result.imported > 0) {
        m_pendingSelectLayoutId = result.pendingSelectLayoutId;
        scheduleLayoutLoad();
    }
    Q_EMIT kzonesImportFinished(result.imported, result.message);
    return result.imported;
}

int SettingsController::importFromKZonesFile(const QString& filePath)
{
    // Defence-in-depth: same sanitiser, and same QFileDialog entry point, as
    // the layout and algorithm imports.
    const QString safe = Utils::sanitizeIOPath(filePath);
    if (safe.isEmpty()) {
        qCWarning(lcCore) << "importFromKZonesFile: refusing unsafe path" << filePath;
        Q_EMIT kzonesImportFinished(0, PhosphorI18n::tr("That file path is not allowed."));
        return 0;
    }
    const auto result = KZonesImporter::importFromFile(safe);
    if (result.imported > 0) {
        m_pendingSelectLayoutId = result.pendingSelectLayoutId;
        scheduleLayoutLoad();
    }
    Q_EMIT kzonesImportFinished(result.imported, result.message);
    return result.imported;
}

// ── Virtual screen configuration ──────────────────────────────────────────

QStringList SettingsController::getPhysicalScreens() const
{
    QDBusMessage reply = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::Screen),
                                                QStringLiteral("getPhysicalScreens"));
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
        return reply.arguments().first().toStringList();
    }
    return {};
}
} // namespace PlasmaZones
