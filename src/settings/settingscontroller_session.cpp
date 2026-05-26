// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Session-state methods for SettingsController:
//   * Font enumeration helpers (Q_INVOKABLE getters consumed by QML).
//   * Stage-assignment + quick-layout-slot setters.
//   * Virtual-desktop + activity tracking (D-Bus signals from KWin /
//     ActivityManager and the daemon's screen-layout broadcast).
//   * Running-windows query (used by the assignments page).
//   * Config import / export (whole-file JSON dump and restore).
//   * Per-screen overrides for autotile / snapping / zone-selector.
//   * Window-geometry persistence (QSettings entry under the
//     organization config file, NOT the main JSON).
//   * KZones import helpers.
//   * Virtual-screen CRUD (D-Bus to daemon + staged-state tracking).
//   * Ordering helpers (effective / resolved / staged snapping +
//     tiling order).
//
// Split out of settingscontroller.cpp to keep that file under the
// 800-line cap (see CLAUDE.md). All methods here are members of
// PlasmaZones::SettingsController and use its private state — same
// class, separate translation unit, no API change.

#include "settingscontroller.h"

#include "../common/screenidresolver.h"
#include "../config/configbackends.h"
#include "../config/configdefaults.h"
#include "../config/configmigration.h"
#include "../core/logging.h"
#include "../core/utils.h"
#include "../pz_i18n.h"
#include "dbusutils.h"
#include "kzonesimporter.h"
#include "virtualscreenutils.h"

#include <PhosphorProtocol/ClientHelpers.h>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QFile>
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

// ═══════════════════════════════════════════════════════════════════════════════
// Quick layout slots (D-Bus to daemon)
// ═══════════════════════════════════════════════════════════════════════════════

QString SettingsController::getQuickLayoutSlot(int slotNumber) const
{
    if (slotNumber < 1 || slotNumber > 9)
        return {};
    QString staged;
    if (m_staging.stagedSnappingQuickSlot(slotNumber, staged))
        return staged;
    QDBusMessage reply = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                                QStringLiteral("getQuickLayoutSlot"), {slotNumber});
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty())
        return reply.arguments().first().toString();
    return {};
}

void SettingsController::setQuickLayoutSlot(int slotNumber, const QString& layoutId)
{
    if (slotNumber < 1 || slotNumber > 9)
        return;
    m_staging.stageSnappingQuickSlot(slotNumber, layoutId);
    setNeedsSave(true);
}

QString SettingsController::getQuickLayoutShortcut(int slotNumber) const
{
    if (slotNumber < 1 || slotNumber > 9)
        return {};
    // Return the default shortcut string -- the standalone cannot query KGlobalAccel
    // since it doesn't link KF6::GlobalAccel. The shortcut is Meta+Alt+N.
    return QStringLiteral("Meta+Alt+%1").arg(slotNumber);
}

QString SettingsController::getTilingQuickLayoutSlot(int slotNumber) const
{
    if (slotNumber < 1 || slotNumber > 9)
        return {};
    QString staged;
    if (m_staging.stagedTilingQuickSlot(slotNumber, staged))
        return staged;
    return m_settings.readTilingQuickLayoutSlot(slotNumber);
}

void SettingsController::setTilingQuickLayoutSlot(int slotNumber, const QString& layoutId)
{
    if (slotNumber < 1 || slotNumber > 9)
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
    }

    QDBusMessage namesReply = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                                     QStringLiteral("getVirtualDesktopNames"));
    if (namesReply.type() == QDBusMessage::ReplyMessage && !namesReply.arguments().isEmpty()) {
        m_virtualDesktopNames = namesReply.arguments().first().toStringList();
    } else if (namesReply.type() == QDBusMessage::ErrorMessage) {
        qCWarning(lcCore) << "refreshVirtualDesktops: getVirtualDesktopNames D-Bus call failed:"
                          << namesReply.errorMessage();
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
            QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
            if (doc.isArray()) {
                m_activities.clear();
                for (const auto& val : doc.array()) {
                    m_activities.append(val.toObject().toVariantMap());
                }
            }
        } else if (infoReply.type() == QDBusMessage::ErrorMessage) {
            qCWarning(lcCore) << "refreshActivities: getAllActivitiesInfo D-Bus call failed:"
                              << infoReply.errorMessage();
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

    // Prune both per-mode disabled-desktop lists — see start.cpp comment re:
    // mid-range renumbering limitation. The per-mode disabledDesktopsChanged
    // signal is forwarded from m_settings (see ctor); no manual emit here.
    for (const auto mode : {PhosphorZones::AssignmentEntry::Snapping, PhosphorZones::AssignmentEntry::Autotile}) {
        QStringList disabled = m_settings.disabledDesktops(mode);
        if (pruneDisabledDesktopEntries(disabled, m_virtualDesktopCount)) {
            m_settings.setDisabledDesktops(mode, disabled);
            setNeedsSave(true);
        }
    }

    Q_EMIT virtualDesktopsChanged();
}

