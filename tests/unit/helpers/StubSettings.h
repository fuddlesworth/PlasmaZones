// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "config/configdefaults.h"
#include "core/interfaces.h"

#include <PhosphorSnapEngine/ISnapSettings.h>

#include <QHash>
#include <QJsonDocument>
#include <QSet>

namespace PlasmaZones {

/**
 * @brief Unified stub ISettings for unit tests
 *
 * Provides sensible defaults for all ISettings pure virtual methods.
 * The defaultLayoutId is mutated via setDefaultLayoutId() (the
 * ISettings setter); tests should call that directly rather than a
 * "test-only" duplicate.
 *
 * Also inherits PhosphorEngine::ISnapSettings so SnapEngine's
 * dynamic_cast<ISnapSettings*>(engineSettings()) succeeds when a stub is wired
 * via setEngineSettings(). The remaining ISnapSettings methods
 * (snappingStickyWindowHandling, moveNewWindowsToLastZone,
 * restoreWindowsToZonesOnLogin, autoAssignAllLayouts) are already
 * implemented for ISettings — the multiple inheritance just registers
 * the second base so the cast resolves.
 *
 * NOTE: This stub does NOT inherit PhosphorEngine::IAutotileSettings —
 * the AutotileEngine fetches its config via a separate code path and
 * no currently-exercised unit test routes through a
 * dynamic_cast<IAutotileSettings*> against the stub. If a future test
 * exercises autotile-engine wiring through ISettings, the stub
 * should grow that base + the 26 IAutotileSettings overrides.
 */
class StubSettings : public ISettings, public PhosphorEngine::ISnapSettings
{
    // No Q_OBJECT — this stub defines no NEW signals/slots; ISettings's
    // meta-object is reused for the inherited signal emits (e.g.
    // `renderingBackendChanged`, `settingsChanged`) that the setters trigger
    // directly.

public:
    explicit StubSettings(QObject* parent = nullptr)
        : ISettings(parent)
    {
    }

    // IDefaultLayoutSettings
    QString defaultLayoutId() const override
    {
        return m_defaultLayoutId;
    }
    void setDefaultLayoutId(const QString& id) override
    {
        if (m_defaultLayoutId == id)
            return;
        m_defaultLayoutId = id;
        Q_EMIT defaultLayoutIdChanged();
        Q_EMIT settingsChanged();
    }
    bool suppressDefaultLayoutAssignment() const override
    {
        return m_suppressDefaultLayoutAssignment;
    }
    void setSuppressDefaultLayoutAssignment(bool suppress) override
    {
        if (m_suppressDefaultLayoutAssignment == suppress)
            return;
        m_suppressDefaultLayoutAssignment = suppress;
        Q_EMIT suppressDefaultLayoutAssignmentChanged();
        Q_EMIT settingsChanged();
    }

    // IZoneActivationSettings
    bool snappingEnabled() const override
    {
        return m_snappingEnabled;
    }
    void setSnappingEnabled(bool value) override
    {
        if (m_snappingEnabled == value) {
            return;
        }
        m_snappingEnabled = value;
        Q_EMIT snappingEnabledChanged();
        Q_EMIT settingsChanged();
    }
    QVariantList dragActivationTriggers() const override
    {
        return m_dragActivationTriggers;
    }
    void setDragActivationTriggers(const QVariantList& triggers) override
    {
        if (m_dragActivationTriggers == triggers) {
            return;
        }
        m_dragActivationTriggers = triggers;
        Q_EMIT dragActivationTriggersChanged();
        Q_EMIT settingsChanged();
    }
    bool zoneSpanEnabled() const override
    {
        return m_zoneSpanEnabled;
    }
    void setZoneSpanEnabled(bool value) override
    {
        if (m_zoneSpanEnabled == value) {
            return;
        }
        m_zoneSpanEnabled = value;
        Q_EMIT zoneSpanEnabledChanged();
        Q_EMIT settingsChanged();
    }
    DragModifier zoneSpanModifier() const override
    {
        return m_zoneSpanModifier;
    }
    void setZoneSpanModifier(DragModifier value) override
    {
        if (m_zoneSpanModifier == value) {
            return;
        }
        m_zoneSpanModifier = value;
        Q_EMIT zoneSpanModifierChanged();
        Q_EMIT settingsChanged();
    }
    QVariantList zoneSpanTriggers() const override
    {
        return m_zoneSpanTriggers;
    }
    void setZoneSpanTriggers(const QVariantList& value) override
    {
        if (m_zoneSpanTriggers == value) {
            return;
        }
        m_zoneSpanTriggers = value;
        Q_EMIT zoneSpanTriggersChanged();
        Q_EMIT settingsChanged();
    }
    bool zoneSpanToggleMode() const override
    {
        return m_zoneSpanToggleMode;
    }
    void setZoneSpanToggleMode(bool value) override
    {
        if (m_zoneSpanToggleMode == value) {
            return;
        }
        m_zoneSpanToggleMode = value;
        Q_EMIT zoneSpanToggleModeChanged();
        Q_EMIT settingsChanged();
    }
    bool toggleActivation() const override
    {
        return m_toggleActivation;
    }
    void setToggleActivation(bool value) override
    {
        if (m_toggleActivation == value) {
            return;
        }
        m_toggleActivation = value;
        Q_EMIT toggleActivationChanged();
        Q_EMIT settingsChanged();
    }

