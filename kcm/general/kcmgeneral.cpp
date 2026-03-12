// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "kcmgeneral.h"
#include <QDBusConnection>
#include <QDBusPendingCall>
#include <QTimer>
#include <KPluginFactory>
#include "../common/dbusutils.h"
#include "../../src/config/configdefaults.h"
#include "../../src/config/settings.h"
#include "../../src/core/constants.h"

K_PLUGIN_CLASS_WITH_JSON(PlasmaZones::KCMGeneral, "kcm_plasmazones_general.json")

namespace PlasmaZones {

KCMGeneral::KCMGeneral(QObject* parent, const KPluginMetaData& data)
    : KQuickConfigModule(parent, data)
{
    m_settings = new Settings(this);
    setButtons(Apply | Default);

    QDBusConnection::sessionBus().connect(QString(DBus::ServiceName), QString(DBus::ObjectPath),
                                          QString(DBus::Interface::Settings), QStringLiteral("settingsChanged"), this,
                                          SLOT(onExternalSettingsChanged()));
}

// ── Load / Save / Defaults ──────────────────────────────────────────────

void KCMGeneral::load()
{
    KQuickConfigModule::load();
    m_settings->load();
    emitAllChanged();
    setNeedsSave(false);
}

void KCMGeneral::save()
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

void KCMGeneral::onExternalSettingsChanged()
{
    if (!m_saving) {
        load();
    }
}

void KCMGeneral::defaults()
{
    KQuickConfigModule::defaults();

    setAnimationsEnabled(ConfigDefaults::animationsEnabled());
    setAnimationDuration(ConfigDefaults::animationDuration());
    setAnimationEasingCurve(ConfigDefaults::animationEasingCurve());
    setAnimationMinDistance(ConfigDefaults::animationMinDistance());
    setAnimationSequenceMode(ConfigDefaults::animationSequenceMode());
    setAnimationStaggerInterval(ConfigDefaults::animationStaggerInterval());
    setShowOsdOnLayoutSwitch(ConfigDefaults::showOsdOnLayoutSwitch());
    setShowNavigationOsd(ConfigDefaults::showNavigationOsd());
    setOsdStyle(ConfigDefaults::osdStyle());
    setOverlayDisplayMode(ConfigDefaults::overlayDisplayMode());
}

void KCMGeneral::emitAllChanged()
{
    Q_EMIT animationsEnabledChanged();
    Q_EMIT animationDurationChanged();
    Q_EMIT animationEasingCurveChanged();
    Q_EMIT animationMinDistanceChanged();
    Q_EMIT animationSequenceModeChanged();
    Q_EMIT animationStaggerIntervalChanged();
    Q_EMIT showOsdOnLayoutSwitchChanged();
    Q_EMIT showNavigationOsdChanged();
    Q_EMIT osdStyleChanged();
    Q_EMIT overlayDisplayModeChanged();
}

// ── Animations ──────────────────────────────────────────────────────────

bool KCMGeneral::animationsEnabled() const
{
    return m_settings->animationsEnabled();
}

void KCMGeneral::setAnimationsEnabled(bool enabled)
{
    if (m_settings->animationsEnabled() != enabled) {
        m_settings->setAnimationsEnabled(enabled);
        Q_EMIT animationsEnabledChanged();
        setNeedsSave(true);
    }
}

int KCMGeneral::animationDuration() const
{
    return m_settings->animationDuration();
}

void KCMGeneral::setAnimationDuration(int duration)
{
    duration = qBound(50, duration, 500);
    if (m_settings->animationDuration() != duration) {
        m_settings->setAnimationDuration(duration);
        Q_EMIT animationDurationChanged();
        setNeedsSave(true);
    }
}

QString KCMGeneral::animationEasingCurve() const
{
    return m_settings->animationEasingCurve();
}

void KCMGeneral::setAnimationEasingCurve(const QString& curve)
{
    if (m_settings->animationEasingCurve() != curve) {
        m_settings->setAnimationEasingCurve(curve);
        Q_EMIT animationEasingCurveChanged();
        setNeedsSave(true);
    }
}

int KCMGeneral::animationMinDistance() const
{
    return m_settings->animationMinDistance();
}

void KCMGeneral::setAnimationMinDistance(int distance)
{
    distance = qBound(0, distance, 200);
    if (m_settings->animationMinDistance() != distance) {
        m_settings->setAnimationMinDistance(distance);
        Q_EMIT animationMinDistanceChanged();
        setNeedsSave(true);
    }
}

int KCMGeneral::animationSequenceMode() const
{
    return m_settings->animationSequenceMode();
}

void KCMGeneral::setAnimationSequenceMode(int mode)
{
    mode = qBound(0, mode, 1);
    if (m_settings->animationSequenceMode() != mode) {
        m_settings->setAnimationSequenceMode(mode);
        Q_EMIT animationSequenceModeChanged();
        setNeedsSave(true);
    }
}

int KCMGeneral::animationStaggerInterval() const
{
    return m_settings->animationStaggerInterval();
}

void KCMGeneral::setAnimationStaggerInterval(int ms)
{
    ms = qBound(static_cast<int>(AutotileDefaults::MinAnimationStaggerIntervalMs), ms,
                static_cast<int>(AutotileDefaults::MaxAnimationStaggerIntervalMs));
    if (m_settings->animationStaggerInterval() != ms) {
        m_settings->setAnimationStaggerInterval(ms);
        Q_EMIT animationStaggerIntervalChanged();
        setNeedsSave(true);
    }
}

int KCMGeneral::animationStaggerIntervalMax() const
{
    return static_cast<int>(AutotileDefaults::MaxAnimationStaggerIntervalMs);
}

// ── OSD ─────────────────────────────────────────────────────────────────

bool KCMGeneral::showOsdOnLayoutSwitch() const
{
    return m_settings->showOsdOnLayoutSwitch();
}

void KCMGeneral::setShowOsdOnLayoutSwitch(bool show)
{
    if (m_settings->showOsdOnLayoutSwitch() != show) {
        m_settings->setShowOsdOnLayoutSwitch(show);
        Q_EMIT showOsdOnLayoutSwitchChanged();
        setNeedsSave(true);
    }
}

bool KCMGeneral::showNavigationOsd() const
{
    return m_settings->showNavigationOsd();
}

void KCMGeneral::setShowNavigationOsd(bool show)
{
    if (m_settings->showNavigationOsd() != show) {
        m_settings->setShowNavigationOsd(show);
        Q_EMIT showNavigationOsdChanged();
        setNeedsSave(true);
    }
}

int KCMGeneral::osdStyle() const
{
    return m_settings->osdStyleInt();
}

void KCMGeneral::setOsdStyle(int style)
{
    if (m_settings->osdStyleInt() != style) {
        m_settings->setOsdStyleInt(style);
        Q_EMIT osdStyleChanged();
        setNeedsSave(true);
    }
}

int KCMGeneral::overlayDisplayMode() const
{
    return m_settings->overlayDisplayModeInt();
}

void KCMGeneral::setOverlayDisplayMode(int mode)
{
    if (m_settings->overlayDisplayModeInt() != mode) {
        m_settings->setOverlayDisplayModeInt(mode);
        Q_EMIT overlayDisplayModeChanged();
        setNeedsSave(true);
    }
}

} // namespace PlasmaZones

#include "kcmgeneral.moc"
