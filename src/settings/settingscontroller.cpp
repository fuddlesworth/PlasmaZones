// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "settingscontroller.h"
#include "../core/logging.h"
#include "../config/configbackend_qsettings.h"
#include "../../kcm/common/dbusutils.h"

#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDesktopServices>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

namespace PlasmaZones {

SettingsController::SettingsController(QObject* parent)
    : QObject(parent)
    , m_screenHelper(&m_settings, this)
{
    // Listen for external settings changes from the daemon
    QDBusConnection::sessionBus().connect(QString(DBus::ServiceName), QString(DBus::ObjectPath),
                                          QString(DBus::Interface::Settings), QStringLiteral("settingsChanged"), this,
                                          SLOT(onExternalSettingsChanged()));

    // Forward daemon running state changes
    connect(&m_daemonController, &DaemonController::runningChanged, this, &SettingsController::daemonRunningChanged);

    // Screen helper signals
    m_screenHelper.connectToDaemonSignals();
    m_screenHelper.refreshScreens();
    connect(&m_screenHelper, &ScreenHelper::screensChanged, this, &SettingsController::screensChanged);
    connect(&m_screenHelper, &ScreenHelper::needsSave, this, [this]() {
        setNeedsSave(true);
    });

    // Layout load timer (debounce)
    m_layoutLoadTimer.setSingleShot(true);
    m_layoutLoadTimer.setInterval(50);
    connect(&m_layoutLoadTimer, &QTimer::timeout, this, &SettingsController::loadLayoutsAsync);

    // Connect layout D-Bus signals for live updates
    QDBusConnection::sessionBus().connect(QString(DBus::ServiceName), QString(DBus::ObjectPath),
                                          QString(DBus::Interface::LayoutManager), QStringLiteral("layoutCreated"),
                                          this, SLOT(loadLayoutsAsync()));
    QDBusConnection::sessionBus().connect(QString(DBus::ServiceName), QString(DBus::ObjectPath),
                                          QString(DBus::Interface::LayoutManager), QStringLiteral("layoutDeleted"),
                                          this, SLOT(loadLayoutsAsync()));
    QDBusConnection::sessionBus().connect(QString(DBus::ServiceName), QString(DBus::ObjectPath),
                                          QString(DBus::Interface::LayoutManager), QStringLiteral("layoutUpdated"),
                                          this, SLOT(loadLayoutsAsync()));

    // Create editor config backend
    m_editorConfig = createDefaultConfigBackend();

    // Initial loads
    loadEditorSettings();
    scheduleLayoutLoad();
}

void SettingsController::setActivePage(const QString& page)
{
    if (m_activePage != page) {
        m_activePage = page;
        Q_EMIT activePageChanged();
    }
}

void SettingsController::load()
{
    m_settings.load();
    loadEditorSettings();
    m_screenHelper.refreshScreens();
    scheduleLayoutLoad();
    setNeedsSave(false);
}

void SettingsController::save()
{
    m_saving = true;

    // Save main settings
    m_settings.save();

    // Save editor settings
    saveEditorSettings();

    // Notify daemon to reload settings (synchronous to avoid race)
    KCMDBus::notifyReload();

    setNeedsSave(false);

    QTimer::singleShot(0, this, [this]() {
        m_saving = false;
    });
}

void SettingsController::defaults()
{
    m_settings.reset();
    m_settings.load();

    // Reset editor defaults
    setEditorDuplicateShortcut(QStringLiteral("Ctrl+D"));
    setEditorSplitHorizontalShortcut(QStringLiteral("Ctrl+Shift+H"));
    setEditorSplitVerticalShortcut(QStringLiteral("Ctrl+Alt+V"));
    setEditorFillShortcut(QStringLiteral("Ctrl+Shift+F"));
    setEditorGridSnappingEnabled(true);
    setEditorEdgeSnappingEnabled(true);
    setEditorSnapIntervalX(0.05);
    setEditorSnapIntervalY(0.05);

    setNeedsSave(true);
}

void SettingsController::launchEditor()
{
    QProcess::startDetached(QStringLiteral("plasmazones-editor"), {});
}

void SettingsController::onExternalSettingsChanged()
{
    if (!m_saving) {
        load();
    }
}

void SettingsController::setNeedsSave(bool needs)
{
    if (m_needsSave != needs) {
        m_needsSave = needs;
        Q_EMIT needsSaveChanged();
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Layout management (D-Bus to daemon, no KCM LayoutManager class needed)
// ═══════════════════════════════════════════════════════════════════════════════

void SettingsController::scheduleLayoutLoad()
{
    m_layoutLoadTimer.start();
}

void SettingsController::loadLayoutsAsync()
{
    QDBusMessage msg =
        QDBusMessage::createMethodCall(QString(DBus::ServiceName), QString(DBus::ObjectPath),
                                       QString(DBus::Interface::LayoutManager), QStringLiteral("getLayoutList"));

    auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), this);

    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        w->deleteLater();

        QVariantList newLayouts;
        QDBusPendingReply<QStringList> reply = *w;

        if (!reply.isError()) {
            const QStringList layoutJsonList = reply.value();
            for (const QString& layoutJson : layoutJsonList) {
                QJsonDocument doc = QJsonDocument::fromJson(layoutJson.toUtf8());
                if (!doc.isNull() && doc.isObject()) {
                    newLayouts.append(doc.object().toVariantMap());
                }
            }
        } else {
            qCWarning(lcCore) << "Failed to load layouts:" << reply.error().message();
        }

        // Sort: manual layouts first, then autotile, each alphabetical
        std::sort(newLayouts.begin(), newLayouts.end(), [](const QVariant& a, const QVariant& b) {
            const QVariantMap mapA = a.toMap();
            const QVariantMap mapB = b.toMap();
            const bool aIsAutotile = mapA.value(QStringLiteral("isAutotile")).toBool();
            const bool bIsAutotile = mapB.value(QStringLiteral("isAutotile")).toBool();
            if (aIsAutotile != bIsAutotile)
                return !aIsAutotile;
            return mapA.value(QStringLiteral("name")).toString().toLower()
                < mapB.value(QStringLiteral("name")).toString().toLower();
        });

        m_layouts = newLayouts;
        Q_EMIT layoutsChanged();
    });
}