void SettingsController::onActivitiesChanged()
{
    refreshActivities();

    // Prune disabled-activity entries that reference removed activities
    if (!m_activities.isEmpty()) {
        QSet<QString> validIds;
        for (const QVariant& v : std::as_const(m_activities)) {
            const QVariantMap map = v.toMap();
            const QString id = map.value(QStringLiteral("id")).toString();
            if (!id.isEmpty()) {
                validIds.insert(id);
            }
        }
        // Per-mode signal forwarded from m_settings (see ctor) — no manual emit.
        for (const auto mode : {PhosphorZones::AssignmentEntry::Snapping, PhosphorZones::AssignmentEntry::Autotile}) {
            QStringList disabledActs = m_settings.disabledActivities(mode);
            if (pruneDisabledActivityEntries(disabledActs, validIds)) {
                m_settings.setDisabledActivities(mode, disabledActs);
                setNeedsSave(true);
            }
        }
    }

    Q_EMIT activitiesChanged();
}

void SettingsController::onScreenLayoutChanged(const QString& screenId, const QString& layoutId, int virtualDesktop)
{
    Q_UNUSED(screenId)
    Q_UNUSED(layoutId)
    Q_UNUSED(virtualDesktop)
    // External assignment change (hotkey, script, toggle) — refresh overview
    Q_EMIT screenLayoutChanged();
}

// Parses the daemon's running-windows JSON payload into a QVariantList of
// {windowClass, appName, caption} maps ready for QML consumption. The
// synchronous getRunningWindows() predecessor was removed in Phase 6 of
// refactor/dbus-performance; only onRunningWindowsAvailable calls this now.
static QVariantList parseRunningWindowsJson(const QString& json)
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

void SettingsController::requestRunningWindows()
{
    // Fire-and-forget: the daemon emits runningWindowsRequested to the
    // KWin effect, which answers via provideRunningWindows, which the
    // daemon fans out on runningWindowsAvailable — caught by our
    // onRunningWindowsAvailable slot. The UI thread never blocks.
    //
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
    if (filePath.isEmpty()) {
        return false;
    }
    // Flush current in-memory settings to disk so the exported file reflects
    // the actual current state, not the last-saved snapshot.
    m_settings.save();
    const QString configPath = PlasmaZones::ConfigDefaults::configFilePath();
    if (!QFile::exists(configPath)) {
        qCWarning(PlasmaZones::lcCore) << "Config file not found:" << configPath;
        return false;
    }
    // Remove destination if it exists (QFile::copy won't overwrite)
    if (QFile::exists(filePath)) {
        QFile::remove(filePath);
    }
    bool ok = QFile::copy(configPath, filePath);
    if (!ok) {
        qCWarning(PlasmaZones::lcCore) << "Failed to export settings to:" << filePath;
    }
    return ok;
}

