// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "settings.h"
#include "colorimporter.h"
#include "configdefaults.h"
#include "../core/constants.h"
#include "../core/logging.h"
#include "../core/utils.h"
#include <KConfig>
#include <KConfigGroup>
#include <KSharedConfig>
#include <KColorScheme>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <climits> // For INT_MAX in readValidatedInt

namespace PlasmaZones {

// ═══════════════════════════════════════════════════════════════════════════════
// Macros for setter patterns
// Reduces boilerplate for ~50+ setter methods
// ═══════════════════════════════════════════════════════════════════════════════

// Simple setter: if changed, update member, emit specific signal, emit settingsChanged
#define SETTINGS_SETTER(Type, name, member, signal) \
    void Settings::set##name(Type value) \
    { \
        if (member != value) { \
            member = value; \
            Q_EMIT signal(); \
            Q_EMIT settingsChanged(); \
        } \
    }

// Clamped int setter: clamp value, then apply if changed
#define SETTINGS_SETTER_CLAMPED(name, member, signal, minVal, maxVal) \
    void Settings::set##name(int value) \
    { \
        value = qBound(minVal, value, maxVal); \
        if (member != value) { \
            member = value; \
            Q_EMIT signal(); \
            Q_EMIT settingsChanged(); \
        } \
    }

namespace {
/**
 * @brief Normalize a UUID string to use the default format (with braces)
 *
 * Handles migration from configs saved with WithoutBraces format.
 * Returns empty string if input is empty or not a valid UUID.
 */
QString normalizeUuidString(const QString& uuidStr)
{
    if (uuidStr.isEmpty()) {
        return QString();
    }

    // Parse as UUID (handles both with and without braces)
    QUuid uuid = QUuid::fromString(uuidStr);
    if (uuid.isNull()) {
        qCWarning(lcConfig) << "Invalid UUID string in config, ignoring:" << uuidStr;
        return QString();
    }

    // Return in default format (with braces) for consistent comparison
    return uuid.toString();
}
// Validate a per-screen zone selector override value.
// Returns a valid QVariant with the clamped value for known keys,
// or an invalid (default-constructed) QVariant for unknown keys or invalid Position=4.
QVariant validatePerScreenValue(const QString& key, const QVariant& value)
{
    namespace K = ZoneSelectorConfigKey;
    if (key == QLatin1String(K::Position)) {
        int pos = qBound(0, value.toInt(), 8);
        if (pos == 4) { return {}; } // Center is invalid
        return pos;
    }
    if (key == QLatin1String(K::LayoutMode))       return qBound(0, value.toInt(), 2);
    if (key == QLatin1String(K::SizeMode))          return qBound(0, value.toInt(), 1);
    if (key == QLatin1String(K::MaxRows))            return qBound(1, value.toInt(), 10);
    if (key == QLatin1String(K::PreviewWidth))       return qBound(80, value.toInt(), 400);
    if (key == QLatin1String(K::PreviewHeight))      return qBound(60, value.toInt(), 300);
    if (key == QLatin1String(K::GridColumns))         return qBound(1, value.toInt(), 10);
    if (key == QLatin1String(K::TriggerDistance))     return qBound(10, value.toInt(), 200);
    if (key == QLatin1String(K::PreviewLockAspect))  return value.toBool();
    return {}; // Unknown key
}

// Apply validated overrides from a QVariantMap onto a ZoneSelectorConfig struct
void applyPerScreenOverrides(ZoneSelectorConfig& config, const QVariantMap& overrides)
{
    namespace K = ZoneSelectorConfigKey;
    auto applyInt = [&](const char* key, int& field) {
        auto it = overrides.constFind(QLatin1String(key));
        if (it != overrides.constEnd()) {
            QVariant v = validatePerScreenValue(QLatin1String(key), it.value());
            if (v.isValid()) { field = v.toInt(); }
        }
    };
    applyInt(K::Position, config.position);
    applyInt(K::LayoutMode, config.layoutMode);
    applyInt(K::SizeMode, config.sizeMode);
    applyInt(K::MaxRows, config.maxRows);
    applyInt(K::PreviewWidth, config.previewWidth);
    applyInt(K::PreviewHeight, config.previewHeight);
    applyInt(K::GridColumns, config.gridColumns);
    applyInt(K::TriggerDistance, config.triggerDistance);
    // PreviewLockAspect (bool)
    auto lockIt = overrides.constFind(QLatin1String(K::PreviewLockAspect));
    if (lockIt != overrides.constEnd()) {
        QVariant v = validatePerScreenValue(QLatin1String(K::PreviewLockAspect), lockIt.value());
        if (v.isValid()) { config.previewLockAspect = v.toBool(); }
    }
}

// Known per-screen config keys for iteration during load
constexpr const char* kPerScreenKeys[] = {
    ZoneSelectorConfigKey::Position,
    ZoneSelectorConfigKey::LayoutMode,
    ZoneSelectorConfigKey::SizeMode,
    ZoneSelectorConfigKey::MaxRows,
    ZoneSelectorConfigKey::PreviewWidth,
    ZoneSelectorConfigKey::PreviewHeight,
    ZoneSelectorConfigKey::PreviewLockAspect,
    ZoneSelectorConfigKey::GridColumns,
    ZoneSelectorConfigKey::TriggerDistance,
};

} // anonymous namespace

Settings::Settings(QObject* parent)
    : ISettings(parent)
{
    load();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Helper Methods
// ═══════════════════════════════════════════════════════════════════════════════

int Settings::readValidatedInt(const KConfigGroup& group, const char* key, int defaultValue,
                               int min, int max, const char* settingName)
{
    int value = group.readEntry(QLatin1String(key), defaultValue);
    if (value < min || value > max) {
        qCWarning(lcConfig) << "Invalid" << settingName << ":" << value
                            << "using default (must be" << min << "-" << max << ")";
        value = defaultValue;
    }
    return value;
}

QColor Settings::readValidatedColor(const KConfigGroup& group, const char* key,
                                    const QColor& defaultValue, const char* settingName)
{
    QColor color = group.readEntry(QLatin1String(key), defaultValue);
    if (!color.isValid()) {
        qCWarning(lcConfig) << "Invalid" << settingName << "color, using default";
        color = defaultValue;
    }
    return color;
}

void Settings::loadIndexedShortcuts(const KConfigGroup& group, const QString& keyPattern,
                                    QString (&shortcuts)[9], const QString (&defaults)[9])
{
    for (int i = 0; i < 9; ++i) {
        QString key = keyPattern.arg(i + 1);
        shortcuts[i] = group.readEntry(key, defaults[i]);
    }
}

std::optional<QVariantList> Settings::parseTriggerListJson(const QString& json)
{
    if (json.isEmpty()) {
        return std::nullopt;
    }
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        qCWarning(lcConfig) << "Invalid trigger list JSON:" << parseError.errorString() << "- using fallback";
        return std::nullopt;
    }
    QVariantList result;
    const QJsonArray arr = doc.array();
    for (const QJsonValue& val : arr) {
        if (val.isObject()) {
            QJsonObject obj = val.toObject();
            QVariantMap trigger;
            trigger[QStringLiteral("modifier")] = obj.value(QLatin1String("modifier")).toInt(0);
            trigger[QStringLiteral("mouseButton")] = obj.value(QLatin1String("mouseButton")).toInt(0);
            result.append(trigger);
        } else {
            qCWarning(lcConfig) << "Non-object element in trigger array (index" << result.size() << ") - skipping";
        }
    }
    if (result.size() > MaxTriggersPerAction) {
        result = result.mid(0, MaxTriggersPerAction);
    }
    return result;  // May be empty (valid [] means no triggers)
}

QVariantList Settings::loadTriggerList(const KConfigGroup& group, const QString& key,
                                        int legacyModifier, int legacyMouseButton)
{
    QString json = group.readEntry(key, QString());
    std::optional<QVariantList> parsed = parseTriggerListJson(json);
    if (parsed.has_value()) {
        qCDebug(lcConfig) << "Loaded" << key << ":" << parsed->size() << "triggers";
        return *parsed;
    }
    // No valid JSON: construct single-element trigger list from legacy values
    QVariantMap trigger;
    trigger[QStringLiteral("modifier")] = legacyModifier;
    trigger[QStringLiteral("mouseButton")] = legacyMouseButton;
    return {trigger};
}

void Settings::saveTriggerList(KConfigGroup& group, const QString& key,
                                const QVariantList& triggers)
{
    QJsonArray arr;
    for (const QVariant& t : triggers) {
        auto map = t.toMap();
        QJsonObject obj;
        obj[QLatin1String("modifier")] = map.value(QStringLiteral("modifier"), 0).toInt();
        obj[QLatin1String("mouseButton")] = map.value(QStringLiteral("mouseButton"), 0).toInt();
        arr.append(obj);
    }
    group.writeEntry(key, QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
}

void Settings::setShiftDragToActivate(bool enable)
{
    if (m_shiftDragToActivate != enable) {
        m_shiftDragToActivate = enable;
        Q_EMIT shiftDragToActivateChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setDragActivationTriggers(const QVariantList& triggers)
{
    QVariantList capped = triggers.mid(0, MaxTriggersPerAction);
    if (m_dragActivationTriggers != capped) {
        m_dragActivationTriggers = capped;
        Q_EMIT dragActivationTriggersChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setZoneSpanModifier(DragModifier modifier)
{
    if (m_zoneSpanModifier != modifier) {
        m_zoneSpanModifier = modifier;
        // Keep triggers in sync: update first trigger's modifier to match
        if (!m_zoneSpanTriggers.isEmpty()) {
            auto first = m_zoneSpanTriggers.first().toMap();
            first[QStringLiteral("modifier")] = static_cast<int>(modifier);
            m_zoneSpanTriggers[0] = first;
        } else {
            QVariantMap trigger;
            trigger[QStringLiteral("modifier")] = static_cast<int>(modifier);
            trigger[QStringLiteral("mouseButton")] = 0;
            m_zoneSpanTriggers = {trigger};
        }
        Q_EMIT zoneSpanModifierChanged();
        Q_EMIT zoneSpanTriggersChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setZoneSpanTriggers(const QVariantList& triggers)
{
    QVariantList capped = triggers.mid(0, MaxTriggersPerAction);
    if (m_zoneSpanTriggers != capped) {
        m_zoneSpanTriggers = capped;
        // Sync legacy zoneSpanModifier from first trigger with a non-zero modifier
        DragModifier synced = DragModifier::Disabled;
        for (const auto& t : capped) {
            int mod = t.toMap().value(QStringLiteral("modifier"), 0).toInt();
            if (mod != 0) {
                synced = static_cast<DragModifier>(
                    qBound(0, mod, static_cast<int>(DragModifier::CtrlAltMeta)));
                break;
            }
        }
        m_zoneSpanModifier = synced;
        Q_EMIT zoneSpanTriggersChanged();
        Q_EMIT zoneSpanModifierChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setZoneSpanModifierInt(int modifier)
{
    if (modifier >= 0 && modifier <= static_cast<int>(DragModifier::CtrlAltMeta)) {
        setZoneSpanModifier(static_cast<DragModifier>(modifier));
    }
}

SETTINGS_SETTER(bool, ZoneSpanEnabled, m_zoneSpanEnabled, zoneSpanEnabledChanged)
SETTINGS_SETTER(bool, ToggleActivation, m_toggleActivation, toggleActivationChanged)

// Simple bool setters
SETTINGS_SETTER(bool, ShowZonesOnAllMonitors, m_showZonesOnAllMonitors, showZonesOnAllMonitorsChanged)
SETTINGS_SETTER(const QStringList&, DisabledMonitors, m_disabledMonitors, disabledMonitorsChanged)

bool Settings::isMonitorDisabled(const QString& screenName) const
{
    if (m_disabledMonitors.contains(screenName)) {
        return true;
    }
    // Backward compat: if screenName looks like a connector name (no colons),
    // resolve to stable EDID-based screen ID and check again
    if (Utils::isConnectorName(screenName)) {
        QString resolved = Utils::screenIdForName(screenName);
        if (resolved != screenName && m_disabledMonitors.contains(resolved)) {
            return true;
        }
    } else {
        // screenName is a screen ID — try reverse lookup to connector name
        // (covers unmigrated entries from screens disconnected during load)
        QString connector = Utils::screenNameForId(screenName);
        if (!connector.isEmpty() && m_disabledMonitors.contains(connector)) {
            return true;
        }
    }
    return false;
}

SETTINGS_SETTER(bool, ShowZoneNumbers, m_showZoneNumbers, showZoneNumbersChanged)
SETTINGS_SETTER(bool, FlashZonesOnSwitch, m_flashZonesOnSwitch, flashZonesOnSwitchChanged)
SETTINGS_SETTER(bool, ShowOsdOnLayoutSwitch, m_showOsdOnLayoutSwitch, showOsdOnLayoutSwitchChanged)
SETTINGS_SETTER(bool, ShowNavigationOsd, m_showNavigationOsd, showNavigationOsdChanged)

void Settings::setOsdStyle(OsdStyle style)
{
    if (m_osdStyle != style) {
        m_osdStyle = style;
        Q_EMIT osdStyleChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setOsdStyleInt(int style)
{
    if (style >= 0 && style <= static_cast<int>(OsdStyle::Preview)) {
        setOsdStyle(static_cast<OsdStyle>(style));
    }
}

void Settings::setUseSystemColors(bool use)
{
    if (m_useSystemColors != use) {
        m_useSystemColors = use;
        if (use) {
            applySystemColorScheme();
        }
        Q_EMIT useSystemColorsChanged();
        Q_EMIT settingsChanged();
    }
}

// Color setters
SETTINGS_SETTER(const QColor&, HighlightColor, m_highlightColor, highlightColorChanged)
SETTINGS_SETTER(const QColor&, InactiveColor, m_inactiveColor, inactiveColorChanged)
SETTINGS_SETTER(const QColor&, BorderColor, m_borderColor, borderColorChanged)
SETTINGS_SETTER(const QColor&, LabelFontColor, m_labelFontColor, labelFontColorChanged)

void Settings::setActiveOpacity(qreal opacity)
{
    opacity = qBound(0.0, opacity, 1.0);
    if (!qFuzzyCompare(m_activeOpacity, opacity)) {
        m_activeOpacity = opacity;
        Q_EMIT activeOpacityChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setInactiveOpacity(qreal opacity)
{
    opacity = qBound(0.0, opacity, 1.0);
    if (!qFuzzyCompare(m_inactiveOpacity, opacity)) {
        m_inactiveOpacity = opacity;
        Q_EMIT inactiveOpacityChanged();
        Q_EMIT settingsChanged();
    }
}

// Border setters (clamped with min 0)
SETTINGS_SETTER_CLAMPED(BorderWidth, m_borderWidth, borderWidthChanged, 0, INT_MAX)
SETTINGS_SETTER_CLAMPED(BorderRadius, m_borderRadius, borderRadiusChanged, 0, INT_MAX)

SETTINGS_SETTER(bool, EnableBlur, m_enableBlur, enableBlurChanged)
SETTINGS_SETTER(const QString&, LabelFontFamily, m_labelFontFamily, labelFontFamilyChanged)
SETTINGS_SETTER_CLAMPED(LabelFontWeight, m_labelFontWeight, labelFontWeightChanged, 100, 900)
SETTINGS_SETTER(bool, LabelFontItalic, m_labelFontItalic, labelFontItalicChanged)
SETTINGS_SETTER(bool, LabelFontUnderline, m_labelFontUnderline, labelFontUnderlineChanged)
SETTINGS_SETTER(bool, LabelFontStrikeout, m_labelFontStrikeout, labelFontStrikeoutChanged)

// Manual setter: SETTINGS_SETTER_CLAMPED only handles int; this is qreal with
// qBound clamping to [0.25, 3.0]. qFuzzyCompare is safe here because both
// operands are always in that range (never near zero where it breaks).
void Settings::setLabelFontSizeScale(qreal scale)
{
    scale = qBound(0.25, scale, 3.0);
    if (!qFuzzyCompare(m_labelFontSizeScale, scale)) {
        m_labelFontSizeScale = scale;
        Q_EMIT labelFontSizeScaleChanged();
        Q_EMIT settingsChanged();
    }
}

SETTINGS_SETTER_CLAMPED(ZonePadding, m_zonePadding, zonePaddingChanged, 0, INT_MAX)
SETTINGS_SETTER_CLAMPED(OuterGap, m_outerGap, outerGapChanged, 0, INT_MAX)
SETTINGS_SETTER_CLAMPED(AdjacentThreshold, m_adjacentThreshold, adjacentThresholdChanged, 0, INT_MAX)
SETTINGS_SETTER_CLAMPED(PollIntervalMs, m_pollIntervalMs, pollIntervalMsChanged, 10, 1000)
SETTINGS_SETTER_CLAMPED(MinimumZoneSizePx, m_minimumZoneSizePx, minimumZoneSizePxChanged, 50, 500)
SETTINGS_SETTER_CLAMPED(MinimumZoneDisplaySizePx, m_minimumZoneDisplaySizePx, minimumZoneDisplaySizePxChanged, 1, 50)

// Behavior bool setters
SETTINGS_SETTER(bool, KeepWindowsInZonesOnResolutionChange, m_keepWindowsInZonesOnResolutionChange, keepWindowsInZonesOnResolutionChangeChanged)
SETTINGS_SETTER(bool, MoveNewWindowsToLastZone, m_moveNewWindowsToLastZone, moveNewWindowsToLastZoneChanged)
SETTINGS_SETTER(bool, RestoreOriginalSizeOnUnsnap, m_restoreOriginalSizeOnUnsnap, restoreOriginalSizeOnUnsnapChanged)
SETTINGS_SETTER(StickyWindowHandling, StickyWindowHandling, m_stickyWindowHandling, stickyWindowHandlingChanged)

void Settings::setStickyWindowHandlingInt(int handling)
{
    if (handling >= static_cast<int>(StickyWindowHandling::TreatAsNormal)
        && handling <= static_cast<int>(StickyWindowHandling::IgnoreAll)) {
        setStickyWindowHandling(static_cast<StickyWindowHandling>(handling));
    }
}

// Session and exclusion setters
SETTINGS_SETTER(bool, RestoreWindowsToZonesOnLogin, m_restoreWindowsToZonesOnLogin, restoreWindowsToZonesOnLoginChanged)
SETTINGS_SETTER(bool, SnapAssistFeatureEnabled, m_snapAssistFeatureEnabled, snapAssistFeatureEnabledChanged)
SETTINGS_SETTER(bool, SnapAssistEnabled, m_snapAssistEnabled, snapAssistEnabledChanged)

void Settings::setSnapAssistTriggers(const QVariantList& triggers)
{
    QVariantList capped = triggers.mid(0, MaxTriggersPerAction);
    if (m_snapAssistTriggers != capped) {
        m_snapAssistTriggers = capped;
        Q_EMIT snapAssistTriggersChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setDefaultLayoutId(const QString& layoutId)
{
    // Normalize to default format (with braces) for consistent comparison
    QString normalizedId = normalizeUuidString(layoutId);
    if (m_defaultLayoutId != normalizedId) {
        m_defaultLayoutId = normalizedId;
        Q_EMIT defaultLayoutIdChanged();
        Q_EMIT settingsChanged();
    }
}

SETTINGS_SETTER(const QStringList&, ExcludedApplications, m_excludedApplications, excludedApplicationsChanged)
SETTINGS_SETTER(const QStringList&, ExcludedWindowClasses, m_excludedWindowClasses, excludedWindowClassesChanged)
SETTINGS_SETTER(bool, ExcludeTransientWindows, m_excludeTransientWindows, excludeTransientWindowsChanged)
SETTINGS_SETTER_CLAMPED(MinimumWindowWidth, m_minimumWindowWidth, minimumWindowWidthChanged, 0, 1000)
SETTINGS_SETTER_CLAMPED(MinimumWindowHeight, m_minimumWindowHeight, minimumWindowHeightChanged, 0, 1000)

// Zone Selector setters
SETTINGS_SETTER(bool, ZoneSelectorEnabled, m_zoneSelectorEnabled, zoneSelectorEnabledChanged)
SETTINGS_SETTER_CLAMPED(ZoneSelectorTriggerDistance, m_zoneSelectorTriggerDistance, zoneSelectorTriggerDistanceChanged, 10, 200)
SETTINGS_SETTER(ZoneSelectorPosition, ZoneSelectorPosition, m_zoneSelectorPosition, zoneSelectorPositionChanged)

void Settings::setZoneSelectorPositionInt(int position)
{
    // Valid positions are 0-8 except 4 (center)
    if (position >= 0 && position <= 8 && position != 4) {
        setZoneSelectorPosition(static_cast<ZoneSelectorPosition>(position));
    }
}

SETTINGS_SETTER(ZoneSelectorLayoutMode, ZoneSelectorLayoutMode, m_zoneSelectorLayoutMode, zoneSelectorLayoutModeChanged)

void Settings::setZoneSelectorLayoutModeInt(int mode)
{
    if (mode >= 0 && mode <= static_cast<int>(ZoneSelectorLayoutMode::Vertical)) {
        setZoneSelectorLayoutMode(static_cast<ZoneSelectorLayoutMode>(mode));
    }
}

SETTINGS_SETTER_CLAMPED(ZoneSelectorPreviewWidth, m_zoneSelectorPreviewWidth, zoneSelectorPreviewWidthChanged, 80, 400)
SETTINGS_SETTER_CLAMPED(ZoneSelectorPreviewHeight, m_zoneSelectorPreviewHeight, zoneSelectorPreviewHeightChanged, 60, 300)
SETTINGS_SETTER(bool, ZoneSelectorPreviewLockAspect, m_zoneSelectorPreviewLockAspect, zoneSelectorPreviewLockAspectChanged)
SETTINGS_SETTER_CLAMPED(ZoneSelectorGridColumns, m_zoneSelectorGridColumns, zoneSelectorGridColumnsChanged, 1, 10)
SETTINGS_SETTER(ZoneSelectorSizeMode, ZoneSelectorSizeMode, m_zoneSelectorSizeMode, zoneSelectorSizeModeChanged)

void Settings::setZoneSelectorSizeModeInt(int mode)
{
    if (mode >= 0 && mode <= static_cast<int>(ZoneSelectorSizeMode::Manual)) {
        setZoneSelectorSizeMode(static_cast<ZoneSelectorSizeMode>(mode));
    }
}

SETTINGS_SETTER_CLAMPED(ZoneSelectorMaxRows, m_zoneSelectorMaxRows, zoneSelectorMaxRowsChanged, 1, 10)

// Shader Effects implementations
SETTINGS_SETTER(bool, EnableShaderEffects, m_enableShaderEffects, enableShaderEffectsChanged)
SETTINGS_SETTER_CLAMPED(ShaderFrameRate, m_shaderFrameRate, shaderFrameRateChanged, 30, 144)
SETTINGS_SETTER(bool, EnableAudioVisualizer, m_enableAudioVisualizer, enableAudioVisualizerChanged)
SETTINGS_SETTER_CLAMPED(AudioSpectrumBarCount, m_audioSpectrumBarCount, audioSpectrumBarCountChanged, 16, 256)

// Global Shortcuts implementations
SETTINGS_SETTER(const QString&, OpenEditorShortcut, m_openEditorShortcut, openEditorShortcutChanged)
SETTINGS_SETTER(const QString&, PreviousLayoutShortcut, m_previousLayoutShortcut, previousLayoutShortcutChanged)
SETTINGS_SETTER(const QString&, NextLayoutShortcut, m_nextLayoutShortcut, nextLayoutShortcutChanged)

void Settings::setQuickLayout1Shortcut(const QString& shortcut)
{
    setQuickLayoutShortcut(0, shortcut);
}
void Settings::setQuickLayout2Shortcut(const QString& shortcut)
{
    setQuickLayoutShortcut(1, shortcut);
}
void Settings::setQuickLayout3Shortcut(const QString& shortcut)
{
    setQuickLayoutShortcut(2, shortcut);
}
void Settings::setQuickLayout4Shortcut(const QString& shortcut)
{
    setQuickLayoutShortcut(3, shortcut);
}
void Settings::setQuickLayout5Shortcut(const QString& shortcut)
{
    setQuickLayoutShortcut(4, shortcut);
}
void Settings::setQuickLayout6Shortcut(const QString& shortcut)
{
    setQuickLayoutShortcut(5, shortcut);
}
void Settings::setQuickLayout7Shortcut(const QString& shortcut)
{
    setQuickLayoutShortcut(6, shortcut);
}
void Settings::setQuickLayout8Shortcut(const QString& shortcut)
{
    setQuickLayoutShortcut(7, shortcut);
}
void Settings::setQuickLayout9Shortcut(const QString& shortcut)
{
    setQuickLayoutShortcut(8, shortcut);
}

QString Settings::quickLayoutShortcut(int index) const
{
    if (index >= 0 && index < 9) {
        return m_quickLayoutShortcuts[index];
    }
    return QString();
}

void Settings::setQuickLayoutShortcut(int index, const QString& shortcut)
{
    if (index >= 0 && index < 9 && m_quickLayoutShortcuts[index] != shortcut) {
        m_quickLayoutShortcuts[index] = shortcut;
        // Emit the appropriate signal based on index
        switch (index) {
        case 0:
            Q_EMIT quickLayout1ShortcutChanged();
            break;
        case 1:
            Q_EMIT quickLayout2ShortcutChanged();
            break;
        case 2:
            Q_EMIT quickLayout3ShortcutChanged();
            break;
        case 3:
            Q_EMIT quickLayout4ShortcutChanged();
            break;
        case 4:
            Q_EMIT quickLayout5ShortcutChanged();
            break;
        case 5:
            Q_EMIT quickLayout6ShortcutChanged();
            break;
        case 6:
            Q_EMIT quickLayout7ShortcutChanged();
            break;
        case 7:
            Q_EMIT quickLayout8ShortcutChanged();
            break;
        case 8:
            Q_EMIT quickLayout9ShortcutChanged();
            break;
        }
        Q_EMIT settingsChanged();
    }
}

// Keyboard Navigation Shortcuts implementations
SETTINGS_SETTER(const QString&, MoveWindowLeftShortcut, m_moveWindowLeftShortcut, moveWindowLeftShortcutChanged)
SETTINGS_SETTER(const QString&, MoveWindowRightShortcut, m_moveWindowRightShortcut, moveWindowRightShortcutChanged)
SETTINGS_SETTER(const QString&, MoveWindowUpShortcut, m_moveWindowUpShortcut, moveWindowUpShortcutChanged)
SETTINGS_SETTER(const QString&, MoveWindowDownShortcut, m_moveWindowDownShortcut, moveWindowDownShortcutChanged)
SETTINGS_SETTER(const QString&, FocusZoneLeftShortcut, m_focusZoneLeftShortcut, focusZoneLeftShortcutChanged)
SETTINGS_SETTER(const QString&, FocusZoneRightShortcut, m_focusZoneRightShortcut, focusZoneRightShortcutChanged)
SETTINGS_SETTER(const QString&, FocusZoneUpShortcut, m_focusZoneUpShortcut, focusZoneUpShortcutChanged)
SETTINGS_SETTER(const QString&, FocusZoneDownShortcut, m_focusZoneDownShortcut, focusZoneDownShortcutChanged)
SETTINGS_SETTER(const QString&, PushToEmptyZoneShortcut, m_pushToEmptyZoneShortcut, pushToEmptyZoneShortcutChanged)
SETTINGS_SETTER(const QString&, RestoreWindowSizeShortcut, m_restoreWindowSizeShortcut, restoreWindowSizeShortcutChanged)
SETTINGS_SETTER(const QString&, ToggleWindowFloatShortcut, m_toggleWindowFloatShortcut, toggleWindowFloatShortcutChanged)

// Swap Window Shortcuts
SETTINGS_SETTER(const QString&, SwapWindowLeftShortcut, m_swapWindowLeftShortcut, swapWindowLeftShortcutChanged)
SETTINGS_SETTER(const QString&, SwapWindowRightShortcut, m_swapWindowRightShortcut, swapWindowRightShortcutChanged)
SETTINGS_SETTER(const QString&, SwapWindowUpShortcut, m_swapWindowUpShortcut, swapWindowUpShortcutChanged)
SETTINGS_SETTER(const QString&, SwapWindowDownShortcut, m_swapWindowDownShortcut, swapWindowDownShortcutChanged)

// Snap to Zone by Number Shortcuts
QString Settings::snapToZoneShortcut(int index) const
{
    if (index >= 0 && index < 9) {
        return m_snapToZoneShortcuts[index];
    }
    return QString();
}

void Settings::setSnapToZoneShortcut(int index, const QString& shortcut)
{
    if (index >= 0 && index < 9 && m_snapToZoneShortcuts[index] != shortcut) {
        m_snapToZoneShortcuts[index] = shortcut;
        switch (index) {
        case 0:
            Q_EMIT snapToZone1ShortcutChanged();
            break;
        case 1:
            Q_EMIT snapToZone2ShortcutChanged();
            break;
        case 2:
            Q_EMIT snapToZone3ShortcutChanged();
            break;
        case 3:
            Q_EMIT snapToZone4ShortcutChanged();
            break;
        case 4:
            Q_EMIT snapToZone5ShortcutChanged();
            break;
        case 5:
            Q_EMIT snapToZone6ShortcutChanged();
            break;
        case 6:
            Q_EMIT snapToZone7ShortcutChanged();
            break;
        case 7:
            Q_EMIT snapToZone8ShortcutChanged();
            break;
        case 8:
            Q_EMIT snapToZone9ShortcutChanged();
            break;
        }
        Q_EMIT settingsChanged();
    }
}

void Settings::setSnapToZone1Shortcut(const QString& shortcut)
{
    setSnapToZoneShortcut(0, shortcut);
}

void Settings::setSnapToZone2Shortcut(const QString& shortcut)
{
    setSnapToZoneShortcut(1, shortcut);
}

void Settings::setSnapToZone3Shortcut(const QString& shortcut)
{
    setSnapToZoneShortcut(2, shortcut);
}

void Settings::setSnapToZone4Shortcut(const QString& shortcut)
{
    setSnapToZoneShortcut(3, shortcut);
}

void Settings::setSnapToZone5Shortcut(const QString& shortcut)
{
    setSnapToZoneShortcut(4, shortcut);
}

void Settings::setSnapToZone6Shortcut(const QString& shortcut)
{
    setSnapToZoneShortcut(5, shortcut);
}

void Settings::setSnapToZone7Shortcut(const QString& shortcut)
{
    setSnapToZoneShortcut(6, shortcut);
}

void Settings::setSnapToZone8Shortcut(const QString& shortcut)
{
    setSnapToZoneShortcut(7, shortcut);
}

void Settings::setSnapToZone9Shortcut(const QString& shortcut)
{
    setSnapToZoneShortcut(8, shortcut);
}

// Rotate Windows Shortcuts
SETTINGS_SETTER(const QString&, RotateWindowsClockwiseShortcut, m_rotateWindowsClockwiseShortcut, rotateWindowsClockwiseShortcutChanged)
SETTINGS_SETTER(const QString&, RotateWindowsCounterclockwiseShortcut, m_rotateWindowsCounterclockwiseShortcut, rotateWindowsCounterclockwiseShortcutChanged)

// Cycle Windows in Zone Shortcuts
SETTINGS_SETTER(const QString&, CycleWindowForwardShortcut, m_cycleWindowForwardShortcut, cycleWindowForwardShortcutChanged)
SETTINGS_SETTER(const QString&, CycleWindowBackwardShortcut, m_cycleWindowBackwardShortcut, cycleWindowBackwardShortcutChanged)
SETTINGS_SETTER(const QString&, ResnapToNewLayoutShortcut, m_resnapToNewLayoutShortcut, resnapToNewLayoutShortcutChanged)
SETTINGS_SETTER(const QString&, SnapAllWindowsShortcut, m_snapAllWindowsShortcut, snapAllWindowsShortcutChanged)

bool Settings::isWindowExcluded(const QString& appName, const QString& windowClass) const
{
    for (const auto& excluded : m_excludedApplications) {
        if (appName.contains(excluded, Qt::CaseInsensitive)) {
            return true;
        }
    }

    for (const auto& excluded : m_excludedWindowClasses) {
        if (windowClass.contains(excluded, Qt::CaseInsensitive)) {
            return true;
        }
    }

    return false;
}

void Settings::load()
{
    auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));

    // Force re-read from disk - KSharedConfig caches in memory, so when KCM writes
    // to disk and daemon calls load(), we need to invalidate the cache first
    config->reparseConfiguration();

    KConfigGroup activation = config->group(QStringLiteral("Activation"));
    KConfigGroup display = config->group(QStringLiteral("Display"));
    KConfigGroup appearance = config->group(QStringLiteral("Appearance"));
    KConfigGroup zones = config->group(QStringLiteral("Zones"));
    KConfigGroup behavior = config->group(QStringLiteral("Behavior"));
    KConfigGroup exclusions = config->group(QStringLiteral("Exclusions"));

    // Activation with validation (defaults from .kcfg via ConfigDefaults)
    m_shiftDragToActivate = activation.readEntry(QLatin1String("ShiftDrag"), ConfigDefaults::shiftDrag()); // Deprecated

    // Read legacy single-value keys as locals for migration to trigger lists
    int legacyDragMod = activation.readEntry(QLatin1String("DragActivationModifier"), -1);
    if (legacyDragMod == -1) {
        if (activation.hasKey(QLatin1String("ShiftDrag")) && m_shiftDragToActivate) {
            legacyDragMod = static_cast<int>(DragModifier::Shift);
        } else {
            legacyDragMod = ConfigDefaults::dragActivationModifier();
        }
    } else {
        legacyDragMod = qBound(0, legacyDragMod, static_cast<int>(DragModifier::CtrlAltMeta));
    }
    int legacyDragBtn = qBound(0, activation.readEntry(QLatin1String("DragActivationMouseButton"),
                                                        ConfigDefaults::dragActivationMouseButton()), 128);
    if (legacyDragBtn == 1) legacyDragBtn = 0;

    // Load trigger list (new multi-bind format) or construct from legacy single values
    m_dragActivationTriggers = loadTriggerList(activation, QLatin1String("DragActivationTriggers"),
                                               legacyDragMod, legacyDragBtn);

    m_zoneSpanEnabled = activation.readEntry(QLatin1String("ZoneSpanEnabled"), ConfigDefaults::zoneSpanEnabled());

    // Zone span modifier: hold this key for paint-to-span zone selection
    // Migration: if old MiddleClickMultiZone key exists but new ZoneSpanModifier doesn't,
    // honour the old disabled state so upgrading users don't get unexpected zone-span activation
    if (activation.hasKey(QLatin1String("MiddleClickMultiZone")) && !activation.hasKey(QLatin1String("ZoneSpanModifier"))) {
        bool oldVal = activation.readEntry(QLatin1String("MiddleClickMultiZone"), true);
        if (!oldVal) {
            activation.writeEntry(QLatin1String("ZoneSpanModifier"), static_cast<int>(DragModifier::Disabled));
            qCInfo(lcConfig) << "Migrated MiddleClickMultiZone=false to ZoneSpanModifier=Disabled";
        }
        activation.deleteEntry(QLatin1String("MiddleClickMultiZone"));
        activation.sync();
    }
    int legacySpanMod = activation.readEntry(QLatin1String("ZoneSpanModifier"), ConfigDefaults::zoneSpanModifier());
    if (legacySpanMod < 0 || legacySpanMod > static_cast<int>(DragModifier::CtrlAltMeta)) {
        qCWarning(lcConfig) << "Invalid ZoneSpanModifier value:" << legacySpanMod << "using default";
        legacySpanMod = ConfigDefaults::zoneSpanModifier();
    }

    m_zoneSpanModifier = static_cast<DragModifier>(legacySpanMod);

    // Zone span triggers list (multi-bind)
    m_zoneSpanTriggers = loadTriggerList(activation, QLatin1String("ZoneSpanTriggers"),
                                          legacySpanMod, 0);

    m_toggleActivation = activation.readEntry(QLatin1String("ToggleActivation"), ConfigDefaults::toggleActivation());

    // Display (defaults from .kcfg via ConfigDefaults)
    m_showZonesOnAllMonitors = display.readEntry(QLatin1String("ShowOnAllMonitors"), ConfigDefaults::showOnAllMonitors());
    m_disabledMonitors = display.readEntry(QLatin1String("DisabledMonitors"), QStringList());
    // Migrate legacy connector names to stable EDID-based screen IDs
    for (int i = 0; i < m_disabledMonitors.size(); ++i) {
        const QString& name = m_disabledMonitors[i];
        if (Utils::isConnectorName(name)) {
            QString resolved = Utils::screenIdForName(name);
            if (resolved != name) {
                m_disabledMonitors[i] = resolved;
            }
        }
    }
    m_showZoneNumbers = display.readEntry(QLatin1String("ShowNumbers"), ConfigDefaults::showNumbers());
    m_flashZonesOnSwitch = display.readEntry(QLatin1String("FlashOnSwitch"), ConfigDefaults::flashOnSwitch());
    m_showOsdOnLayoutSwitch = display.readEntry(QLatin1String("ShowOsdOnLayoutSwitch"), ConfigDefaults::showOsdOnLayoutSwitch());
    m_showNavigationOsd = display.readEntry(QLatin1String("ShowNavigationOsd"), ConfigDefaults::showNavigationOsd());
    m_osdStyle = static_cast<OsdStyle>(readValidatedInt(display, "OsdStyle", ConfigDefaults::osdStyle(), 0, 2, "OSD style"));

    // Appearance with validation (defaults from .kcfg via ConfigDefaults)
    m_useSystemColors = appearance.readEntry(QLatin1String("UseSystemColors"), ConfigDefaults::useSystemColors());

    // Validate colors
    m_highlightColor = readValidatedColor(appearance, "HighlightColor", ConfigDefaults::highlightColor(), "highlight");
    m_inactiveColor = readValidatedColor(appearance, "InactiveColor", ConfigDefaults::inactiveColor(), "inactive");
    m_borderColor = readValidatedColor(appearance, "BorderColor", ConfigDefaults::borderColor(), "border");
    m_labelFontColor = readValidatedColor(appearance, "LabelFontColor", ConfigDefaults::labelFontColor(), "label font");

    // Validate opacity (0.0 to 1.0)
    qreal activeOpacity = appearance.readEntry(QLatin1String("ActiveOpacity"), ConfigDefaults::activeOpacity());
    if (activeOpacity < 0.0 || activeOpacity > 1.0) {
        qCWarning(lcConfig) << "Invalid active opacity:" << activeOpacity << "clamping to valid range";
        activeOpacity = qBound(0.0, activeOpacity, 1.0);
    }
    m_activeOpacity = activeOpacity;

    qreal inactiveOpacity = appearance.readEntry(QLatin1String("InactiveOpacity"), ConfigDefaults::inactiveOpacity());
    if (inactiveOpacity < 0.0 || inactiveOpacity > 1.0) {
        qCWarning(lcConfig) << "Invalid inactive opacity:" << inactiveOpacity << "clamping to valid range";
        inactiveOpacity = qBound(0.0, inactiveOpacity, 1.0);
    }
    m_inactiveOpacity = inactiveOpacity;

    // Validate dimensions (non-negative) with INT_MAX as upper bound
    m_borderWidth = readValidatedInt(appearance, "BorderWidth", ConfigDefaults::borderWidth(), 0, INT_MAX, "border width");
    m_borderRadius = readValidatedInt(appearance, "BorderRadius", ConfigDefaults::borderRadius(), 0, INT_MAX, "border radius");

    m_enableBlur = appearance.readEntry(QLatin1String("EnableBlur"), ConfigDefaults::enableBlur());
    m_labelFontFamily = appearance.readEntry(QLatin1String("LabelFontFamily"), ConfigDefaults::labelFontFamily());
    qreal fontScale = appearance.readEntry(QLatin1String("LabelFontSizeScale"), ConfigDefaults::labelFontSizeScale());
    m_labelFontSizeScale = qBound(0.25, fontScale, 3.0);
    m_labelFontWeight = readValidatedInt(appearance, "LabelFontWeight", ConfigDefaults::labelFontWeight(), 100, 900, "label font weight");
    m_labelFontItalic = appearance.readEntry(QLatin1String("LabelFontItalic"), ConfigDefaults::labelFontItalic());
    m_labelFontUnderline = appearance.readEntry(QLatin1String("LabelFontUnderline"), ConfigDefaults::labelFontUnderline());
    m_labelFontStrikeout = appearance.readEntry(QLatin1String("LabelFontStrikeout"), ConfigDefaults::labelFontStrikeout());

    // Zones with validation (defaults from .kcfg via ConfigDefaults)
    m_zonePadding = readValidatedInt(zones, "Padding", ConfigDefaults::zonePadding(), 0, INT_MAX, "zone padding");
    m_outerGap = readValidatedInt(zones, "OuterGap", ConfigDefaults::outerGap(), 0, INT_MAX, "outer gap");
    m_adjacentThreshold = readValidatedInt(zones, "AdjacentThreshold", ConfigDefaults::adjacentThreshold(), 0, INT_MAX, "adjacent threshold");

    // Performance and behavior settings with validation
    m_pollIntervalMs = readValidatedInt(zones, "PollIntervalMs", ConfigDefaults::pollIntervalMs(), 10, 1000, "poll interval");
    m_minimumZoneSizePx = readValidatedInt(zones, "MinimumZoneSizePx", ConfigDefaults::minimumZoneSizePx(), 50, 500, "minimum zone size");
    m_minimumZoneDisplaySizePx = readValidatedInt(zones, "MinimumZoneDisplaySizePx", ConfigDefaults::minimumZoneDisplaySizePx(), 1, 50, "minimum zone display size");

    // Behavior (defaults from .kcfg via ConfigDefaults)
    m_keepWindowsInZonesOnResolutionChange = behavior.readEntry(QLatin1String("KeepOnResolutionChange"), ConfigDefaults::keepWindowsInZonesOnResolutionChange());
    m_moveNewWindowsToLastZone = behavior.readEntry(QLatin1String("MoveNewToLastZone"), ConfigDefaults::moveNewWindowsToLastZone());
    m_restoreOriginalSizeOnUnsnap = behavior.readEntry(QLatin1String("RestoreSizeOnUnsnap"), ConfigDefaults::restoreOriginalSizeOnUnsnap());
    int stickyHandling =
        behavior.readEntry(QLatin1String("StickyWindowHandling"), ConfigDefaults::stickyWindowHandling());
    m_stickyWindowHandling =
        static_cast<StickyWindowHandling>(qBound(static_cast<int>(StickyWindowHandling::TreatAsNormal), stickyHandling,
                                                 static_cast<int>(StickyWindowHandling::IgnoreAll)));
    m_restoreWindowsToZonesOnLogin = behavior.readEntry(QLatin1String("RestoreWindowsToZonesOnLogin"), ConfigDefaults::restoreWindowsToZonesOnLogin());
    m_snapAssistFeatureEnabled = activation.readEntry(QLatin1String("SnapAssistFeatureEnabled"), ConfigDefaults::snapAssistFeatureEnabled());
    // Snap Assist: Activation group (migration: read Behavior if not in Activation)
    const QString snapAssistEnabledKey = QLatin1String("SnapAssistEnabled");
    const QString snapAssistTriggersKey = QLatin1String("SnapAssistTriggers");
    m_snapAssistEnabled = activation.hasKey(snapAssistEnabledKey)
        ? activation.readEntry(snapAssistEnabledKey, ConfigDefaults::snapAssistEnabled())
        : behavior.readEntry(snapAssistEnabledKey, ConfigDefaults::snapAssistEnabled());
    QString snapAssistTriggersJson = activation.hasKey(snapAssistTriggersKey)
        ? activation.readEntry(snapAssistTriggersKey, QString())
        : behavior.readEntry(snapAssistTriggersKey, QString());
    m_snapAssistTriggers = parseTriggerListJson(snapAssistTriggersJson).value_or(ConfigDefaults::snapAssistTriggers());
    // Normalize UUID to default format (with braces) for consistent comparison
    // Handles migration from configs saved with WithoutBraces format
    const QString oldDefaultLayoutId = m_defaultLayoutId;
    m_defaultLayoutId = normalizeUuidString(behavior.readEntry(QLatin1String("DefaultLayoutId"), QString()));

    // Exclusions (defaults from .kcfg via ConfigDefaults)
    m_excludedApplications = exclusions.readEntry(QLatin1String("Applications"), QStringList());
    m_excludedWindowClasses = exclusions.readEntry(QLatin1String("WindowClasses"), QStringList());
    m_excludeTransientWindows = exclusions.readEntry(QLatin1String("ExcludeTransientWindows"), ConfigDefaults::excludeTransientWindows());
    int minWidth = exclusions.readEntry(QLatin1String("MinimumWindowWidth"), ConfigDefaults::minimumWindowWidth());
    m_minimumWindowWidth = qBound(0, minWidth, 2000);  // Match kcfg max for 4K monitors
    int minHeight = exclusions.readEntry(QLatin1String("MinimumWindowHeight"), ConfigDefaults::minimumWindowHeight());
    m_minimumWindowHeight = qBound(0, minHeight, 2000);  // Match kcfg max for 4K monitors

    // Zone Selector (defaults from .kcfg via ConfigDefaults)
    KConfigGroup zoneSelector = config->group(QStringLiteral("ZoneSelector"));
    m_zoneSelectorEnabled = zoneSelector.readEntry(QLatin1String("Enabled"), ConfigDefaults::zoneSelectorEnabled());
    m_zoneSelectorTriggerDistance = readValidatedInt(zoneSelector, "TriggerDistance", ConfigDefaults::triggerDistance(), 10, 200, "zone selector trigger distance");
    int selectorPos = zoneSelector.readEntry(QLatin1String("Position"), ConfigDefaults::position());
    // Valid positions are 0-8 except 4 (center)
    if (selectorPos >= 0 && selectorPos <= 8 && selectorPos != 4) {
        m_zoneSelectorPosition = static_cast<ZoneSelectorPosition>(selectorPos);
    } else {
        m_zoneSelectorPosition = static_cast<ZoneSelectorPosition>(ConfigDefaults::position());
    }
    int selectorMode = zoneSelector.readEntry(QLatin1String("LayoutMode"), ConfigDefaults::layoutMode());
    m_zoneSelectorLayoutMode = static_cast<ZoneSelectorLayoutMode>(
        qBound(0, selectorMode, static_cast<int>(ZoneSelectorLayoutMode::Vertical)));
    m_zoneSelectorPreviewWidth = readValidatedInt(zoneSelector, "PreviewWidth", ConfigDefaults::previewWidth(), 80, 400, "zone selector preview width");
    m_zoneSelectorPreviewHeight = readValidatedInt(zoneSelector, "PreviewHeight", ConfigDefaults::previewHeight(), 60, 300, "zone selector preview height");
    m_zoneSelectorPreviewLockAspect = zoneSelector.readEntry(QLatin1String("PreviewLockAspect"), ConfigDefaults::previewLockAspect());
    m_zoneSelectorGridColumns = readValidatedInt(zoneSelector, "GridColumns", ConfigDefaults::gridColumns(), 1, 10, "zone selector grid columns");

    // Size mode (Auto/Manual)
    int sizeMode = zoneSelector.readEntry(QLatin1String("SizeMode"), ConfigDefaults::sizeMode());
    m_zoneSelectorSizeMode =
        static_cast<ZoneSelectorSizeMode>(qBound(0, sizeMode, static_cast<int>(ZoneSelectorSizeMode::Manual)));

    // Max visible rows before scrolling (Auto mode)
    m_zoneSelectorMaxRows = readValidatedInt(zoneSelector, "MaxRows", ConfigDefaults::maxRows(), 1, 10, "zone selector max rows");

    // Per-screen zone selector overrides (groups matching ZoneSelector:*)
    m_perScreenZoneSelectorSettings.clear();
    const QStringList allGroups = config->groupList();
    const QLatin1String perScreenPrefix("ZoneSelector:");
    for (const QString& groupName : allGroups) {
        if (groupName.startsWith(perScreenPrefix)) {
            QString screenName = groupName.mid(perScreenPrefix.size());
            if (screenName.isEmpty()) {
                continue;
            }
            KConfigGroup screenGroup = config->group(groupName);
            QVariantMap overrides;
            for (const char* key : kPerScreenKeys) {
                QLatin1String keyStr(key);
                if (screenGroup.hasKey(keyStr)) {
                    // Read raw value: bool for PreviewLockAspect, int for everything else
                    QVariant raw;
                    if (keyStr == QLatin1String(ZoneSelectorConfigKey::PreviewLockAspect)) {
                        raw = screenGroup.readEntry(keyStr, true);
                    } else {
                        raw = screenGroup.readEntry(keyStr, 0);
                    }
                    QVariant validated = validatePerScreenValue(keyStr, raw);
                    if (validated.isValid()) {
                        overrides[QString::fromLatin1(key)] = validated;
                    }
                }
            }
            if (!overrides.isEmpty()) {
                m_perScreenZoneSelectorSettings[screenName] = overrides;
                qCDebug(lcConfig) << "Loaded per-screen zone selector overrides for" << screenName << ":" << overrides.keys();
            }
        }
    }

    // Migrate legacy connector name keys to stable EDID-based screen IDs
    {
        QHash<QString, QVariantMap> migrated;
        for (auto it = m_perScreenZoneSelectorSettings.begin(); it != m_perScreenZoneSelectorSettings.end(); ) {
            if (Utils::isConnectorName(it.key())) {
                QString resolved = Utils::screenIdForName(it.key());
                if (resolved != it.key()) {
                    migrated[resolved] = it.value();
                    it = m_perScreenZoneSelectorSettings.erase(it);
                    continue;
                }
            }
            ++it;
        }
        for (auto mit = migrated.constBegin(); mit != migrated.constEnd(); ++mit) {
            m_perScreenZoneSelectorSettings.insert(mit.key(), mit.value());
        }
    }

    // Shader Effects (defaults from .kcfg via ConfigDefaults)
    // Save old values so we can emit specific signals for settings with runtime side-effects
    // (load() writes directly to members, bypassing the setters that emit signals)
    const bool oldEnableShaders = m_enableShaderEffects;
    const int oldShaderFrameRate = m_shaderFrameRate;
    const bool oldEnableAudioViz = m_enableAudioVisualizer;
    const int oldBarCount = m_audioSpectrumBarCount;

    KConfigGroup shaders = config->group(QStringLiteral("Shaders"));
    m_enableShaderEffects = shaders.readEntry(QLatin1String("EnableShaderEffects"), ConfigDefaults::enableShaderEffects());
    m_shaderFrameRate = qBound(30, shaders.readEntry(QLatin1String("ShaderFrameRate"), ConfigDefaults::shaderFrameRate()), 144);
    m_enableAudioVisualizer = shaders.readEntry(QLatin1String("EnableAudioVisualizer"), ConfigDefaults::enableAudioVisualizer());
    m_audioSpectrumBarCount = qBound(16, shaders.readEntry(QLatin1String("AudioSpectrumBarCount"), ConfigDefaults::audioSpectrumBarCount()), 256);

    // Global Shortcuts (all KGlobalAccel shortcuts in one group)
    KConfigGroup globalShortcuts = config->group(QStringLiteral("GlobalShortcuts"));
    m_openEditorShortcut = globalShortcuts.readEntry(QLatin1String("OpenEditorShortcut"), ConfigDefaults::openEditorShortcut());
    m_previousLayoutShortcut = globalShortcuts.readEntry(QLatin1String("PreviousLayoutShortcut"), ConfigDefaults::previousLayoutShortcut());
    m_nextLayoutShortcut = globalShortcuts.readEntry(QLatin1String("NextLayoutShortcut"), ConfigDefaults::nextLayoutShortcut());
    // Quick layout shortcuts
    const QString quickLayoutDefaults[9] = {
        ConfigDefaults::quickLayout1Shortcut(), ConfigDefaults::quickLayout2Shortcut(),
        ConfigDefaults::quickLayout3Shortcut(), ConfigDefaults::quickLayout4Shortcut(),
        ConfigDefaults::quickLayout5Shortcut(), ConfigDefaults::quickLayout6Shortcut(),
        ConfigDefaults::quickLayout7Shortcut(), ConfigDefaults::quickLayout8Shortcut(),
        ConfigDefaults::quickLayout9Shortcut()
    };
    loadIndexedShortcuts(globalShortcuts, QStringLiteral("QuickLayout%1Shortcut"), m_quickLayoutShortcuts, quickLayoutDefaults);

    m_moveWindowLeftShortcut = globalShortcuts.readEntry(QLatin1String("MoveWindowLeft"), ConfigDefaults::moveWindowLeftShortcut());
    m_moveWindowRightShortcut = globalShortcuts.readEntry(QLatin1String("MoveWindowRight"), ConfigDefaults::moveWindowRightShortcut());
    m_moveWindowUpShortcut = globalShortcuts.readEntry(QLatin1String("MoveWindowUp"), ConfigDefaults::moveWindowUpShortcut());
    m_moveWindowDownShortcut = globalShortcuts.readEntry(QLatin1String("MoveWindowDown"), ConfigDefaults::moveWindowDownShortcut());
    m_focusZoneLeftShortcut = globalShortcuts.readEntry(QLatin1String("FocusZoneLeft"), ConfigDefaults::focusZoneLeftShortcut());
    m_focusZoneRightShortcut = globalShortcuts.readEntry(QLatin1String("FocusZoneRight"), ConfigDefaults::focusZoneRightShortcut());
    m_focusZoneUpShortcut = globalShortcuts.readEntry(QLatin1String("FocusZoneUp"), ConfigDefaults::focusZoneUpShortcut());
    m_focusZoneDownShortcut = globalShortcuts.readEntry(QLatin1String("FocusZoneDown"), ConfigDefaults::focusZoneDownShortcut());
    m_pushToEmptyZoneShortcut = globalShortcuts.readEntry(QLatin1String("PushToEmptyZone"), ConfigDefaults::pushToEmptyZoneShortcut());
    m_restoreWindowSizeShortcut = globalShortcuts.readEntry(QLatin1String("RestoreWindowSize"), ConfigDefaults::restoreWindowSizeShortcut());
    m_toggleWindowFloatShortcut = globalShortcuts.readEntry(QLatin1String("ToggleWindowFloat"), ConfigDefaults::toggleWindowFloatShortcut());
    m_swapWindowLeftShortcut = globalShortcuts.readEntry(QLatin1String("SwapWindowLeft"), ConfigDefaults::swapWindowLeftShortcut());
    m_swapWindowRightShortcut = globalShortcuts.readEntry(QLatin1String("SwapWindowRight"), ConfigDefaults::swapWindowRightShortcut());
    m_swapWindowUpShortcut = globalShortcuts.readEntry(QLatin1String("SwapWindowUp"), ConfigDefaults::swapWindowUpShortcut());
    m_swapWindowDownShortcut = globalShortcuts.readEntry(QLatin1String("SwapWindowDown"), ConfigDefaults::swapWindowDownShortcut());

    const QString snapToZoneDefaults[9] = {
        ConfigDefaults::snapToZone1Shortcut(), ConfigDefaults::snapToZone2Shortcut(),
        ConfigDefaults::snapToZone3Shortcut(), ConfigDefaults::snapToZone4Shortcut(),
        ConfigDefaults::snapToZone5Shortcut(), ConfigDefaults::snapToZone6Shortcut(),
        ConfigDefaults::snapToZone7Shortcut(), ConfigDefaults::snapToZone8Shortcut(),
        ConfigDefaults::snapToZone9Shortcut()
    };
    loadIndexedShortcuts(globalShortcuts, QStringLiteral("SnapToZone%1"), m_snapToZoneShortcuts, snapToZoneDefaults);

    m_rotateWindowsClockwiseShortcut =
        globalShortcuts.readEntry(QLatin1String("RotateWindowsClockwise"), ConfigDefaults::rotateWindowsClockwiseShortcut());
    m_rotateWindowsCounterclockwiseShortcut =
        globalShortcuts.readEntry(QLatin1String("RotateWindowsCounterclockwise"), ConfigDefaults::rotateWindowsCounterclockwiseShortcut());
    m_cycleWindowForwardShortcut =
        globalShortcuts.readEntry(QLatin1String("CycleWindowForward"), ConfigDefaults::cycleWindowForwardShortcut());
    m_cycleWindowBackwardShortcut =
        globalShortcuts.readEntry(QLatin1String("CycleWindowBackward"), ConfigDefaults::cycleWindowBackwardShortcut());
    m_resnapToNewLayoutShortcut =
        globalShortcuts.readEntry(QLatin1String("ResnapToNewLayoutShortcut"), ConfigDefaults::resnapToNewLayoutShortcut());
    m_snapAllWindowsShortcut =
        globalShortcuts.readEntry(QLatin1String("SnapAllWindowsShortcut"), ConfigDefaults::snapAllWindowsShortcut());

    // Apply system colors if enabled
    if (m_useSystemColors) {
        applySystemColorScheme();
    }

    qCInfo(lcConfig) << "Settings loaded successfully";

    // Notify listeners so the overlay updates when KCM saves settings.
    Q_EMIT settingsChanged();

    // Emit specific signals for settings with runtime side-effects (e.g. starting/stopping
    // CAVA, recreating overlay windows). load() bypasses the setters, so these won't fire
    // unless we emit them explicitly after comparing old vs new values.
    if (m_enableShaderEffects != oldEnableShaders)
        Q_EMIT enableShaderEffectsChanged();
    if (m_shaderFrameRate != oldShaderFrameRate)
        Q_EMIT shaderFrameRateChanged();
    if (m_enableAudioVisualizer != oldEnableAudioViz)
        Q_EMIT enableAudioVisualizerChanged();
    if (m_audioSpectrumBarCount != oldBarCount)
        Q_EMIT audioSpectrumBarCountChanged();
    if (m_defaultLayoutId != oldDefaultLayoutId)
        Q_EMIT defaultLayoutIdChanged();
}

void Settings::save()
{
    auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
    KConfigGroup activation = config->group(QStringLiteral("Activation"));
    KConfigGroup display = config->group(QStringLiteral("Display"));
    KConfigGroup appearance = config->group(QStringLiteral("Appearance"));
    KConfigGroup zones = config->group(QStringLiteral("Zones"));
    KConfigGroup behavior = config->group(QStringLiteral("Behavior"));
    KConfigGroup exclusions = config->group(QStringLiteral("Exclusions"));

    // Activation
    activation.writeEntry(QLatin1String("ShiftDrag"), m_shiftDragToActivate); // Deprecated, kept for compatibility
    // Multi-trigger lists as JSON (replace legacy single-value keys)
    saveTriggerList(activation, QLatin1String("DragActivationTriggers"), m_dragActivationTriggers);
    activation.deleteEntry(QLatin1String("DragActivationModifier"));
    activation.deleteEntry(QLatin1String("DragActivationMouseButton"));
    // Clean up removed multi-zone modifier keys (legacy)
    activation.deleteEntry(QLatin1String("MultiZoneModifier"));
    activation.deleteEntry(QLatin1String("MultiZoneTriggers"));
    activation.deleteEntry(QLatin1String("MultiZoneMouseButton"));
    activation.writeEntry(QLatin1String("ZoneSpanEnabled"), m_zoneSpanEnabled);
    activation.writeEntry(QLatin1String("ZoneSpanModifier"), static_cast<int>(m_zoneSpanModifier));
    saveTriggerList(activation, QLatin1String("ZoneSpanTriggers"), m_zoneSpanTriggers);
    activation.deleteEntry(QLatin1String("ZoneSpanMouseButton"));
    activation.writeEntry(QLatin1String("ToggleActivation"), m_toggleActivation);

    // Display
    display.writeEntry(QLatin1String("ShowOnAllMonitors"), m_showZonesOnAllMonitors);
    display.writeEntry(QLatin1String("DisabledMonitors"), m_disabledMonitors);
    display.writeEntry(QLatin1String("ShowNumbers"), m_showZoneNumbers);
    display.writeEntry(QLatin1String("FlashOnSwitch"), m_flashZonesOnSwitch);
    display.writeEntry(QLatin1String("ShowOsdOnLayoutSwitch"), m_showOsdOnLayoutSwitch);
    display.writeEntry(QLatin1String("ShowNavigationOsd"), m_showNavigationOsd);
    display.writeEntry(QLatin1String("OsdStyle"), static_cast<int>(m_osdStyle));

    // Appearance
    appearance.writeEntry(QLatin1String("UseSystemColors"), m_useSystemColors);
    appearance.writeEntry(QLatin1String("HighlightColor"), m_highlightColor);
    appearance.writeEntry(QLatin1String("InactiveColor"), m_inactiveColor);
    appearance.writeEntry(QLatin1String("BorderColor"), m_borderColor);
    appearance.writeEntry(QLatin1String("LabelFontColor"), m_labelFontColor);
    appearance.writeEntry(QLatin1String("ActiveOpacity"), m_activeOpacity);
    appearance.writeEntry(QLatin1String("InactiveOpacity"), m_inactiveOpacity);
    appearance.writeEntry(QLatin1String("BorderWidth"), m_borderWidth);
    appearance.writeEntry(QLatin1String("BorderRadius"), m_borderRadius);
    appearance.writeEntry(QLatin1String("EnableBlur"), m_enableBlur);
    appearance.writeEntry(QLatin1String("LabelFontFamily"), m_labelFontFamily);
    appearance.writeEntry(QLatin1String("LabelFontSizeScale"), m_labelFontSizeScale);
    appearance.writeEntry(QLatin1String("LabelFontWeight"), m_labelFontWeight);
    appearance.writeEntry(QLatin1String("LabelFontItalic"), m_labelFontItalic);
    appearance.writeEntry(QLatin1String("LabelFontUnderline"), m_labelFontUnderline);
    appearance.writeEntry(QLatin1String("LabelFontStrikeout"), m_labelFontStrikeout);

    // Zones
    zones.writeEntry(QLatin1String("Padding"), m_zonePadding);
    zones.writeEntry(QLatin1String("OuterGap"), m_outerGap);
    zones.writeEntry(QLatin1String("AdjacentThreshold"), m_adjacentThreshold);

    // Performance and behavior
    zones.writeEntry(QLatin1String("PollIntervalMs"), m_pollIntervalMs);
    zones.writeEntry(QLatin1String("MinimumZoneSizePx"), m_minimumZoneSizePx);
    zones.writeEntry(QLatin1String("MinimumZoneDisplaySizePx"), m_minimumZoneDisplaySizePx);

    // Behavior
    behavior.writeEntry(QLatin1String("KeepOnResolutionChange"), m_keepWindowsInZonesOnResolutionChange);
    behavior.writeEntry(QLatin1String("MoveNewToLastZone"), m_moveNewWindowsToLastZone);
    behavior.writeEntry(QLatin1String("RestoreSizeOnUnsnap"), m_restoreOriginalSizeOnUnsnap);
    behavior.writeEntry(QLatin1String("StickyWindowHandling"), static_cast<int>(m_stickyWindowHandling));
    behavior.writeEntry(QLatin1String("RestoreWindowsToZonesOnLogin"), m_restoreWindowsToZonesOnLogin);
    activation.writeEntry(QLatin1String("SnapAssistFeatureEnabled"), m_snapAssistFeatureEnabled);
    // Snap Assist: Activation group (migrate from Behavior on save)
    activation.writeEntry(QLatin1String("SnapAssistEnabled"), m_snapAssistEnabled);
    saveTriggerList(activation, QLatin1String("SnapAssistTriggers"), m_snapAssistTriggers);
    behavior.deleteEntry(QLatin1String("SnapAssistEnabled"));
    behavior.deleteEntry(QLatin1String("SnapAssistTriggers"));
    behavior.writeEntry(QLatin1String("DefaultLayoutId"), m_defaultLayoutId);

    // Exclusions
    exclusions.writeEntry(QLatin1String("Applications"), m_excludedApplications);
    exclusions.writeEntry(QLatin1String("WindowClasses"), m_excludedWindowClasses);
    exclusions.writeEntry(QLatin1String("ExcludeTransientWindows"), m_excludeTransientWindows);
    exclusions.writeEntry(QLatin1String("MinimumWindowWidth"), m_minimumWindowWidth);
    exclusions.writeEntry(QLatin1String("MinimumWindowHeight"), m_minimumWindowHeight);

    // Zone Selector
    KConfigGroup zoneSelector = config->group(QStringLiteral("ZoneSelector"));
    zoneSelector.writeEntry(QLatin1String("Enabled"), m_zoneSelectorEnabled);
    zoneSelector.writeEntry(QLatin1String("TriggerDistance"), m_zoneSelectorTriggerDistance);
    zoneSelector.writeEntry(QLatin1String("Position"), static_cast<int>(m_zoneSelectorPosition));
    zoneSelector.writeEntry(QLatin1String("LayoutMode"), static_cast<int>(m_zoneSelectorLayoutMode));
    zoneSelector.writeEntry(QLatin1String("PreviewWidth"), m_zoneSelectorPreviewWidth);
    zoneSelector.writeEntry(QLatin1String("PreviewHeight"), m_zoneSelectorPreviewHeight);
    zoneSelector.writeEntry(QLatin1String("PreviewLockAspect"), m_zoneSelectorPreviewLockAspect);
    zoneSelector.writeEntry(QLatin1String("GridColumns"), m_zoneSelectorGridColumns);
    zoneSelector.writeEntry(QLatin1String("SizeMode"), static_cast<int>(m_zoneSelectorSizeMode));
    zoneSelector.writeEntry(QLatin1String("MaxRows"), m_zoneSelectorMaxRows);

    // Per-screen zone selector overrides - delete all old groups, rewrite current ones
    const QStringList allGroups = config->groupList();
    const QLatin1String perScreenPrefix("ZoneSelector:");
    for (const QString& groupName : allGroups) {
        if (groupName.startsWith(perScreenPrefix)) {
            config->deleteGroup(groupName);
        }
    }
    for (auto it = m_perScreenZoneSelectorSettings.constBegin(); it != m_perScreenZoneSelectorSettings.constEnd(); ++it) {
        const QVariantMap& overrides = it.value();
        if (overrides.isEmpty()) {
            continue;
        }
        KConfigGroup screenGroup = config->group(QStringLiteral("ZoneSelector:") + it.key());
        for (auto oit = overrides.constBegin(); oit != overrides.constEnd(); ++oit) {
            screenGroup.writeEntry(oit.key(), oit.value());
        }
    }

    // Shader Effects
    KConfigGroup shaders = config->group(QStringLiteral("Shaders"));
    shaders.writeEntry(QLatin1String("EnableShaderEffects"), m_enableShaderEffects);
    shaders.writeEntry(QLatin1String("ShaderFrameRate"), m_shaderFrameRate);
    shaders.writeEntry(QLatin1String("EnableAudioVisualizer"), m_enableAudioVisualizer);
    shaders.writeEntry(QLatin1String("AudioSpectrumBarCount"), m_audioSpectrumBarCount);

    // Global Shortcuts (all KGlobalAccel shortcuts in one group)
    KConfigGroup globalShortcuts = config->group(QStringLiteral("GlobalShortcuts"));
    globalShortcuts.writeEntry(QLatin1String("OpenEditorShortcut"), m_openEditorShortcut);
    globalShortcuts.writeEntry(QLatin1String("PreviousLayoutShortcut"), m_previousLayoutShortcut);
    globalShortcuts.writeEntry(QLatin1String("NextLayoutShortcut"), m_nextLayoutShortcut);
    for (int i = 0; i < 9; ++i) {
        globalShortcuts.writeEntry(QStringLiteral("QuickLayout%1Shortcut").arg(i + 1), m_quickLayoutShortcuts[i]);
    }
    globalShortcuts.writeEntry(QLatin1String("MoveWindowLeft"), m_moveWindowLeftShortcut);
    globalShortcuts.writeEntry(QLatin1String("MoveWindowRight"), m_moveWindowRightShortcut);
    globalShortcuts.writeEntry(QLatin1String("MoveWindowUp"), m_moveWindowUpShortcut);
    globalShortcuts.writeEntry(QLatin1String("MoveWindowDown"), m_moveWindowDownShortcut);
    globalShortcuts.writeEntry(QLatin1String("FocusZoneLeft"), m_focusZoneLeftShortcut);
    globalShortcuts.writeEntry(QLatin1String("FocusZoneRight"), m_focusZoneRightShortcut);
    globalShortcuts.writeEntry(QLatin1String("FocusZoneUp"), m_focusZoneUpShortcut);
    globalShortcuts.writeEntry(QLatin1String("FocusZoneDown"), m_focusZoneDownShortcut);
    globalShortcuts.writeEntry(QLatin1String("PushToEmptyZone"), m_pushToEmptyZoneShortcut);
    globalShortcuts.writeEntry(QLatin1String("RestoreWindowSize"), m_restoreWindowSizeShortcut);
    globalShortcuts.writeEntry(QLatin1String("ToggleWindowFloat"), m_toggleWindowFloatShortcut);
    globalShortcuts.writeEntry(QLatin1String("SwapWindowLeft"), m_swapWindowLeftShortcut);
    globalShortcuts.writeEntry(QLatin1String("SwapWindowRight"), m_swapWindowRightShortcut);
    globalShortcuts.writeEntry(QLatin1String("SwapWindowUp"), m_swapWindowUpShortcut);
    globalShortcuts.writeEntry(QLatin1String("SwapWindowDown"), m_swapWindowDownShortcut);
    for (int i = 0; i < 9; ++i) {
        globalShortcuts.writeEntry(QStringLiteral("SnapToZone%1").arg(i + 1), m_snapToZoneShortcuts[i]);
    }
    globalShortcuts.writeEntry(QLatin1String("RotateWindowsClockwise"), m_rotateWindowsClockwiseShortcut);
    globalShortcuts.writeEntry(QLatin1String("RotateWindowsCounterclockwise"), m_rotateWindowsCounterclockwiseShortcut);
    globalShortcuts.writeEntry(QLatin1String("CycleWindowForward"), m_cycleWindowForwardShortcut);
    globalShortcuts.writeEntry(QLatin1String("CycleWindowBackward"), m_cycleWindowBackwardShortcut);
    globalShortcuts.writeEntry(QLatin1String("ResnapToNewLayoutShortcut"), m_resnapToNewLayoutShortcut);
    globalShortcuts.writeEntry(QLatin1String("SnapAllWindowsShortcut"), m_snapAllWindowsShortcut);

    config->sync();
}

void Settings::reset()
{
    // Clear all config groups and reload with defaults
    // This avoids duplicating ~160 lines of default assignments
    auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));

    // Delete all setting groups (load() will use ConfigDefaults for missing keys)
    const QStringList groups = {
        QStringLiteral("Activation"),
        QStringLiteral("Display"),
        QStringLiteral("Appearance"),
        QStringLiteral("Zones"),
        QStringLiteral("Behavior"),
        QStringLiteral("Exclusions"),
        QStringLiteral("ZoneSelector"),
        QStringLiteral("Shaders"),
        QStringLiteral("GlobalShortcuts")
    };

    for (const QString& groupName : groups) {
        config->deleteGroup(groupName);
    }

    // Also delete per-screen zone selector override groups
    const QStringList allGroups = config->groupList();
    for (const QString& groupName : allGroups) {
        if (groupName.startsWith(QLatin1String("ZoneSelector:"))) {
            config->deleteGroup(groupName);
        }
    }
    config->sync();

    // Reload from (now empty) config - will use ConfigDefaults for all values
    load();

    qCInfo(lcConfig) << "Settings reset to defaults";
}

// ═══════════════════════════════════════════════════════════════════════════════
// Per-Screen Zone Selector Config
// ═══════════════════════════════════════════════════════════════════════════════

ZoneSelectorConfig Settings::resolvedZoneSelectorConfig(const QString& screenName) const
{
    // Start with global defaults
    ZoneSelectorConfig config = {
        static_cast<int>(m_zoneSelectorPosition),
        static_cast<int>(m_zoneSelectorLayoutMode),
        static_cast<int>(m_zoneSelectorSizeMode),
        m_zoneSelectorMaxRows,
        m_zoneSelectorPreviewWidth,
        m_zoneSelectorPreviewHeight,
        m_zoneSelectorPreviewLockAspect,
        m_zoneSelectorGridColumns,
        m_zoneSelectorTriggerDistance
    };

    // Apply per-screen overrides if they exist
    auto it = m_perScreenZoneSelectorSettings.constFind(screenName);
    // Backward compat: if screenName looks like a connector name, try resolving
    if (it == m_perScreenZoneSelectorSettings.constEnd() && Utils::isConnectorName(screenName)) {
        QString resolved = Utils::screenIdForName(screenName);
        if (resolved != screenName) {
            it = m_perScreenZoneSelectorSettings.constFind(resolved);
        }
    }
    if (it == m_perScreenZoneSelectorSettings.constEnd()) {
        return config;
    }

    applyPerScreenOverrides(config, it.value());
    return config;
}

QVariantMap Settings::getPerScreenZoneSelectorSettings(const QString& screenName) const
{
    return m_perScreenZoneSelectorSettings.value(screenName);
}

void Settings::setPerScreenZoneSelectorSetting(const QString& screenName, const QString& key, const QVariant& value)
{
    if (screenName.isEmpty() || key.isEmpty()) {
        return;
    }

    // Validate and clamp using centralized validation (rejects unknown keys)
    QVariant validated = validatePerScreenValue(key, value);
    if (!validated.isValid()) {
        qCWarning(lcConfig) << "Unknown or invalid per-screen zone selector key:" << key;
        return;
    }

    // Only emit if value actually changed
    QVariantMap& screenSettings = m_perScreenZoneSelectorSettings[screenName];
    if (screenSettings.value(key) == validated) {
        return;
    }
    screenSettings[key] = validated;
    Q_EMIT perScreenZoneSelectorSettingsChanged();
    Q_EMIT settingsChanged();
}

void Settings::clearPerScreenZoneSelectorSettings(const QString& screenName)
{
    if (m_perScreenZoneSelectorSettings.remove(screenName)) {
        Q_EMIT perScreenZoneSelectorSettingsChanged();
        Q_EMIT settingsChanged();
    }
}

bool Settings::hasPerScreenZoneSelectorSettings(const QString& screenName) const
{
    return m_perScreenZoneSelectorSettings.contains(screenName);
}

QStringList Settings::screensWithZoneSelectorOverrides() const
{
    return m_perScreenZoneSelectorSettings.keys();
}

QString Settings::loadColorsFromFile(const QString& filePath)
{
    // Delegate to ColorImporter
    ColorImportResult result = ColorImporter::importFromFile(filePath);
    if (!result.success) {
        return result.errorMessage;
    }

    // Apply imported colors
    setHighlightColor(result.highlightColor);
    setInactiveColor(result.inactiveColor);
    setBorderColor(result.borderColor);
    setLabelFontColor(result.labelFontColor);

    m_useSystemColors = false;
    Q_EMIT useSystemColorsChanged();

    return QString(); // Success - no error
}

void Settings::applySystemColorScheme()
{
    KColorScheme scheme(QPalette::Active, KColorScheme::Selection);

    QColor highlight = scheme.background(KColorScheme::ActiveBackground).color();
    highlight.setAlpha(Defaults::HighlightAlpha);
    m_highlightColor = highlight;

    QColor inactive = scheme.background(KColorScheme::NormalBackground).color();
    inactive.setAlpha(Defaults::InactiveAlpha);
    m_inactiveColor = inactive;

    QColor border = scheme.foreground(KColorScheme::NormalText).color();
    border.setAlpha(Defaults::BorderAlpha);
    m_borderColor = border;

    m_labelFontColor = scheme.foreground(KColorScheme::NormalText).color();

    Q_EMIT highlightColorChanged();
    Q_EMIT inactiveColorChanged();
    Q_EMIT borderColorChanged();
    Q_EMIT labelFontColorChanged();
}

} // namespace PlasmaZones
