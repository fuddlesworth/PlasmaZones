// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "settingsadaptor.h"
#include "../core/interfaces.h"
#include "../config/settings.h" // For concrete Settings type
#include "../core/dbusvariantutils.h"
#include <PhosphorAnimationShaders/ShaderProfileTree.h>
#include "../core/logging.h"
#include "../core/shaderregistry.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QColor>
#include <QDBusVariant>
#include <functional>
#include <optional>

namespace PlasmaZones {

SettingsAdaptor::SettingsAdaptor(ISettings* settings, ShaderRegistry* shaderRegistry, QObject* parent)
    : QDBusAbstractAdaptor(parent)
    , m_settings(settings)
    , m_shaderRegistry(shaderRegistry)
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
    // registry is per-process and injected via constructor; unit tests
    // that don't pass one get nullptr and skip this connection.
    // ShaderRegistry::refresh() is always invoked from the main thread
    // (D-Bus refreshShaders slot) so a direct connection is safe; if
    // that invariant ever changes, switch to Qt::QueuedConnection here.
    if (m_shaderRegistry) {
        connect(m_shaderRegistry, &ShaderRegistry::shadersChanged, this, &SettingsAdaptor::invalidateShaderCaches);
    }
}

SettingsAdaptor::~SettingsAdaptor()
{
    // Flush any pending debounced saves before destruction so config
    // changes aren't lost on shutdown. Skipped when m_settings has already
    // been cleared via detach() — the owning Daemon performs the save
    // itself in stop(), and our borrowed pointer would dangle by the time
    // ~QObject destroys us (the owning unique_ptr has already run).
    if (m_settings && m_saveTimer->isActive()) {
        m_saveTimer->stop();
        m_settings->save();
        qCInfo(lcDbusSettings) << "Flushed pending save on destruction";
    }
}

void SettingsAdaptor::detach()
{
    // Flush once here — Daemon::stop() also calls m_settings->save()
    // explicitly, so the pending-write isn't really at risk, but keeping
    // the flush makes detach() safe to call from any shutdown path, not
    // just the daemon's happy-path. m_saveTimer is constructed in the
    // initializer list and never nulled, so no null-check is needed on
    // it here (the dtor uses the same pattern).
    if (m_settings && m_saveTimer->isActive()) {
        m_saveTimer->stop();
        m_settings->save();
    }
    if (m_settings) {
        disconnect(m_settings, nullptr, this, nullptr);
    }
    if (m_shaderRegistry) {
        disconnect(m_shaderRegistry, nullptr, this, nullptr);
    }
    // Clear the registries before nulling m_settings so a queued D-Bus
    // call that slipped past unregisterObject() lands on an empty-getter
    // hash (returning an empty QVariant) instead of the registered
    // lambdas, which close over `this` and would deref a null m_settings.
    m_getters.clear();
    m_setters.clear();
    m_cachedAvailableShaders.clear();
    m_cachedAvailableShadersValid = false;
    m_cachedShaderInfo.clear();
    m_cachedShaderDefaults.clear();
    m_settings = nullptr;
    m_shaderRegistry = nullptr;
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

    // Concrete Settings pointer for properties not on ISettings interface.
    // The per-screen override API is fully on ISettings — this cast is
    // used only by the REGISTER_CONCRETE_* macros below for dozens of
    // global properties that haven't been hoisted to the segregated
    // interfaces yet. Every REGISTER_CONCRETE_* call site is wrapped in
    // an `if (concrete)` block, so test backends that don't supply a
    // concrete Settings simply don't register those keys — setSetting
    // returns "key not found" instead of dereferencing a null pointer.
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

    // PhosphorZones::Zone span modifier (legacy single value)
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

    // PhosphorZones::Zone span triggers list (multi-bind)
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
    REGISTER_BOOL_SETTING("showOsdOnDesktopSwitch", showOsdOnDesktopSwitch, setShowOsdOnDesktopSwitch)
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
    // Per-mode disable lists. Six entries — one per (context, mode) pair —
    // because both the read and the write need a Mode argument that the
    // REGISTER_STRINGLIST_SETTING macro doesn't expose. Pre-v3 these were a
    // single set of three keys whose values silently gated both modes; the
    // new wire schema names the mode explicitly so consumers can't conflate.
#define REGISTER_PER_MODE_DISABLE(keyName, modeEnum, getterFn, setterFn)                                               \
    m_getters[QStringLiteral(keyName)] = [this]() {                                                                    \
        return m_settings->getterFn(modeEnum);                                                                         \
    };                                                                                                                 \
    m_setters[QStringLiteral(keyName)] = [this](const QVariant& v) {                                                   \
        m_settings->setterFn(modeEnum, v.toStringList());                                                              \
        return true;                                                                                                   \
    };                                                                                                                 \
    m_schemas[QStringLiteral(keyName)] = QStringLiteral("stringlist");

    REGISTER_PER_MODE_DISABLE("snappingDisabledMonitors", PhosphorZones::AssignmentEntry::Snapping, disabledMonitors,
                              setDisabledMonitors)
    REGISTER_PER_MODE_DISABLE("autotileDisabledMonitors", PhosphorZones::AssignmentEntry::Autotile, disabledMonitors,
                              setDisabledMonitors)
    REGISTER_PER_MODE_DISABLE("snappingDisabledDesktops", PhosphorZones::AssignmentEntry::Snapping, disabledDesktops,
                              setDisabledDesktops)
    REGISTER_PER_MODE_DISABLE("autotileDisabledDesktops", PhosphorZones::AssignmentEntry::Autotile, disabledDesktops,
                              setDisabledDesktops)
    REGISTER_PER_MODE_DISABLE("snappingDisabledActivities", PhosphorZones::AssignmentEntry::Snapping,
                              disabledActivities, setDisabledActivities)
    REGISTER_PER_MODE_DISABLE("autotileDisabledActivities", PhosphorZones::AssignmentEntry::Autotile,
                              disabledActivities, setDisabledActivities)
#undef REGISTER_PER_MODE_DISABLE

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

    // PhosphorZones::Zone settings
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
    REGISTER_BOOL_SETTING("autoAssignAllLayouts", autoAssignAllLayouts, setAutoAssignAllLayouts)
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

    // PhosphorZones::Zone selector settings
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

    // Phase 6: shader profile tree (JSON blob round-trip via D-Bus)
    if (concrete) {
        m_getters[QStringLiteral("shaderProfileTree")] = [concrete]() {
            return QString::fromUtf8(
                QJsonDocument(concrete->shaderProfileTree().toJson()).toJson(QJsonDocument::Compact));
        };
        m_setters[QStringLiteral("shaderProfileTree")] = [concrete](const QVariant& v) -> bool {
            const QJsonDocument doc = QJsonDocument::fromJson(v.toString().toUtf8());
            if (!doc.isObject())
                return false;
            concrete->setShaderProfileTree(PhosphorAnimationShaders::ShaderProfileTree::fromJson(doc.object()));
            return true;
        };
    }

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
    if (!m_settings) {
        return;
    }
    m_settings->load();
}