    // IZoneVisualizationSettings
    bool showZonesOnAllMonitors() const override
    {
        return m_showZonesOnAllMonitors;
    }
    void setShowZonesOnAllMonitors(bool value) override
    {
        if (m_showZonesOnAllMonitors == value) {
            return;
        }
        m_showZonesOnAllMonitors = value;
        Q_EMIT showZonesOnAllMonitorsChanged();
        Q_EMIT settingsChanged();
    }
    QStringList disabledMonitors(PhosphorZones::AssignmentEntry::Mode mode) const override
    {
        return m_disabledMonitors.value(static_cast<int>(mode));
    }
    void setDisabledMonitors(PhosphorZones::AssignmentEntry::Mode mode, const QStringList& entries) override
    {
        if (m_disabledMonitors.value(static_cast<int>(mode)) == entries) {
            return;
        }
        m_disabledMonitors.insert(static_cast<int>(mode), entries);
        Q_EMIT settingsChanged();
    }
    bool isMonitorDisabled(PhosphorZones::AssignmentEntry::Mode mode, const QString& screenIdOrName) const override
    {
        return m_disabledMonitors.value(static_cast<int>(mode)).contains(screenIdOrName);
    }
    QStringList disabledDesktops(PhosphorZones::AssignmentEntry::Mode mode) const override
    {
        return m_disabledDesktops.value(static_cast<int>(mode));
    }
    void setDisabledDesktops(PhosphorZones::AssignmentEntry::Mode mode, const QStringList& entries) override
    {
        if (m_disabledDesktops.value(static_cast<int>(mode)) == entries) {
            return;
        }
        m_disabledDesktops.insert(static_cast<int>(mode), entries);
        Q_EMIT settingsChanged();
    }
    bool isDesktopDisabled(PhosphorZones::AssignmentEntry::Mode, const QString& /*screenIdOrName*/, int) const override
    {
        return false;
    }
    QStringList disabledActivities(PhosphorZones::AssignmentEntry::Mode mode) const override
    {
        return m_disabledActivities.value(static_cast<int>(mode));
    }
    void setDisabledActivities(PhosphorZones::AssignmentEntry::Mode mode, const QStringList& entries) override
    {
        if (m_disabledActivities.value(static_cast<int>(mode)) == entries) {
            return;
        }
        m_disabledActivities.insert(static_cast<int>(mode), entries);
        Q_EMIT settingsChanged();
    }
    bool isActivityDisabled(PhosphorZones::AssignmentEntry::Mode, const QString& /*screenIdOrName*/,
                            const QString&) const override
    {
        return false;
    }
    bool showZoneNumbers() const override
    {
        return m_showZoneNumbers;
    }
    void setShowZoneNumbers(bool value) override
    {
        if (m_showZoneNumbers == value) {
            return;
        }
        m_showZoneNumbers = value;
        Q_EMIT showZoneNumbersChanged();
        Q_EMIT settingsChanged();
    }
    bool flashZonesOnSwitch() const override
    {
        return m_flashZonesOnSwitch;
    }
    void setFlashZonesOnSwitch(bool value) override
    {
        if (m_flashZonesOnSwitch == value) {
            return;
        }
        m_flashZonesOnSwitch = value;
        Q_EMIT flashZonesOnSwitchChanged();
        Q_EMIT settingsChanged();
    }
    bool showOsdOnLayoutSwitch() const override
    {
        return m_showOsdOnLayoutSwitch;
    }
    void setShowOsdOnLayoutSwitch(bool value) override
    {
        if (m_showOsdOnLayoutSwitch == value) {
            return;
        }
        m_showOsdOnLayoutSwitch = value;
        Q_EMIT showOsdOnLayoutSwitchChanged();
        Q_EMIT settingsChanged();
    }
    bool showOsdOnDesktopSwitch() const override
    {
        return m_showOsdOnDesktopSwitch;
    }
    void setShowOsdOnDesktopSwitch(bool value) override
    {
        if (m_showOsdOnDesktopSwitch == value) {
            return;
        }
        m_showOsdOnDesktopSwitch = value;
        Q_EMIT showOsdOnDesktopSwitchChanged();
        Q_EMIT settingsChanged();
    }
    bool showNavigationOsd() const override
    {
        return m_showNavigationOsd;
    }
    void setShowNavigationOsd(bool value) override
    {
        if (m_showNavigationOsd == value) {
            return;
        }
        m_showNavigationOsd = value;
        Q_EMIT showNavigationOsdChanged();
        Q_EMIT settingsChanged();
    }
    OsdStyle osdStyle() const override
    {
        return m_osdStyle;
    }
    void setOsdStyle(OsdStyle value) override
    {
        if (m_osdStyle == value) {
            return;
        }
        m_osdStyle = value;
        Q_EMIT osdStyleChanged();
        Q_EMIT settingsChanged();
    }
    OverlayDisplayMode overlayDisplayMode() const override
    {
        return m_overlayDisplayMode;
    }
    void setOverlayDisplayMode(OverlayDisplayMode value) override
    {
        if (m_overlayDisplayMode == value) {
            return;
        }
        m_overlayDisplayMode = value;
        Q_EMIT overlayDisplayModeChanged();
        Q_EMIT settingsChanged();
    }
    bool useSystemColors() const override
    {
        return m_useSystemColors;
    }
    void setUseSystemColors(bool value) override
    {
        if (m_useSystemColors == value) {
            return;
        }
        m_useSystemColors = value;
        Q_EMIT useSystemColorsChanged();
        Q_EMIT settingsChanged();
    }
    QColor highlightColor() const override
    {
        return m_highlightColor;
    }
    void setHighlightColor(const QColor& value) override
    {
        if (m_highlightColor == value) {
            return;
        }
        m_highlightColor = value;
        Q_EMIT highlightColorChanged();
        Q_EMIT settingsChanged();
    }
    QColor inactiveColor() const override
    {
        return m_inactiveColor;
    }
    void setInactiveColor(const QColor& value) override
    {
        if (m_inactiveColor == value) {
            return;
        }
        m_inactiveColor = value;
        Q_EMIT inactiveColorChanged();
        Q_EMIT settingsChanged();
    }
    QColor borderColor() const override
    {
        return m_borderColor;
    }
    void setBorderColor(const QColor& value) override
    {
        if (m_borderColor == value) {
            return;
        }
        m_borderColor = value;
        Q_EMIT borderColorChanged();
        Q_EMIT settingsChanged();
    }
    QColor labelFontColor() const override
    {
        return m_labelFontColor;
    }
    void setLabelFontColor(const QColor& value) override
    {
        if (m_labelFontColor == value) {
            return;
        }
        m_labelFontColor = value;
        Q_EMIT labelFontColorChanged();
        Q_EMIT settingsChanged();
    }
    qreal activeOpacity() const override
    {
        return m_activeOpacity;
    }
    void setActiveOpacity(qreal value) override
    {
        if (m_activeOpacity == value) {
            return;
        }
        m_activeOpacity = value;
        Q_EMIT activeOpacityChanged();
        Q_EMIT settingsChanged();
    }
    qreal inactiveOpacity() const override
    {
        return m_inactiveOpacity;
    }
    void setInactiveOpacity(qreal value) override
    {
        if (m_inactiveOpacity == value) {
            return;
        }
        m_inactiveOpacity = value;
        Q_EMIT inactiveOpacityChanged();
        Q_EMIT settingsChanged();
    }
    int borderWidth() const override
    {
        return m_borderWidth;
    }
    void setBorderWidth(int value) override
    {
        if (m_borderWidth == value) {
            return;
        }
        m_borderWidth = value;
        Q_EMIT borderWidthChanged();
        Q_EMIT settingsChanged();
    }
    int borderRadius() const override
    {
        return m_borderRadius;
    }
    void setBorderRadius(int value) override
    {
        if (m_borderRadius == value) {
            return;
        }
        m_borderRadius = value;
        Q_EMIT borderRadiusChanged();
        Q_EMIT settingsChanged();
    }
    bool enableBlur() const override
    {
        return m_enableBlur;
    }
    void setEnableBlur(bool value) override
    {
        if (m_enableBlur == value) {
            return;
        }
        m_enableBlur = value;
        Q_EMIT enableBlurChanged();
        Q_EMIT settingsChanged();
    }
    QString labelFontFamily() const override
    {
        return m_labelFontFamily;
    }
    void setLabelFontFamily(const QString& value) override
    {
        if (m_labelFontFamily == value) {
            return;
        }
        m_labelFontFamily = value;
        Q_EMIT labelFontFamilyChanged();
        Q_EMIT settingsChanged();
    }
    qreal labelFontSizeScale() const override
    {
        return m_labelFontSizeScale;
    }
    void setLabelFontSizeScale(qreal value) override
    {
        if (m_labelFontSizeScale == value) {
            return;
        }
        m_labelFontSizeScale = value;
        Q_EMIT labelFontSizeScaleChanged();
        Q_EMIT settingsChanged();
    }
    int labelFontWeight() const override
    {
        return m_labelFontWeight;
    }
    void setLabelFontWeight(int value) override
    {
        if (m_labelFontWeight == value) {
            return;
        }
        m_labelFontWeight = value;
        Q_EMIT labelFontWeightChanged();
        Q_EMIT settingsChanged();
    }
    bool labelFontItalic() const override
    {
        return m_labelFontItalic;
    }
    void setLabelFontItalic(bool value) override
    {
        if (m_labelFontItalic == value) {
            return;
        }
        m_labelFontItalic = value;
        Q_EMIT labelFontItalicChanged();
        Q_EMIT settingsChanged();
    }
    bool labelFontUnderline() const override
    {
        return m_labelFontUnderline;
    }
    void setLabelFontUnderline(bool value) override
    {
        if (m_labelFontUnderline == value) {
            return;
        }
        m_labelFontUnderline = value;
        Q_EMIT labelFontUnderlineChanged();
        Q_EMIT settingsChanged();
    }
    bool labelFontStrikeout() const override
    {
        return m_labelFontStrikeout;
    }
    void setLabelFontStrikeout(bool value) override
    {
        if (m_labelFontStrikeout == value) {
            return;
        }
        m_labelFontStrikeout = value;
        Q_EMIT labelFontStrikeoutChanged();
        Q_EMIT settingsChanged();
    }
    int shaderFrameRate() const override
    {
        return m_shaderFrameRate;
    }
    void setShaderFrameRate(int value) override
    {
        if (m_shaderFrameRate == value) {
            return;
        }
        m_shaderFrameRate = value;
        Q_EMIT shaderFrameRateChanged();
        Q_EMIT settingsChanged();
    }
    bool enableAudioVisualizer() const override
    {
        return m_enableAudioVisualizer;
    }
    void setEnableAudioVisualizer(bool value) override
    {
        if (m_enableAudioVisualizer == value) {
            return;
        }
        m_enableAudioVisualizer = value;
        Q_EMIT enableAudioVisualizerChanged();
        Q_EMIT settingsChanged();
    }
    // Audio spectrum (Shaders.Audio) — routed through ConfigDefaults so a
    // future default tweak flows into tests automatically (the file's
    // standing anti-drift convention).
    int audioSpectrumBarCount() const override
    {
        return m_audioSpectrumBarCount;
    }
    void setAudioSpectrumBarCount(int value) override
    {
        if (m_audioSpectrumBarCount == value) {
            return;
        }
        m_audioSpectrumBarCount = value;
        Q_EMIT audioSpectrumBarCountChanged();
        Q_EMIT settingsChanged();
    }
    bool audioAutosens() const override
    {
        return m_audioAutosens;
    }
    void setAudioAutosens(bool value) override
    {
        if (m_audioAutosens == value) {
            return;
        }
        m_audioAutosens = value;
        Q_EMIT audioAutosensChanged();
        Q_EMIT settingsChanged();
    }
    int audioSensitivity() const override
    {
        return m_audioSensitivity;
    }
    void setAudioSensitivity(int value) override
    {
        if (m_audioSensitivity == value) {
            return;
        }
        m_audioSensitivity = value;
        Q_EMIT audioSensitivityChanged();
        Q_EMIT settingsChanged();
    }
    int audioNoiseReduction() const override
    {
        return m_audioNoiseReduction;
    }
    void setAudioNoiseReduction(int value) override
    {
        if (m_audioNoiseReduction == value) {
            return;
        }
        m_audioNoiseReduction = value;
        Q_EMIT audioNoiseReductionChanged();
        Q_EMIT settingsChanged();
    }
    int audioLowerCutoffHz() const override
    {
        return m_audioLowerCutoffHz;
    }
    void setAudioLowerCutoffHz(int value) override
    {
        if (m_audioLowerCutoffHz == value) {
            return;
        }
        m_audioLowerCutoffHz = value;
        Q_EMIT audioLowerCutoffHzChanged();
        Q_EMIT settingsChanged();
    }
    int audioHigherCutoffHz() const override
    {
        return m_audioHigherCutoffHz;
    }
    void setAudioHigherCutoffHz(int value) override
    {
        if (m_audioHigherCutoffHz == value) {
            return;
        }
        m_audioHigherCutoffHz = value;
        Q_EMIT audioHigherCutoffHzChanged();
        Q_EMIT settingsChanged();
    }
    bool audioMonstercat() const override
    {
        return m_audioMonstercat;
    }
    void setAudioMonstercat(bool value) override
    {
        if (m_audioMonstercat == value) {
            return;
        }
        m_audioMonstercat = value;
        Q_EMIT audioMonstercatChanged();
        Q_EMIT settingsChanged();
    }
    bool audioWaves() const override
    {
        return m_audioWaves;
    }
    void setAudioWaves(bool value) override
    {
        if (m_audioWaves == value) {
            return;
        }
        m_audioWaves = value;
        Q_EMIT audioWavesChanged();
        Q_EMIT settingsChanged();
    }
    QString audioChannelMode() const override
    {
        return m_audioChannelMode;
    }
    void setAudioChannelMode(const QString& value) override
    {
        if (m_audioChannelMode == value) {
            return;
        }
        m_audioChannelMode = value;
        Q_EMIT audioChannelModeChanged();
        Q_EMIT settingsChanged();
    }
    bool audioReverse() const override
    {
        return m_audioReverse;
    }
    void setAudioReverse(bool value) override
    {
        if (m_audioReverse == value) {
            return;
        }
        m_audioReverse = value;
        Q_EMIT audioReverseChanged();
        Q_EMIT settingsChanged();
    }
    int audioExtraSmoothing() const override
    {
        return m_audioExtraSmoothing;
    }
    void setAudioExtraSmoothing(int value) override
    {
        if (m_audioExtraSmoothing == value) {
            return;
        }
        m_audioExtraSmoothing = value;
        Q_EMIT audioExtraSmoothingChanged();
        Q_EMIT settingsChanged();
    }
    QString audioInputMethod() const override
    {
        return m_audioInputMethod;
    }
    void setAudioInputMethod(const QString& value) override
    {
        if (m_audioInputMethod == value) {
            return;
        }
        m_audioInputMethod = value;
        Q_EMIT audioInputMethodChanged();
        Q_EMIT settingsChanged();
    }
    QString audioInputSource() const override
    {
        return m_audioInputSource;
    }
    void setAudioInputSource(const QString& value) override
    {
        if (m_audioInputSource == value) {
            return;
        }
        m_audioInputSource = value;
        Q_EMIT audioInputSourceChanged();
        Q_EMIT settingsChanged();
    }

