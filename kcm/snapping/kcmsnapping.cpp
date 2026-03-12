// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "kcmsnapping.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QTimer>
#include "../common/dbusutils.h"
#include "../common/screenhelper.h"
#include "../common/screenprovider.h"
#include <KPluginFactory>
#include "../../src/config/configdefaults.h"
#include "../../src/config/settings.h"
#include "../../src/core/constants.h"
#include "../../src/core/interfaces.h"
#include "../../src/core/modifierutils.h"

K_PLUGIN_CLASS_WITH_JSON(PlasmaZones::KCMSnapping, "kcm_plasmazones_snapping.json")

namespace PlasmaZones {

KCMSnapping::KCMSnapping(QObject* parent, const KPluginMetaData& data)
    : KQuickConfigModule(parent, data)
{
    m_settings = new Settings(this);
    setButtons(Apply | Default);

    m_screenHelper = std::make_unique<ScreenHelper>(m_settings, this);
    connect(m_screenHelper.get(), &ScreenHelper::screensChanged, this, &KCMSnapping::screensChanged);
    connect(m_screenHelper.get(), &ScreenHelper::needsSave, this, [this]() {
        setNeedsSave(true);
    });

    m_screenHelper->refreshScreens();

    // Reload when another process or sub-KCM saves settings
    QDBusConnection::sessionBus().connect(QString(DBus::ServiceName), QString(DBus::ObjectPath),
                                          QString(DBus::Interface::Settings), QStringLiteral("settingsChanged"), this,
                                          SLOT(onExternalSettingsChanged()));

    // Listen for screen changes from the daemon
    m_screenHelper->connectToDaemonSignals();
}

// ── Load / Save ─────────────────────────────────────────────────────────

void KCMSnapping::load()
{
    KQuickConfigModule::load();
    m_settings->load();
    m_screenHelper->refreshScreens();
    emitAllChanged();
    setNeedsSave(false);
}

void KCMSnapping::save()
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

void KCMSnapping::onExternalSettingsChanged()
{
    if (!m_saving) {
        load();
    }
}

void KCMSnapping::defaults()
{
    KQuickConfigModule::defaults();

    // Reset all managed properties to ConfigDefaults values
    setSnappingEnabled(ConfigDefaults::snappingEnabled());
    setDragActivationTriggers(convertTriggersForQml(ConfigDefaults::dragActivationTriggers()));
    setToggleActivation(ConfigDefaults::toggleActivation());
    setZoneSpanEnabled(ConfigDefaults::zoneSpanEnabled());
    setZoneSpanTriggers(convertTriggersForQml(ConfigDefaults::zoneSpanTriggers()));
    setSnapAssistFeatureEnabled(ConfigDefaults::snapAssistFeatureEnabled());
    setSnapAssistEnabled(ConfigDefaults::snapAssistEnabled());
    setSnapAssistTriggers(convertTriggersForQml(ConfigDefaults::snapAssistTriggers()));

    setShowZonesOnAllMonitors(false);
    setShowZoneNumbers(true);
    setFlashZonesOnSwitch(true);
    setKeepWindowsInZonesOnResolutionChange(ConfigDefaults::keepWindowsInZonesOnResolutionChange());
    setMoveNewWindowsToLastZone(ConfigDefaults::moveNewWindowsToLastZone());
    setRestoreOriginalSizeOnUnsnap(ConfigDefaults::restoreOriginalSizeOnUnsnap());
    setStickyWindowHandling(ConfigDefaults::stickyWindowHandling());
    setRestoreWindowsToZonesOnLogin(ConfigDefaults::restoreWindowsToZonesOnLogin());

    setUseSystemColors(ConfigDefaults::useSystemColors());
    setHighlightColor(ConfigDefaults::highlightColor());
    setInactiveColor(ConfigDefaults::inactiveColor());
    setBorderColor(ConfigDefaults::borderColor());
    setLabelFontColor(ConfigDefaults::labelFontColor());
    setActiveOpacity(ConfigDefaults::activeOpacity());
    setInactiveOpacity(ConfigDefaults::inactiveOpacity());
    setBorderWidth(ConfigDefaults::borderWidth());
    setBorderRadius(ConfigDefaults::borderRadius());
    setEnableBlur(ConfigDefaults::enableBlur());
    setLabelFontFamily(ConfigDefaults::labelFontFamily());
    setLabelFontSizeScale(ConfigDefaults::labelFontSizeScale());
    setLabelFontWeight(ConfigDefaults::labelFontWeight());
    setLabelFontItalic(ConfigDefaults::labelFontItalic());
    setLabelFontUnderline(ConfigDefaults::labelFontUnderline());
    setLabelFontStrikeout(ConfigDefaults::labelFontStrikeout());

    setEnableShaderEffects(ConfigDefaults::enableShaderEffects());
    setShaderFrameRate(ConfigDefaults::shaderFrameRate());
    setEnableAudioVisualizer(ConfigDefaults::enableAudioVisualizer());
    setAudioSpectrumBarCount(ConfigDefaults::audioSpectrumBarCount());

    setZonePadding(ConfigDefaults::zonePadding());
    setOuterGap(ConfigDefaults::outerGap());
    setUsePerSideOuterGap(ConfigDefaults::usePerSideOuterGap());
    setOuterGapTop(ConfigDefaults::outerGapTop());
    setOuterGapBottom(ConfigDefaults::outerGapBottom());
    setOuterGapLeft(ConfigDefaults::outerGapLeft());
    setOuterGapRight(ConfigDefaults::outerGapRight());
    setAdjacentThreshold(ConfigDefaults::adjacentThreshold());

    setZoneSelectorEnabled(ConfigDefaults::zoneSelectorEnabled());
    setZoneSelectorTriggerDistance(ConfigDefaults::triggerDistance());
    setZoneSelectorPosition(ConfigDefaults::position());
    setZoneSelectorLayoutMode(ConfigDefaults::layoutMode());
    setZoneSelectorPreviewWidth(ConfigDefaults::previewWidth());
    setZoneSelectorPreviewHeight(ConfigDefaults::previewHeight());
    setZoneSelectorPreviewLockAspect(ConfigDefaults::previewLockAspect());
    setZoneSelectorGridColumns(ConfigDefaults::gridColumns());
    setZoneSelectorSizeMode(ConfigDefaults::sizeMode());
    setZoneSelectorMaxRows(ConfigDefaults::maxRows());
}

void KCMSnapping::emitAllChanged()
{
    Q_EMIT dragActivationTriggersChanged();
    Q_EMIT alwaysActivateOnDragChanged();
    Q_EMIT toggleActivationChanged();
    Q_EMIT snappingEnabledChanged();
    Q_EMIT zoneSpanEnabledChanged();
    Q_EMIT zoneSpanTriggersChanged();
    Q_EMIT snapAssistFeatureEnabledChanged();
    Q_EMIT snapAssistEnabledChanged();
    Q_EMIT snapAssistTriggersChanged();
    Q_EMIT showZonesOnAllMonitorsChanged();
    Q_EMIT showZoneNumbersChanged();
    Q_EMIT flashZonesOnSwitchChanged();
    Q_EMIT keepWindowsInZonesOnResolutionChangeChanged();
    Q_EMIT moveNewWindowsToLastZoneChanged();
    Q_EMIT restoreOriginalSizeOnUnsnapChanged();
    Q_EMIT stickyWindowHandlingChanged();
    Q_EMIT restoreWindowsToZonesOnLoginChanged();
    Q_EMIT useSystemColorsChanged();
    Q_EMIT highlightColorChanged();
    Q_EMIT inactiveColorChanged();
    Q_EMIT borderColorChanged();
    Q_EMIT labelFontColorChanged();
    Q_EMIT activeOpacityChanged();
    Q_EMIT inactiveOpacityChanged();
    Q_EMIT borderWidthChanged();
    Q_EMIT borderRadiusChanged();
    Q_EMIT enableBlurChanged();
    Q_EMIT labelFontFamilyChanged();
    Q_EMIT labelFontSizeScaleChanged();
    Q_EMIT labelFontWeightChanged();
    Q_EMIT labelFontItalicChanged();
    Q_EMIT labelFontUnderlineChanged();
    Q_EMIT labelFontStrikeoutChanged();
    Q_EMIT enableShaderEffectsChanged();
    Q_EMIT shaderFrameRateChanged();
    Q_EMIT enableAudioVisualizerChanged();
    Q_EMIT audioSpectrumBarCountChanged();
    Q_EMIT zonePaddingChanged();
    Q_EMIT outerGapChanged();
    Q_EMIT usePerSideOuterGapChanged();
    Q_EMIT outerGapTopChanged();
    Q_EMIT outerGapBottomChanged();
    Q_EMIT outerGapLeftChanged();
    Q_EMIT outerGapRightChanged();
    Q_EMIT adjacentThresholdChanged();
    Q_EMIT zoneSelectorEnabledChanged();
    Q_EMIT zoneSelectorTriggerDistanceChanged();
    Q_EMIT zoneSelectorPositionChanged();
    Q_EMIT zoneSelectorLayoutModeChanged();
    Q_EMIT zoneSelectorPreviewWidthChanged();
    Q_EMIT zoneSelectorPreviewHeightChanged();
    Q_EMIT zoneSelectorPreviewLockAspectChanged();
    Q_EMIT zoneSelectorGridColumnsChanged();
    Q_EMIT zoneSelectorSizeModeChanged();
    Q_EMIT zoneSelectorMaxRowsChanged();
    Q_EMIT screensChanged();
}

// ── Trigger conversion helpers ──────────────────────────────────────────

QVariantList KCMSnapping::convertTriggersForQml(const QVariantList& triggers)
{
    QVariantList result;
    for (const auto& t : triggers) {
        auto map = t.toMap();
        QVariantMap converted;
        converted[QStringLiteral("modifier")] =
            ModifierUtils::dragModifierToBitmask(map.value(QStringLiteral("modifier"), 0).toInt());
        converted[QStringLiteral("mouseButton")] = map.value(QStringLiteral("mouseButton"), 0);
        result.append(converted);
    }
    return result;
}

QVariantList KCMSnapping::convertTriggersForStorage(const QVariantList& triggers)
{
    QVariantList result;
    for (const auto& t : triggers) {
        auto map = t.toMap();
        QVariantMap stored;
        stored[QStringLiteral("modifier")] =
            ModifierUtils::bitmaskToDragModifier(map.value(QStringLiteral("modifier"), 0).toInt());
        stored[QStringLiteral("mouseButton")] = map.value(QStringLiteral("mouseButton"), 0);
        result.append(stored);
    }
    return result;
}

// ── Activation getters ──────────────────────────────────────────────────

QVariantList KCMSnapping::dragActivationTriggers() const
{
    return convertTriggersForQml(m_settings->dragActivationTriggers());
}

QVariantList KCMSnapping::defaultDragActivationTriggers() const
{
    return convertTriggersForQml(ConfigDefaults::dragActivationTriggers());
}

bool KCMSnapping::alwaysActivateOnDrag() const
{
    const int alwaysActive = static_cast<int>(DragModifier::AlwaysActive);
    const auto triggers = m_settings->dragActivationTriggers();
    for (const auto& t : triggers) {
        if (t.toMap().value(QStringLiteral("modifier"), 0).toInt() == alwaysActive) {
            return true;
        }
    }
    return false;
}

bool KCMSnapping::toggleActivation() const
{
    return m_settings->toggleActivation();
}

bool KCMSnapping::snappingEnabled() const
{
    return m_settings->snappingEnabled();
}

bool KCMSnapping::zoneSpanEnabled() const
{
    return m_settings->zoneSpanEnabled();
}

QVariantList KCMSnapping::zoneSpanTriggers() const
{
    return convertTriggersForQml(m_settings->zoneSpanTriggers());
}

QVariantList KCMSnapping::defaultZoneSpanTriggers() const
{
    return convertTriggersForQml(ConfigDefaults::zoneSpanTriggers());
}

// ── Activation setters ──────────────────────────────────────────────────

void KCMSnapping::setDragActivationTriggers(const QVariantList& triggers)
{
    const bool wasAlwaysActive = alwaysActivateOnDrag();
    const QVariantList converted = convertTriggersForStorage(triggers);
    if (m_settings->dragActivationTriggers() != converted) {
        m_settings->setDragActivationTriggers(converted);
        Q_EMIT dragActivationTriggersChanged();
        if (alwaysActivateOnDrag() != wasAlwaysActive) {
            Q_EMIT alwaysActivateOnDragChanged();
        }
        setNeedsSave(true);
    }
}

void KCMSnapping::setAlwaysActivateOnDrag(bool enabled)
{
    if (alwaysActivateOnDrag() == enabled) {
        return;
    }
    if (enabled) {
        // Single AlwaysActive trigger -- written directly in storage format (DragModifier enum)
        QVariantMap trigger;
        trigger[QStringLiteral("modifier")] = static_cast<int>(DragModifier::AlwaysActive);
        trigger[QStringLiteral("mouseButton")] = 0;
        m_settings->setDragActivationTriggers({trigger});
    } else {
        // Revert to default triggers
        m_settings->setDragActivationTriggers(ConfigDefaults::dragActivationTriggers());
    }
    Q_EMIT alwaysActivateOnDragChanged();
    Q_EMIT dragActivationTriggersChanged();
    setNeedsSave(true);
}

void KCMSnapping::setToggleActivation(bool enable)
{
    if (m_settings->toggleActivation() != enable) {
        m_settings->setToggleActivation(enable);
        Q_EMIT toggleActivationChanged();
        setNeedsSave(true);
    }
}

void KCMSnapping::setSnappingEnabled(bool enabled)
{
    if (m_settings->snappingEnabled() != enabled) {
        m_settings->setSnappingEnabled(enabled);
        Q_EMIT snappingEnabledChanged();
        setNeedsSave(true);
    }
}

void KCMSnapping::setZoneSpanEnabled(bool enabled)
{
    if (m_settings->zoneSpanEnabled() != enabled) {
        m_settings->setZoneSpanEnabled(enabled);
        Q_EMIT zoneSpanEnabledChanged();
        setNeedsSave(true);
    }
}

void KCMSnapping::setZoneSpanTriggers(const QVariantList& triggers)
{
    const QVariantList converted = convertTriggersForStorage(triggers);
    if (m_settings->zoneSpanTriggers() != converted) {
        m_settings->setZoneSpanTriggers(converted);
        Q_EMIT zoneSpanTriggersChanged();
        setNeedsSave(true);
    }
}

// ── Snap Assist getters ─────────────────────────────────────────────────

bool KCMSnapping::snapAssistFeatureEnabled() const
{
    return m_settings->snapAssistFeatureEnabled();
}

bool KCMSnapping::snapAssistEnabled() const
{
    return m_settings->snapAssistEnabled();
}

QVariantList KCMSnapping::snapAssistTriggers() const
{
    return convertTriggersForQml(m_settings->snapAssistTriggers());
}

QVariantList KCMSnapping::defaultSnapAssistTriggers() const
{
    return convertTriggersForQml(ConfigDefaults::snapAssistTriggers());
}

// ── Snap Assist setters ─────────────────────────────────────────────────

void KCMSnapping::setSnapAssistFeatureEnabled(bool enabled)
{
    if (m_settings->snapAssistFeatureEnabled() != enabled) {
        m_settings->setSnapAssistFeatureEnabled(enabled);
        Q_EMIT snapAssistFeatureEnabledChanged();
        setNeedsSave(true);
    }
}

void KCMSnapping::setSnapAssistEnabled(bool enabled)
{
    if (m_settings->snapAssistEnabled() != enabled) {
        m_settings->setSnapAssistEnabled(enabled);
        Q_EMIT snapAssistEnabledChanged();
        setNeedsSave(true);
    }
}

void KCMSnapping::setSnapAssistTriggers(const QVariantList& triggers)
{
    QVariantList converted = convertTriggersForStorage(triggers);
    if (m_settings->snapAssistTriggers() != converted) {
        m_settings->setSnapAssistTriggers(converted);
        Q_EMIT snapAssistTriggersChanged();
        setNeedsSave(true);
    }
}

// ── Display / Behavior getters ──────────────────────────────────────────

bool KCMSnapping::showZonesOnAllMonitors() const
{
    return m_settings->showZonesOnAllMonitors();
}

bool KCMSnapping::showZoneNumbers() const
{
    return m_settings->showZoneNumbers();
}

bool KCMSnapping::flashZonesOnSwitch() const
{
    return m_settings->flashZonesOnSwitch();
}

bool KCMSnapping::keepWindowsInZonesOnResolutionChange() const
{
    return m_settings->keepWindowsInZonesOnResolutionChange();
}

bool KCMSnapping::moveNewWindowsToLastZone() const
{
    return m_settings->moveNewWindowsToLastZone();
}

bool KCMSnapping::restoreOriginalSizeOnUnsnap() const
{
    return m_settings->restoreOriginalSizeOnUnsnap();
}

int KCMSnapping::stickyWindowHandling() const
{
    return static_cast<int>(m_settings->stickyWindowHandling());
}

bool KCMSnapping::restoreWindowsToZonesOnLogin() const
{
    return m_settings->restoreWindowsToZonesOnLogin();
}

// ── Display / Behavior setters ──────────────────────────────────────────

void KCMSnapping::setShowZonesOnAllMonitors(bool show)
{
    if (m_settings->showZonesOnAllMonitors() != show) {
        m_settings->setShowZonesOnAllMonitors(show);
        Q_EMIT showZonesOnAllMonitorsChanged();
        setNeedsSave(true);
    }
}

void KCMSnapping::setShowZoneNumbers(bool show)
{
    if (m_settings->showZoneNumbers() != show) {
        m_settings->setShowZoneNumbers(show);
        Q_EMIT showZoneNumbersChanged();
        setNeedsSave(true);
    }
}

void KCMSnapping::setFlashZonesOnSwitch(bool flash)
{
    if (m_settings->flashZonesOnSwitch() != flash) {
        m_settings->setFlashZonesOnSwitch(flash);
        Q_EMIT flashZonesOnSwitchChanged();
        setNeedsSave(true);
    }
}

void KCMSnapping::setKeepWindowsInZonesOnResolutionChange(bool keep)
{
    if (m_settings->keepWindowsInZonesOnResolutionChange() != keep) {
        m_settings->setKeepWindowsInZonesOnResolutionChange(keep);
        Q_EMIT keepWindowsInZonesOnResolutionChangeChanged();
        setNeedsSave(true);
    }
}

void KCMSnapping::setMoveNewWindowsToLastZone(bool move)
{
    if (m_settings->moveNewWindowsToLastZone() != move) {
        m_settings->setMoveNewWindowsToLastZone(move);
        Q_EMIT moveNewWindowsToLastZoneChanged();
        setNeedsSave(true);
    }
}

void KCMSnapping::setRestoreOriginalSizeOnUnsnap(bool restore)
{
    if (m_settings->restoreOriginalSizeOnUnsnap() != restore) {
        m_settings->setRestoreOriginalSizeOnUnsnap(restore);
        Q_EMIT restoreOriginalSizeOnUnsnapChanged();
        setNeedsSave(true);
    }
}

void KCMSnapping::setStickyWindowHandling(int handling)
{
    int clamped = qBound(static_cast<int>(StickyWindowHandling::TreatAsNormal), handling,
                         static_cast<int>(StickyWindowHandling::IgnoreAll));
    if (static_cast<int>(m_settings->stickyWindowHandling()) != clamped) {
        m_settings->setStickyWindowHandling(static_cast<StickyWindowHandling>(clamped));
        Q_EMIT stickyWindowHandlingChanged();
        setNeedsSave(true);
    }
}

void KCMSnapping::setRestoreWindowsToZonesOnLogin(bool restore)
{
    if (m_settings->restoreWindowsToZonesOnLogin() != restore) {
        m_settings->setRestoreWindowsToZonesOnLogin(restore);
        Q_EMIT restoreWindowsToZonesOnLoginChanged();
        setNeedsSave(true);
    }
}

} // namespace PlasmaZones

#include "kcmsnapping.moc"
