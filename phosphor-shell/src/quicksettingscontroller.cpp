// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#include "QuickSettingsController.h"
#include "PhosphorShellI18n.h"

#include <KConfigGroup>
#include <KConfigWatcher>
#include <QCoreApplication>
#include <QPointer>
#include <QDBusArgument>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QTime>

namespace PhosphorShellApp {
namespace {
const QString kwin = QStringLiteral("org.kde.KWin");
const QString nightPath = QStringLiteral("/org/kde/KWin/NightLight");
const QString nightInterface = QStringLiteral("org.kde.KWin.NightLight");
const QString scheduleService = QStringLiteral("org.kde.NightTime");
const QString schedulePath = QStringLiteral("/org/kde/NightTime/Manager");
const QString scheduleInterface = QStringLiteral("org.kde.NightTime.Manager");
const QString power = QStringLiteral("org.freedesktop.UPower.PowerProfiles");
const QString powerPath = QStringLiteral("/org/freedesktop/UPower/PowerProfiles");
const QString properties = QStringLiteral("org.freedesktop.DBus.Properties");
QDBusMessage getAll(const QString& service, const QString& path, const QString& interface)
{
    auto message = QDBusMessage::createMethodCall(service, path, properties, QStringLiteral("GetAll"));
    message << interface;
    return message;
}
QDBusMessage nightCall(const QString& method, const QString& owner)
{
    return QDBusMessage::createMethodCall(owner, nightPath, nightInterface, method);
}
QStringList profileIds(const QVariant& value)
{
    QList<QVariantMap> rows;
    if (value.metaType() == QMetaType::fromType<QDBusArgument>())
        rows = qdbus_cast<QList<QVariantMap>>(value.value<QDBusArgument>());
    else
        for (const auto& item : value.toList())
            rows.append(item.toMap());
    QStringList result;
    for (const auto& row : rows) {
        const auto id = row.value(QLatin1String("Profile")).toString();
        if ((id == QLatin1String("power-saver") || id == QLatin1String("balanced")
             || id == QLatin1String("performance"))
            && !result.contains(id))
            result.append(id);
    }
    return result;
}
}
QuickSettingsController::QuickSettingsController(QObject* parent)
    : QuickSettingsController(QDBusConnection::sessionBus(), QDBusConnection::systemBus(), QStringLiteral("kwinrc"),
                              parent)
{
}
QuickSettingsController::QuickSettingsController(QDBusConnection session, QDBusConnection system, QString configPath,
                                                 QObject* parent)
    : QObject(parent)
    , m_session(std::move(session))
    , m_system(std::move(system))
    , m_config(KSharedConfig::openConfig(configPath, KConfig::SimpleConfig))
    , m_scheduleConfig(
          KSharedConfig::openConfig(QFileInfo(configPath).path() == QLatin1String(".")
                                        ? QStringLiteral("knighttimerc")
                                        : QFileInfo(configPath).dir().filePath(QStringLiteral("knighttimerc")),
                                    KConfig::SimpleConfig))
{
    auto* compositor = new QDBusServiceWatcher(kwin, m_session, QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(compositor, &QDBusServiceWatcher::serviceOwnerChanged, this, [this] {
        const bool hadState = m_cookie || m_nightPending || m_night != NightState{};
        ++m_nightEpoch;
        m_cookie.reset();
        m_cookieOwner.clear();
        m_resumeTimer.stop();
        m_resumeAt = 0;
        m_autoResume = false;
        m_nightPending = false;
        m_night = {};
        if (hadState)
            Q_EMIT changed();
        refreshNightLight();
    });
    auto* profiles = new QDBusServiceWatcher(power, m_system, QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(profiles, &QDBusServiceWatcher::serviceOwnerChanged, this, [this] {
        const bool hadState = m_powerPending || m_power != PowerState{};
        ++m_powerEpoch;
        m_powerPending = false;
        m_power = {};
        if (hadState)
            Q_EMIT changed();
        refreshPower();
    });
    auto* scheduler =
        new QDBusServiceWatcher(scheduleService, m_session, QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(scheduler, &QDBusServiceWatcher::serviceOwnerChanged, this, &QuickSettingsController::refreshSchedule);
    for (const auto& item : {qMakePair(nightPath, kwin), qMakePair(powerPath, power)}) {
        auto& bus = item.second == kwin ? m_session : m_system;
        bus.connect(item.second, item.first, properties, QStringLiteral("PropertiesChanged"), this,
                    SLOT(propertiesChanged(QString, QVariantMap, QStringList)));
    }
    // KConfig notifications use relative names. Absolute injected paths
    // use file and directory watches, including atomic file replacement.
    QStringList absolutePaths;
    for (const auto& config : {m_config, m_scheduleConfig}) {
        if (QDir::isAbsolutePath(config->name())) {
            absolutePaths.append(config->name());
            continue;
        }
        const auto watcher = KConfigWatcher::create(config);
        connect(watcher.data(), &KConfigWatcher::configChanged, this, &QuickSettingsController::refreshNightLight);
        m_configWatchers.append(watcher);
    }
    if (!absolutePaths.isEmpty()) {
        auto* watcher = new QFileSystemWatcher(this);
        auto updatePaths = [watcher, absolutePaths] {
            for (const auto& path : absolutePaths) {
                const auto directory = QFileInfo(path).absolutePath();
                if (!watcher->directories().contains(directory))
                    watcher->addPath(directory);
                if (QFileInfo::exists(path) && !watcher->files().contains(path))
                    watcher->addPath(path);
            }
        };
        auto reload = [this, updatePaths] {
            updatePaths();
            refreshNightLight();
        };
        connect(watcher, &QFileSystemWatcher::fileChanged, this, reload);
        connect(watcher, &QFileSystemWatcher::directoryChanged, this, reload);
        updatePaths();
    }
    m_resumeTimer.setSingleShot(true);
    connect(&m_resumeTimer, &QTimer::timeout, this, &QuickSettingsController::resumeNightLight);
    readNightConfig();
    refreshNightLight();
    refreshPower();
    refreshSchedule();
}
QuickSettingsController::~QuickSettingsController()
{
    if (m_cookie) {
        auto message = nightCall(QStringLiteral("uninhibit"), m_cookieOwner);
        message << *m_cookie;
        m_session.asyncCall(message);
    }
}
void QuickSettingsController::readNightConfig()
{
    m_config->reparseConfiguration();
    m_scheduleConfig->reparseConfiguration();
    const KConfigGroup night(m_config, QStringLiteral("NightColor"));
    const KConfigGroup general(m_scheduleConfig, QStringLiteral("General"));
    const KConfigGroup times(m_scheduleConfig, QStringLiteral("Times"));
    const int temperature = qBound(1000, night.readEntry(QStringLiteral("NightTemperature"), 4500), 6500);
    const QString mode = night.readEntry(QStringLiteral("Mode"), QStringLiteral("DarkLight"));
    const QString source = general.readEntry(QStringLiteral("Source"), QStringLiteral("Location"));
    const QString schedule = mode == QLatin1String("Constant") ? QStringLiteral("manual")
        : source == QLatin1String("Times")                     ? QStringLiteral("custom")
                                                               : QStringLiteral("automatic");
    const QString morning =
        times.readEntry(QStringLiteral("SunriseStart"), QTime(6, 0)).toString(QStringLiteral("HH:mm"));
    const QString evening =
        times.readEntry(QStringLiteral("SunsetStart"), QTime(18, 0)).toString(QStringLiteral("HH:mm"));
    if (m_temperature != temperature || m_schedule != schedule || m_morning != morning || m_evening != evening) {
        if (m_cookie && (m_schedule != schedule || m_morning != morning || m_evening != evening)) {
            m_autoResume = schedule != QLatin1String("manual");
            m_resumeAt = 0;
            m_resumeTimer.stop();
        }
        m_temperature = temperature;
        m_schedule = schedule;
        m_morning = morning;
        m_evening = evening;
        Q_EMIT changed();
    }
}
void QuickSettingsController::refreshSchedule()
{
    const auto serial = ++m_scheduleRead;
    auto* watcher = new QDBusPendingCallWatcher(
        m_session.asyncCall(getAll(scheduleService, schedulePath, scheduleInterface)), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, serial] {
        const QDBusPendingReply<QVariantMap> reply = *watcher;
        watcher->deleteLater();
        if (serial != m_scheduleRead)
            return;
        const bool available = !reply.isError() && reply.value().value(QLatin1String("Version")).toUInt() >= 1;
        if (available != m_scheduleAvailable) {
            m_scheduleAvailable = available;
            Q_EMIT changed();
        }
    });
}
void QuickSettingsController::refreshNightLight()
{
    readNightConfig();
    refreshSchedule();
    const auto serial = ++m_nightRead;
    const auto epoch = m_nightEpoch;
    auto* watcher = new QDBusPendingCallWatcher(m_session.asyncCall(getAll(kwin, nightPath, nightInterface)), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, serial, epoch] {
        const QDBusPendingReply<QVariantMap> reply = *watcher;
        watcher->deleteLater();
        if (serial != m_nightRead || epoch != m_nightEpoch)
            return;
        const auto values = reply.isError() ? QVariantMap{} : reply.value();
        const NightState state{
            values.value(QLatin1String("available")).toBool(), values.value(QLatin1String("enabled")).toBool(),
            values.value(QLatin1String("running")).toBool(), values.value(QLatin1String("inhibited")).toBool(),
            values.value(QLatin1String("scheduledTransitionDateTime")).toLongLong()};
        if (state != m_night) {
            m_night = state;
            Q_EMIT changed();
        }
        if (m_cookie && m_autoResume && (m_resumeAt == 0 || m_resumeAt > QDateTime::currentSecsSinceEpoch())
            && state.transition > QDateTime::currentSecsSinceEpoch())
            m_resumeAt = state.transition;
        if (m_cookie && state.available && !state.enabled && m_nightError.isEmpty())
            resumeNightLight();
        else
            scheduleResume();
    });
}
void QuickSettingsController::refreshPower()
{
    const auto serial = ++m_powerRead;
    const auto epoch = m_powerEpoch;
    auto* watcher = new QDBusPendingCallWatcher(m_system.asyncCall(getAll(power, powerPath, power)), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, serial, epoch] {
        const QDBusPendingReply<QVariantMap> reply = *watcher;
        watcher->deleteLater();
        if (serial != m_powerRead || epoch != m_powerEpoch)
            return;
        const auto values = reply.isError() ? QVariantMap{} : reply.value();
        const PowerState state{!reply.isError(), values.value(QLatin1String("ActiveProfile")).toString(),
                               profileIds(values.value(QLatin1String("Profiles"))),
                               values.value(QLatin1String("PerformanceDegraded")).toString()};
        if (state != m_power) {
            m_power = state;
            Q_EMIT changed();
        }
    });
}
void QuickSettingsController::propertiesChanged(const QString& interface, const QVariantMap&, const QStringList&)
{
    if (interface == nightInterface)
        refreshNightLight();
    else if (interface == power)
        refreshPower();
}
void QuickSettingsController::setNightError(const QString& error)
{
    if (error == m_nightError)
        return;
    m_nightError = error;
    Q_EMIT changed();
}
void QuickSettingsController::applyNightConfig(const QVariantMap& night, const QVariantMap& general,
                                               const QVariantMap& times)
{
    if (!m_night.available || m_nightPending)
        return;
    const bool shared = !general.isEmpty() || !times.isEmpty();
    if (shared && !m_scheduleAvailable) {
        setNightError(PhosphorI18n::tr("The system day and night schedule is unavailable"));
        return;
    }
    if (!m_config->isConfigWritable(false) || (shared && !m_scheduleConfig->isConfigWritable(false))) {
        setNightError(PhosphorI18n::tr("Could not save Night light settings"));
        return;
    }
    // Both files belong to KDE. Preserve unrelated keys and restore the
    // previous values if either save fails, including previously absent keys.
    auto previous = [](const KSharedConfig::Ptr& config, const QString& name, const QVariantMap& entries) {
        const KConfigGroup group(config, name);
        QVariantMap values;
        for (auto it = entries.cbegin(); it != entries.cend(); ++it)
            values.insert(it.key(),
                          group.hasKey(it.key()) ? QVariant(group.readEntry(it.key(), QString())) : QVariant());
        return values;
    };
    auto writeGroup = [](const KSharedConfig::Ptr& config, const QString& name, const QVariantMap& entries) {
        KConfigGroup group(config, name);
        for (auto it = entries.cbegin(); it != entries.cend(); ++it) {
            if (it.value().isValid())
                group.writeEntry(it.key(), it.value(), KConfig::Notify);
            else
                group.deleteEntry(it.key(), KConfig::Notify);
        }
    };
    const auto oldNight = previous(m_config, QStringLiteral("NightColor"), night);
    const auto oldGeneral = previous(m_scheduleConfig, QStringLiteral("General"), general);
    const auto oldTimes = previous(m_scheduleConfig, QStringLiteral("Times"), times);
    writeGroup(m_config, QStringLiteral("NightColor"), night);
    writeGroup(m_scheduleConfig, QStringLiteral("General"), general);
    writeGroup(m_scheduleConfig, QStringLiteral("Times"), times);
    if ((shared && !m_scheduleConfig->sync()) || !m_config->sync()) {
        writeGroup(m_config, QStringLiteral("NightColor"), oldNight);
        writeGroup(m_scheduleConfig, QStringLiteral("General"), oldGeneral);
        writeGroup(m_scheduleConfig, QStringLiteral("Times"), oldTimes);
        const bool restoredSchedule = !shared || m_scheduleConfig->sync();
        const bool restoredNight = m_config->sync();
        // A failed rollback must not be silently saved later by a destructor.
        m_config->markAsClean();
        m_scheduleConfig->markAsClean();
        setNightError(
            restoredSchedule && restoredNight
                ? PhosphorI18n::tr("Could not save Night light settings")
                : PhosphorI18n::tr(
                      "Some Night light settings could not be saved. Check the current schedule and try again."));
        readNightConfig();
        return;
    }
    m_nightPending = true;
    m_nightError.clear();
    Q_EMIT changed();
    const auto epoch = m_nightEpoch;
    auto message = QDBusMessage::createMethodCall(kwin, QStringLiteral("/KWin"), kwin, QStringLiteral("reconfigure"));
    auto* watcher = new QDBusPendingCallWatcher(m_session.asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, epoch] {
        const QDBusPendingReply<> reply = *watcher;
        watcher->deleteLater();
        if (epoch != m_nightEpoch)
            return;
        finishNightWrite(reply.isError() ? PhosphorI18n::tr("Settings were saved but Night light could not apply them")
                                         : QString());
    });
}
void QuickSettingsController::finishNightWrite(const QString& error)
{
    m_nightPending = false;
    m_nightError = error;
    Q_EMIT changed();
    refreshNightLight();
}
void QuickSettingsController::toggleNightLight()
{
    setNightLightEnabled(!m_night.enabled);
}
void QuickSettingsController::setNightLightEnabled(bool enabled)
{
    if (enabled != m_night.enabled)
        applyNightConfig({{QStringLiteral("Active"), enabled}});
}
void QuickSettingsController::setNightLightTemperature(int temperature)
{
    if (temperature < 1000 || temperature > 6500) {
        setNightError(PhosphorI18n::tr("Choose a temperature from 1000 K to 6500 K"));
        return;
    }
    if (temperature != m_temperature)
        applyNightConfig({{QStringLiteral("NightTemperature"), temperature}});
}
void QuickSettingsController::setNightLightSchedule(const QString& schedule)
{
    if (schedule == QLatin1String("manual"))
        applyNightConfig({{QStringLiteral("Mode"), QStringLiteral("Constant")}});
    else if (schedule == QLatin1String("custom") || schedule == QLatin1String("automatic"))
        applyNightConfig(
            {{QStringLiteral("Mode"), QStringLiteral("DarkLight")}},
            {{QStringLiteral("Source"),
              schedule == QLatin1String("custom") ? QStringLiteral("Times") : QStringLiteral("Location")}});
    else
        setNightError(PhosphorI18n::tr("Choose a valid Night light schedule"));
}
void QuickSettingsController::setNightLightTimes(const QString& morning, const QString& evening)
{
    const QTime start = QTime::fromString(morning, QStringLiteral("HH:mm"));
    const QTime end = QTime::fromString(evening, QStringLiteral("HH:mm"));
    if (!start.isValid() || !end.isValid() || start == end) {
        setNightError(PhosphorI18n::tr("Choose different valid start and end times"));
        return;
    }
    // Keep transitions from overlapping when the user chooses a short interval.
    const int span = (start.secsTo(end) + 86400) % 86400;
    const int duration = qMin(1800, qMin(span, 86400 - span) / 2);
    applyNightConfig({{QStringLiteral("Mode"), QStringLiteral("DarkLight")}},
                     {{QStringLiteral("Source"), QStringLiteral("Times")}},
                     {{QStringLiteral("SunriseStart"), start},
                      {QStringLiteral("SunsetStart"), end},
                      {QStringLiteral("TransitionDuration"), duration}});
}
void QuickSettingsController::toggleNightLightPause()
{
    if (!m_night.available || !m_night.enabled || m_nightPending)
        return;
    if (m_cookie) {
        resumeNightLight();
        return;
    }
    m_nightPending = true;
    m_nightError.clear();
    Q_EMIT changed();
    const auto epoch = m_nightEpoch;
    // Resolve a unique owner before creating a cookie. Retain that address
    // for cleanup even if the compositor's well-known name is replaced.
    auto lookup =
        QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
                                       QStringLiteral("org.freedesktop.DBus"), QStringLiteral("GetNameOwner"));
    lookup << kwin;
    auto* ownerWatcher = new QDBusPendingCallWatcher(m_session.asyncCall(lookup), this);
    connect(ownerWatcher, &QDBusPendingCallWatcher::finished, this, [this, ownerWatcher, epoch] {
        const QDBusPendingReply<QString> ownerReply = *ownerWatcher;
        ownerWatcher->deleteLater();
        if (epoch != m_nightEpoch)
            return;
        if (ownerReply.isError() || ownerReply.value().isEmpty()) {
            finishNightWrite(PhosphorI18n::tr("Could not pause Night light"));
            return;
        }
        const auto owner = ownerReply.value();
        const QPointer<QuickSettingsController> guard(this);
        const auto bus = m_session;
        auto* watcher = new QDBusPendingCallWatcher(bus.asyncCall(nightCall(QStringLiteral("inhibit"), owner)),
                                                    QCoreApplication::instance());
        connect(watcher, &QDBusPendingCallWatcher::finished, watcher, [guard, bus, watcher, epoch, owner] {
            const QDBusPendingReply<uint> reply = *watcher;
            watcher->deleteLater();
            if (!guard || epoch != guard->m_nightEpoch) {
                if (!reply.isError()) {
                    auto cleanup = nightCall(QStringLiteral("uninhibit"), owner);
                    cleanup << reply.value();
                    bus.asyncCall(cleanup);
                }
                return;
            }
            if (!reply.isError()) {
                guard->m_cookie = reply.value();
                guard->m_cookieOwner = owner;
                guard->m_autoResume = guard->m_schedule != QLatin1String("manual");
                guard->m_resumeAt = guard->m_autoResume ? guard->m_night.transition : 0;
            }
            guard->finishNightWrite(reply.isError() ? PhosphorI18n::tr("Could not pause Night light") : QString());
        });
    });
}
void QuickSettingsController::scheduleResume()
{
    if (!m_cookie || !m_autoResume || !m_resumeAt)
        return;
    const auto milliseconds = qMax<qint64>(1, (m_resumeAt - QDateTime::currentSecsSinceEpoch()) * 1000);
    // Recheck wall time across suspend and clock adjustments.
    m_resumeTimer.start(int(qMin<qint64>(milliseconds, 30000)));
}
void QuickSettingsController::resumeNightLight()
{
    if (!m_cookie)
        return;
    if (m_nightPending) {
        m_resumeTimer.start(250);
        return;
    }
    if (m_resumeAt > QDateTime::currentSecsSinceEpoch() && sender() == &m_resumeTimer) {
        scheduleResume();
        return;
    }
    m_nightPending = true;
    Q_EMIT changed();
    const auto epoch = m_nightEpoch;
    auto message = nightCall(QStringLiteral("uninhibit"), m_cookieOwner);
    message << *m_cookie;
    auto* watcher = new QDBusPendingCallWatcher(m_session.asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, epoch] {
        const QDBusPendingReply<> reply = *watcher;
        watcher->deleteLater();
        if (epoch != m_nightEpoch)
            return;
        m_autoResume = false;
        m_resumeAt = 0;
        m_resumeTimer.stop();
        if (!reply.isError()) {
            m_cookie.reset();
            m_cookieOwner.clear();
        }
        finishNightWrite(reply.isError() ? PhosphorI18n::tr("Could not resume Night light") : QString());
    });
}
void QuickSettingsController::setPowerProfile(const QString& profile)
{
    if (!m_power.available || m_powerPending || profile == m_power.profile)
        return;
    if (!m_power.profiles.contains(profile)) {
        const auto error = PhosphorI18n::tr("This power profile is not available on your device");
        if (m_powerError != error) {
            m_powerError = error;
            Q_EMIT changed();
        }
        return;
    }
    m_powerPending = true;
    m_powerError.clear();
    Q_EMIT changed();
    const auto epoch = m_powerEpoch;
    auto message = QDBusMessage::createMethodCall(power, powerPath, properties, QStringLiteral("Set"));
    message << power << QStringLiteral("ActiveProfile") << QVariant::fromValue(QDBusVariant(profile));
    auto* watcher = new QDBusPendingCallWatcher(m_system.asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, epoch] {
        const QDBusPendingReply<> reply = *watcher;
        watcher->deleteLater();
        if (epoch != m_powerEpoch)
            return;
        m_powerPending = false;
        m_powerError = reply.isError() ? PhosphorI18n::tr("Could not change the power profile") : QString();
        Q_EMIT changed();
        refreshPower();
    });
}
}