    // IZoneGeometrySettings — gaps are config-backed again, so the interface
    // carries setters. Member-backed here so tests can round-trip.
    int innerGap() const override
    {
        return m_innerGap;
    }
    void setInnerGap(int v) override
    {
        if (m_innerGap == v) {
            return;
        }
        m_innerGap = v;
        Q_EMIT innerGapChanged();
        Q_EMIT settingsChanged();
    }
    int outerGap() const override
    {
        return m_outerGap;
    }
    void setOuterGap(int v) override
    {
        if (m_outerGap == v) {
            return;
        }
        m_outerGap = v;
        Q_EMIT outerGapChanged();
        Q_EMIT settingsChanged();
    }
    bool usePerSideOuterGap() const override
    {
        return m_usePerSideOuterGap;
    }
    void setUsePerSideOuterGap(bool v) override
    {
        if (m_usePerSideOuterGap == v) {
            return;
        }
        m_usePerSideOuterGap = v;
        Q_EMIT usePerSideOuterGapChanged();
        Q_EMIT settingsChanged();
    }
    int outerGapTop() const override
    {
        return m_outerGapTop;
    }
    void setOuterGapTop(int v) override
    {
        if (m_outerGapTop == v) {
            return;
        }
        m_outerGapTop = v;
        Q_EMIT outerGapTopChanged();
        Q_EMIT settingsChanged();
    }
    int outerGapBottom() const override
    {
        return m_outerGapBottom;
    }
    void setOuterGapBottom(int v) override
    {
        if (m_outerGapBottom == v) {
            return;
        }
        m_outerGapBottom = v;
        Q_EMIT outerGapBottomChanged();
        Q_EMIT settingsChanged();
    }
    int outerGapLeft() const override
    {
        return m_outerGapLeft;
    }
    void setOuterGapLeft(int v) override
    {
        if (m_outerGapLeft == v) {
            return;
        }
        m_outerGapLeft = v;
        Q_EMIT outerGapLeftChanged();
        Q_EMIT settingsChanged();
    }
    int outerGapRight() const override
    {
        return m_outerGapRight;
    }
    void setOuterGapRight(int v) override
    {
        if (m_outerGapRight == v) {
            return;
        }
        m_outerGapRight = v;
        Q_EMIT outerGapRightChanged();
        Q_EMIT settingsChanged();
    }

    // Window appearance (config-backed border + title-bar defaults)
    bool showWindowBorder() const override
    {
        return m_showWindowBorder;
    }
    void setShowWindowBorder(bool v) override
    {
        if (m_showWindowBorder == v) {
            return;
        }
        m_showWindowBorder = v;
        Q_EMIT showWindowBorderChanged();
        Q_EMIT settingsChanged();
    }
    QString windowBorderScope() const override
    {
        return m_windowBorderScope;
    }
    void setWindowBorderScope(const QString& v) override
    {
        if (m_windowBorderScope == v) {
            return;
        }
        m_windowBorderScope = v;
        Q_EMIT windowBorderScopeChanged();
        Q_EMIT settingsChanged();
    }
    int windowBorderWidth() const override
    {
        return m_windowBorderWidth;
    }
    void setWindowBorderWidth(int v) override
    {
        if (m_windowBorderWidth == v) {
            return;
        }
        m_windowBorderWidth = v;
        Q_EMIT windowBorderWidthChanged();
        Q_EMIT settingsChanged();
    }
    int windowBorderRadius() const override
    {
        return m_windowBorderRadius;
    }
    void setWindowBorderRadius(int v) override
    {
        if (m_windowBorderRadius == v) {
            return;
        }
        m_windowBorderRadius = v;
        Q_EMIT windowBorderRadiusChanged();
        Q_EMIT settingsChanged();
    }
    QString windowBorderColorActive() const override
    {
        return m_windowBorderColorActive;
    }
    void setWindowBorderColorActive(const QString& v) override
    {
        if (m_windowBorderColorActive == v) {
            return;
        }
        m_windowBorderColorActive = v;
        Q_EMIT windowBorderColorActiveChanged();
        Q_EMIT settingsChanged();
    }
    QString windowBorderColorInactive() const override
    {
        return m_windowBorderColorInactive;
    }
    void setWindowBorderColorInactive(const QString& v) override
    {
        if (m_windowBorderColorInactive == v) {
            return;
        }
        m_windowBorderColorInactive = v;
        Q_EMIT windowBorderColorInactiveChanged();
        Q_EMIT settingsChanged();
    }
    bool hideWindowTitleBars() const override
    {
        return m_hideWindowTitleBars;
    }
    void setHideWindowTitleBars(bool v) override
    {
        if (m_hideWindowTitleBars == v) {
            return;
        }
        m_hideWindowTitleBars = v;
        Q_EMIT hideWindowTitleBarsChanged();
        Q_EMIT settingsChanged();
    }
    QString windowTitleBarScope() const override
    {
        return m_windowTitleBarScope;
    }
    void setWindowTitleBarScope(const QString& v) override
    {
        if (m_windowTitleBarScope == v) {
            return;
        }
        m_windowTitleBarScope = v;
        Q_EMIT windowTitleBarScopeChanged();
        Q_EMIT settingsChanged();
    }
    int focusFadeDuration() const override
    {
        return m_focusFadeDuration;
    }
    void setFocusFadeDuration(int ms) override
    {
        if (m_focusFadeDuration == ms) {
            return;
        }
        m_focusFadeDuration = ms;
        Q_EMIT focusFadeDurationChanged();
        Q_EMIT settingsChanged();
    }
    int adjacentThreshold() const override
    {
        return m_adjacentThreshold;
    }
    void setAdjacentThreshold(int value) override
    {
        if (m_adjacentThreshold == value) {
            return;
        }
        m_adjacentThreshold = value;
        Q_EMIT adjacentThresholdChanged();
        Q_EMIT settingsChanged();
    }
    int pollIntervalMs() const override
    {
        return m_pollIntervalMs;
    }
    void setPollIntervalMs(int value) override
    {
        if (m_pollIntervalMs == value) {
            return;
        }
        m_pollIntervalMs = value;
        Q_EMIT pollIntervalMsChanged();
        Q_EMIT settingsChanged();
    }
    int minimumZoneSizePx() const override
    {
        return m_minimumZoneSizePx;
    }
    void setMinimumZoneSizePx(int value) override
    {
        if (m_minimumZoneSizePx == value) {
            return;
        }
        m_minimumZoneSizePx = value;
        Q_EMIT minimumZoneSizePxChanged();
        Q_EMIT settingsChanged();
    }
    int minimumZoneDisplaySizePx() const override
    {
        return m_minimumZoneDisplaySizePx;
    }
    void setMinimumZoneDisplaySizePx(int value) override
    {
        if (m_minimumZoneDisplaySizePx == value) {
            return;
        }
        m_minimumZoneDisplaySizePx = value;
        Q_EMIT minimumZoneDisplaySizePxChanged();
        Q_EMIT settingsChanged();
    }

    // IWindowExclusionSettings — the per-app / per-class exclusion list
    // accessors retired in v4 (folded into unified Rule store).
    bool excludeTransientWindows() const override
    {
        return m_excludeTransientWindows;
    }
    void setExcludeTransientWindows(bool exclude) override
    {
        if (m_excludeTransientWindows == exclude) {
            return;
        }
        m_excludeTransientWindows = exclude;
        Q_EMIT excludeTransientWindowsChanged();
        Q_EMIT settingsChanged();
    }
    int minimumWindowWidth() const override
    {
        return m_minimumWindowWidth;
    }
    void setMinimumWindowWidth(int w) override
    {
        if (m_minimumWindowWidth == w) {
            return;
        }
        m_minimumWindowWidth = w;
        Q_EMIT minimumWindowWidthChanged();
        Q_EMIT settingsChanged();
    }
    int minimumWindowHeight() const override
    {
        return m_minimumWindowHeight;
    }
    void setMinimumWindowHeight(int h) override
    {
        if (m_minimumWindowHeight == h) {
            return;
        }
        m_minimumWindowHeight = h;
        Q_EMIT minimumWindowHeightChanged();
        Q_EMIT settingsChanged();
    }

