// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "settingsadaptor.h"
#include "../core/interfaces.h"
#include "../config/settings.h" // For concrete Settings type
#include "../core/dbusvariantutils.h"
#include "../core/logging.h"
#include "../core/shaderregistry.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QColor>
#include <QDBusVariant>

namespace PlasmaZones {

SettingsAdaptor::SettingsAdaptor(ISettings* settings, QObject* parent)
    : QDBusAbstractAdaptor(parent)
    , m_settings(settings)
    , m_saveTimer(new QTimer(this))
{
    Q_ASSERT(settings);
    initializeRegistry();

    // Configure debounced save timer (performance optimization)
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(SaveDebounceMs);
    connect(m_saveTimer, &QTimer::timeout, this, [this]() {
        m_settings->save();
        qCInfo(lcDbusSettings) << "Settings save completed";
    });

    // Connect to interface signals (DIP)
    connect(m_settings, &ISettings::settingsChanged, this, &SettingsAdaptor::settingsChanged);

    // Drop shader caches whenever the registry reloads from disk. The
    // registry is a singleton, but may not yet exist in unit tests that
    // instantiate SettingsAdaptor without a daemon — guard the connect.
    if (auto* registry = ShaderRegistry::instance()) {
        connect(registry, &ShaderRegistry::shadersChanged, this, &SettingsAdaptor::invalidateShaderCaches);
    }
}

SettingsAdaptor::~SettingsAdaptor()
{
    // Flush any pending debounced saves before destruction
    // This ensures settings are not lost on shutdown
    if (m_saveTimer->isActive()) {
        m_saveTimer->stop();
        m_settings->save();
        qCInfo(lcDbusSettings) << "Flushed pending save on destruction";
    }
}

void SettingsAdaptor::scheduleSave()
{
    // Restart the timer on each setting change (debouncing)
    // This batches multiple rapid changes into a single save
    m_saveTimer->start();
}

// Note: Macros are defined in initializeRegistry() to avoid namespace pollution

void SettingsAdaptor::initializeRegistry()
{
// Register getters and setters for all settings
// This registry pattern allows adding new settings without modifying setSetting()
// Using macros to reduce repetition

// Helper macros
#define REGISTER_STRING_SETTING(name, getter, setter)                                                                  \
    m_getters[QStringLiteral(name)] = [this]() {                                                                       \
        return m_settings->getter();                                                                                   \
    };                                                                                                                 \
    m_setters[QStringLiteral(name)] = [this](const QVariant& v) {                                                      \
        m_settings->setter(v.toString());                                                                              \
        return true;                                                                                                   \
    };                                                                                                                 \
    m_schemas[QStringLiteral(name)] = QStringLiteral("string");

#define REGISTER_BOOL_SETTING(name, getter, setter)                                                                    \
    m_getters[QStringLiteral(name)] = [this]() {                                                                       \
        return m_settings->getter();                                                                                   \
    };                                                                                                                 \
    m_setters[QStringLiteral(name)] = [this](const QVariant& v) {                                                      \
        m_settings->setter(v.toBool());                                                                                \
        return true;                                                                                                   \
    };                                                                                                                 \
    m_schemas[QStringLiteral(name)] = QStringLiteral("bool");

#define REGISTER_INT_SETTING(name, getter, setter)                                                                     \
    m_getters[QStringLiteral(name)] = [this]() {                                                                       \
        return m_settings->getter();                                                                                   \
    };                                                                                                                 \
    m_setters[QStringLiteral(name)] = [this](const QVariant& v) {                                                      \
        m_settings->setter(v.toInt());                                                                                 \
        return true;                                                                                                   \
    };                                                                                                                 \
    m_schemas[QStringLiteral(name)] = QStringLiteral("int");

#define REGISTER_DOUBLE_SETTING(name, getter, setter)                                                                  \
    m_getters[QStringLiteral(name)] = [this]() {                                                                       \
        return m_settings->getter();                                                                                   \
    };                                                                                                                 \
    m_setters[QStringLiteral(name)] = [this](const QVariant& v) {                                                      \
        m_settings->setter(v.toDouble());                                                                              \
        return true;                                                                                                   \
    };                                                                                                                 \
    m_schemas[QStringLiteral(name)] = QStringLiteral("double");

#define REGISTER_COLOR_SETTING(keyName, getter, setter)                                                                \
    m_getters[QStringLiteral(keyName)] = [this]() {                                                                    \
        QColor color = m_settings->getter();                                                                           \
        return color.name(QColor::HexArgb);                                                                            \
    };                                                                                                                 \
    m_setters[QStringLiteral(keyName)] = [this](const QVariant& v) {                                                   \
        m_settings->setter(QColor(v.toString()));                                                                      \
        return true;                                                                                                   \
    };                                                                                                                 \
    m_schemas[QStringLiteral(keyName)] = QStringLiteral("color");

#define REGISTER_STRINGLIST_SETTING(name, getter, setter)                                                              \
    m_getters[QStringLiteral(name)] = [this]() {                                                                       \
        return m_settings->getter();                                                                                   \
    };                                                                                                                 \
    m_setters[QStringLiteral(name)] = [this](const QVariant& v) {                                                      \
        m_settings->setter(v.toStringList());                                                                          \
        return true;                                                                                                   \
    };                                                                                                                 \
    m_schemas[QStringLiteral(name)] = QStringLiteral("stringlist");

    // Concrete Settings pointer for properties not on ISettings interface
    auto* concrete = qobject_cast<Settings*>(m_settings);

// Macros for concrete Settings entries (same pattern as REGISTER_* but captures 'concrete')
#define REGISTER_CONCRETE_BOOL(name, getter, setter)                                                                   \
    m_getters[QStringLiteral(name)] = [concrete]() {                                                                   \
        return concrete->getter();                                                                                     \
    };                                                                                                                 \
    m_setters[QStringLiteral(name)] = [concrete](const QVariant& v) {                                                  \
        concrete->setter(v.toBool());                                                                                  \
        return true;                                                                                                   \
    };                                                                                                                 \
    m_schemas[QStringLiteral(name)] = QStringLiteral("bool");
#define REGISTER_CONCRETE_INT(name, getter, setter)                                                                    \
    m_getters[QStringLiteral(name)] = [concrete]() {                                                                   \
        return concrete->getter();                                                                                     \
    };                                                                                                                 \
    m_setters[QStringLiteral(name)] = [concrete](const QVariant& v) {                                                  \
        concrete->setter(v.toInt());                                                                                   \
        return true;                                                                                                   \
    };                                                                                                                 \
    m_schemas[QStringLiteral(name)] = QStringLiteral("int");
#define REGISTER_CONCRETE_DOUBLE(name, getter, setter)                                                                 \
    m_getters[QStringLiteral(name)] = [concrete]() {                                                                   \
        return concrete->getter();                                                                                     \
    };                                                                                                                 \
    m_setters[QStringLiteral(name)] = [concrete](const QVariant& v) {                                                  \
        concrete->setter(v.toDouble());                                                                                \
        return true;                                                                                                   \
    };                                                                                                                 \
    m_schemas[QStringLiteral(name)] = QStringLiteral("double");
#define REGISTER_CONCRETE_STRING(name, getter, setter)                                                                 \
    m_getters[QStringLiteral(name)] = [concrete]() {                                                                   \
        return concrete->getter();                                                                                     \
    };                                                                                                                 \
    m_setters[QStringLiteral(name)] = [concrete](const QVariant& v) {                                                  \
        concrete->setter(v.toString());                                                                                \
        return true;                                                                                                   \
    };                                                                                                                 \
    m_schemas[QStringLiteral(name)] = QStringLiteral("string");

    // Activation settings — drag activation triggers list (multi-bind)
    m_getters[QStringLiteral("dragActivationTriggers")] = [this]() {
        return QVariant::fromValue(m_settings->dragActivationTriggers());
    };
    m_setters[QStringLiteral("dragActivationTriggers")] = [this](const QVariant& v) {
        m_settings->setDragActivationTriggers(v.toList());
        return true;
    };
    m_schemas[QStringLiteral("dragActivationTriggers")] = QStringLiteral("stringlist");

    REGISTER_BOOL_SETTING("zoneSpanEnabled", zoneSpanEnabled, setZoneSpanEnabled)

    // Zone span modifier (legacy single value)
    m_getters[QStringLiteral("zoneSpanModifier")] = [this]() {
        return static_cast<int>(m_settings->zoneSpanModifier());
    };
    m_setters[QStringLiteral("zoneSpanModifier")] = [this](const QVariant& v) {
        int mod = v.toInt();
        if (mod >= 0 && mod <= static_cast<int>(DragModifier::CtrlAltMeta)) {
            m_settings->setZoneSpanModifier(static_cast<DragModifier>(mod));
            return true;
        }
        return false;
    };
    m_schemas[QStringLiteral("zoneSpanModifier")] = QStringLiteral("int");

    // Zone span triggers list (multi-bind)
    m_getters[QStringLiteral("zoneSpanTriggers")] = [this]() {
        return QVariant::fromValue(m_settings->zoneSpanTriggers());
    };
    m_setters[QStringLiteral("zoneSpanTriggers")] = [this](const QVariant& v) {
        m_settings->setZoneSpanTriggers(v.toList());
        return true;
    };
    m_schemas[QStringLiteral("zoneSpanTriggers")] = QStringLiteral("stringlist");

    // Autotile drag-insert triggers list (multi-bind) — consumed by the KWin
    // effect so it knows which modifier/mouse-button combos should forward
    // dragMoved events to the daemon during an autotile-bypassed drag.
    m_getters[QStringLiteral("autotileDragInsertTriggers")] = [this]() {
        return QVariant::fromValue(m_settings->autotileDragInsertTriggers());
    };
    m_setters[QStringLiteral("autotileDragInsertTriggers")] = [this](const QVariant& v) {
        m_settings->setAutotileDragInsertTriggers(v.toList());
        return true;
    };
    m_schemas[QStringLiteral("autotileDragInsertTriggers")] = QStringLiteral("stringlist");

    REGISTER_BOOL_SETTING("autotileDragInsertToggle", autotileDragInsertToggle, setAutotileDragInsertToggle)
    REGISTER_BOOL_SETTING("toggleActivation", toggleActivation, setToggleActivation)
    REGISTER_BOOL_SETTING("snappingEnabled", snappingEnabled, setSnappingEnabled)

    // Display settings
    REGISTER_BOOL_SETTING("showZonesOnAllMonitors", showZonesOnAllMonitors, setShowZonesOnAllMonitors)
    REGISTER_BOOL_SETTING("showZoneNumbers", showZoneNumbers, setShowZoneNumbers)
    REGISTER_BOOL_SETTING("flashZonesOnSwitch", flashZonesOnSwitch, setFlashZonesOnSwitch)
    REGISTER_BOOL_SETTING("showOsdOnLayoutSwitch", showOsdOnLayoutSwitch, setShowOsdOnLayoutSwitch)
    REGISTER_BOOL_SETTING("showNavigationOsd", showNavigationOsd, setShowNavigationOsd)
    // osdStyle: enum (0=None, 1=Text, 2=Preview) — use interface's OsdStyle
    m_getters[QStringLiteral("osdStyle")] = [this]() {
        return static_cast<int>(m_settings->osdStyle());
    };
    m_setters[QStringLiteral("osdStyle")] = [this](const QVariant& v) {
        int val = v.toInt();
        if (val >= 0 && val <= 2) {
            m_settings->setOsdStyle(static_cast<OsdStyle>(val));
            return true;
        }
        return false;
    };
    m_schemas[QStringLiteral("osdStyle")] = QStringLiteral("int");
    // overlayDisplayMode: enum (0=ZoneRectangles, 1=LayoutPreview)
    m_getters[QStringLiteral("overlayDisplayMode")] = [this]() {
        return static_cast<int>(m_settings->overlayDisplayMode());
    };
    m_setters[QStringLiteral("overlayDisplayMode")] = [this](const QVariant& v) {
        int val = v.toInt();
        if (val >= 0 && val <= 1) {
            m_settings->setOverlayDisplayMode(static_cast<OverlayDisplayMode>(val));
            return true;
        }
        return false;
    };
    m_schemas[QStringLiteral("overlayDisplayMode")] = QStringLiteral("int");
    REGISTER_STRINGLIST_SETTING("disabledMonitors", disabledMonitors, setDisabledMonitors)
    REGISTER_STRINGLIST_SETTING("disabledDesktops", disabledDesktops, setDisabledDesktops)
    REGISTER_STRINGLIST_SETTING("disabledActivities", disabledActivities, setDisabledActivities)

    // Appearance settings
    REGISTER_BOOL_SETTING("useSystemColors", useSystemColors, setUseSystemColors)
    REGISTER_COLOR_SETTING("highlightColor", highlightColor, setHighlightColor)
    REGISTER_COLOR_SETTING("inactiveColor", inactiveColor, setInactiveColor)
    REGISTER_COLOR_SETTING("borderColor", borderColor, setBorderColor)
    REGISTER_COLOR_SETTING("labelFontColor", labelFontColor, setLabelFontColor)
    REGISTER_DOUBLE_SETTING("activeOpacity", activeOpacity, setActiveOpacity)
    REGISTER_DOUBLE_SETTING("inactiveOpacity", inactiveOpacity, setInactiveOpacity)
    REGISTER_INT_SETTING("borderWidth", borderWidth, setBorderWidth)
    REGISTER_INT_SETTING("borderRadius", borderRadius, setBorderRadius)
    REGISTER_BOOL_SETTING("enableBlur", enableBlur, setEnableBlur)
    REGISTER_STRING_SETTING("labelFontFamily", labelFontFamily, setLabelFontFamily)
    // Custom setter with range validation (0.25-3.0) instead of REGISTER_DOUBLE_SETTING
    m_getters[QStringLiteral("labelFontSizeScale")] = [this]() {
        return m_settings->labelFontSizeScale();
    };
    m_setters[QStringLiteral("labelFontSizeScale")] = [this](const QVariant& v) {
        bool ok;
        double val = v.toDouble(&ok);
        if (!ok || val < 0.25 || val > 3.0) {
            return false;
        }
        m_settings->setLabelFontSizeScale(val);
        return true;
    };
    m_schemas[QStringLiteral("labelFontSizeScale")] = QStringLiteral("double");
    REGISTER_INT_SETTING("labelFontWeight", labelFontWeight, setLabelFontWeight)
    REGISTER_BOOL_SETTING("labelFontItalic", labelFontItalic, setLabelFontItalic)
    REGISTER_BOOL_SETTING("labelFontUnderline", labelFontUnderline, setLabelFontUnderline)
    REGISTER_BOOL_SETTING("labelFontStrikeout", labelFontStrikeout, setLabelFontStrikeout)
    REGISTER_STRING_SETTING("renderingBackend", renderingBackend, setRenderingBackend)
    REGISTER_BOOL_SETTING("enableShaderEffects", enableShaderEffects, setEnableShaderEffects)
    REGISTER_INT_SETTING("shaderFrameRate", shaderFrameRate, setShaderFrameRate)
    REGISTER_BOOL_SETTING("enableAudioVisualizer", enableAudioVisualizer, setEnableAudioVisualizer)
    REGISTER_INT_SETTING("audioSpectrumBarCount", audioSpectrumBarCount, setAudioSpectrumBarCount)

    // Zone settings
    REGISTER_INT_SETTING("zonePadding", zonePadding, setZonePadding)
    REGISTER_INT_SETTING("outerGap", outerGap, setOuterGap)
    REGISTER_BOOL_SETTING("usePerSideOuterGap", usePerSideOuterGap, setUsePerSideOuterGap)
    REGISTER_INT_SETTING("outerGapTop", outerGapTop, setOuterGapTop)
    REGISTER_INT_SETTING("outerGapBottom", outerGapBottom, setOuterGapBottom)
    REGISTER_INT_SETTING("outerGapLeft", outerGapLeft, setOuterGapLeft)
    REGISTER_INT_SETTING("outerGapRight", outerGapRight, setOuterGapRight)
    REGISTER_INT_SETTING("adjacentThreshold", adjacentThreshold, setAdjacentThreshold)
    REGISTER_INT_SETTING("pollIntervalMs", pollIntervalMs, setPollIntervalMs)
    REGISTER_INT_SETTING("minimumZoneSizePx", minimumZoneSizePx, setMinimumZoneSizePx)
    REGISTER_INT_SETTING("minimumZoneDisplaySizePx", minimumZoneDisplaySizePx, setMinimumZoneDisplaySizePx)

    // Behavior settings
    REGISTER_BOOL_SETTING("keepWindowsInZonesOnResolutionChange", keepWindowsInZonesOnResolutionChange,
                          setKeepWindowsInZonesOnResolutionChange)
    REGISTER_BOOL_SETTING("moveNewWindowsToLastZone", moveNewWindowsToLastZone, setMoveNewWindowsToLastZone)
    REGISTER_BOOL_SETTING("restoreOriginalSizeOnUnsnap", restoreOriginalSizeOnUnsnap, setRestoreOriginalSizeOnUnsnap)
    // stickyWindowHandling: enum (0=TreatAsNormal, 1=RestoreOnly, 2=IgnoreAll)
    m_getters[QStringLiteral("stickyWindowHandling")] = [this]() {
        return static_cast<int>(m_settings->stickyWindowHandling());
    };
    m_setters[QStringLiteral("stickyWindowHandling")] = [this](const QVariant& v) {
        int val = v.toInt();
        if (val >= 0 && val <= 2) {
            m_settings->setStickyWindowHandling(static_cast<StickyWindowHandling>(val));
            return true;
        }
        return false;
    };
    m_schemas[QStringLiteral("stickyWindowHandling")] = QStringLiteral("int");
    REGISTER_BOOL_SETTING("restoreWindowsToZonesOnLogin", restoreWindowsToZonesOnLogin, setRestoreWindowsToZonesOnLogin)
    REGISTER_BOOL_SETTING("snapAssistFeatureEnabled", snapAssistFeatureEnabled, setSnapAssistFeatureEnabled)
    REGISTER_BOOL_SETTING("snapAssistEnabled", snapAssistEnabled, setSnapAssistEnabled)

    // Snap assist triggers (when always-enabled is off, hold any trigger at drop to enable)
    m_getters[QStringLiteral("snapAssistTriggers")] = [this]() {
        return QVariant::fromValue(m_settings->snapAssistTriggers());
    };
    m_setters[QStringLiteral("snapAssistTriggers")] = [this](const QVariant& v) {
        m_settings->setSnapAssistTriggers(v.toList());
        return true;
    };
    m_schemas[QStringLiteral("snapAssistTriggers")] = QStringLiteral("stringlist");

    // Default layout
    REGISTER_STRING_SETTING("defaultLayoutId", defaultLayoutId, setDefaultLayoutId)

    // Exclusions
    REGISTER_STRINGLIST_SETTING("excludedApplications", excludedApplications, setExcludedApplications)
    REGISTER_STRINGLIST_SETTING("excludedWindowClasses", excludedWindowClasses, setExcludedWindowClasses)
    REGISTER_BOOL_SETTING("excludeTransientWindows", excludeTransientWindows, setExcludeTransientWindows)
    REGISTER_INT_SETTING("minimumWindowWidth", minimumWindowWidth, setMinimumWindowWidth)
    REGISTER_INT_SETTING("minimumWindowHeight", minimumWindowHeight, setMinimumWindowHeight)

    // Zone selector settings
    REGISTER_BOOL_SETTING("zoneSelectorEnabled", zoneSelectorEnabled, setZoneSelectorEnabled)
    REGISTER_INT_SETTING("zoneSelectorTriggerDistance", zoneSelectorTriggerDistance, setZoneSelectorTriggerDistance)
    // zoneSelectorPosition: enum (0=TopLeft .. 8=BottomRight)
    m_getters[QStringLiteral("zoneSelectorPosition")] = [this]() {
        return static_cast<int>(m_settings->zoneSelectorPosition());
    };
    m_setters[QStringLiteral("zoneSelectorPosition")] = [this](const QVariant& v) {
        int val = v.toInt();
        if (val >= 0 && val <= 8) {
            m_settings->setZoneSelectorPosition(static_cast<ZoneSelectorPosition>(val));
            return true;
        }
        return false;
    };
    m_schemas[QStringLiteral("zoneSelectorPosition")] = QStringLiteral("int");
    // zoneSelectorLayoutMode: enum (0=Grid, 1=Horizontal, 2=Vertical)
    m_getters[QStringLiteral("zoneSelectorLayoutMode")] = [this]() {
        return static_cast<int>(m_settings->zoneSelectorLayoutMode());
    };
    m_setters[QStringLiteral("zoneSelectorLayoutMode")] = [this](const QVariant& v) {
        int val = v.toInt();
        if (val >= 0 && val <= 2) {
            m_settings->setZoneSelectorLayoutMode(static_cast<ZoneSelectorLayoutMode>(val));
            return true;
        }
        return false;
    };
    m_schemas[QStringLiteral("zoneSelectorLayoutMode")] = QStringLiteral("int");
    // zoneSelectorSizeMode: enum (0=Auto, 1=Manual)
    m_getters[QStringLiteral("zoneSelectorSizeMode")] = [this]() {
        return static_cast<int>(m_settings->zoneSelectorSizeMode());
    };
    m_setters[QStringLiteral("zoneSelectorSizeMode")] = [this](const QVariant& v) {
        int val = v.toInt();
        if (val >= 0 && val <= 1) {
            m_settings->setZoneSelectorSizeMode(static_cast<ZoneSelectorSizeMode>(val));
            return true;
        }
        return false;
    };
    m_schemas[QStringLiteral("zoneSelectorSizeMode")] = QStringLiteral("int");
    REGISTER_INT_SETTING("zoneSelectorMaxRows", zoneSelectorMaxRows, setZoneSelectorMaxRows)
    REGISTER_INT_SETTING("zoneSelectorPreviewWidth", zoneSelectorPreviewWidth, setZoneSelectorPreviewWidth)
    REGISTER_INT_SETTING("zoneSelectorPreviewHeight", zoneSelectorPreviewHeight, setZoneSelectorPreviewHeight)
    REGISTER_BOOL_SETTING("zoneSelectorPreviewLockAspect", zoneSelectorPreviewLockAspect,
                          setZoneSelectorPreviewLockAspect)
    REGISTER_INT_SETTING("zoneSelectorGridColumns", zoneSelectorGridColumns, setZoneSelectorGridColumns)

    // Animation settings (global — applies to snapping and autotiling)
    REGISTER_BOOL_SETTING("animationsEnabled", animationsEnabled, setAnimationsEnabled)
    REGISTER_INT_SETTING("animationDuration", animationDuration, setAnimationDuration)
    REGISTER_STRING_SETTING("animationEasingCurve", animationEasingCurve, setAnimationEasingCurve)
    REGISTER_INT_SETTING("animationMinDistance", animationMinDistance, setAnimationMinDistance)
    REGISTER_INT_SETTING("animationSequenceMode", animationSequenceMode, setAnimationSequenceMode)
    REGISTER_INT_SETTING("animationStaggerInterval", animationStaggerInterval, setAnimationStaggerInterval)

    // Autotile core settings (concrete Settings only)
    if (concrete) {
        REGISTER_CONCRETE_BOOL("autotileEnabled", autotileEnabled, setAutotileEnabled)
        REGISTER_CONCRETE_STRING("defaultAutotileAlgorithm", defaultAutotileAlgorithm, setDefaultAutotileAlgorithm)
        REGISTER_CONCRETE_DOUBLE("autotileSplitRatio", autotileSplitRatio, setAutotileSplitRatio)
        REGISTER_CONCRETE_INT("autotileMasterCount", autotileMasterCount, setAutotileMasterCount)
        // Per-algorithm settings map (QVariantMap)
        m_getters[QStringLiteral("autotilePerAlgorithmSettings")] = [concrete]() {
            return QVariant::fromValue(concrete->autotilePerAlgorithmSettings());
        };
        m_setters[QStringLiteral("autotilePerAlgorithmSettings")] = [concrete](const QVariant& v) {
            concrete->setAutotilePerAlgorithmSettings(v.toMap());
            return true;
        };
        m_schemas[QStringLiteral("autotilePerAlgorithmSettings")] = QStringLiteral("map");
        REGISTER_CONCRETE_INT("autotileInnerGap", autotileInnerGap, setAutotileInnerGap)
        REGISTER_CONCRETE_INT("autotileOuterGap", autotileOuterGap, setAutotileOuterGap)
        REGISTER_CONCRETE_BOOL("autotileUsePerSideOuterGap", autotileUsePerSideOuterGap, setAutotileUsePerSideOuterGap)
        REGISTER_CONCRETE_INT("autotileOuterGapTop", autotileOuterGapTop, setAutotileOuterGapTop)
        REGISTER_CONCRETE_INT("autotileOuterGapBottom", autotileOuterGapBottom, setAutotileOuterGapBottom)
        REGISTER_CONCRETE_INT("autotileOuterGapLeft", autotileOuterGapLeft, setAutotileOuterGapLeft)
        REGISTER_CONCRETE_INT("autotileOuterGapRight", autotileOuterGapRight, setAutotileOuterGapRight)
        REGISTER_CONCRETE_BOOL("autotileFocusNewWindows", autotileFocusNewWindows, setAutotileFocusNewWindows)
        REGISTER_CONCRETE_BOOL("autotileSmartGaps", autotileSmartGaps, setAutotileSmartGaps)
        REGISTER_CONCRETE_INT("autotileMaxWindows", autotileMaxWindows, setAutotileMaxWindows)
        REGISTER_CONCRETE_BOOL("autotileRespectMinimumSize", autotileRespectMinimumSize, setAutotileRespectMinimumSize)
        // autotileInsertPosition: enum (0=End, 1=AfterFocused, 2=AsMaster) — needs range validation
        m_getters[QStringLiteral("autotileInsertPosition")] = [concrete]() {
            return static_cast<int>(concrete->autotileInsertPosition());
        };
        m_setters[QStringLiteral("autotileInsertPosition")] = [concrete](const QVariant& v) {
            int val = v.toInt();
            if (val >= 0 && val <= 2) {
                concrete->setAutotileInsertPosition(static_cast<Settings::AutotileInsertPosition>(val));
                return true;
            }
            return false;
        };
        m_schemas[QStringLiteral("autotileInsertPosition")] = QStringLiteral("int");
    }

    // Autotile decoration settings (on ISettings interface)
    REGISTER_BOOL_SETTING("autotileHideTitleBars", autotileHideTitleBars, setAutotileHideTitleBars)
    REGISTER_BOOL_SETTING("autotileShowBorder", autotileShowBorder, setAutotileShowBorder)
    REGISTER_INT_SETTING("autotileBorderWidth", autotileBorderWidth, setAutotileBorderWidth)
    REGISTER_INT_SETTING("autotileBorderRadius", autotileBorderRadius, setAutotileBorderRadius)
    REGISTER_COLOR_SETTING("autotileBorderColor", autotileBorderColor, setAutotileBorderColor)
    REGISTER_COLOR_SETTING("autotileInactiveBorderColor", autotileInactiveBorderColor, setAutotileInactiveBorderColor)
    REGISTER_BOOL_SETTING("autotileUseSystemBorderColors", autotileUseSystemBorderColors,
                          setAutotileUseSystemBorderColors)
    REGISTER_BOOL_SETTING("autotileFocusFollowsMouse", autotileFocusFollowsMouse, setAutotileFocusFollowsMouse)
    m_getters[QStringLiteral("autotileStickyWindowHandling")] = [this]() {
        return static_cast<int>(m_settings->autotileStickyWindowHandling());
    };
    m_setters[QStringLiteral("autotileStickyWindowHandling")] = [this](const QVariant& v) {
        int val = v.toInt();
        if (val >= 0 && val <= 2) {
            m_settings->setAutotileStickyWindowHandling(static_cast<StickyWindowHandling>(val));
            return true;
        }
        return false;
    };
    m_schemas[QStringLiteral("autotileStickyWindowHandling")] = QStringLiteral("int");
    m_getters[QStringLiteral("autotileDragBehavior")] = [this]() {
        return static_cast<int>(m_settings->autotileDragBehavior());
    };
    m_setters[QStringLiteral("autotileDragBehavior")] = [this](const QVariant& v) {
        int val = v.toInt();
        if (val >= static_cast<int>(AutotileDragBehavior::Float)
            && val <= static_cast<int>(AutotileDragBehavior::Reorder)) {
            m_settings->setAutotileDragBehavior(static_cast<AutotileDragBehavior>(val));
            return true;
        }
        return false;
    };
    m_schemas[QStringLiteral("autotileDragBehavior")] = QStringLiteral("int");
    m_getters[QStringLiteral("autotileOverflowBehavior")] = [this]() {
        return static_cast<int>(m_settings->autotileOverflowBehavior());
    };
    m_setters[QStringLiteral("autotileOverflowBehavior")] = [this](const QVariant& v) {
        int val = v.toInt();
        if (val >= static_cast<int>(AutotileOverflowBehavior::Float)
            && val <= static_cast<int>(AutotileOverflowBehavior::Unlimited)) {
            m_settings->setAutotileOverflowBehavior(static_cast<AutotileOverflowBehavior>(val));
            return true;
        }
        return false;
    };
    m_schemas[QStringLiteral("autotileOverflowBehavior")] = QStringLiteral("int");
    REGISTER_STRINGLIST_SETTING("lockedScreens", lockedScreens, setLockedScreens)

    // Autotile shortcuts (concrete Settings only)
    if (concrete) {
        REGISTER_CONCRETE_STRING("autotileToggleShortcut", autotileToggleShortcut, setAutotileToggleShortcut)
        REGISTER_CONCRETE_STRING("autotileFocusMasterShortcut", autotileFocusMasterShortcut,
                                 setAutotileFocusMasterShortcut)
        REGISTER_CONCRETE_STRING("autotileSwapMasterShortcut", autotileSwapMasterShortcut,
                                 setAutotileSwapMasterShortcut)
        REGISTER_CONCRETE_STRING("autotileIncMasterRatioShortcut", autotileIncMasterRatioShortcut,
                                 setAutotileIncMasterRatioShortcut)
        REGISTER_CONCRETE_STRING("autotileDecMasterRatioShortcut", autotileDecMasterRatioShortcut,
                                 setAutotileDecMasterRatioShortcut)
        REGISTER_CONCRETE_STRING("autotileIncMasterCountShortcut", autotileIncMasterCountShortcut,
                                 setAutotileIncMasterCountShortcut)
        REGISTER_CONCRETE_STRING("autotileDecMasterCountShortcut", autotileDecMasterCountShortcut,
                                 setAutotileDecMasterCountShortcut)
        REGISTER_CONCRETE_STRING("autotileRetileShortcut", autotileRetileShortcut, setAutotileRetileShortcut)
    }

    // Global shortcuts (concrete Settings only)
    if (concrete) {
        REGISTER_CONCRETE_STRING("openEditorShortcut", openEditorShortcut, setOpenEditorShortcut)
        REGISTER_CONCRETE_STRING("previousLayoutShortcut", previousLayoutShortcut, setPreviousLayoutShortcut)
        REGISTER_CONCRETE_STRING("nextLayoutShortcut", nextLayoutShortcut, setNextLayoutShortcut)
        REGISTER_CONCRETE_STRING("quickLayout1Shortcut", quickLayout1Shortcut, setQuickLayout1Shortcut)
        REGISTER_CONCRETE_STRING("quickLayout2Shortcut", quickLayout2Shortcut, setQuickLayout2Shortcut)
        REGISTER_CONCRETE_STRING("quickLayout3Shortcut", quickLayout3Shortcut, setQuickLayout3Shortcut)
        REGISTER_CONCRETE_STRING("quickLayout4Shortcut", quickLayout4Shortcut, setQuickLayout4Shortcut)
        REGISTER_CONCRETE_STRING("quickLayout5Shortcut", quickLayout5Shortcut, setQuickLayout5Shortcut)
        REGISTER_CONCRETE_STRING("quickLayout6Shortcut", quickLayout6Shortcut, setQuickLayout6Shortcut)
        REGISTER_CONCRETE_STRING("quickLayout7Shortcut", quickLayout7Shortcut, setQuickLayout7Shortcut)
        REGISTER_CONCRETE_STRING("quickLayout8Shortcut", quickLayout8Shortcut, setQuickLayout8Shortcut)
        REGISTER_CONCRETE_STRING("quickLayout9Shortcut", quickLayout9Shortcut, setQuickLayout9Shortcut)

        // Navigation shortcuts
        REGISTER_CONCRETE_STRING("moveWindowLeftShortcut", moveWindowLeftShortcut, setMoveWindowLeftShortcut)
        REGISTER_CONCRETE_STRING("moveWindowRightShortcut", moveWindowRightShortcut, setMoveWindowRightShortcut)
        REGISTER_CONCRETE_STRING("moveWindowUpShortcut", moveWindowUpShortcut, setMoveWindowUpShortcut)
        REGISTER_CONCRETE_STRING("moveWindowDownShortcut", moveWindowDownShortcut, setMoveWindowDownShortcut)
        REGISTER_CONCRETE_STRING("focusZoneLeftShortcut", focusZoneLeftShortcut, setFocusZoneLeftShortcut)
        REGISTER_CONCRETE_STRING("focusZoneRightShortcut", focusZoneRightShortcut, setFocusZoneRightShortcut)
        REGISTER_CONCRETE_STRING("focusZoneUpShortcut", focusZoneUpShortcut, setFocusZoneUpShortcut)
        REGISTER_CONCRETE_STRING("focusZoneDownShortcut", focusZoneDownShortcut, setFocusZoneDownShortcut)
        REGISTER_CONCRETE_STRING("pushToEmptyZoneShortcut", pushToEmptyZoneShortcut, setPushToEmptyZoneShortcut)
        REGISTER_CONCRETE_STRING("restoreWindowSizeShortcut", restoreWindowSizeShortcut, setRestoreWindowSizeShortcut)
        REGISTER_CONCRETE_STRING("toggleWindowFloatShortcut", toggleWindowFloatShortcut, setToggleWindowFloatShortcut)

        // Swap window shortcuts
        REGISTER_CONCRETE_STRING("swapWindowLeftShortcut", swapWindowLeftShortcut, setSwapWindowLeftShortcut)
        REGISTER_CONCRETE_STRING("swapWindowRightShortcut", swapWindowRightShortcut, setSwapWindowRightShortcut)
        REGISTER_CONCRETE_STRING("swapWindowUpShortcut", swapWindowUpShortcut, setSwapWindowUpShortcut)
        REGISTER_CONCRETE_STRING("swapWindowDownShortcut", swapWindowDownShortcut, setSwapWindowDownShortcut)

        // Snap to zone by number shortcuts
        REGISTER_CONCRETE_STRING("snapToZone1Shortcut", snapToZone1Shortcut, setSnapToZone1Shortcut)
        REGISTER_CONCRETE_STRING("snapToZone2Shortcut", snapToZone2Shortcut, setSnapToZone2Shortcut)
        REGISTER_CONCRETE_STRING("snapToZone3Shortcut", snapToZone3Shortcut, setSnapToZone3Shortcut)
        REGISTER_CONCRETE_STRING("snapToZone4Shortcut", snapToZone4Shortcut, setSnapToZone4Shortcut)
        REGISTER_CONCRETE_STRING("snapToZone5Shortcut", snapToZone5Shortcut, setSnapToZone5Shortcut)
        REGISTER_CONCRETE_STRING("snapToZone6Shortcut", snapToZone6Shortcut, setSnapToZone6Shortcut)
        REGISTER_CONCRETE_STRING("snapToZone7Shortcut", snapToZone7Shortcut, setSnapToZone7Shortcut)
        REGISTER_CONCRETE_STRING("snapToZone8Shortcut", snapToZone8Shortcut, setSnapToZone8Shortcut)
        REGISTER_CONCRETE_STRING("snapToZone9Shortcut", snapToZone9Shortcut, setSnapToZone9Shortcut)

        // Other action shortcuts
        REGISTER_CONCRETE_STRING("rotateWindowsClockwiseShortcut", rotateWindowsClockwiseShortcut,
                                 setRotateWindowsClockwiseShortcut)
        REGISTER_CONCRETE_STRING("rotateWindowsCounterclockwiseShortcut", rotateWindowsCounterclockwiseShortcut,
                                 setRotateWindowsCounterclockwiseShortcut)
        REGISTER_CONCRETE_STRING("cycleWindowForwardShortcut", cycleWindowForwardShortcut,
                                 setCycleWindowForwardShortcut)
        REGISTER_CONCRETE_STRING("cycleWindowBackwardShortcut", cycleWindowBackwardShortcut,
                                 setCycleWindowBackwardShortcut)
        REGISTER_CONCRETE_STRING("resnapToNewLayoutShortcut", resnapToNewLayoutShortcut, setResnapToNewLayoutShortcut)
        REGISTER_CONCRETE_STRING("snapAllWindowsShortcut", snapAllWindowsShortcut, setSnapAllWindowsShortcut)
        REGISTER_CONCRETE_STRING("layoutPickerShortcut", layoutPickerShortcut, setLayoutPickerShortcut)
        REGISTER_CONCRETE_STRING("toggleLayoutLockShortcut", toggleLayoutLockShortcut, setToggleLayoutLockShortcut)
    }

// Clean up macros (local scope)
#undef REGISTER_STRING_SETTING
#undef REGISTER_BOOL_SETTING
#undef REGISTER_INT_SETTING
#undef REGISTER_DOUBLE_SETTING
#undef REGISTER_COLOR_SETTING
#undef REGISTER_STRINGLIST_SETTING
#undef REGISTER_CONCRETE_BOOL
#undef REGISTER_CONCRETE_INT
#undef REGISTER_CONCRETE_DOUBLE
#undef REGISTER_CONCRETE_STRING
}

void SettingsAdaptor::reloadSettings()
{
    m_settings->load();
}

void SettingsAdaptor::saveSettings()
{
    m_settings->save();
}

void SettingsAdaptor::resetToDefaults()
{
    m_settings->reset();
}

QString SettingsAdaptor::getAllSettings()
{
    QJsonObject settings;
    for (auto it = m_getters.constBegin(); it != m_getters.constEnd(); ++it) {
        settings[it.key()] = QJsonValue::fromVariant(it.value()());
    }
    return QString::fromUtf8(QJsonDocument(settings).toJson());
}

QDBusVariant SettingsAdaptor::getSetting(const QString& key)
{
    if (key.isEmpty()) {
        qCWarning(lcDbusSettings) << "getSetting: empty key";
        // Return a valid but empty QDBusVariant to avoid marshalling errors
        // (QDBusVariant() with no argument creates an invalid variant that can't be sent)
        return QDBusVariant(QVariant(QString()));
    }

    auto it = m_getters.find(key);
    if (it != m_getters.end()) {
        QVariant value = it.value()();
        // Ensure we never return an invalid variant - use empty string as fallback
        if (!value.isValid()) {
            qCWarning(lcDbusSettings) << "Setting" << key << "returned invalid variant, using empty string";
            return QDBusVariant(QVariant(QString()));
        }
        return QDBusVariant(value);
    }

    qCWarning(lcDbusSettings) << "Setting key not found:" << key;
    // Return a valid but empty QDBusVariant with error indicator
    // Callers should check for empty string as "not found" indicator
    return QDBusVariant(QVariant(QString()));
}

bool SettingsAdaptor::setSetting(const QString& key, const QDBusVariant& value)
{
    if (key.isEmpty()) {
        qCWarning(lcDbusSettings) << "setSetting: empty key";
        return false;
    }

    auto it = m_setters.find(key);
    if (it == m_setters.end()) {
        qCWarning(lcDbusSettings) << "Setting key not found:" << key;
        return false;
    }

    const QVariant converted = DBusVariantUtils::convertDbusArgument(value.variant());

    // Value-equality guard: if the incoming value already matches the
    // currently stored value, skip the setter invocation and the debounced
    // save-timer restart. Gated on scalar variant types where operator==
    // is reliable — composite types (QVariantList of QVariantMaps used by
    // dragActivationTriggers, for example) compare on internal layout
    // rather than semantic equality and would produce false negatives.
    // The schema map's type string is NOT used as the gate because at
    // least one key (dragActivationTriggers) advertises "stringlist" while
    // actually storing a list-of-maps — the actual QVariant type is the
    // authoritative source.
    const int typeId = converted.metaType().id();
    const bool scalarType =
        (typeId == QMetaType::Bool || typeId == QMetaType::Int || typeId == QMetaType::UInt
         || typeId == QMetaType::LongLong || typeId == QMetaType::ULongLong || typeId == QMetaType::Double
         || typeId == QMetaType::QString || typeId == QMetaType::QStringList);
    if (scalarType) {
        auto getterIt = m_getters.constFind(key);
        if (getterIt != m_getters.constEnd()) {
            const QVariant current = getterIt.value()();
            if (current.isValid() && current == converted) {
                // Already current — nothing to do. Return true because the
                // post-condition ("the setting now equals the supplied value")
                // holds, which is what D-Bus callers rely on.
                return true;
            }
        }
    }

    const bool result = it.value()(converted);
    if (result) {
        // Use debounced save instead of immediate save (performance optimization)
        // This batches multiple rapid setting changes into a single disk write
        scheduleSave();
        qCInfo(lcDbusSettings) << "Setting" << key << "updated, save scheduled";
    } else {
        qCWarning(lcDbusSettings) << "Failed to set setting:" << key;
    }
    return result;
}

QVariantMap SettingsAdaptor::getSettings(const QStringList& keys)
{
    QVariantMap result;
    if (keys.isEmpty()) {
        return result;
    }

    for (const QString& key : keys) {
        if (key.isEmpty()) {
            continue;
        }
        auto it = m_getters.find(key);
        if (it == m_getters.end()) {
            // Unknown keys are expected when callers probe for optional
            // keys; the batch contract is "omit missing, caller uses its
            // own default". Log at debug so production logs stay quiet.
            qCDebug(lcDbusSettings) << "getSettings: unknown key" << key;
            continue;
        }
        QVariant value = it.value()();
        if (!value.isValid()) {
            qCWarning(lcDbusSettings) << "getSettings: setting" << key << "returned invalid variant, omitting";
            continue;
        }
        result.insert(key, value);
    }
    return result;
}

bool SettingsAdaptor::setSettings(const QVariantMap& settings)
{
    if (settings.isEmpty()) {
        qCWarning(lcDbusSettings) << "setSettings: empty map";
        return false;
    }

    // Stop any pending debounced save — we will save synchronously below
    m_saveTimer->stop();

    // Block settingsChanged during the batch — each setter emits it individually,
    // which would trigger N daemon handler invocations (autotile transitions, KWin
    // effect reloads) mid-batch with partially-applied state. Block all signals
    // during iteration, save once, then the KCM's notifyReload() triggers load()
    // which emits settingsChanged once with all values committed.
    bool allOk = true;
    {
        QSignalBlocker blocker(m_settings);
        for (auto it = settings.constBegin(); it != settings.constEnd(); ++it) {
            const QString& key = it.key();
            auto setter = m_setters.find(key);
            if (setter == m_setters.end()) {
                qCWarning(lcDbusSettings) << "setSettings: unknown key" << key;
                allOk = false;
                continue;
            }
            // Convert QDBusArgument types to plain Qt types before passing to setters.
            // Complex types (QVariantList of QVariantMaps, e.g. dragActivationTriggers)
            // arrive from D-Bus as QDBusArgument objects; without conversion, toList()/toMap()
            // return empty containers, silently zeroing trigger settings.
            QVariant converted = DBusVariantUtils::convertDbusArgument(it.value());
            if (!setter.value()(converted)) {
                qCWarning(lcDbusSettings) << "setSettings: setter failed for key" << key;
                allOk = false;
            }
        }
    }

    // Save once with all values applied
    m_settings->save();
    qCInfo(lcDbusSettings) << "setSettings: batch applied" << settings.size() << "keys, allOk:" << allOk;

    return allOk;
}

QStringList SettingsAdaptor::getSettingKeys()
{
    return m_getters.keys();
}

QString SettingsAdaptor::getSettingSchema(const QString& key)
{
    QJsonObject result;

    if (key.isEmpty()) {
        qCWarning(lcDbusSettings) << "getSettingSchema: empty key";
        return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
    }

    auto it = m_schemas.find(key);
    if (it != m_schemas.end()) {
        result[QLatin1String("key")] = key;
        result[QLatin1String("type")] = it.value();
    } else {
        qCWarning(lcDbusSettings) << "getSettingSchema: unknown key" << key;
    }

    return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
}

QString SettingsAdaptor::getAllSettingSchemas()
{
    QJsonObject result;

    for (auto it = m_schemas.constBegin(); it != m_schemas.constEnd(); ++it) {
        QJsonObject entry;
        entry[QLatin1String("type")] = it.value();
        result[it.key()] = entry;
    }

    return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
}

// ═══════════════════════════════════════════════════════════════════════════════
// Per-Screen Settings D-Bus Methods
// ═══════════════════════════════════════════════════════════════════════════════

void SettingsAdaptor::setPerScreenSetting(const QString& screenId, const QString& category, const QString& key,
                                          const QDBusVariant& value)
{
    auto* concrete = qobject_cast<Settings*>(m_settings);
    if (!concrete) {
        qCWarning(lcDbusSettings) << "setPerScreenSetting: concrete Settings not available";
        return;
    }
    if (category == QLatin1String("autotile")) {
        concrete->setPerScreenAutotileSetting(screenId, key, value.variant());
    } else if (category == QLatin1String("snapping")) {
        concrete->setPerScreenSnappingSetting(screenId, key, value.variant());
    } else if (category == QLatin1String("zoneSelector")) {
        concrete->setPerScreenZoneSelectorSetting(screenId, key, value.variant());
    } else {
        qCWarning(lcDbusSettings) << "setPerScreenSetting: unknown category" << category;
        return;
    }
    scheduleSave();
}

void SettingsAdaptor::clearPerScreenSettings(const QString& screenId, const QString& category)
{
    auto* concrete = qobject_cast<Settings*>(m_settings);
    if (!concrete) {
        qCWarning(lcDbusSettings) << "clearPerScreenSettings: concrete Settings not available";
        return;
    }
    if (category == QLatin1String("autotile")) {
        concrete->clearPerScreenAutotileSettings(screenId);
    } else if (category == QLatin1String("snapping")) {
        concrete->clearPerScreenSnappingSettings(screenId);
    } else if (category == QLatin1String("zoneSelector")) {
        concrete->clearPerScreenZoneSelectorSettings(screenId);
    } else {
        qCWarning(lcDbusSettings) << "clearPerScreenSettings: unknown category" << category;
        return;
    }
    scheduleSave();
}

QVariantMap SettingsAdaptor::getPerScreenSettings(const QString& screenId, const QString& category)
{
    auto* concrete = qobject_cast<Settings*>(m_settings);
    if (!concrete) {
        qCWarning(lcDbusSettings) << "getPerScreenSettings: concrete Settings not available";
        return {};
    }
    if (category == QLatin1String("autotile")) {
        return concrete->getPerScreenAutotileSettings(screenId);
    } else if (category == QLatin1String("snapping")) {
        return concrete->getPerScreenSnappingSettings(screenId);
    } else if (category == QLatin1String("zoneSelector")) {
        return concrete->getPerScreenZoneSelectorSettings(screenId);
    }
    qCWarning(lcDbusSettings) << "getPerScreenSettings: unknown category" << category;
    return {};
}

bool SettingsAdaptor::setPerScreenSettings(const QString& screenId, const QString& category, const QVariantMap& values)
{
    if (values.isEmpty()) {
        // Empty map is a valid no-op — treat like setSettings batch and
        // return true so callers don't need to guard for it.
        return true;
    }
    auto* concrete = qobject_cast<Settings*>(m_settings);
    if (!concrete) {
        qCWarning(lcDbusSettings) << "setPerScreenSettings: concrete Settings not available";
        return false;
    }

    // Single category dispatch outside the loop — each underlying Settings
    // method is O(1) per call, so the hot path is one hash lookup per key.
    using PerScreenSetter = std::function<void(const QString&, const QString&, const QVariant&)>;
    PerScreenSetter setter;
    if (category == QLatin1String("autotile")) {
        setter = [concrete](const QString& id, const QString& k, const QVariant& v) {
            concrete->setPerScreenAutotileSetting(id, k, v);
        };
    } else if (category == QLatin1String("snapping")) {
        setter = [concrete](const QString& id, const QString& k, const QVariant& v) {
            concrete->setPerScreenSnappingSetting(id, k, v);
        };
    } else if (category == QLatin1String("zoneSelector")) {
        setter = [concrete](const QString& id, const QString& k, const QVariant& v) {
            concrete->setPerScreenZoneSelectorSetting(id, k, v);
        };
    } else {
        qCWarning(lcDbusSettings) << "setPerScreenSettings: unknown category" << category;
        return false;
    }

    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        // Values arriving over the wire can be QDBusArgument-wrapped when
        // they contain lists or maps; normalize to plain Qt types first —
        // matches the single-key setPerScreenSetting path exactly.
        const QVariant converted = DBusVariantUtils::convertDbusArgument(it.value());
        setter(screenId, it.key(), converted);
    }

