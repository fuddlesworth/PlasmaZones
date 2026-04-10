// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include "../common/animationstyle.h"
#include "../common/springparams.h"
#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <optional>

namespace PlasmaZones {

// SpringParams is defined in src/common/springparams.h (shared with kwin-effect).
// Serialization methods are declared here (need ConfigDefaults for bounds clamping).

/// Serialize SpringParams to JSON
PLASMAZONES_EXPORT QJsonObject springParamsToJson(const SpringParams& sp);

/// Deserialize SpringParams from JSON with bounds clamping
PLASMAZONES_EXPORT SpringParams springParamsFromJson(const QJsonObject& obj);

/**
 * @brief Per-event animation configuration with optional fields for inheritance
 *
 * Fields wrapped in std::optional — nullopt means "inherit from parent".
 * Use AnimationProfileTree::resolvedProfile() to get a fully populated profile.
 */
struct PLASMAZONES_EXPORT AnimationProfile
{
    std::optional<bool> enabled;
    std::optional<TimingMode> timingMode;
    std::optional<int> duration; ///< Milliseconds (ignored for spring)
    std::optional<QString> easingCurve; ///< Bezier "x1,y1,x2,y2" or named curve
    std::optional<SpringParams> spring;
    std::optional<AnimationStyle> style;
    std::optional<qreal> styleParam; ///< Style-specific (e.g., minScale for popin)
    std::optional<QString> shaderPath; ///< For AnimationStyle::Custom
    std::optional<QVariantMap> shaderParams; ///< Uniform overrides for custom shader
    std::optional<QString> geometryMode; ///< C++ geometry mode: "morph", "popin", "slidefade"

    /// True if all fields are nullopt (fully inherits from parent)
    bool isEmpty() const;

    /// Merge another profile on top of this one (other's set fields override this)
    void mergeFrom(const AnimationProfile& other);

    /// Fill nullopt fields from defaults (leaves already-set fields untouched)
    void fillDefaultsFrom(const AnimationProfile& defaults);

    QJsonObject toJson() const;
    static AnimationProfile fromJson(const QJsonObject& obj);

    bool operator==(const AnimationProfile& other) const;
};

/**
 * @brief Hierarchical animation event tree with per-node profiles and inheritance
 *
 * Tree structure (hardcoded, 4 levels):
 *   global
 *     ├── windowGeometry
 *     │   ├── snap → snapIn, snapOut, snapResize
 *     │   ├── layoutSwitch → layoutSwitchIn, layoutSwitchOut
 *     │   └── autotileBorder → borderIn, borderOut
 *     ├── overlay
 *     │   ├── zoneHighlight → zoneHighlightIn, zoneHighlightOut
 *     │   ├── osd → layoutOsdIn, layoutOsdOut, navigationOsdIn, navigationOsdOut
 *     │   ├── popup → layoutPickerIn, layoutPickerOut, snapAssistIn, snapAssistOut
 *     │   ├── zoneSelector → zoneSelectorIn, zoneSelectorOut
 *     │   └── preview → previewIn, previewOut
 *     └── dim
 *
 * Empty/missing nodes inherit all values from their parent.
 */
class PLASMAZONES_EXPORT AnimationProfileTree
{
public:
    AnimationProfileTree();

    /// Resolve a fully-inherited profile for an event (walks up tree, merging)
    AnimationProfile resolvedProfile(const QString& eventName) const;

    /// Get/set raw (non-inherited) profile for an event node
    AnimationProfile rawProfile(const QString& eventName) const;
    void setProfile(const QString& eventName, const AnimationProfile& profile);

    /// Remove per-event override (reverts to full inheritance)
    void clearProfile(const QString& eventName);

    /// Serialization
    QJsonObject toJson() const;
    static AnimationProfileTree fromJson(const QJsonObject& obj);

    /// Tree structure queries
    static QString parentOf(const QString& eventName);
    static QStringList childrenOf(const QString& eventName);
    static QStringList allEventNames();
    static bool isValidEventName(const QString& name);