    // Decoration window filtering — stub accessors backed by m_decoration*
    // state, mirroring the animation-filter stubs below.
    bool decorationExcludeTransientWindows() const override
    {
        return m_decorationExcludeTransientWindows;
    }
    void setDecorationExcludeTransientWindows(bool exclude) override
    {
        if (m_decorationExcludeTransientWindows == exclude) {
            return;
        }
        m_decorationExcludeTransientWindows = exclude;
        Q_EMIT decorationExcludeTransientWindowsChanged();
        Q_EMIT settingsChanged();
    }
    int decorationMinimumWindowWidth() const override
    {
        return m_decorationMinimumWindowWidth;
    }
    void setDecorationMinimumWindowWidth(int width) override
    {
        if (m_decorationMinimumWindowWidth == width) {
            return;
        }
        m_decorationMinimumWindowWidth = width;
        Q_EMIT decorationMinimumWindowWidthChanged();
        Q_EMIT settingsChanged();
    }
    int decorationMinimumWindowHeight() const override
    {
        return m_decorationMinimumWindowHeight;
    }
    void setDecorationMinimumWindowHeight(int height) override
    {
        if (m_decorationMinimumWindowHeight == height) {
            return;
        }
        m_decorationMinimumWindowHeight = height;
        Q_EMIT decorationMinimumWindowHeightChanged();
        Q_EMIT settingsChanged();
    }

    // Animation window filtering — pure-stub no-op accessors backed by
    // m_animation* state so tests that exercise the filter cascade can
    // round-trip values through the stub without involving the real
    // PhosphorConfig::Store. Uses the same value-changed-guarded emit
    // pattern the concrete Settings layer uses.
    bool animationExcludeTransientWindows() const override
    {
        return m_animationExcludeTransientWindows;
    }
    void setAnimationExcludeTransientWindows(bool exclude) override
    {
        if (m_animationExcludeTransientWindows == exclude) {
            return;
        }
        m_animationExcludeTransientWindows = exclude;
        Q_EMIT animationExcludeTransientWindowsChanged();
        Q_EMIT settingsChanged();
    }
    bool animationExcludeNotificationsAndOsd() const override
    {
        return m_animationExcludeNotificationsAndOsd;
    }
    void setAnimationExcludeNotificationsAndOsd(bool exclude) override
    {
        if (m_animationExcludeNotificationsAndOsd == exclude) {
            return;
        }
        m_animationExcludeNotificationsAndOsd = exclude;
        Q_EMIT animationExcludeNotificationsAndOsdChanged();
        Q_EMIT settingsChanged();
    }
    int animationMinimumWindowWidth() const override
    {
        return m_animationMinimumWindowWidth;
    }
    void setAnimationMinimumWindowWidth(int width) override
    {
        if (m_animationMinimumWindowWidth == width) {
            return;
        }
        m_animationMinimumWindowWidth = width;
        Q_EMIT animationMinimumWindowWidthChanged();
        Q_EMIT settingsChanged();
    }
    int animationMinimumWindowHeight() const override
    {
        return m_animationMinimumWindowHeight;
    }
    void setAnimationMinimumWindowHeight(int height) override
    {
        if (m_animationMinimumWindowHeight == height) {
            return;
        }
        m_animationMinimumWindowHeight = height;
        Q_EMIT animationMinimumWindowHeightChanged();
        Q_EMIT settingsChanged();
    }
    // animationExcludedApplications / animationExcludedWindowClasses
    // overrides retired in v4 alongside the ISettings virtuals — the
    // lists folded into ExcludeAnimations Rules.

    // IZoneSelectorSettings
    bool zoneSelectorEnabled() const override
    {
        return m_zoneSelectorEnabled;
    }
    void setZoneSelectorEnabled(bool value) override
    {
        if (m_zoneSelectorEnabled == value) {
            return;
        }
        m_zoneSelectorEnabled = value;
        Q_EMIT zoneSelectorEnabledChanged();
        Q_EMIT settingsChanged();
    }
    int zoneSelectorTriggerDistance() const override
    {
        return m_zoneSelectorTriggerDistance;
    }
    void setZoneSelectorTriggerDistance(int value) override
    {
        if (m_zoneSelectorTriggerDistance == value) {
            return;
        }
        m_zoneSelectorTriggerDistance = value;
        Q_EMIT zoneSelectorTriggerDistanceChanged();
        Q_EMIT settingsChanged();
    }
    ZoneSelectorPosition zoneSelectorPosition() const override
    {
        return m_zoneSelectorPosition;
    }
    void setZoneSelectorPosition(ZoneSelectorPosition value) override
    {
        if (m_zoneSelectorPosition == value) {
            return;
        }
        m_zoneSelectorPosition = value;
        Q_EMIT zoneSelectorPositionChanged();
        Q_EMIT settingsChanged();
    }
    ZoneSelectorLayoutMode zoneSelectorLayoutMode() const override
    {
        return m_zoneSelectorLayoutMode;
    }
    void setZoneSelectorLayoutMode(ZoneSelectorLayoutMode value) override
    {
        if (m_zoneSelectorLayoutMode == value) {
            return;
        }
        m_zoneSelectorLayoutMode = value;
        Q_EMIT zoneSelectorLayoutModeChanged();
        Q_EMIT settingsChanged();
    }
    int zoneSelectorPreviewWidth() const override
    {
        return m_zoneSelectorPreviewWidth;
    }
    void setZoneSelectorPreviewWidth(int value) override
    {
        if (m_zoneSelectorPreviewWidth == value) {
            return;
        }
        m_zoneSelectorPreviewWidth = value;
        Q_EMIT zoneSelectorPreviewWidthChanged();
        Q_EMIT settingsChanged();
    }
    int zoneSelectorPreviewHeight() const override
    {
        return m_zoneSelectorPreviewHeight;
    }
    void setZoneSelectorPreviewHeight(int value) override
    {
        if (m_zoneSelectorPreviewHeight == value) {
            return;
        }
        m_zoneSelectorPreviewHeight = value;
        Q_EMIT zoneSelectorPreviewHeightChanged();
        Q_EMIT settingsChanged();
    }
    bool zoneSelectorPreviewLockAspect() const override
    {
        return m_zoneSelectorPreviewLockAspect;
    }
    void setZoneSelectorPreviewLockAspect(bool value) override
    {
        if (m_zoneSelectorPreviewLockAspect == value) {
            return;
        }
        m_zoneSelectorPreviewLockAspect = value;
        Q_EMIT zoneSelectorPreviewLockAspectChanged();
        Q_EMIT settingsChanged();
    }
    int zoneSelectorGridColumns() const override
    {
        return m_zoneSelectorGridColumns;
    }
    void setZoneSelectorGridColumns(int value) override
    {
        if (m_zoneSelectorGridColumns == value) {
            return;
        }
        m_zoneSelectorGridColumns = value;
        Q_EMIT zoneSelectorGridColumnsChanged();
        Q_EMIT settingsChanged();
    }
    ZoneSelectorSizeMode zoneSelectorSizeMode() const override
    {
        return m_zoneSelectorSizeMode;
    }
    void setZoneSelectorSizeMode(ZoneSelectorSizeMode value) override
    {
        if (m_zoneSelectorSizeMode == value) {
            return;
        }
        m_zoneSelectorSizeMode = value;
        Q_EMIT zoneSelectorSizeModeChanged();
        Q_EMIT settingsChanged();
    }
    int zoneSelectorMaxRows() const override
    {
        return m_zoneSelectorMaxRows;
    }
    void setZoneSelectorMaxRows(int value) override
    {
        if (m_zoneSelectorMaxRows == value) {
            return;
        }
        m_zoneSelectorMaxRows = value;
        Q_EMIT zoneSelectorMaxRowsChanged();
        Q_EMIT settingsChanged();
    }