void SettingsAdaptor::saveSettings()
{
    if (!m_settings) {
        return;
    }
    m_settings->save();
}

void SettingsAdaptor::resetToDefaults()
{
    if (!m_settings) {
        return;
    }
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
    // save-timer restart. Covers scalar variant types plus the two Qt
    // composite types that the settings surface actually uses
    // (QVariantList and QVariantMap) — both compare structurally via
    // element-wise recursion in Qt's operator==, so the guard is reliable
    // for keys like dragActivationTriggers (list-of-maps) that would
    // otherwise always take the full setter path on every idle UI tick.
    //
    // The schema map's type string is NOT used as the gate because at
    // least one key (dragActivationTriggers) advertises "stringlist" while
    // actually storing a list-of-maps — the actual QVariant type is the
    // authoritative source. Types outside this allow-list (custom QObject
    // pointers, exotic Q_DECLARE_METATYPE payloads) fall through to the
    // full setter to avoid false negatives from a non-structural operator==.
    const int typeId = converted.metaType().id();
    const bool comparableType =
        (typeId == QMetaType::Bool || typeId == QMetaType::Int || typeId == QMetaType::UInt
         || typeId == QMetaType::LongLong || typeId == QMetaType::ULongLong || typeId == QMetaType::Double
         || typeId == QMetaType::Float || typeId == QMetaType::QString || typeId == QMetaType::QStringList
         || typeId == QMetaType::QUrl || typeId == QMetaType::QByteArray || typeId == QMetaType::QChar
         || typeId == QMetaType::QVariantList || typeId == QMetaType::QVariantMap);
    if (comparableType) {
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
    } else {
        // Exotic-type fall-through. Debug-log once so a future caller that
        // tries to optimize same-value writes on a new custom type can
        // trace back to why their key isn't being short-circuited.
        qCDebug(lcDbusSettings) << "setSetting: value-equality guard skipped for non-comparable type"
                                << converted.metaType().name() << "key=" << key;
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
    if (!m_settings) {
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

// Category → {get, set, clear} dispatch table. Closing over @p settings
// lets every per-screen D-Bus entry point share one dispatch definition
// instead of re-deriving the if/else ladder per call. Every method is
// on ISettings with a default no-op body, so backends that don't
// support per-screen state (test stubs) simply inherit the no-op —
// no qobject_cast required.
namespace {
struct PerScreenDispatch
{
    std::function<QVariantMap(const QString&)> get;
    std::function<void(const QString&, const QString&, const QVariant&)> set;
    std::function<void(const QString&)> clear;
};

std::optional<PerScreenDispatch> dispatchFor(ISettings* settings, const QString& category)
{
    if (category == QLatin1String("autotile")) {
        return PerScreenDispatch{
            [settings](const QString& id) {
                return settings->getPerScreenAutotileSettings(id);
            },
            [settings](const QString& id, const QString& k, const QVariant& v) {
                settings->setPerScreenAutotileSetting(id, k, v);
            },
            [settings](const QString& id) {
                settings->clearPerScreenAutotileSettings(id);
            },
        };
    }
    if (category == QLatin1String("snapping")) {
        return PerScreenDispatch{
            [settings](const QString& id) {
                return settings->getPerScreenSnappingSettings(id);
            },
            [settings](const QString& id, const QString& k, const QVariant& v) {
                settings->setPerScreenSnappingSetting(id, k, v);
            },
            [settings](const QString& id) {
                settings->clearPerScreenSnappingSettings(id);
            },
        };
    }
    if (category == QLatin1String("zoneSelector")) {
        return PerScreenDispatch{
            [settings](const QString& id) {
                return settings->getPerScreenZoneSelectorSettings(id);
            },
            [settings](const QString& id, const QString& k, const QVariant& v) {
                settings->setPerScreenZoneSelectorSetting(id, k, v);
            },
            [settings](const QString& id) {
                settings->clearPerScreenZoneSelectorSettings(id);
            },
        };
    }
    return std::nullopt;
}
} // namespace

void SettingsAdaptor::setPerScreenSetting(const QString& screenId, const QString& category, const QString& key,
                                          const QDBusVariant& value)
{
    if (!m_settings) {
        return;
    }
    auto dispatch = dispatchFor(m_settings, category);
    if (!dispatch) {
        qCWarning(lcDbusSettings) << "setPerScreenSetting: unknown category" << category;
        return;
    }
    dispatch->set(screenId, key, value.variant());
    scheduleSave();
}

void SettingsAdaptor::clearPerScreenSettings(const QString& screenId, const QString& category)
{
    if (!m_settings) {
        return;
    }
    auto dispatch = dispatchFor(m_settings, category);
    if (!dispatch) {
        qCWarning(lcDbusSettings) << "clearPerScreenSettings: unknown category" << category;
        return;
    }
    dispatch->clear(screenId);
    scheduleSave();
}

QVariantMap SettingsAdaptor::getPerScreenSettings(const QString& screenId, const QString& category)
{
    if (!m_settings) {
        return {};
    }
    auto dispatch = dispatchFor(m_settings, category);
    if (!dispatch) {
        qCWarning(lcDbusSettings) << "getPerScreenSettings: unknown category" << category;
        return {};
    }
    return dispatch->get(screenId);
}

bool SettingsAdaptor::setPerScreenSettings(const QString& screenId, const QString& category, const QVariantMap& values)
{
    if (values.isEmpty()) {
        // Empty map is a valid no-op — treat like setSettings batch and
        // return true so callers don't need to guard for it.
        return true;
    }
    if (!m_settings) {
        return false;
    }
    auto dispatch = dispatchFor(m_settings, category);
    if (!dispatch) {
        qCWarning(lcDbusSettings) << "setPerScreenSettings: unknown category" << category;
        return false;
    }

    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        // Values arriving over the wire can be QDBusArgument-wrapped when
        // they contain lists or maps; normalize to plain Qt types first —
        // matches the single-key setPerScreenSetting path exactly.
        const QVariant converted = DBusVariantUtils::convertDbusArgument(it.value());
        dispatch->set(screenId, it.key(), converted);
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
    auto* registry = m_shaderRegistry;
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
    auto* registry = m_shaderRegistry;
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
    auto* registry = m_shaderRegistry;
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
    auto* registry = m_shaderRegistry;
    return registry ? registry->translateParamsToUniforms(shaderId, params) : QVariantMap();
}

bool SettingsAdaptor::shadersEnabled()
{
    auto* registry = m_shaderRegistry;
    return registry ? registry->shadersEnabled() : false;
}

bool SettingsAdaptor::userShadersEnabled()
{
    auto* registry = m_shaderRegistry;
    return registry ? registry->userShadersEnabled() : false;
}

QString SettingsAdaptor::userShaderDirectory()
{
    auto* registry = m_shaderRegistry;
    return registry ? registry->userShaderDirectory() : QString();
}

void SettingsAdaptor::openUserShaderDirectory()
{
    auto* registry = m_shaderRegistry;
    if (registry) {
        registry->openUserShaderDirectory();
    }
}

void SettingsAdaptor::refreshShaders()
{
    // Drop our memoized view before asking the registry to reload — if
    // the shadersChanged signal isn't connected (e.g. the registry was
    // never injected in this composition root), we still guarantee the
    // next D-Bus query hits the fresh registry.
    invalidateShaderCaches();
    auto* registry = m_shaderRegistry;
    if (registry) {
        registry->refresh();
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Window Picker D-Bus Methods
// ═══════════════════════════════════════════════════════════════════════════════

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
    // Fan out to every client that subscribed to runningWindowsAvailable —
    // SettingsController caches the last payload on the client side, so
    // there is no server-side state to keep here.
    Q_EMIT runningWindowsAvailable(json);
}

} // namespace PlasmaZones