void SettingsController::createNewLayout()
{
    QDBusMessage reply = KCMDBus::callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("createLayout"),
                                             {QStringLiteral("New Layout"), QStringLiteral("custom")});

    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
        QString newLayoutId = reply.arguments().first().toString();
        if (!newLayoutId.isEmpty()) {
            editLayout(newLayoutId);
        }
    }
    scheduleLayoutLoad();
}

void SettingsController::deleteLayout(const QString& layoutId)
{
    QDBusMessage reply =
        KCMDBus::callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("deleteLayout"), {layoutId});
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qCWarning(lcCore) << "deleteLayout failed:" << reply.errorMessage();
    }
    scheduleLayoutLoad();
}

void SettingsController::duplicateLayout(const QString& layoutId)
{
    KCMDBus::callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("duplicateLayout"), {layoutId});
    scheduleLayoutLoad();
}

void SettingsController::editLayout(const QString& layoutId)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(QString(DBus::ServiceName), QString(DBus::ObjectPath),
                                                      QString(DBus::Interface::LayoutManager),
                                                      QStringLiteral("openEditorForLayoutOnScreen"));
    msg << layoutId << QString();
    QDBusConnection::sessionBus().asyncCall(msg);
}

void SettingsController::openLayoutsFolder()
{
    const QString path =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/plasmazones/layouts");
    QDir dir(path);
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

// ═══════════════════════════════════════════════════════════════════════════════
// Screen helpers
// ═══════════════════════════════════════════════════════════════════════════════

bool SettingsController::isMonitorDisabled(const QString& screenName) const
{
    return m_screenHelper.isMonitorDisabled(screenName);
}

void SettingsController::setMonitorDisabled(const QString& screenName, bool disabled)
{
    m_screenHelper.setMonitorDisabled(screenName, disabled);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Editor settings (read/write via IConfigBackend for [Editor] group)
// ═══════════════════════════════════════════════════════════════════════════════

void SettingsController::loadEditorSettings()
{
    if (!m_editorConfig)
        return;

    m_editorConfig->reparseConfiguration();
    auto group = m_editorConfig->group(QStringLiteral("Editor"));

    m_editorDuplicateShortcut = group->readString(QStringLiteral("EditorDuplicateShortcut"), QStringLiteral("Ctrl+D"));
    m_editorSplitHorizontalShortcut =
        group->readString(QStringLiteral("EditorSplitHorizontalShortcut"), QStringLiteral("Ctrl+Shift+H"));
    m_editorSplitVerticalShortcut =
        group->readString(QStringLiteral("EditorSplitVerticalShortcut"), QStringLiteral("Ctrl+Alt+V"));
    m_editorFillShortcut = group->readString(QStringLiteral("EditorFillShortcut"), QStringLiteral("Ctrl+Shift+F"));
    m_editorGridSnappingEnabled = group->readBool(QStringLiteral("GridSnappingEnabled"), true);
    m_editorEdgeSnappingEnabled = group->readBool(QStringLiteral("EdgeSnappingEnabled"), true);

    double intervalX = group->readDouble(QStringLiteral("SnapIntervalX"), -1.0);
    if (intervalX < 0.0)
        intervalX = group->readDouble(QStringLiteral("SnapInterval"), 0.05);
    m_editorSnapIntervalX = intervalX;

    double intervalY = group->readDouble(QStringLiteral("SnapIntervalY"), -1.0);
    if (intervalY < 0.0)
        intervalY = group->readDouble(QStringLiteral("SnapInterval"), 0.05);
    m_editorSnapIntervalY = intervalY;

    Q_EMIT editorDuplicateShortcutChanged();
    Q_EMIT editorSplitHorizontalShortcutChanged();
    Q_EMIT editorSplitVerticalShortcutChanged();
    Q_EMIT editorFillShortcutChanged();
    Q_EMIT editorGridSnappingEnabledChanged();
    Q_EMIT editorEdgeSnappingEnabledChanged();
    Q_EMIT editorSnapIntervalXChanged();
    Q_EMIT editorSnapIntervalYChanged();
}

void SettingsController::saveEditorSettings()
{
    if (!m_editorConfig)
        return;

    auto group = m_editorConfig->group(QStringLiteral("Editor"));

    group->writeString(QStringLiteral("EditorDuplicateShortcut"), m_editorDuplicateShortcut);
    group->writeString(QStringLiteral("EditorSplitHorizontalShortcut"), m_editorSplitHorizontalShortcut);
    group->writeString(QStringLiteral("EditorSplitVerticalShortcut"), m_editorSplitVerticalShortcut);
    group->writeString(QStringLiteral("EditorFillShortcut"), m_editorFillShortcut);
    group->writeBool(QStringLiteral("GridSnappingEnabled"), m_editorGridSnappingEnabled);
    group->writeBool(QStringLiteral("EdgeSnappingEnabled"), m_editorEdgeSnappingEnabled);
    group->writeDouble(QStringLiteral("SnapIntervalX"), m_editorSnapIntervalX);
    group->writeDouble(QStringLiteral("SnapIntervalY"), m_editorSnapIntervalY);

    m_editorConfig->sync();
}

// Editor getters

QString SettingsController::editorDuplicateShortcut() const
{
    return m_editorDuplicateShortcut;
}
QString SettingsController::editorSplitHorizontalShortcut() const
{
    return m_editorSplitHorizontalShortcut;
}
QString SettingsController::editorSplitVerticalShortcut() const
{
    return m_editorSplitVerticalShortcut;
}
QString SettingsController::editorFillShortcut() const
{
    return m_editorFillShortcut;
}
bool SettingsController::editorGridSnappingEnabled() const
{
    return m_editorGridSnappingEnabled;
}
bool SettingsController::editorEdgeSnappingEnabled() const
{
    return m_editorEdgeSnappingEnabled;
}
qreal SettingsController::editorSnapIntervalX() const
{
    return m_editorSnapIntervalX;
}
qreal SettingsController::editorSnapIntervalY() const
{
    return m_editorSnapIntervalY;
}

// Editor setters

void SettingsController::setEditorDuplicateShortcut(const QString& shortcut)
{
    if (m_editorDuplicateShortcut != shortcut) {
        m_editorDuplicateShortcut = shortcut;
        Q_EMIT editorDuplicateShortcutChanged();
        setNeedsSave(true);
    }
}

void SettingsController::setEditorSplitHorizontalShortcut(const QString& shortcut)
{
    if (m_editorSplitHorizontalShortcut != shortcut) {
        m_editorSplitHorizontalShortcut = shortcut;
        Q_EMIT editorSplitHorizontalShortcutChanged();
        setNeedsSave(true);
    }
}

void SettingsController::setEditorSplitVerticalShortcut(const QString& shortcut)
{
    if (m_editorSplitVerticalShortcut != shortcut) {
        m_editorSplitVerticalShortcut = shortcut;
        Q_EMIT editorSplitVerticalShortcutChanged();
        setNeedsSave(true);
    }
}

void SettingsController::setEditorFillShortcut(const QString& shortcut)
{
    if (m_editorFillShortcut != shortcut) {
        m_editorFillShortcut = shortcut;
        Q_EMIT editorFillShortcutChanged();
        setNeedsSave(true);
    }
}

void SettingsController::setEditorGridSnappingEnabled(bool enabled)
{
    if (m_editorGridSnappingEnabled != enabled) {
        m_editorGridSnappingEnabled = enabled;
        Q_EMIT editorGridSnappingEnabledChanged();
        setNeedsSave(true);
    }
}

void SettingsController::setEditorEdgeSnappingEnabled(bool enabled)
{
    if (m_editorEdgeSnappingEnabled != enabled) {
        m_editorEdgeSnappingEnabled = enabled;
        Q_EMIT editorEdgeSnappingEnabledChanged();
        setNeedsSave(true);
    }
}

void SettingsController::setEditorSnapIntervalX(qreal interval)
{
    interval = qBound(0.01, interval, 1.0);
    if (!qFuzzyCompare(m_editorSnapIntervalX, interval)) {
        m_editorSnapIntervalX = interval;
        Q_EMIT editorSnapIntervalXChanged();
        setNeedsSave(true);
    }
}

void SettingsController::setEditorSnapIntervalY(qreal interval)
{
    interval = qBound(0.01, interval, 1.0);
    if (!qFuzzyCompare(m_editorSnapIntervalY, interval)) {
        m_editorSnapIntervalY = interval;
        Q_EMIT editorSnapIntervalYChanged();
        setNeedsSave(true);
    }
}

void SettingsController::resetEditorDefaults()
{
    setEditorDuplicateShortcut(QStringLiteral("Ctrl+D"));
    setEditorSplitHorizontalShortcut(QStringLiteral("Ctrl+Shift+H"));
    setEditorSplitVerticalShortcut(QStringLiteral("Ctrl+Alt+V"));
    setEditorFillShortcut(QStringLiteral("Ctrl+Shift+F"));
    setEditorGridSnappingEnabled(true);
    setEditorEdgeSnappingEnabled(true);
    setEditorSnapIntervalX(0.05);
    setEditorSnapIntervalY(0.05);
}

} // namespace PlasmaZones