    // IWindowBehaviorSettings
    bool keepWindowsInZonesOnResolutionChange() const override
    {
        return m_keepWindowsInZonesOnResolutionChange;
    }
    void setKeepWindowsInZonesOnResolutionChange(bool value) override
    {
        if (m_keepWindowsInZonesOnResolutionChange == value) {
            return;
        }
        m_keepWindowsInZonesOnResolutionChange = value;
        Q_EMIT keepWindowsInZonesOnResolutionChangeChanged();
        Q_EMIT settingsChanged();
    }
    bool moveNewWindowsToLastZone() const override
    {
        return m_moveNewWindowsToLastZone;
    }
    void setMoveNewWindowsToLastZone(bool value) override
    {
        if (m_moveNewWindowsToLastZone == value) {
            return;
        }
        m_moveNewWindowsToLastZone = value;
        Q_EMIT moveNewWindowsToLastZoneChanged();
        Q_EMIT settingsChanged();
    }
    // ISnapSettings::focusNewWindows() — shares the ISettings snappingFocusNewWindows
    // member so a test can drive the snap engine's focus-new path.
    bool focusNewWindows() const override
    {
        return m_snappingFocusNewWindows;
    }
    bool restoreOriginalSizeOnUnsnap() const override
    {
        return m_restoreOriginalSizeOnUnsnap;
    }
    void setRestoreOriginalSizeOnUnsnap(bool value) override
    {
        if (m_restoreOriginalSizeOnUnsnap == value) {
            return;
        }
        m_restoreOriginalSizeOnUnsnap = value;
        Q_EMIT restoreOriginalSizeOnUnsnapChanged();
        Q_EMIT settingsChanged();
    }
    StickyWindowHandling snappingStickyWindowHandling() const override
    {
        return m_snappingStickyWindowHandling;
    }
    void setSnappingStickyWindowHandling(StickyWindowHandling value) override
    {
        if (m_snappingStickyWindowHandling == value) {
            return;
        }
        m_snappingStickyWindowHandling = value;
        Q_EMIT snappingStickyWindowHandlingChanged();
        Q_EMIT settingsChanged();
    }
    bool restoreWindowsToZonesOnLogin() const override
    {
        return m_restoreWindowsToZonesOnLogin;
    }
    void setRestoreWindowsToZonesOnLogin(bool value) override
    {
        if (m_restoreWindowsToZonesOnLogin == value) {
            return;
        }
        m_restoreWindowsToZonesOnLogin = value;
        Q_EMIT restoreWindowsToZonesOnLoginChanged();
        Q_EMIT settingsChanged();
    }
    bool snappingRestoreFloatedWindowsOnLogin() const override
    {
        return m_snappingRestoreFloatedWindowsOnLogin;
    }
    void setSnappingRestoreFloatedWindowsOnLogin(bool value) override
    {
        if (m_snappingRestoreFloatedWindowsOnLogin == value)
            return;
        m_snappingRestoreFloatedWindowsOnLogin = value;
        Q_EMIT snappingRestoreFloatedWindowsOnLoginChanged();
        Q_EMIT settingsChanged();
    }
    bool autotileRestoreFloatedWindowsOnLogin() const override
    {
        return m_autotileRestoreFloatedWindowsOnLogin;
    }
    void setAutotileRestoreFloatedWindowsOnLogin(bool value) override
    {
        if (m_autotileRestoreFloatedWindowsOnLogin == value)
            return;
        m_autotileRestoreFloatedWindowsOnLogin = value;
        Q_EMIT autotileRestoreFloatedWindowsOnLoginChanged();
        Q_EMIT settingsChanged();
    }
    // ISettings getter/setter; the ISnapSettings bridge (unfloatFallbackToZone)
    // is defined alongside the other ISnapSettings overrides below.
    bool snapUnfloatFallbackToZone() const override
    {
        return m_snapUnfloatFallbackToZone;
    }
    void setSnapUnfloatFallbackToZone(bool value) override
    {
        if (m_snapUnfloatFallbackToZone == value)
            return;
        m_snapUnfloatFallbackToZone = value;
        Q_EMIT snapUnfloatFallbackToZoneChanged();
        Q_EMIT settingsChanged();
    }
    bool unfloatFallbackToZone() const override
    {
        return m_snapUnfloatFallbackToZone;
    }
    bool autoAssignAllLayouts() const override
    {
        return m_autoAssignAllLayouts;
    }
    void setAutoAssignAllLayouts(bool enabled) override
    {
        if (m_autoAssignAllLayouts == enabled)
            return;
        m_autoAssignAllLayouts = enabled;
        Q_EMIT autoAssignAllLayoutsChanged();
        Q_EMIT settingsChanged();
    }
    bool snapAssistFeatureEnabled() const override
    {
        return m_snapAssistFeatureEnabled;
    }
    void setSnapAssistFeatureEnabled(bool enabled) override
    {
        if (m_snapAssistFeatureEnabled == enabled)
            return;
        m_snapAssistFeatureEnabled = enabled;
        Q_EMIT snapAssistFeatureEnabledChanged();
        Q_EMIT settingsChanged();
    }
    bool snapAssistEnabled() const override
    {
        return m_snapAssistEnabled;
    }
    void setSnapAssistEnabled(bool enabled) override
    {
        if (m_snapAssistEnabled == enabled)
            return;
        m_snapAssistEnabled = enabled;
        Q_EMIT snapAssistEnabledChanged();
        Q_EMIT settingsChanged();
    }
    QVariantList snapAssistTriggers() const override
    {
        return m_snapAssistTriggers;
    }
    void setSnapAssistTriggers(const QVariantList& value) override
    {
        if (m_snapAssistTriggers == value) {
            return;
        }
        m_snapAssistTriggers = value;
        Q_EMIT snapAssistTriggersChanged();
        Q_EMIT settingsChanged();
    }
    bool filterLayoutsByAspectRatio() const override
    {
        return m_filterLayoutsByAspectRatio;
    }
    void setFilterLayoutsByAspectRatio(bool value) override
    {
        if (m_filterLayoutsByAspectRatio == value) {
            return;
        }
        m_filterLayoutsByAspectRatio = value;
        Q_EMIT filterLayoutsByAspectRatioChanged();
        Q_EMIT settingsChanged();
    }

    // IOrderingSettings
    QStringList snappingLayoutOrder() const override
    {
        return m_snappingLayoutOrder;
    }
    void setSnappingLayoutOrder(const QStringList& order) override
    {
        if (m_snappingLayoutOrder == order)
            return;
        m_snappingLayoutOrder = order;
        Q_EMIT snappingLayoutOrderChanged();
        Q_EMIT settingsChanged();
    }
    QStringList tilingAlgorithmOrder() const override
    {
        return m_tilingAlgorithmOrder;
    }
    void setTilingAlgorithmOrder(const QStringList& order) override
    {
        if (m_tilingAlgorithmOrder == order)
            return;
        m_tilingAlgorithmOrder = order;
        Q_EMIT tilingAlgorithmOrderChanged();
        Q_EMIT settingsChanged();
    }

    // Animation settings (ISettings)
    bool animationsEnabled() const override
    {
        return m_animationsEnabled;
    }
    void setAnimationsEnabled(bool value) override
    {
        if (m_animationsEnabled == value) {
            return;
        }
        m_animationsEnabled = value;
        Q_EMIT animationsEnabledChanged();
        Q_EMIT settingsChanged();
    }
    int animationDuration() const override
    {
        return m_animationDuration;
    }
    void setAnimationDuration(int value) override
    {
        if (m_animationDuration == value) {
            return;
        }
        m_animationDuration = value;
        Q_EMIT animationDurationChanged();
        Q_EMIT settingsChanged();
    }
    QString animationEasingCurve() const override
    {
        return m_animationEasingCurve;
    }
    void setAnimationEasingCurve(const QString& value) override
    {
        if (m_animationEasingCurve == value) {
            return;
        }
        m_animationEasingCurve = value;
        Q_EMIT animationEasingCurveChanged();
        Q_EMIT settingsChanged();
    }
    int animationMinDistance() const override
    {
        return m_animationMinDistance;
    }
    void setAnimationMinDistance(int value) override
    {
        if (m_animationMinDistance == value) {
            return;
        }
        m_animationMinDistance = value;
        Q_EMIT animationMinDistanceChanged();
        Q_EMIT settingsChanged();
    }
    int animationSequenceMode() const override
    {
        return m_animationSequenceMode;
    }
    void setAnimationSequenceMode(int value) override
    {
        if (m_animationSequenceMode == value) {
            return;
        }
        m_animationSequenceMode = value;
        Q_EMIT animationSequenceModeChanged();
        Q_EMIT settingsChanged();
    }
    int animationStaggerInterval() const override
    {
        return m_animationStaggerInterval;
    }
    void setAnimationStaggerInterval(int value) override
    {
        if (m_animationStaggerInterval == value) {
            return;
        }
        m_animationStaggerInterval = value;
        Q_EMIT animationStaggerIntervalChanged();
        Q_EMIT settingsChanged();
    }
    PhosphorAnimationShaders::ShaderProfileTree shaderProfileTree() const override
    {
        return m_shaderProfileTree;
    }
    void setShaderProfileTree(const PhosphorAnimationShaders::ShaderProfileTree& value) override
    {
        if (m_shaderProfileTree == value) {
            return;
        }
        m_shaderProfileTree = value;
        Q_EMIT shaderProfileTreeChanged();
        Q_EMIT settingsChanged();
    }
    PhosphorSurfaceShaders::DecorationProfileTree decorationProfileTree() const override
    {
        return m_decorationProfileTree;
    }
    void setDecorationProfileTree(const PhosphorSurfaceShaders::DecorationProfileTree& value) override
    {
        if (m_decorationProfileTree == value) {
            return;
        }
        m_decorationProfileTree = value;
        Q_EMIT decorationProfileTreeChanged();
        Q_EMIT settingsChanged();
    }
    QString decorationProfileTreeJson() const override
    {
        // Serialize the SAME tree the typed accessor returns, so the two
        // accessors stay coherent for any test that reads the JSON facade.
        return QString::fromUtf8(QJsonDocument(decorationProfileTree().toJson()).toJson(QJsonDocument::Compact));
    }
    void setDecorationProfileTreeJson(const QString& json) override
    {
        // Parse into the SAME tree the typed accessor exposes, so set-then-get round-trips.
        // Dropping the value made this the one key the effect fetches by SettingProperty
        // that no stub-backed test could move — while the comment below boasted "real
        // storage, not no-op setters".
        const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
        if (doc.isNull() || !doc.isObject()) {
            return;
        }
        setDecorationProfileTree(PhosphorSurfaceShaders::DecorationProfileTree::fromJson(doc.object()));
    }

