// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "settingsadaptor.h"
#include "../core/interfaces.h"
#include "../config/settings.h" // For concrete Settings type
#include "../core/logging.h"
#include "../core/shaderregistry.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QColor>
#include <QDBusVariant>
#include <QDBusAbstractAdaptor> // Explicit include to ensure proper namespace

namespace PlasmaZones {

SettingsAdaptor::SettingsAdaptor(ISettings* settings, QObject* parent)
    : QDBusAbstractAdaptor(parent)
    , m_settings(settings)
{
    Q_ASSERT(settings);
    initializeRegistry();

    // Connect to interface signals (DIP)
    connect(m_settings, &ISettings::settingsChanged, this, &SettingsAdaptor::settingsChanged);
}

// Note: Macros are defined in initializeRegistry() to avoid namespace pollution

void SettingsAdaptor::initializeRegistry()
{
// Register getters and setters for all settings
// This registry pattern allows adding new settings without modifying setSetting()
// Using macros to reduce repetition (DRY)

// Helper macros for DRY pattern (reduces repetition)
#define REGISTER_STRING_SETTING(name, getter, setter)                                                                  \
    m_getters[QStringLiteral(name)] = [this]() {                                                                       \
        return m_settings->getter();                                                                                   \
    };                                                                                                                 \
    m_setters[QStringLiteral(name)] = [this](const QVariant& v) {                                                      \
        m_settings->setter(v.toString());                                                                              \
        return true;                                                                                                   \
    };

#define REGISTER_BOOL_SETTING(name, getter, setter)                                                                    \
    m_getters[QStringLiteral(name)] = [this]() {                                                                       \
        return m_settings->getter();                                                                                   \
    };                                                                                                                 \
    m_setters[QStringLiteral(name)] = [this](const QVariant& v) {                                                      \
        m_settings->setter(v.toBool());                                                                                \
        return true;                                                                                                   \
    };

#define REGISTER_INT_SETTING(name, getter, setter)                                                                     \
    m_getters[QStringLiteral(name)] = [this]() {                                                                       \
        return m_settings->getter();                                                                                   \
    };                                                                                                                 \
    m_setters[QStringLiteral(name)] = [this](const QVariant& v) {                                                      \
        m_settings->setter(v.toInt());                                                                                 \
        return true;                                                                                                   \
    };

#define REGISTER_DOUBLE_SETTING(name, getter, setter)                                                                  \
    m_getters[QStringLiteral(name)] = [this]() {                                                                       \
        return m_settings->getter();                                                                                   \
    };                                                                                                                 \
    m_setters[QStringLiteral(name)] = [this](const QVariant& v) {                                                      \
        m_settings->setter(v.toDouble());                                                                              \
        return true;                                                                                                   \
    };

#define REGISTER_COLOR_SETTING(keyName, getter, setter)                                                                \
    m_getters[QStringLiteral(keyName)] = [this]() {                                                                    \
        QColor color = m_settings->getter();                                                                           \
        return color.name(QColor::HexArgb);                                                                            \
    };                                                                                                                 \
    m_setters[QStringLiteral(keyName)] = [this](const QVariant& v) {                                                   \
        m_settings->setter(QColor(v.toString()));                                                                      \
        return true;                                                                                                   \
    };

#define REGISTER_STRINGLIST_SETTING(name, getter, setter)                                                              \
    m_getters[QStringLiteral(name)] = [this]() {                                                                       \
        return m_settings->getter();                                                                                   \
    };                                                                                                                 \
    m_setters[QStringLiteral(name)] = [this](const QVariant& v) {                                                      \
        m_settings->setter(v.toStringList());                                                                          \
        return true;                                                                                                   \
    };

    // Activation settings (DRY: using macros)
    REGISTER_BOOL_SETTING("shiftDragToActivate", shiftDragToActivate, setShiftDragToActivate) // Deprecated

    // New modifier settings (enum as int)
    m_getters[QStringLiteral("dragActivationModifier")] = [this]() {
        return static_cast<int>(m_settings->dragActivationModifier());
    };
    m_setters[QStringLiteral("dragActivationModifier")] = [this](const QVariant& v) {
        int mod = v.toInt();
        if (mod >= 0 && mod <= static_cast<int>(DragModifier::CtrlAltMeta)) {
            m_settings->setDragActivationModifier(static_cast<DragModifier>(mod));
            return true;
        }
        return false;
    };

    // Skip-snap modifier: hold this key to move window without snapping
    m_getters[QStringLiteral("skipSnapModifier")] = [this]() {
        return static_cast<int>(m_settings->skipSnapModifier());
    };
    m_setters[QStringLiteral("skipSnapModifier")] = [this](const QVariant& v) {
        int mod = v.toInt();
        if (mod >= 0 && mod <= static_cast<int>(DragModifier::AlwaysActive)) {
            m_settings->setSkipSnapModifier(static_cast<DragModifier>(mod));
            return true;
        }
        return false;
    };

    // Multi-zone modifier: hold this key to span windows across multiple zones
    m_getters[QStringLiteral("multiZoneModifier")] = [this]() {
        return static_cast<int>(m_settings->multiZoneModifier());
    };
    m_setters[QStringLiteral("multiZoneModifier")] = [this](const QVariant& v) {
        int mod = v.toInt();
        if (mod >= 0 && mod <= static_cast<int>(DragModifier::CtrlAltMeta)) {
            m_settings->setMultiZoneModifier(static_cast<DragModifier>(mod));
            return true;
        }
        return false;
    };

    REGISTER_BOOL_SETTING("middleClickMultiZone", middleClickMultiZone, setMiddleClickMultiZone)

    // Display settings (DRY: using macros)
    REGISTER_BOOL_SETTING("showZonesOnAllMonitors", showZonesOnAllMonitors, setShowZonesOnAllMonitors)
    REGISTER_BOOL_SETTING("showZoneNumbers", showZoneNumbers, setShowZoneNumbers)
    REGISTER_BOOL_SETTING("flashZonesOnSwitch", flashZonesOnSwitch, setFlashZonesOnSwitch)
    REGISTER_BOOL_SETTING("showOsdOnLayoutSwitch", showOsdOnLayoutSwitch, setShowOsdOnLayoutSwitch)

    // Appearance settings (DRY: using macros)
    REGISTER_BOOL_SETTING("useSystemColors", useSystemColors, setUseSystemColors)
    REGISTER_COLOR_SETTING("highlightColor", highlightColor, setHighlightColor)
    REGISTER_COLOR_SETTING("inactiveColor", inactiveColor, setInactiveColor)
    REGISTER_COLOR_SETTING("borderColor", borderColor, setBorderColor)
    REGISTER_COLOR_SETTING("numberColor", numberColor, setNumberColor)
    REGISTER_DOUBLE_SETTING("activeOpacity", activeOpacity, setActiveOpacity)
    REGISTER_DOUBLE_SETTING("inactiveOpacity", inactiveOpacity, setInactiveOpacity)
    REGISTER_INT_SETTING("borderWidth", borderWidth, setBorderWidth)
    REGISTER_INT_SETTING("borderRadius", borderRadius, setBorderRadius)
    REGISTER_BOOL_SETTING("enableBlur", enableBlur, setEnableBlur)
    REGISTER_BOOL_SETTING("enableShaderEffects", enableShaderEffects, setEnableShaderEffects)
    REGISTER_INT_SETTING("shaderFrameRate", shaderFrameRate, setShaderFrameRate)

    // Zone settings (DRY: using macros)
    REGISTER_INT_SETTING("zonePadding", zonePadding, setZonePadding)
    REGISTER_INT_SETTING("adjacentThreshold", adjacentThreshold, setAdjacentThreshold)
    REGISTER_INT_SETTING("pollIntervalMs", pollIntervalMs, setPollIntervalMs)
    REGISTER_INT_SETTING("minimumZoneSizePx", minimumZoneSizePx, setMinimumZoneSizePx)
    REGISTER_INT_SETTING("minimumZoneDisplaySizePx", minimumZoneDisplaySizePx, setMinimumZoneDisplaySizePx)

    // Behavior settings (DRY: using macros)
    REGISTER_BOOL_SETTING("keepWindowsInZonesOnResolutionChange", keepWindowsInZonesOnResolutionChange,
                          setKeepWindowsInZonesOnResolutionChange)
    REGISTER_BOOL_SETTING("moveNewWindowsToLastZone", moveNewWindowsToLastZone, setMoveNewWindowsToLastZone)
    REGISTER_BOOL_SETTING("restoreOriginalSizeOnUnsnap", restoreOriginalSizeOnUnsnap, setRestoreOriginalSizeOnUnsnap)

    // Exclusions (DRY: using macros)
    REGISTER_STRINGLIST_SETTING("excludedApplications", excludedApplications, setExcludedApplications)
    REGISTER_STRINGLIST_SETTING("excludedWindowClasses", excludedWindowClasses, setExcludedWindowClasses)

// Clean up macros (local scope)
#undef REGISTER_STRING_SETTING
#undef REGISTER_BOOL_SETTING
#undef REGISTER_INT_SETTING
#undef REGISTER_DOUBLE_SETTING
#undef REGISTER_COLOR_SETTING
#undef REGISTER_STRINGLIST_SETTING
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
        qCWarning(lcDbusSettings) << "Cannot get setting - empty key";
        return QDBusVariant();
    }