    // One debounced save for the whole batch. The per-screen save path
    // coalesces multiple category updates into a single disk write.
    scheduleSave();
    qCInfo(lcDbusSettings) << "setPerScreenSettings: batch applied" << values.size() << "keys on screen" << screenId
                           << "category" << category;
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Shader Registry D-Bus Methods
// ═══════════════════════════════════════════════════════════════════════════════

QVariantList SettingsAdaptor::availableShaders()
{
    if (m_cachedAvailableShadersValid) {
        return m_cachedAvailableShaders;
    }
    auto* registry = ShaderRegistry::instance();
    if (!registry) {
        return QVariantList();
    }
    m_cachedAvailableShaders = registry->availableShadersVariant();
    m_cachedAvailableShadersValid = true;
    return m_cachedAvailableShaders;
}

QVariantMap SettingsAdaptor::shaderInfo(const QString& shaderId)
{
    auto it = m_cachedShaderInfo.constFind(shaderId);
    if (it != m_cachedShaderInfo.constEnd()) {
        return it.value();
    }
    auto* registry = ShaderRegistry::instance();
    if (!registry) {
        return QVariantMap();
    }
    const QVariantMap info = registry->shaderInfo(shaderId);
    m_cachedShaderInfo.insert(shaderId, info);
    return info;
}

QVariantMap SettingsAdaptor::defaultShaderParams(const QString& shaderId)
{
    auto it = m_cachedShaderDefaults.constFind(shaderId);
    if (it != m_cachedShaderDefaults.constEnd()) {
        return it.value();
    }
    auto* registry = ShaderRegistry::instance();
    if (!registry) {
        return QVariantMap();
    }
    const QVariantMap defaults = registry->defaultParams(shaderId);
    m_cachedShaderDefaults.insert(shaderId, defaults);
    return defaults;
}

void SettingsAdaptor::invalidateShaderCaches()
{
    m_cachedAvailableShaders.clear();
    m_cachedAvailableShadersValid = false;
    m_cachedShaderInfo.clear();
    m_cachedShaderDefaults.clear();
}

QVariantMap SettingsAdaptor::translateShaderParams(const QString& shaderId, const QVariantMap& params)
{
    auto* registry = ShaderRegistry::instance();
    return registry ? registry->translateParamsToUniforms(shaderId, params) : QVariantMap();
}

bool SettingsAdaptor::shadersEnabled()
{
    auto* registry = ShaderRegistry::instance();
    return registry ? registry->shadersEnabled() : false;
}

bool SettingsAdaptor::userShadersEnabled()
{
    auto* registry = ShaderRegistry::instance();
    return registry ? registry->userShadersEnabled() : false;
}

QString SettingsAdaptor::userShaderDirectory()
{
    auto* registry = ShaderRegistry::instance();
    return registry ? registry->userShaderDirectory() : QString();
}

void SettingsAdaptor::openUserShaderDirectory()
{
    auto* registry = ShaderRegistry::instance();
    if (registry) {
        registry->openUserShaderDirectory();
    }
}

void SettingsAdaptor::refreshShaders()
{
    // Drop our memoized view before asking the registry to reload — if
    // the ShaderRegistry::shadersChanged signal isn't connected (e.g. the
    // singleton was created after this adaptor), we still guarantee the
    // next D-Bus query hits the fresh registry.
    invalidateShaderCaches();
    auto* registry = ShaderRegistry::instance();
    if (registry) {
        registry->refresh();
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Window Picker D-Bus Methods
// ═══════════════════════════════════════════════════════════════════════════════

QString SettingsAdaptor::getRunningWindows()
{
    qCWarning(lcDbusSettings) << "getRunningWindows: DEPRECATED blocking path — callers should switch to "
                                 "requestRunningWindows() + runningWindowsAvailable signal";

    // Guard against reentrant calls (shouldn't happen via D-Bus serialization,
    // but protects against unexpected provideRunningWindows calls)
    if (m_windowListLoop) {
        return QStringLiteral("[]");
    }

    m_pendingWindowList.clear();

    QEventLoop loop;
    m_windowListLoop = &loop;

    // Blocking call: waits for KWin effect to respond via provideRunningWindows().
    // The 2s timeout prevents indefinite blocking if the effect is unloaded or
    // unresponsive. Retained only for backward compatibility with clients that
    // have not yet migrated to the async flow.
    constexpr int WindowListTimeoutMs = 2000;
    QTimer::singleShot(WindowListTimeoutMs, &loop, &QEventLoop::quit);

    // Signal the KWin effect to enumerate windows
    Q_EMIT runningWindowsRequested();

    // Block until provideRunningWindows() is called or timeout
    loop.exec();

    m_windowListLoop = nullptr;
    return m_pendingWindowList;
}

void SettingsAdaptor::requestRunningWindows()
{
    // Fire-and-forget. Callers receive the reply asynchronously via the
    // runningWindowsAvailable signal (emitted from provideRunningWindows).
    // Safe to call while a previous request is still in flight — the
    // effect side is idempotent, and the last-arriving payload is the
    // one subscribers see.
    Q_EMIT runningWindowsRequested();
}

void SettingsAdaptor::provideRunningWindows(const QString& json)
{
    m_pendingWindowList = json;

    // Legacy path: unblock any blocking getRunningWindows() caller.
    if (m_windowListLoop && m_windowListLoop->isRunning()) {
        m_windowListLoop->quit();
    }

    // Async path: fan out to subscribers of the new signal. Always emit
    // even when a legacy blocking caller is present — the subscribers are
    // disjoint and the broadcast is harmless for the blocking waiter.
    Q_EMIT runningWindowsAvailable(json);
}

} // namespace PlasmaZones