    // Decorations.Performance (ISettings). Real storage, not no-op setters: the
    // daemon arms its idle ladder off decorationPauseWhenIdleChanged /
    // decorationIdleTimeoutSecChanged, so a stub that could never emit them would make
    // the whole idle relay untestable — and a setter that silently drops the value
    // breaks the get-after-set contract every other settable here honours.
    bool decorationAnimateFocusedOnly() const override
    {
        return m_decorationAnimateFocusedOnly;
    }
    void setDecorationAnimateFocusedOnly(bool value) override
    {
        if (m_decorationAnimateFocusedOnly == value) {
            return;
        }
        m_decorationAnimateFocusedOnly = value;
        Q_EMIT decorationAnimateFocusedOnlyChanged();
        Q_EMIT settingsChanged();
    }
    bool decorationPauseWhenIdle() const override
    {
        return m_decorationPauseWhenIdle;
    }
    void setDecorationPauseWhenIdle(bool value) override
    {
        if (m_decorationPauseWhenIdle == value) {
            return;
        }
        m_decorationPauseWhenIdle = value;
        Q_EMIT decorationPauseWhenIdleChanged();
        Q_EMIT settingsChanged();
    }
    int decorationIdleTimeoutSec() const override
    {
        return m_decorationIdleTimeoutSec;
    }
    void setDecorationIdleTimeoutSec(int value) override
    {
        // Mirror the schema's clampInt so the stub cannot hand the daemon a timeout
        // the real Settings would never produce.
        const int clamped =
            qBound(ConfigDefaults::decorationIdleTimeoutSecMin(), value, ConfigDefaults::decorationIdleTimeoutSecMax());
        if (m_decorationIdleTimeoutSec == clamped) {
            return;
        }
        m_decorationIdleTimeoutSec = clamped;
        Q_EMIT decorationIdleTimeoutSecChanged();
        Q_EMIT settingsChanged();
    }

    // Autotile decoration settings (ISettings)
    bool autotileFocusFollowsMouse() const override
    {
        return m_autotileFocusFollowsMouse;
    }
    void setAutotileFocusFollowsMouse(bool value) override
    {
        if (m_autotileFocusFollowsMouse == value) {
            return;
        }
        m_autotileFocusFollowsMouse = value;
        Q_EMIT autotileFocusFollowsMouseChanged();
        Q_EMIT settingsChanged();
    }
    bool snappingFocusNewWindows() const override
    {
        return m_snappingFocusNewWindows;
    }
    void setSnappingFocusNewWindows(bool v) override
    {
        if (m_snappingFocusNewWindows == v) {
            return;
        }
        m_snappingFocusNewWindows = v;
        Q_EMIT snappingFocusNewWindowsChanged();
        Q_EMIT settingsChanged();
    }
    bool snappingFocusFollowsMouse() const override
    {
        return m_snappingFocusFollowsMouse;
    }
    void setSnappingFocusFollowsMouse(bool v) override
    {
        if (m_snappingFocusFollowsMouse == v) {
            return;
        }
        m_snappingFocusFollowsMouse = v;
        Q_EMIT snappingFocusFollowsMouseChanged();
        Q_EMIT settingsChanged();
    }
    StickyWindowHandling autotileStickyWindowHandling() const override
    {
        return m_autotileStickyWindowHandling;
    }
    void setAutotileStickyWindowHandling(StickyWindowHandling value) override
    {
        if (m_autotileStickyWindowHandling == value) {
            return;
        }
        m_autotileStickyWindowHandling = value;
        Q_EMIT autotileStickyWindowHandlingChanged();
        Q_EMIT settingsChanged();
    }
    AutotileDragBehavior autotileDragBehavior() const override
    {
        return m_autotileDragBehavior;
    }
    void setAutotileDragBehavior(AutotileDragBehavior value) override
    {
        if (m_autotileDragBehavior == value) {
            return;
        }
        m_autotileDragBehavior = value;
        Q_EMIT autotileDragBehaviorChanged();
        Q_EMIT settingsChanged();
    }
    AutotileOverflowBehavior autotileOverflowBehavior() const override
    {
        return m_autotileOverflowBehavior;
    }
    void setAutotileOverflowBehavior(AutotileOverflowBehavior value) override
    {
        if (m_autotileOverflowBehavior == value) {
            return;
        }
        m_autotileOverflowBehavior = value;
        Q_EMIT autotileOverflowBehaviorChanged();
        Q_EMIT settingsChanged();
    }
    QVariantList autotileDragInsertTriggers() const override
    {
        return m_autotileDragInsertTriggers;
    }
    void setAutotileDragInsertTriggers(const QVariantList& value) override
    {
        if (m_autotileDragInsertTriggers == value) {
            return;
        }
        m_autotileDragInsertTriggers = value;
        Q_EMIT autotileDragInsertTriggersChanged();
        Q_EMIT settingsChanged();
    }
    bool autotileDragInsertToggle() const override
    {
        return m_autotileDragInsertToggle;
    }
    void setAutotileDragInsertToggle(bool value) override
    {
        if (m_autotileDragInsertToggle == value) {
            return;
        }
        m_autotileDragInsertToggle = value;
        Q_EMIT autotileDragInsertToggleChanged();
        Q_EMIT settingsChanged();
    }
    QVariantMap autotilePerAlgorithmSettings() const override
    {
        return m_autotilePerAlgorithmSettings;
    }
    void setAutotilePerAlgorithmSettings(const QVariantMap& settings) override
    {
        if (m_autotilePerAlgorithmSettings == settings)
            return;
        m_autotilePerAlgorithmSettings = settings;
        Q_EMIT autotilePerAlgorithmSettingsChanged();
        Q_EMIT settingsChanged();
    }
    QString loadColorsFromFile(const QString&) override
    {
        // Stub returns "not supported" so a test that exercised this
        // path could distinguish a real failure from a missing impl,
        // but the controllers under test don't reach this in their
        // unit-test paths today.
        return QStringLiteral("loadColorsFromFile: stub not supported");
    }

    // Editor settings — round-trip the stub members so a test can
    // exercise the EditorPageController setter/getter contract.
    //
    // Every setter below pairs the field-specific signal with the umbrella
    // `settingsChanged()` emit. The concrete Settings class does the same
    // via the P_STORE_SET_{BOOL,STRING,INT,DOUBLE} macros (settings.cpp
    // ~2759-2784) — the stub matches so tests can rely on `settingsChanged()`
    // firing on any setter, regardless of which ISettings backend is wired.
    QString editorDuplicateShortcut() const override
    {
        return m_editorDuplicateShortcut;
    }
    void setEditorDuplicateShortcut(const QString& s) override
    {
        if (m_editorDuplicateShortcut == s)
            return;
        m_editorDuplicateShortcut = s;
        Q_EMIT editorDuplicateShortcutChanged();
        Q_EMIT settingsChanged();
    }
    QString editorSplitHorizontalShortcut() const override
    {
        return m_editorSplitHorizontalShortcut;
    }
    void setEditorSplitHorizontalShortcut(const QString& s) override
    {
        if (m_editorSplitHorizontalShortcut == s)
            return;
        m_editorSplitHorizontalShortcut = s;
        Q_EMIT editorSplitHorizontalShortcutChanged();
        Q_EMIT settingsChanged();
    }
    QString editorSplitVerticalShortcut() const override
    {
        return m_editorSplitVerticalShortcut;
    }
    void setEditorSplitVerticalShortcut(const QString& s) override
    {
        if (m_editorSplitVerticalShortcut == s)
            return;
        m_editorSplitVerticalShortcut = s;
        Q_EMIT editorSplitVerticalShortcutChanged();
        Q_EMIT settingsChanged();
    }
    QString editorFillShortcut() const override
    {
        return m_editorFillShortcut;
    }
    void setEditorFillShortcut(const QString& s) override
    {
        if (m_editorFillShortcut == s)
            return;
        m_editorFillShortcut = s;
        Q_EMIT editorFillShortcutChanged();
        Q_EMIT settingsChanged();
    }
    bool editorGridSnappingEnabled() const override
    {
        return m_editorGridSnappingEnabled;
    }
    void setEditorGridSnappingEnabled(bool e) override
    {
        if (m_editorGridSnappingEnabled == e)
            return;
        m_editorGridSnappingEnabled = e;
        Q_EMIT editorGridSnappingEnabledChanged();
        Q_EMIT settingsChanged();
    }
    bool editorEdgeSnappingEnabled() const override
    {
        return m_editorEdgeSnappingEnabled;
    }
    void setEditorEdgeSnappingEnabled(bool e) override
    {
        if (m_editorEdgeSnappingEnabled == e)
            return;
        m_editorEdgeSnappingEnabled = e;
        Q_EMIT editorEdgeSnappingEnabledChanged();
        Q_EMIT settingsChanged();
    }
    qreal editorSnapIntervalX() const override
    {
        return m_editorSnapIntervalX;
    }
    void setEditorSnapIntervalX(qreal v) override
    {
        if (qFuzzyCompare(m_editorSnapIntervalX, v))
            return;
        m_editorSnapIntervalX = v;
        Q_EMIT editorSnapIntervalXChanged();
        Q_EMIT settingsChanged();
    }
    qreal editorSnapIntervalY() const override
    {
        return m_editorSnapIntervalY;
    }
    void setEditorSnapIntervalY(qreal v) override
    {
        if (qFuzzyCompare(m_editorSnapIntervalY, v))
            return;
        m_editorSnapIntervalY = v;
        Q_EMIT editorSnapIntervalYChanged();
        Q_EMIT settingsChanged();
    }
    int editorSnapOverrideModifier() const override
    {
        return m_editorSnapOverrideModifier;
    }
    void setEditorSnapOverrideModifier(int m) override
    {
        if (m_editorSnapOverrideModifier == m)
            return;
        m_editorSnapOverrideModifier = m;
        Q_EMIT editorSnapOverrideModifierChanged();
        Q_EMIT settingsChanged();
    }
    bool fillOnDropEnabled() const override
    {
        return m_fillOnDropEnabled;
    }
    void setFillOnDropEnabled(bool e) override
    {
        if (m_fillOnDropEnabled == e)
            return;
        m_fillOnDropEnabled = e;
        Q_EMIT fillOnDropEnabledChanged();
        Q_EMIT settingsChanged();
    }
    int fillOnDropModifier() const override
    {
        return m_fillOnDropModifier;
    }
    void setFillOnDropModifier(int m) override
    {
        if (m_fillOnDropModifier == m)
            return;
        m_fillOnDropModifier = m;
        Q_EMIT fillOnDropModifierChanged();
        Q_EMIT settingsChanged();
    }
    QStringList lockedScreens() const override
    {
        return m_lockedScreens;
    }
    void setLockedScreens(const QStringList& value) override
    {
        if (m_lockedScreens == value) {
            return;
        }
        m_lockedScreens = value;
        Q_EMIT lockedScreensChanged();
        Q_EMIT settingsChanged();
    }
    bool isScreenLocked(const QString& screenId) const override
    {
        return m_lockedScreens.contains(screenId);
    }
    void setScreenLocked(const QString& screenId, bool locked) override
    {
        const bool had = m_lockedScreens.contains(screenId);
        if (had == locked) {
            return;
        }
        if (locked) {
            m_lockedScreens.append(screenId);
        } else {
            m_lockedScreens.removeAll(screenId);
        }
        Q_EMIT lockedScreensChanged();
        Q_EMIT settingsChanged();
    }
    bool isContextLocked(const QString&, int, const QString&) const override
    {
        return false;
    }
    void setContextLocked(const QString&, int, const QString&, bool) override
    {
    }