bool SettingsController::importAllSettings(const QString& filePath)
{
    if (filePath.isEmpty() || !QFile::exists(filePath)) {
        return false;
    }

    const QString configPath = PlasmaZones::ConfigDefaults::configFilePath();

    // Detect if the imported file is legacy INI format (not JSON).
    // If so, run the migration converter to produce a JSON file.
    bool isLegacyIni = false;
    {
        QFile f(filePath);
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
                return false;
            }
            if (head.size() >= 4 && byte(0) == 0xFF && byte(1) == 0xFE && byte(2) == 0x00 && byte(3) == 0x00) {
                qCWarning(PlasmaZones::lcCore) << "Import file has UTF-32 LE BOM, refusing:" << filePath;
                return false;
            }
            if (head.size() >= 2 && ((byte(0) == 0xFE && byte(1) == 0xFF) || (byte(0) == 0xFF && byte(1) == 0xFE))) {
                qCWarning(PlasmaZones::lcCore) << "Import file has UTF-16 BOM, refusing:" << filePath;
                return false;
            }
            head = head.trimmed();
            // Skip UTF-8 BOM (EF BB BF) if present — trimmed() only strips ASCII whitespace.
            if (head.size() >= 3 && byte(0) == 0xEF && byte(1) == 0xBB && byte(2) == 0xBF) {
                head = head.mid(3).trimmed();
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
        return false;
    }

    bool ok = false;
    if (isLegacyIni) {
        // Convert INI to JSON in-place using the migration module
        if (QFile::exists(configPath)) {
            QFile::remove(configPath);
        }
        ok = PlasmaZones::ConfigMigration::migrateIniToJson(filePath, configPath);
        if (!ok) {
            qCWarning(PlasmaZones::lcCore) << "Failed to convert legacy INI file:" << filePath;
        }
    } else {
        // Validate JSON before overwriting current config
        QFile importFile(filePath);
        if (!importFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qCWarning(PlasmaZones::lcCore) << "Failed to open import file:" << filePath;
            ok = false;
        } else {
            QJsonParseError parseErr;
            QJsonDocument importDoc = QJsonDocument::fromJson(importFile.readAll(), &parseErr);
            if (parseErr.error != QJsonParseError::NoError || !importDoc.isObject()) {
                qCWarning(PlasmaZones::lcCore) << "Invalid JSON in import file:" << filePath << parseErr.errorString();
                ok = false;
            } else {
                // Valid JSON — copy to config path
                if (QFile::exists(configPath)) {
                    QFile::remove(configPath);
                }
                ok = QFile::copy(filePath, configPath);
                if (!ok) {
                    qCWarning(PlasmaZones::lcCore) << "Failed to import settings from:" << filePath;
                }
            }
        }
    }

    if (!ok) {
        // Restore backup on failure. Use rename-then-verify so we never
        // delete the (possibly still-good) configPath without proof the
        // backup is in place — a failed rename would otherwise leave the
        // user with no config file at all.
        if (QFile::exists(backupPath)) {
            if (QFile::exists(configPath)) {
                QFile::remove(configPath);
            }
            if (!QFile::rename(backupPath, configPath)) {
                qCWarning(PlasmaZones::lcCore)
                    << "Failed to restore config from backup after failed import. Backup remains at:" << backupPath;
            }
        }
    } else {
        // Clean up backup on success
        QFile::remove(backupPath);
        // Wrap the in-memory reload so property NOTIFY signals don't mark
        // pages dirty — the imported config is already on disk.
        m_loading = true;
        m_settings.load();
        m_loading = false;
        // Page controllers with their own on-disk staging surfaces
        // (animations / window-rules) must reload too — m_settings.load()
        // only refreshes settings.json-backed state. Without these the
        // imported config disagrees with what the page controllers still
        // hold in their in-memory snapshots.
        if (m_animationsPage) {
            m_animationsPage->revertPending();
        }
        if (m_windowRulesPage) {
            m_windowRulesPage->revert();
        }
        DaemonDBus::notifyReload();
        setNeedsSave(false);
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
// Per-screen autotile overrides
// ═══════════════════════════════════════════════════════════════════════════════

QVariantMap SettingsController::getPerScreenAutotileSettings(const QString& screenName) const
{
    return m_settings.getPerScreenAutotileSettings(screenName);
}

void SettingsController::setPerScreenAutotileSetting(const QString& screenName, const QString& key,
                                                     const QVariant& value)
{
    m_settings.setPerScreenAutotileSetting(screenName, key, value);
    setNeedsSave(true);
}

void SettingsController::clearPerScreenAutotileSettings(const QString& screenName)
{
    m_settings.clearPerScreenAutotileSettings(screenName);
    setNeedsSave(true);
}

bool SettingsController::hasPerScreenAutotileSettings(const QString& screenName) const
{
    return m_settings.hasPerScreenAutotileSettings(screenName);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Per-screen snapping overrides
// ═══════════════════════════════════════════════════════════════════════════════

QVariantMap SettingsController::getPerScreenSnappingSettings(const QString& screenName) const
{
    return m_settings.getPerScreenSnappingSettings(screenName);
}

void SettingsController::setPerScreenSnappingSetting(const QString& screenName, const QString& key,
                                                     const QVariant& value)
{
    m_settings.setPerScreenSnappingSetting(screenName, key, value);
    setNeedsSave(true);
}

void SettingsController::clearPerScreenSnappingSettings(const QString& screenName)
{
    m_settings.clearPerScreenSnappingSettings(screenName);
    setNeedsSave(true);
}

bool SettingsController::hasPerScreenSnappingSettings(const QString& screenName) const
{
    return m_settings.hasPerScreenSnappingSettings(screenName);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Per-screen zone selector overrides
// ═══════════════════════════════════════════════════════════════════════════════

QVariantMap SettingsController::getPerScreenZoneSelectorSettings(const QString& screenName) const
{
    return m_settings.getPerScreenZoneSelectorSettings(screenName);
}

void SettingsController::setPerScreenZoneSelectorSetting(const QString& screenName, const QString& key,
                                                         const QVariant& value)
{
    m_settings.setPerScreenZoneSelectorSetting(screenName, key, value);
    setNeedsSave(true);
}

void SettingsController::clearPerScreenZoneSelectorSettings(const QString& screenName)
{
    m_settings.clearPerScreenZoneSelectorSettings(screenName);
    setNeedsSave(true);
}

bool SettingsController::hasPerScreenZoneSelectorSettings(const QString& screenName) const
{
    return m_settings.hasPerScreenZoneSelectorSettings(screenName);
}

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
    const auto result = KZonesImporter::importFromFile(filePath);
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

QVariantList SettingsController::getVirtualScreenConfig(const QString& physicalScreenId) const
{
    QDBusMessage reply = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::Screen),
                                                QStringLiteral("getVirtualScreenConfig"), {physicalScreenId});
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
        QString json = reply.arguments().first().toString();
        QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
        if (doc.isObject()) {
            QJsonObject root = doc.object();
            QJsonArray screensArr = root.value(QLatin1String("screens")).toArray();
            QVariantList result;
            for (const auto& entry : screensArr) {
                QJsonObject screenObj = entry.toObject();
                QJsonObject regionObj = screenObj.value(QLatin1String("region")).toObject();
                QVariantMap screen;
                screen[QStringLiteral("displayName")] = screenObj.value(QLatin1String("displayName")).toString();
                screen[QStringLiteral("x")] = regionObj.value(::PhosphorZones::ZoneJsonKeys::X).toDouble();
                screen[QStringLiteral("y")] = regionObj.value(::PhosphorZones::ZoneJsonKeys::Y).toDouble();
                screen[QStringLiteral("width")] = regionObj.value(::PhosphorZones::ZoneJsonKeys::Width).toDouble();
                screen[QStringLiteral("height")] = regionObj.value(::PhosphorZones::ZoneJsonKeys::Height).toDouble();
                screen[QStringLiteral("index")] = screenObj.value(QLatin1String("index")).toInt();
                result.append(screen);
            }
            return result;
        }
    }
    return {};
}

void SettingsController::applyVirtualScreenConfig(const QString& physicalScreenId, const QVariantList& screens)
{
    QJsonObject root;
    root[QLatin1String("physicalScreenId")] = physicalScreenId;

    QJsonArray screensArr;
    for (int i = 0; i < screens.size(); ++i) {
        Phosphor::Screens::VirtualScreenDef def =
            VirtualScreenUtils::variantMapToVirtualScreenDef(screens[i].toMap(), physicalScreenId, i);
        if (!def.isValid()) {
            qCWarning(lcConfig) << "Skipping invalid virtual screen def for" << physicalScreenId << "index" << i
                                << "region:" << def.region;
            continue;
        }
        QJsonObject screenObj;
        screenObj[QLatin1String("index")] = def.index;
        screenObj[QLatin1String("displayName")] = def.displayName;
        screenObj[QLatin1String("region")] = QJsonObject{{::PhosphorZones::ZoneJsonKeys::X, def.region.x()},
                                                         {::PhosphorZones::ZoneJsonKeys::Y, def.region.y()},
                                                         {::PhosphorZones::ZoneJsonKeys::Width, def.region.width()},
                                                         {::PhosphorZones::ZoneJsonKeys::Height, def.region.height()}};
        screensArr.append(screenObj);
    }
    root[QLatin1String("screens")] = screensArr;

    QString json = QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
    DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::Screen),
                           QStringLiteral("setVirtualScreenConfig"), {physicalScreenId, json});
}

void SettingsController::removeVirtualScreenConfig(const QString& physicalScreenId)
{
    applyVirtualScreenConfig(physicalScreenId, {});
}

void SettingsController::stageVirtualScreenConfig(const QString& physicalScreenId, const QVariantList& screens)
{
    m_staging.stageVirtualScreenConfig(physicalScreenId, screens);
    setNeedsSave(true);
}

void SettingsController::stageVirtualScreenRemoval(const QString& physicalScreenId)
{
    m_staging.stageVirtualScreenRemoval(physicalScreenId);
    setNeedsSave(true);
}

bool SettingsController::hasUnsavedVirtualScreenConfig(const QString& physicalScreenId) const
{
    return m_staging.hasUnsavedVirtualScreenConfig(physicalScreenId);
}

QVariantList SettingsController::getStagedVirtualScreenConfig(const QString& physicalScreenId) const
{
    return m_staging.stagedVirtualScreenConfig(physicalScreenId);
}

} // namespace PlasmaZones
