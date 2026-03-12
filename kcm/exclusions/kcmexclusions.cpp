// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "kcmexclusions.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QTimer>
#include <QJsonArray>
#include "../common/dbusutils.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <KPluginFactory>
#include "../../src/config/configdefaults.h"
#include "../../src/config/settings.h"
#include "../../src/core/constants.h"

K_PLUGIN_CLASS_WITH_JSON(PlasmaZones::KCMExclusions, "kcm_plasmazones_exclusions.json")

namespace PlasmaZones {

KCMExclusions::KCMExclusions(QObject* parent, const KPluginMetaData& data)
    : KQuickConfigModule(parent, data)
{
    m_settings = new Settings(this);
    setButtons(Apply | Default);

    QDBusConnection::sessionBus().connect(QString(DBus::ServiceName), QString(DBus::ObjectPath),
                                          QString(DBus::Interface::Settings), QStringLiteral("settingsChanged"), this,
                                          SLOT(onExternalSettingsChanged()));
}

// ── Load / Save ─────────────────────────────────────────────────────────

void KCMExclusions::load()
{
    KQuickConfigModule::load();
    m_settings->load();
    emitAllChanged();
    setNeedsSave(false);
}

void KCMExclusions::save()
{
    m_saving = true;
    m_settings->save();

    KCMDBus::notifyReload();

    KQuickConfigModule::save();
    setNeedsSave(false);
    QTimer::singleShot(0, this, [this]() {
        m_saving = false;
    });
}

void KCMExclusions::onExternalSettingsChanged()
{
    if (!m_saving) {
        load();
    }
}

void KCMExclusions::defaults()
{
    KQuickConfigModule::defaults();

    setExcludedApplications({});
    setExcludedWindowClasses({});
    setExcludeTransientWindows(ConfigDefaults::excludeTransientWindows());
    setMinimumWindowWidth(ConfigDefaults::minimumWindowWidth());
    setMinimumWindowHeight(ConfigDefaults::minimumWindowHeight());
}

void KCMExclusions::emitAllChanged()
{
    Q_EMIT excludedApplicationsChanged();
    Q_EMIT excludedWindowClassesChanged();
    Q_EMIT excludeTransientWindowsChanged();
    Q_EMIT minimumWindowWidthChanged();
    Q_EMIT minimumWindowHeightChanged();
}

// ── Properties ──────────────────────────────────────────────────────────

QStringList KCMExclusions::excludedApplications() const
{
    return m_settings->excludedApplications();
}

void KCMExclusions::setExcludedApplications(const QStringList& apps)
{
    if (m_settings->excludedApplications() != apps) {
        m_settings->setExcludedApplications(apps);
        Q_EMIT excludedApplicationsChanged();
        setNeedsSave(true);
    }
}

QStringList KCMExclusions::excludedWindowClasses() const
{
    return m_settings->excludedWindowClasses();
}

void KCMExclusions::setExcludedWindowClasses(const QStringList& classes)
{
    if (m_settings->excludedWindowClasses() != classes) {
        m_settings->setExcludedWindowClasses(classes);
        Q_EMIT excludedWindowClassesChanged();
        setNeedsSave(true);
    }
}

bool KCMExclusions::excludeTransientWindows() const
{
    return m_settings->excludeTransientWindows();
}

void KCMExclusions::setExcludeTransientWindows(bool exclude)
{
    if (m_settings->excludeTransientWindows() != exclude) {
        m_settings->setExcludeTransientWindows(exclude);
        Q_EMIT excludeTransientWindowsChanged();
        setNeedsSave(true);
    }
}

int KCMExclusions::minimumWindowWidth() const
{
    return m_settings->minimumWindowWidth();
}

void KCMExclusions::setMinimumWindowWidth(int width)
{
    if (m_settings->minimumWindowWidth() != width) {
        m_settings->setMinimumWindowWidth(width);
        Q_EMIT minimumWindowWidthChanged();
        setNeedsSave(true);
    }
}

int KCMExclusions::minimumWindowHeight() const
{
    return m_settings->minimumWindowHeight();
}

void KCMExclusions::setMinimumWindowHeight(int height)
{
    if (m_settings->minimumWindowHeight() != height) {
        m_settings->setMinimumWindowHeight(height);
        Q_EMIT minimumWindowHeightChanged();
        setNeedsSave(true);
    }
}

// ── List manipulation ───────────────────────────────────────────────────

void KCMExclusions::addExcludedApp(const QString& app)
{
    auto apps = m_settings->excludedApplications();
    if (!apps.contains(app)) {
        apps.append(app);
        setExcludedApplications(apps);
    }
}

void KCMExclusions::removeExcludedApp(int index)
{
    auto apps = m_settings->excludedApplications();
    if (index >= 0 && index < apps.size()) {
        apps.removeAt(index);
        setExcludedApplications(apps);
    }
}

void KCMExclusions::addExcludedWindowClass(const QString& windowClass)
{
    auto classes = m_settings->excludedWindowClasses();
    if (!classes.contains(windowClass)) {
        classes.append(windowClass);
        setExcludedWindowClasses(classes);
    }
}

void KCMExclusions::removeExcludedWindowClass(int index)
{
    auto classes = m_settings->excludedWindowClasses();
    if (index >= 0 && index < classes.size()) {
        classes.removeAt(index);
        setExcludedWindowClasses(classes);
    }
}

// ── D-Bus: running windows ──────────────────────────────────────────────

QVariantList KCMExclusions::getRunningWindows()
{
    QDBusMessage reply = KCMDBus::callDaemon(QString(DBus::Interface::Settings), QStringLiteral("getRunningWindows"));

    if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty()) {
        return {};
    }

    QString json = reply.arguments().at(0).toString();
    if (json.isEmpty()) {
        return {};
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        return {};
    }

    QVariantList result;
    const QJsonArray array = doc.array();
    for (const QJsonValue& value : array) {
        if (!value.isObject()) {
            continue;
        }
        QJsonObject obj = value.toObject();
        QVariantMap item;
        item[QStringLiteral("windowClass")] = obj[QLatin1String("windowClass")].toString();
        item[QStringLiteral("appName")] = obj[QLatin1String("appName")].toString();
        item[QStringLiteral("caption")] = obj[QLatin1String("caption")].toString();
        result.append(item);
    }

    return result;
}

} // namespace PlasmaZones

#include "kcmexclusions.moc"