    // Rendering (ISettings)
    QString renderingBackend() const override
    {
        return m_renderingBackend;
    }
    void setRenderingBackend(const QString& backend) override
    {
        const QString value = ConfigDefaults::normalizeRenderingBackend(backend);
        if (m_renderingBackend != value) {
            m_renderingBackend = value;
            Q_EMIT renderingBackendChanged();
            Q_EMIT settingsChanged();
        }
    }

    // Per-screen autotile config (used by WindowAppearanceController's
    // per-monitor gapValue/writeGap and the per-screen gap accessors). Stored in
    // short-key form, mirroring the real Settings normalization so a test can
    // write "AutotileInnerGap" and read back "InnerGap".
    QVariantMap getPerScreenAutotileSettings(const QString& screenIdOrName) const override
    {
        return m_perScreenAutotile.value(screenIdOrName);
    }
    void setPerScreenAutotileSetting(const QString& screenIdOrName, const QString& key, const QVariant& value) override
    {
        const QString shortKey =
            key.startsWith(QLatin1String("Autotile")) ? key.mid(QLatin1String("Autotile").size()) : key;
        m_perScreenAutotile[screenIdOrName][shortKey] = value;
        Q_EMIT perScreenAutotileSettingsChanged();
        Q_EMIT settingsChanged();
    }
    QVariantMap perScreenGapOverrides(const QString& screenIdOrName) const override
    {
        // This set must mirror the 7 PerScreenSnappingKey gap dimensions and
        // stay in sync with the production predicate isPerScreenGapDimensionKey
        // (file-local in settings/perscreen.cpp, so not shareable here). A key
        // added on one side but not the other silently drops (or leaks) a gap
        // dimension from the stub's gap subset.
        static const QSet<QString> gapKeys = {
            QStringLiteral("InnerGap"),      QStringLiteral("OuterGap"),       QStringLiteral("UsePerSideOuterGap"),
            QStringLiteral("OuterGapTop"),   QStringLiteral("OuterGapBottom"), QStringLiteral("OuterGapLeft"),
            QStringLiteral("OuterGapRight"),
        };
        QVariantMap gaps;
        const QVariantMap all = m_perScreenAutotile.value(screenIdOrName);
        for (auto it = all.constBegin(); it != all.constEnd(); ++it) {
            if (gapKeys.contains(it.key())) {
                gaps.insert(it.key(), it.value());
            }
        }
        return gaps;
    }

    // Persistence (ISettings)
    void load() override
    {
    }
    void save() override
    {
    }
    void reset() override
    {
    }

private:
    QHash<QString, QVariantMap> m_perScreenAutotile;
    QString m_defaultLayoutId;
    bool m_suppressDefaultLayoutAssignment = ConfigDefaults::suppressDefaultLayoutAssignment();
    QString m_renderingBackend = ConfigDefaults::renderingBackend();
    bool m_snapAssistFeatureEnabled = ConfigDefaults::snapAssistFeatureEnabled();
    bool m_snapAssistEnabled = ConfigDefaults::snapAssistEnabled();
    bool m_autoAssignAllLayouts = ConfigDefaults::autoAssignAllLayouts();
    bool m_snappingRestoreFloatedWindowsOnLogin = ConfigDefaults::snappingRestoreFloatedWindowsOnLogin();
    bool m_autotileRestoreFloatedWindowsOnLogin = ConfigDefaults::autotileRestoreFloatedWindowsOnLogin();
    bool m_snapUnfloatFallbackToZone = ConfigDefaults::snapUnfloatFallbackToZone();
    bool m_snappingFocusNewWindows = ConfigDefaults::snappingFocusNewWindows();
    bool m_snappingFocusFollowsMouse = ConfigDefaults::snappingFocusFollowsMouse();
    QStringList m_snappingLayoutOrder;
    QStringList m_tilingAlgorithmOrder;
    QVariantList m_dragActivationTriggers;
    // Seeded from ConfigDefaults, every one of them. The nine below were already
    // member-backed and so escaped the sweep that fixed the hardcoded getters — including
    // BOTH snap-assist flags, which read false against a default of TRUE while the comment
    // here claimed they had been fixed. A comment describing a fix that did not land is
    // worse than no comment: it stops the next reader from looking.
    // Every remaining getter, member-backed and seeded from ConfigDefaults. They used to
    // return hardcoded literals with no-op setters: a value no test could move, against a
    // baseline production does not have. Fourteen disagreed with the real default outright,
    // and two of those (snap assist, and the animations master gate) read FALSE against a
    // default of TRUE — the same inversion the settings-registry tripwire exists to catch,
    // sitting in the fixture meant to prove the absence of it.
    AutotileDragBehavior m_autotileDragBehavior =
        static_cast<AutotileDragBehavior>(ConfigDefaults::autotileDragBehavior());
    AutotileOverflowBehavior m_autotileOverflowBehavior =
        static_cast<AutotileOverflowBehavior>(ConfigDefaults::autotileOverflowBehavior());
    DragModifier m_zoneSpanModifier = static_cast<DragModifier>(ConfigDefaults::zoneSpanModifier());
    OsdStyle m_osdStyle = static_cast<OsdStyle>(ConfigDefaults::osdStyle());
    OverlayDisplayMode m_overlayDisplayMode = static_cast<OverlayDisplayMode>(ConfigDefaults::overlayDisplayMode());
    // Empty, matching ConfigDefaults::shaderProfileTree() — which is a QVariantMap and so
    // cannot seed the tree type directly. The default IS the empty tree either way.
    PhosphorAnimationShaders::ShaderProfileTree m_shaderProfileTree;
    PhosphorSurfaceShaders::DecorationProfileTree m_decorationProfileTree =
        static_cast<PhosphorSurfaceShaders::DecorationProfileTree>(ConfigDefaults::decorationProfileTree());
    QColor m_borderColor = ConfigDefaults::borderColor();
    QColor m_highlightColor = ConfigDefaults::highlightColor();
    QColor m_inactiveColor = ConfigDefaults::inactiveColor();
    QColor m_labelFontColor = ConfigDefaults::labelFontColor();
    QString m_animationEasingCurve = ConfigDefaults::animationEasingCurve();
    QString m_audioChannelMode = ConfigDefaults::audioChannelMode();
    QString m_audioInputMethod = ConfigDefaults::audioInputMethod();
    QString m_audioInputSource = ConfigDefaults::audioInputSource();
    QString m_labelFontFamily = ConfigDefaults::labelFontFamily();
    // Per-mode disable lists, keyed by the AssignmentEntry::Mode int. Real storage: the
    // setters used to drop the value entirely, so no test could ever disable anything.
    QHash<int, QStringList> m_disabledMonitors;
    QHash<int, QStringList> m_disabledDesktops;
    QHash<int, QStringList> m_disabledActivities;
    QStringList m_lockedScreens = ConfigDefaults::lockedScreens();
    QVariantList m_autotileDragInsertTriggers = ConfigDefaults::autotileDragInsertTriggers();
    QVariantList m_snapAssistTriggers = ConfigDefaults::snapAssistTriggers();
    QVariantList m_zoneSpanTriggers = ConfigDefaults::zoneSpanTriggers();
    StickyWindowHandling m_autotileStickyWindowHandling =
        static_cast<StickyWindowHandling>(ConfigDefaults::autotileStickyWindowHandling());
    StickyWindowHandling m_snappingStickyWindowHandling =
        static_cast<StickyWindowHandling>(ConfigDefaults::snappingStickyWindowHandling());
    ZoneSelectorLayoutMode m_zoneSelectorLayoutMode = static_cast<ZoneSelectorLayoutMode>(ConfigDefaults::layoutMode());
    ZoneSelectorPosition m_zoneSelectorPosition = static_cast<ZoneSelectorPosition>(ConfigDefaults::position());
    ZoneSelectorSizeMode m_zoneSelectorSizeMode = static_cast<ZoneSelectorSizeMode>(ConfigDefaults::sizeMode());
    bool m_animationsEnabled = ConfigDefaults::animationsEnabled();
    bool m_audioAutosens = ConfigDefaults::audioAutosens();
    bool m_audioMonstercat = ConfigDefaults::audioMonstercat();
    bool m_audioReverse = ConfigDefaults::audioReverse();
    bool m_audioWaves = ConfigDefaults::audioWaves();
    bool m_autotileDragInsertToggle = ConfigDefaults::autotileDragInsertToggle();
    bool m_autotileFocusFollowsMouse = ConfigDefaults::autotileFocusFollowsMouse();
    bool m_enableAudioVisualizer = ConfigDefaults::enableAudioVisualizer();
    bool m_enableBlur = ConfigDefaults::enableBlur();
    bool m_filterLayoutsByAspectRatio = ConfigDefaults::filterLayoutsByAspectRatio();
    bool m_flashZonesOnSwitch = ConfigDefaults::flashOnSwitch();
    bool m_keepWindowsInZonesOnResolutionChange = ConfigDefaults::keepWindowsInZonesOnResolutionChange();
    bool m_labelFontItalic = ConfigDefaults::labelFontItalic();
    bool m_labelFontStrikeout = ConfigDefaults::labelFontStrikeout();
    bool m_labelFontUnderline = ConfigDefaults::labelFontUnderline();
    bool m_moveNewWindowsToLastZone = ConfigDefaults::moveNewWindowsToLastZone();
    bool m_restoreOriginalSizeOnUnsnap = ConfigDefaults::restoreOriginalSizeOnUnsnap();
    bool m_restoreWindowsToZonesOnLogin = ConfigDefaults::restoreWindowsToZonesOnLogin();
    bool m_showNavigationOsd = ConfigDefaults::showNavigationOsd();
    bool m_showOsdOnDesktopSwitch = ConfigDefaults::showOsdOnDesktopSwitch();
    bool m_showOsdOnLayoutSwitch = ConfigDefaults::showOsdOnLayoutSwitch();
    bool m_showZoneNumbers = ConfigDefaults::showNumbers();
    bool m_showZonesOnAllMonitors = ConfigDefaults::showOnAllMonitors();
    bool m_snappingEnabled = ConfigDefaults::snappingEnabled();
    bool m_toggleActivation = ConfigDefaults::toggleActivation();
    bool m_useSystemColors = ConfigDefaults::useSystemColors();
    bool m_zoneSelectorEnabled = ConfigDefaults::zoneSelectorEnabled();
    bool m_zoneSelectorPreviewLockAspect = ConfigDefaults::previewLockAspect();
    bool m_zoneSpanEnabled = ConfigDefaults::zoneSpanEnabled();
    bool m_zoneSpanToggleMode = ConfigDefaults::zoneSpanToggleMode();
    int m_adjacentThreshold = ConfigDefaults::adjacentThreshold();
    int m_animationDuration = ConfigDefaults::animationDuration();
    int m_animationMinDistance = ConfigDefaults::animationMinDistance();
    int m_animationSequenceMode = ConfigDefaults::animationSequenceMode();
    int m_animationStaggerInterval = ConfigDefaults::animationStaggerInterval();
    int m_audioExtraSmoothing = ConfigDefaults::audioExtraSmoothing();
    int m_audioHigherCutoffHz = ConfigDefaults::audioHigherCutoffHz();
    int m_audioLowerCutoffHz = ConfigDefaults::audioLowerCutoffHz();
    int m_audioNoiseReduction = ConfigDefaults::audioNoiseReduction();
    int m_audioSensitivity = ConfigDefaults::audioSensitivity();
    int m_audioSpectrumBarCount = ConfigDefaults::audioSpectrumBarCount();
    int m_borderRadius = ConfigDefaults::borderRadius();
    int m_borderWidth = ConfigDefaults::borderWidth();
    int m_labelFontWeight = ConfigDefaults::labelFontWeight();
    int m_minimumZoneDisplaySizePx = ConfigDefaults::minimumZoneDisplaySizePx();
    int m_minimumZoneSizePx = ConfigDefaults::minimumZoneSizePx();
    int m_pollIntervalMs = ConfigDefaults::pollIntervalMs();
    int m_shaderFrameRate = ConfigDefaults::shaderFrameRate();
    int m_zoneSelectorGridColumns = ConfigDefaults::gridColumns();
    int m_zoneSelectorMaxRows = ConfigDefaults::maxRows();
    int m_zoneSelectorPreviewHeight = ConfigDefaults::previewHeight();
    int m_zoneSelectorPreviewWidth = ConfigDefaults::previewWidth();
    int m_zoneSelectorTriggerDistance = ConfigDefaults::triggerDistance();
    qreal m_activeOpacity = static_cast<qreal>(ConfigDefaults::activeOpacity());
    qreal m_inactiveOpacity = static_cast<qreal>(ConfigDefaults::inactiveOpacity());
    qreal m_labelFontSizeScale = static_cast<qreal>(ConfigDefaults::labelFontSizeScale());

