// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#include "StatusIconsController.h"
#include "StatusIconsSource.h"
#include "PhosphorShellI18n.h"

#include <PhosphorTheme/AppearanceStore.h>

namespace PhosphorShellApp {
namespace {
const QStringList ids{QStringLiteral("wifi"),    QStringLiteral("audio"),      QStringLiteral("bluetooth"),
                      QStringLiteral("battery"), QStringLiteral("microphone"), QStringLiteral("nightlight"),
                      QStringLiteral("focus"),   QStringLiteral("airplane"),   QStringLiteral("power")};
const QString orderKey = QStringLiteral("statusOrder");
const QString visibilityKey = QStringLiteral("statusVisibility");
const QString limitKey = QStringLiteral("statusLimit");
const QString percentKey = QStringLiteral("statusBatteryPercent");

QVariantList orderValues(const QStringList& order)
{
    QVariantList result;
    for (const auto& id : order)
        result.append(id);
    return result;
}

QVariantMap catalog(const QString& id)
{
    QString title, description, condition, icon, detail;
    if (id == QLatin1String("wifi")) {
        title = PhosphorI18n::tr("Wi-Fi");
        description = PhosphorI18n::tr("Your wireless connection");
        condition = PhosphorI18n::tr("When connected");
        icon = QStringLiteral("network-wireless");
        detail = QStringLiteral("network");
    } else if (id == QLatin1String("audio")) {
        title = PhosphorI18n::tr("Sound");
        description = PhosphorI18n::tr("Volume and output device");
        condition = PhosphorI18n::tr("When muted");
        icon = QStringLiteral("audio-volume-high");
        detail = id;
    } else if (id == QLatin1String("bluetooth")) {
        title = PhosphorI18n::tr("Bluetooth");
        description = PhosphorI18n::tr("Your connected devices");
        condition = PhosphorI18n::tr("When connected");
        icon = QStringLiteral("network-bluetooth-symbolic");
        detail = id;
    } else if (id == QLatin1String("battery")) {
        title = PhosphorI18n::tr("Battery");
        description = PhosphorI18n::tr("Charge and time remaining");
        condition = PhosphorI18n::tr("When present");
        icon = QStringLiteral("battery-good-symbolic");
        detail = id;
    } else if (id == QLatin1String("microphone")) {
        title = PhosphorI18n::tr("Microphone");
        description = PhosphorI18n::tr("Input activity and mute");
        condition = PhosphorI18n::tr("When in use");
        icon = QStringLiteral("audio-input-microphone");
        detail = id;
    } else if (id == QLatin1String("nightlight")) {
        title = PhosphorI18n::tr("Night light");
        description = PhosphorI18n::tr("Warmer display colors");
        condition = PhosphorI18n::tr("When on");
        icon = QStringLiteral("weather-clear-night-symbolic");
        detail = id;
    } else if (id == QLatin1String("focus")) {
        title = PhosphorI18n::tr("Do not disturb");
        description = PhosphorI18n::tr("Keep notifications quiet");
        condition = PhosphorI18n::tr("When on");
        icon = QStringLiteral("notifications-disabled");
        detail = QStringLiteral("notification");
    } else if (id == QLatin1String("airplane")) {
        title = PhosphorI18n::tr("Airplane mode");
        description = PhosphorI18n::tr("Wireless radio state");
        condition = PhosphorI18n::tr("When on");
        icon = QStringLiteral("flightmode-on");
        detail = id;
    } else {
        title = PhosphorI18n::tr("Power profile");
        description = PhosphorI18n::tr("Performance and energy use");
        condition = PhosphorI18n::tr("When not balanced");
        icon = QStringLiteral("speedometer");
        detail = id;
    }
    return {{QStringLiteral("id"), id},
            {QStringLiteral("title"), title},
            {QStringLiteral("description"), description},
            {QStringLiteral("condition"), condition},
            {QStringLiteral("iconName"), icon},
            {QStringLiteral("detailId"), detail}};
}
}

StatusIconsController::StatusIconsController(PhosphorTheme::AppearanceStore* store, StatusIconsSource* source,
                                             QObject* parent)
    : QObject(parent)
    , m_store(store)
{
    if (store) {
        connect(store, &PhosphorTheme::AppearanceStore::changed, this, &StatusIconsController::rebuild);
        connect(store, &QObject::destroyed, this, &StatusIconsController::rebuild);
    }
    attachSource(source);
}
StatusIconsController::~StatusIconsController() = default;
void StatusIconsController::attachSource(StatusIconsSource* source)
{
    m_source = source;
    if (source) {
        connect(source, &StatusIconsSource::changed, this, &StatusIconsController::rebuild);
        connect(source, &QObject::destroyed, this, &StatusIconsController::rebuild);
    }
    rebuild();
}
QStringList StatusIconsController::order() const
{
    QStringList result;
    const auto values = m_store ? m_store->values().value(orderKey).toList() : QVariantList{};
    for (const auto& value : values) {
        const auto id = value.toString();
        if (ids.contains(id) && !result.contains(id))
            result.append(id);
    }
    for (const auto& id : ids) {
        if (!result.contains(id))
            result.append(id);
    }
    return result;
}
void StatusIconsController::rebuild()
{
    const auto preferences = m_store ? m_store->values() : PhosphorTheme::AppearanceStore::defaults();
    const auto defaults = PhosphorTheme::AppearanceStore::defaults();
    const auto visibility = preferences.value(visibilityKey).toMap();
    const int limit = qBound(2, preferences.value(limitKey, 4).toInt(), 6);
    const auto snapshot = m_source ? m_source->snapshot() : StatusIconsSnapshot{};
    QVariantList all, shown, overflow;
    for (const auto& id : order()) {
        auto row = catalog(id);
        const auto state = snapshot.states.value(id);
        auto policy = visibility.value(id, defaults.value(visibilityKey).toMap().value(id)).toString();
        if (policy != QLatin1String("always") && policy != QLatin1String("auto") && policy != QLatin1String("hidden"))
            policy = QStringLiteral("hidden");
        row.insert(QStringLiteral("visibility"), policy);
        row.insert(QStringLiteral("summary"),
                   state.summary.isEmpty() ? PhosphorI18n::tr("Unavailable") : state.summary);
        row.insert(QStringLiteral("active"), state.active);
        row.insert(QStringLiteral("off"), state.off);
        row.insert(QStringLiteral("available"), state.available);
        row.insert(QStringLiteral("attention"), state.attention);
        row.insert(QStringLiteral("pending"), state.pending);
        row.insert(QStringLiteral("error"), state.error);
        row.insert(QStringLiteral("percent"), state.percent < 0 ? -1 : qBound(0, state.percent, 100));
        if (!state.iconName.isEmpty())
            row.insert(QStringLiteral("iconName"), state.iconName);
        all.append(row);
        if (policy == QLatin1String("always") || (policy == QLatin1String("auto") && state.active)) {
            if (shown.size() < limit)
                shown.append(row);
            else
                overflow.append(row);
        }
    }
    const QVariantMap data{{QStringLiteral("icons"), all},
                           {QStringLiteral("shown"), shown},
                           {QStringLiteral("overflow"), overflow},
                           {QStringLiteral("limit"), limit},
                           {QStringLiteral("percent"), preferences.value(percentKey, true)},
                           {QStringLiteral("volume"), snapshot.volume},
                           {QStringLiteral("muted"), snapshot.muted},
                           {QStringLiteral("audioAvailable"), snapshot.audioAvailable},
                           {QStringLiteral("outputs"), snapshot.outputs},
                           {QStringLiteral("powerProfile"), snapshot.powerProfile},
                           {QStringLiteral("powerProfiles"), snapshot.powerProfiles},
                           {QStringLiteral("error"), m_error}};
    if (data != m_data) {
        m_data = data;
        Q_EMIT changed();
    }
}
QVariantList StatusIconsController::icons() const
{
    return m_data.value(QStringLiteral("icons")).toList();
}
QVariantList StatusIconsController::shownIcons() const
{
    return m_data.value(QStringLiteral("shown")).toList();
}
QVariantList StatusIconsController::overflowIcons() const
{
    return m_data.value(QStringLiteral("overflow")).toList();
}
int StatusIconsController::maxVisible() const
{
    return m_data.value(QStringLiteral("limit")).toInt();
}
bool StatusIconsController::showBatteryPercentage() const
{
    return m_data.value(QStringLiteral("percent")).toBool();
}
int StatusIconsController::volume() const
{
    return m_data.value(QStringLiteral("volume")).toInt();
}
bool StatusIconsController::muted() const
{
    return m_data.value(QStringLiteral("muted")).toBool();
}
bool StatusIconsController::audioAvailable() const
{
    return m_data.value(QStringLiteral("audioAvailable")).toBool();
}
QVariantList StatusIconsController::outputs() const
{
    return m_data.value(QStringLiteral("outputs")).toList();
}
QString StatusIconsController::powerProfile() const
{
    return m_data.value(QStringLiteral("powerProfile")).toString();
}
QStringList StatusIconsController::powerProfiles() const
{
    return m_data.value(QStringLiteral("powerProfiles")).toStringList();
}
QString StatusIconsController::error() const
{
    return m_data.value(QStringLiteral("error")).toString();
}
bool StatusIconsController::save(const QVariantMap& patch)
{
    if (!m_store)
        return false;
    auto values = m_store->values();
    for (auto it = patch.cbegin(); it != patch.cend(); ++it)
        values.insert(it.key(), it.value());
    const bool saved = m_store->setValues(values);
    m_error = saved ? QString{} : m_store->error();
    rebuild();
    return saved;
}
bool StatusIconsController::setVisibility(const QString& id, const QString& policy)
{
    if (!m_store || !ids.contains(id)
        || (policy != QLatin1String("always") && policy != QLatin1String("auto") && policy != QLatin1String("hidden")))
        return false;
    auto visibility = m_store->values().value(visibilityKey).toMap();
    visibility.insert(id, policy);
    return save({{visibilityKey, visibility}});
}
bool StatusIconsController::moveIcon(const QString& id, int delta)
{
    auto values = order();
    const auto index = values.indexOf(id);
    if (index < 0 || (delta != -1 && delta != 1) || index + delta < 0 || index + delta >= values.size())
        return false;
    values.swapItemsAt(index, index + delta);
    return save({{orderKey, orderValues(values)}});
}
bool StatusIconsController::moveBefore(const QString& id, const QString& beforeId)
{
    if (!ids.contains(id) || (!beforeId.isEmpty() && !ids.contains(beforeId)) || id == beforeId)
        return false;
    auto values = order();
    values.removeAll(id);
    values.insert(beforeId.isEmpty() ? values.size() : values.indexOf(beforeId), id);
    return save({{orderKey, orderValues(values)}});
}
bool StatusIconsController::resetIcons()
{
    QVariantMap patch;
    const auto defaults = PhosphorTheme::AppearanceStore::defaults();
    for (const auto& key : {orderKey, visibilityKey, limitKey, percentKey})
        patch.insert(key, defaults.value(key));
    return save(patch);
}
bool StatusIconsController::setMaxVisible(int value)
{
    return value >= 2 && value <= 6 && save({{limitKey, value}});
}
bool StatusIconsController::setShowBatteryPercentage(bool value)
{
    return save({{percentKey, value}});
}
bool StatusIconsController::toggle(const QString& id)
{
    if (!m_source || !ids.contains(id))
        return false;
    const auto state = m_source->snapshot().states.value(id);
    return state.available && !state.pending && m_source->toggle(id);
}
bool StatusIconsController::setVolume(int percent)
{
    return m_source && percent >= 0 && percent <= 100 && m_source->setVolume(percent);
}
bool StatusIconsController::selectOutput(const QString& id)
{
    return m_source && m_source->selectOutput(id);
}
bool StatusIconsController::setPowerProfile(const QString& id)
{
    return m_source && powerProfiles().contains(id) && m_source->setPowerProfile(id);
}
void StatusIconsController::setAudioHost(PhosphorServicePipeWire::PipeWireHost* host)
{
    if (m_source)
        m_source->setAudioHost(host);
}
}