    auto it = m_getters.find(key);
    if (it != m_getters.end()) {
        return QDBusVariant(it.value()());
    }

    qCWarning(lcDbusSettings) << "Setting key not found:" << key;
    return QDBusVariant();
}

bool SettingsAdaptor::setSetting(const QString& key, const QDBusVariant& value)
{
    if (key.isEmpty()) {
        qCWarning(lcDbusSettings) << "Cannot set setting - empty key";
        return false;
    }

    auto it = m_setters.find(key);
    if (it != m_setters.end()) {
        bool result = it.value()(value.variant());
        if (result) {
            m_settings->save();
            qCDebug(lcDbusSettings) << "Setting" << key << "updated successfully";
        } else {
            qCWarning(lcDbusSettings) << "Failed to set setting:" << key;
        }
        return result;
    }

    qCWarning(lcDbusSettings) << "Setting key not found:" << key;
    return false;
}

QStringList SettingsAdaptor::getSettingKeys()
{
    return m_getters.keys();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Shader Registry D-Bus Methods
// ═══════════════════════════════════════════════════════════════════════════════

QVariantList SettingsAdaptor::availableShaders()
{
    auto *registry = ShaderRegistry::instance();
    return registry ? registry->availableShadersVariant() : QVariantList();
}

QVariantMap SettingsAdaptor::shaderInfo(const QString& shaderId)
{
    auto *registry = ShaderRegistry::instance();
    return registry ? registry->shaderInfo(shaderId) : QVariantMap();
}

QVariantMap SettingsAdaptor::defaultShaderParams(const QString& shaderId)
{
    auto *registry = ShaderRegistry::instance();
    return registry ? registry->defaultParams(shaderId) : QVariantMap();
}

bool SettingsAdaptor::shadersEnabled()
{
    auto *registry = ShaderRegistry::instance();
    return registry ? registry->shadersEnabled() : false;
}

bool SettingsAdaptor::userShadersEnabled()
{
    auto *registry = ShaderRegistry::instance();
    return registry ? registry->userShadersEnabled() : false;
}

QString SettingsAdaptor::userShaderDirectory()
{
    auto *registry = ShaderRegistry::instance();
    return registry ? registry->userShaderDirectory() : QString();
}

void SettingsAdaptor::openUserShaderDirectory()
{
    auto *registry = ShaderRegistry::instance();
    if (registry) {
        registry->openUserShaderDirectory();
    }
}

void SettingsAdaptor::refreshShaders()
{
    auto *registry = ShaderRegistry::instance();
    if (registry) {
        registry->refresh();
    }
}

} // namespace PlasmaZones