    // The window-exclusion defaults, from ConfigDefaults for the same reason the
    // animation-filter ones below are. These three were hardcoded, and all three disagreed
    // with production: excludeTransientWindows read false against a default of TRUE (and
    // its setter was a no-op, so no test could even move it), and the two minimum sizes
    // read 0 against 200x150 — so every stub-backed test of the size filter was exercising
    // a filter that passes everything.
    bool m_excludeTransientWindows = ConfigDefaults::excludeTransientWindows();
    int m_minimumWindowWidth = ConfigDefaults::minimumWindowWidth();
    int m_minimumWindowHeight = ConfigDefaults::minimumWindowHeight();
    // Animation-filter defaults routed through ConfigDefaults so a future
    // tweak to the production defaults flows into tests automatically — keeps
    // the stub from drifting into "tests pass against a stale baseline".
    bool m_animationExcludeTransientWindows = ConfigDefaults::animationExcludeTransientWindows();
    bool m_animationExcludeNotificationsAndOsd = ConfigDefaults::animationExcludeNotificationsAndOsd();
    int m_animationMinimumWindowWidth = ConfigDefaults::animationMinimumWindowWidth();
    int m_animationMinimumWindowHeight = ConfigDefaults::animationMinimumWindowHeight();
    // Decoration-filter defaults, routed through ConfigDefaults like the
    // animation-filter defaults above.
    bool m_decorationExcludeTransientWindows = ConfigDefaults::decorationExcludeTransientWindows();
    int m_decorationMinimumWindowWidth = ConfigDefaults::decorationMinimumWindowWidth();
    int m_decorationMinimumWindowHeight = ConfigDefaults::decorationMinimumWindowHeight();
    QVariantMap m_autotilePerAlgorithmSettings;
    QString m_editorDuplicateShortcut = ConfigDefaults::editorDuplicateShortcut();
    QString m_editorSplitHorizontalShortcut = ConfigDefaults::editorSplitHorizontalShortcut();
    QString m_editorSplitVerticalShortcut = ConfigDefaults::editorSplitVerticalShortcut();
    QString m_editorFillShortcut = ConfigDefaults::editorFillShortcut();
    bool m_editorGridSnappingEnabled = ConfigDefaults::editorGridSnappingEnabled();
    bool m_editorEdgeSnappingEnabled = ConfigDefaults::editorEdgeSnappingEnabled();
    qreal m_editorSnapIntervalX = ConfigDefaults::editorSnapIntervalX();
    qreal m_editorSnapIntervalY = ConfigDefaults::editorSnapIntervalY();
    int m_editorSnapOverrideModifier = ConfigDefaults::editorSnapOverrideModifier();
    bool m_fillOnDropEnabled = ConfigDefaults::fillOnDropEnabled();
    int m_fillOnDropModifier = ConfigDefaults::fillOnDropModifier();
    // Gaps — config-backed defaults routed through ConfigDefaults.
    int m_innerGap = ConfigDefaults::innerGap();
    int m_outerGap = ConfigDefaults::outerGap();
    bool m_usePerSideOuterGap = ConfigDefaults::usePerSideOuterGap();
    int m_outerGapTop = ConfigDefaults::outerGapTop();
    int m_outerGapBottom = ConfigDefaults::outerGapBottom();
    int m_outerGapLeft = ConfigDefaults::outerGapLeft();
    int m_outerGapRight = ConfigDefaults::outerGapRight();
    // Window appearance — config-backed defaults routed through ConfigDefaults.
    bool m_showWindowBorder = ConfigDefaults::showWindowBorder();
    QString m_windowBorderScope = ConfigDefaults::windowBorderScope();
    int m_windowBorderWidth = ConfigDefaults::windowBorderWidth();
    int m_windowBorderRadius = ConfigDefaults::windowBorderRadius();
    QString m_windowBorderColorActive = ConfigDefaults::windowBorderColorActive();
    QString m_windowBorderColorInactive = ConfigDefaults::windowBorderColorInactive();
    bool m_hideWindowTitleBars = ConfigDefaults::hideWindowTitleBars();
    QString m_windowTitleBarScope = ConfigDefaults::windowTitleBarScope();
    int m_focusFadeDuration = ConfigDefaults::focusFadeDuration();
    bool m_decorationAnimateFocusedOnly = ConfigDefaults::decorationAnimateFocusedOnly();
    bool m_decorationPauseWhenIdle = ConfigDefaults::decorationPauseWhenIdle();
    int m_decorationIdleTimeoutSec = ConfigDefaults::decorationIdleTimeoutSec();
};

} // namespace PlasmaZones
