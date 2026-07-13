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
        return true;
    }
    void setSnappingEnabled(bool) override
    {
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
        return false;
    }
    void setZoneSpanEnabled(bool) override
    {
    }
    DragModifier zoneSpanModifier() const override
    {
        return DragModifier::Disabled;
    }
    void setZoneSpanModifier(DragModifier) override
    {
    }
    QVariantList zoneSpanTriggers() const override
    {
        return {};
    }
    void setZoneSpanTriggers(const QVariantList&) override
    {
    }
    bool zoneSpanToggleMode() const override
    {
        return false;
    }
    void setZoneSpanToggleMode(bool) override
    {
    }
    bool toggleActivation() const override
    {
        return false;
    }
    void setToggleActivation(bool) override
    {
    }

    // IZoneVisualizationSettings
    bool showZonesOnAllMonitors() const override
    {
        return false;
    }
    void setShowZonesOnAllMonitors(bool) override
    {
    }
    QStringList disabledMonitors(PhosphorZones::AssignmentEntry::Mode) const override
    {
        return {};
    }
    void setDisabledMonitors(PhosphorZones::AssignmentEntry::Mode, const QStringList&) override
    {
    }
    bool isMonitorDisabled(PhosphorZones::AssignmentEntry::Mode, const QString&) const override
    {
        return false;
    }
    QStringList disabledDesktops(PhosphorZones::AssignmentEntry::Mode) const override
    {
        return {};
    }
    void setDisabledDesktops(PhosphorZones::AssignmentEntry::Mode, const QStringList&) override
    {
    }
    bool isDesktopDisabled(PhosphorZones::AssignmentEntry::Mode, const QString& /*screenIdOrName*/, int) const override
    {
        return false;
    }
    QStringList disabledActivities(PhosphorZones::AssignmentEntry::Mode) const override
    {
        return {};
    }
    void setDisabledActivities(PhosphorZones::AssignmentEntry::Mode, const QStringList&) override
    {
    }
    bool isActivityDisabled(PhosphorZones::AssignmentEntry::Mode, const QString& /*screenIdOrName*/,
                            const QString&) const override
    {
        return false;
    }
    bool showZoneNumbers() const override
    {
        return true;
    }
    void setShowZoneNumbers(bool) override
    {
    }
    bool flashZonesOnSwitch() const override
    {
        return false;
    }
    void setFlashZonesOnSwitch(bool) override
    {
    }
    bool showOsdOnLayoutSwitch() const override
    {
        return false;
    }
    void setShowOsdOnLayoutSwitch(bool) override
    {
    }
    bool showOsdOnDesktopSwitch() const override
    {
        return false;
    }
    void setShowOsdOnDesktopSwitch(bool) override
    {
    }
    bool showNavigationOsd() const override
    {
        return false;
    }
    void setShowNavigationOsd(bool) override
    {
    }
    OsdStyle osdStyle() const override
    {
        return OsdStyle::None;
    }
    void setOsdStyle(OsdStyle) override
    {
    }
    OverlayDisplayMode overlayDisplayMode() const override
    {
        return OverlayDisplayMode::ZoneRectangles;
    }
    void setOverlayDisplayMode(OverlayDisplayMode) override
    {
    }
    bool useSystemColors() const override
    {
        return false;
    }
    void setUseSystemColors(bool) override
    {
    }
    QColor highlightColor() const override
    {
        return Qt::blue;
    }
    void setHighlightColor(const QColor&) override
    {
    }
    QColor inactiveColor() const override
    {
        return Qt::gray;
    }
    void setInactiveColor(const QColor&) override
    {
    }
    QColor borderColor() const override
    {
        return Qt::white;
    }
    void setBorderColor(const QColor&) override
    {
    }
    QColor labelFontColor() const override
    {
        return Qt::white;
    }
    void setLabelFontColor(const QColor&) override
    {
    }
    qreal activeOpacity() const override
    {
        return 0.5;
    }
    void setActiveOpacity(qreal) override
    {
    }
    qreal inactiveOpacity() const override
    {
        return 0.3;
    }
    void setInactiveOpacity(qreal) override
    {
    }
    int borderWidth() const override
    {
        return 2;
    }
    void setBorderWidth(int) override
    {
    }
    int borderRadius() const override
    {
        return 8;
    }
    void setBorderRadius(int) override
    {
    }
    bool enableBlur() const override
    {
        return false;
    }
    void setEnableBlur(bool) override
    {
    }
    QString labelFontFamily() const override
    {
        return {};
    }
    void setLabelFontFamily(const QString&) override
    {
    }
    qreal labelFontSizeScale() const override
    {
        return 1.0;
    }
    void setLabelFontSizeScale(qreal) override
    {
    }
    int labelFontWeight() const override
    {
        return 400;
    }
    void setLabelFontWeight(int) override
    {
    }
    bool labelFontItalic() const override
    {
        return false;
    }
    void setLabelFontItalic(bool) override
    {
    }
    bool labelFontUnderline() const override
    {
        return false;
    }
    void setLabelFontUnderline(bool) override
    {
    }
    bool labelFontStrikeout() const override
    {
        return false;
    }
    void setLabelFontStrikeout(bool) override
    {
    }
    int shaderFrameRate() const override
    {
        return ConfigDefaults::shaderFrameRate();
    }
    void setShaderFrameRate(int) override
    {
    }
    bool enableAudioVisualizer() const override
    {
        return ConfigDefaults::enableAudioVisualizer();
    }
    void setEnableAudioVisualizer(bool) override
    {
    }
    // Audio spectrum (Shaders.Audio) — routed through ConfigDefaults so a
    // future default tweak flows into tests automatically (the file's
    // standing anti-drift convention).
    int audioSpectrumBarCount() const override
    {
        return ConfigDefaults::audioSpectrumBarCount();
    }
    void setAudioSpectrumBarCount(int) override
    {
    }
    bool audioAutosens() const override
    {
        return ConfigDefaults::audioAutosens();
    }
    void setAudioAutosens(bool) override
    {
    }
    int audioSensitivity() const override
    {
        return ConfigDefaults::audioSensitivity();
    }
    void setAudioSensitivity(int) override
    {
    }
    int audioNoiseReduction() const override
    {
        return ConfigDefaults::audioNoiseReduction();
    }
    void setAudioNoiseReduction(int) override
    {
    }
    int audioLowerCutoffHz() const override
    {
        return ConfigDefaults::audioLowerCutoffHz();
    }
    void setAudioLowerCutoffHz(int) override
    {
    }
    int audioHigherCutoffHz() const override
    {
        return ConfigDefaults::audioHigherCutoffHz();
    }
    void setAudioHigherCutoffHz(int) override
    {
    }
    bool audioMonstercat() const override
    {
        return ConfigDefaults::audioMonstercat();
    }
    void setAudioMonstercat(bool) override
    {
    }
    bool audioWaves() const override
    {
        return ConfigDefaults::audioWaves();
    }
    void setAudioWaves(bool) override
    {
    }
    QString audioChannelMode() const override
    {
        return ConfigDefaults::audioChannelMode();
    }
    void setAudioChannelMode(const QString&) override
    {
    }
    bool audioReverse() const override
    {
        return ConfigDefaults::audioReverse();
    }
    void setAudioReverse(bool) override
    {
    }
    int audioExtraSmoothing() const override
    {
        return ConfigDefaults::audioExtraSmoothing();
    }
    void setAudioExtraSmoothing(int) override
    {
    }
    QString audioInputMethod() const override
    {
        return ConfigDefaults::audioInputMethod();
    }
    void setAudioInputMethod(const QString&) override
    {
    }
    QString audioInputSource() const override
    {
        return ConfigDefaults::audioInputSource();
    }
    void setAudioInputSource(const QString&) override
    {
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
        return 20;
    }
    void setAdjacentThreshold(int) override
    {
    }
    int pollIntervalMs() const override
    {
        return 50;
    }
    void setPollIntervalMs(int) override
    {
    }
    int minimumZoneSizePx() const override
    {
        return 100;
    }
    void setMinimumZoneSizePx(int) override
    {
    }
    int minimumZoneDisplaySizePx() const override
    {
        return 10;
    }
    void setMinimumZoneDisplaySizePx(int) override
    {
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
        return true;
    }
    void setZoneSelectorEnabled(bool) override
    {
    }
    int zoneSelectorTriggerDistance() const override
    {
        return 50;
    }
    void setZoneSelectorTriggerDistance(int) override
    {
    }
    ZoneSelectorPosition zoneSelectorPosition() const override
    {
        return ZoneSelectorPosition::Top;
    }
    void setZoneSelectorPosition(ZoneSelectorPosition) override
    {
    }
    ZoneSelectorLayoutMode zoneSelectorLayoutMode() const override
    {
        return ZoneSelectorLayoutMode::Grid;
    }
    void setZoneSelectorLayoutMode(ZoneSelectorLayoutMode) override
    {
    }
    int zoneSelectorPreviewWidth() const override
    {
        return 180;
    }
    void setZoneSelectorPreviewWidth(int) override
    {
    }
    int zoneSelectorPreviewHeight() const override
    {
        return 101;
    }
    void setZoneSelectorPreviewHeight(int) override
    {
    }
    bool zoneSelectorPreviewLockAspect() const override
    {
        return true;
    }
    void setZoneSelectorPreviewLockAspect(bool) override
    {
    }
    int zoneSelectorGridColumns() const override
    {
        return 5;
    }
    void setZoneSelectorGridColumns(int) override
    {
    }
    ZoneSelectorSizeMode zoneSelectorSizeMode() const override
    {
        return ZoneSelectorSizeMode::Auto;
    }
    void setZoneSelectorSizeMode(ZoneSelectorSizeMode) override
    {
    }
    int zoneSelectorMaxRows() const override
    {
        return 4;
    }
    void setZoneSelectorMaxRows(int) override
    {
    }

    // IWindowBehaviorSettings
    bool keepWindowsInZonesOnResolutionChange() const override
    {
        return true;
    }
    void setKeepWindowsInZonesOnResolutionChange(bool) override
    {
    }
    bool moveNewWindowsToLastZone() const override
    {
        return false;
    }
    void setMoveNewWindowsToLastZone(bool) override
    {
    }
    // ISnapSettings::focusNewWindows() — shares the ISettings snappingFocusNewWindows
    // member so a test can drive the snap engine's focus-new path.
    bool focusNewWindows() const override
    {
        return m_snappingFocusNewWindows;
    }
    bool restoreOriginalSizeOnUnsnap() const override
    {
        return true;
    }
    void setRestoreOriginalSizeOnUnsnap(bool) override
    {
    }
    StickyWindowHandling snappingStickyWindowHandling() const override
    {
        return StickyWindowHandling::TreatAsNormal;
    }
    void setSnappingStickyWindowHandling(StickyWindowHandling) override
    {
    }
    bool restoreWindowsToZonesOnLogin() const override
    {
        return true;
    }
    void setRestoreWindowsToZonesOnLogin(bool) override
    {
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
        return {};
    }
    void setSnapAssistTriggers(const QVariantList&) override
    {
    }
    bool filterLayoutsByAspectRatio() const override
    {
        return true;
    }
    void setFilterLayoutsByAspectRatio(bool) override
    {
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
        return false;
    }
    void setAnimationsEnabled(bool) override
    {
    }
    int animationDuration() const override
    {
        return 200;
    }
    void setAnimationDuration(int) override
    {
    }
    QString animationEasingCurve() const override
    {
        return {};
    }
    void setAnimationEasingCurve(const QString&) override
    {
    }
    int animationMinDistance() const override
    {
        return 10;
    }
    void setAnimationMinDistance(int) override
    {
    }
    int animationSequenceMode() const override
    {
        return 0;
    }
    void setAnimationSequenceMode(int) override
    {
    }
    int animationStaggerInterval() const override
    {
        return 50;
    }
    void setAnimationStaggerInterval(int) override
    {
    }
    PhosphorAnimationShaders::ShaderProfileTree shaderProfileTree() const override
    {
        return {};
    }
    void setShaderProfileTree(const PhosphorAnimationShaders::ShaderProfileTree&) override
    {
    }
    PhosphorSurfaceShaders::DecorationProfileTree decorationProfileTree() const override
    {
        return ConfigDefaults::decorationProfileTree();
    }
    void setDecorationProfileTree(const PhosphorSurfaceShaders::DecorationProfileTree&) override
    {
    }
    QString decorationProfileTreeJson() const override
    {
        // Serialize the SAME tree the typed accessor returns, so the two
        // accessors stay coherent for any test that reads the JSON facade.
        return QString::fromUtf8(QJsonDocument(decorationProfileTree().toJson()).toJson(QJsonDocument::Compact));
    }
    void setDecorationProfileTreeJson(const QString&) override
    {
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
        return false;
    }
    void setAutotileFocusFollowsMouse(bool) override
    {
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
        return StickyWindowHandling::TreatAsNormal;
    }
    void setAutotileStickyWindowHandling(StickyWindowHandling) override
    {
    }
    AutotileDragBehavior autotileDragBehavior() const override
    {
        return AutotileDragBehavior::Float;
    }
    void setAutotileDragBehavior(AutotileDragBehavior) override
    {
    }
    AutotileOverflowBehavior autotileOverflowBehavior() const override
    {
        return AutotileOverflowBehavior::Float;
    }
    void setAutotileOverflowBehavior(AutotileOverflowBehavior) override
    {
    }
    QVariantList autotileDragInsertTriggers() const override
    {
        return ConfigDefaults::autotileDragInsertTriggers();
    }
    void setAutotileDragInsertTriggers(const QVariantList&) override
    {
    }
    bool autotileDragInsertToggle() const override
    {
        return false;
    }
    void setAutotileDragInsertToggle(bool) override
    {
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
        return {};
    }
    void setLockedScreens(const QStringList&) override
    {
    }
    bool isScreenLocked(const QString&) const override
    {
        return false;
    }
    void setScreenLocked(const QString&, bool) override
    {
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
    bool m_suppressDefaultLayoutAssignment = false;
    QString m_renderingBackend = ConfigDefaults::renderingBackend();
    bool m_snapAssistFeatureEnabled = false;
    bool m_snapAssistEnabled = false;
    bool m_autoAssignAllLayouts = false;
    bool m_snappingRestoreFloatedWindowsOnLogin = true;
    bool m_autotileRestoreFloatedWindowsOnLogin = true;
    bool m_snapUnfloatFallbackToZone = false;
    bool m_snappingFocusNewWindows = false;
    bool m_snappingFocusFollowsMouse = false;
    QStringList m_snappingLayoutOrder;
    QStringList m_tilingAlgorithmOrder;
    QVariantList m_dragActivationTriggers;
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