    bool operator==(const AnimationProfileTree& other) const;

    /// Create a default tree with sensible global defaults
    static AnimationProfileTree defaultTree();

private:
    QHash<QString, AnimationProfile> m_profiles;
};

/// Named constants for animation event names used across the codebase.
/// Use these instead of inline QStringLiteral("snapIn") etc.
namespace AnimationEvents {

// ── Root ──
inline QString global()
{
    return QStringLiteral("global");
}

// ── Domain nodes ──
inline QString windowGeometry()
{
    return QStringLiteral("windowGeometry");
}
inline QString overlay()
{
    return QStringLiteral("overlay");
}

// ── Window geometry: snap ──
inline QString snap()
{
    return QStringLiteral("snap");
}
inline QString snapIn()
{
    return QStringLiteral("snapIn");
}
inline QString snapOut()
{
    return QStringLiteral("snapOut");
}
inline QString snapResize()
{
    return QStringLiteral("snapResize");
}

// ── Window geometry: layout switch ──
inline QString layoutSwitch()
{
    return QStringLiteral("layoutSwitch");
}
inline QString layoutSwitchIn()
{
    return QStringLiteral("layoutSwitchIn");
}
inline QString layoutSwitchOut()
{
    return QStringLiteral("layoutSwitchOut");
}

// ── Window geometry: autotile border ──
inline QString autotileBorder()
{
    return QStringLiteral("autotileBorder");
}
inline QString borderIn()
{
    return QStringLiteral("borderIn");
}
inline QString borderOut()
{
    return QStringLiteral("borderOut");
}

// ── Overlay: zone highlight ──
inline QString zoneHighlight()
{
    return QStringLiteral("zoneHighlight");
}
inline QString zoneHighlightIn()
{
    return QStringLiteral("zoneHighlightIn");
}
inline QString zoneHighlightOut()
{
    return QStringLiteral("zoneHighlightOut");
}

// ── Overlay: OSD ──
inline QString osd()
{
    return QStringLiteral("osd");
}
inline QString layoutOsdIn()
{
    return QStringLiteral("layoutOsdIn");
}
inline QString layoutOsdOut()
{
    return QStringLiteral("layoutOsdOut");
}
inline QString navigationOsdIn()
{
    return QStringLiteral("navigationOsdIn");
}
inline QString navigationOsdOut()
{
    return QStringLiteral("navigationOsdOut");
}

// ── Overlay: popup ──
inline QString popup()
{
    return QStringLiteral("popup");
}
inline QString layoutPickerIn()
{
    return QStringLiteral("layoutPickerIn");
}
inline QString layoutPickerOut()
{
    return QStringLiteral("layoutPickerOut");
}
inline QString snapAssistIn()
{
    return QStringLiteral("snapAssistIn");
}
inline QString snapAssistOut()
{
    return QStringLiteral("snapAssistOut");
}

// ── Overlay: zone selector ──
inline QString zoneSelector()
{
    return QStringLiteral("zoneSelector");
}
inline QString zoneSelectorIn()
{
    return QStringLiteral("zoneSelectorIn");
}
inline QString zoneSelectorOut()
{
    return QStringLiteral("zoneSelectorOut");
}

// ── Overlay: preview ──
inline QString preview()
{
    return QStringLiteral("preview");
}
inline QString previewIn()
{
    return QStringLiteral("previewIn");
}
inline QString previewOut()
{
    return QStringLiteral("previewOut");
}

// ── Standalone ──
inline QString dim()
{
    return QStringLiteral("dim");
}

} // namespace AnimationEvents

class ISettings;

/// Resolve an animation profile for the given event from settings, or return an empty profile.
/// Shared helper to avoid duplicating the resolve-or-default pattern across SnapEngine / WTA.
PLASMAZONES_EXPORT AnimationProfile resolvedProfileOrDefault(ISettings* settings, const QString& eventName);

} // namespace PlasmaZones
