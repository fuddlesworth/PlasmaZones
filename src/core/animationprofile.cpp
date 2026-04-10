// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "animationprofile.h"
#include "interfaces.h"
#include "../common/animationtreedata.h"
#include "../config/configdefaults.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QtMath>

namespace PlasmaZones {

// ═══════════════════════════════════════════════════════════════════════════════
// SpringParams serialization (operator== is inline in springparams.h)
// ═══════════════════════════════════════════════════════════════════════════════

QJsonObject springParamsToJson(const SpringParams& sp)
{
    QJsonObject obj;
    obj[ConfigDefaults::animProfileSpringDampingKey()] = sp.dampingRatio;
    obj[ConfigDefaults::animProfileSpringStiffnessKey()] = sp.stiffness;
    obj[ConfigDefaults::animProfileSpringEpsilonKey()] = sp.epsilon;
    return obj;
}

SpringParams springParamsFromJson(const QJsonObject& obj)
{
    SpringParams sp;
    if (obj.contains(ConfigDefaults::animProfileSpringDampingKey()))
        sp.dampingRatio =
            qBound(ConfigDefaults::springDampingRatioMin(),
                   obj[ConfigDefaults::animProfileSpringDampingKey()].toDouble(ConfigDefaults::springDampingRatio()),
                   ConfigDefaults::springDampingRatioMax());
    if (obj.contains(ConfigDefaults::animProfileSpringStiffnessKey()))
        sp.stiffness =
            qBound(ConfigDefaults::springStiffnessMin(),
                   obj[ConfigDefaults::animProfileSpringStiffnessKey()].toDouble(ConfigDefaults::springStiffness()),
                   ConfigDefaults::springStiffnessMax());
    if (obj.contains(ConfigDefaults::animProfileSpringEpsilonKey()))
        sp.epsilon =
            qBound(ConfigDefaults::springEpsilonMin(),
                   obj[ConfigDefaults::animProfileSpringEpsilonKey()].toDouble(ConfigDefaults::springEpsilon()),
                   ConfigDefaults::springEpsilonMax());
    return sp;
}

// ═══════════════════════════════════════════════════════════════════════════════
// AnimationProfile
// ═══════════════════════════════════════════════════════════════════════════════

bool AnimationProfile::isEmpty() const
{
    return !enabled.has_value() && !timingMode.has_value() && !duration.has_value() && !easingCurve.has_value()
        && !spring.has_value() && !style.has_value() && !styleParam.has_value() && !shaderPath.has_value()
        && !shaderParams.has_value() && !geometryMode.has_value();
}

namespace {
template<typename T>
void mergeOptionalField(std::optional<T>& target, const std::optional<T>& source, bool overrideExisting)
{
    if (overrideExisting) {
        if (source.has_value())
            target = source;
    } else {
        if (!target.has_value())
            target = source;
    }
}
} // namespace

void AnimationProfile::mergeFrom(const AnimationProfile& other)
{
    mergeOptionalField(enabled, other.enabled, true);
    mergeOptionalField(timingMode, other.timingMode, true);
    mergeOptionalField(duration, other.duration, true);
    mergeOptionalField(easingCurve, other.easingCurve, true);
    mergeOptionalField(spring, other.spring, true);
    mergeOptionalField(style, other.style, true);
    mergeOptionalField(styleParam, other.styleParam, true);
    mergeOptionalField(shaderPath, other.shaderPath, true);
    mergeOptionalField(shaderParams, other.shaderParams, true);
    mergeOptionalField(geometryMode, other.geometryMode, true);
}

void AnimationProfile::fillDefaultsFrom(const AnimationProfile& defaults)
{
    mergeOptionalField(enabled, defaults.enabled, false);
    mergeOptionalField(timingMode, defaults.timingMode, false);
    mergeOptionalField(duration, defaults.duration, false);
    mergeOptionalField(easingCurve, defaults.easingCurve, false);
    mergeOptionalField(spring, defaults.spring, false);
    mergeOptionalField(style, defaults.style, false);
    mergeOptionalField(styleParam, defaults.styleParam, false);
    mergeOptionalField(shaderPath, defaults.shaderPath, false);
    mergeOptionalField(shaderParams, defaults.shaderParams, false);
    mergeOptionalField(geometryMode, defaults.geometryMode, false);
}

QJsonObject AnimationProfile::toJson() const
{
    QJsonObject obj;
    if (enabled.has_value())
        obj[ConfigDefaults::animProfileEnabledKey()] = *enabled;
    if (timingMode.has_value())
        obj[ConfigDefaults::animProfileTimingModeKey()] = static_cast<int>(*timingMode);
    if (duration.has_value())
        obj[ConfigDefaults::animProfileDurationKey()] = *duration;
    if (easingCurve.has_value())
        obj[ConfigDefaults::animProfileEasingCurveKey()] = *easingCurve;
    if (spring.has_value())
        obj[ConfigDefaults::animProfileSpringKey()] = springParamsToJson(*spring);
    if (style.has_value())
        obj[ConfigDefaults::animProfileStyleKey()] = animationStyleToString(*style);
    if (styleParam.has_value())
        obj[ConfigDefaults::animProfileStyleParamKey()] = *styleParam;
    if (shaderPath.has_value())
        obj[ConfigDefaults::animProfileShaderPathKey()] = *shaderPath;
    if (shaderParams.has_value())
        obj[ConfigDefaults::animProfileShaderParamsKey()] = QJsonObject::fromVariantMap(*shaderParams);
    if (geometryMode.has_value())
        obj[ConfigDefaults::animProfileGeometryKey()] = *geometryMode;
    return obj;
}

AnimationProfile AnimationProfile::fromJson(const QJsonObject& obj)
{
    AnimationProfile p;
    if (obj.contains(ConfigDefaults::animProfileEnabledKey()))
        p.enabled = obj[ConfigDefaults::animProfileEnabledKey()].toBool();
    if (obj.contains(ConfigDefaults::animProfileTimingModeKey())) {
        const int tm = obj[ConfigDefaults::animProfileTimingModeKey()].toInt();
        if (tm >= 0 && tm <= static_cast<int>(TimingMode::Spring))
            p.timingMode = static_cast<TimingMode>(tm);
    }
    if (obj.contains(ConfigDefaults::animProfileDurationKey()))
        p.duration =
            qBound(ConfigDefaults::animProfileDurationMin(), obj[ConfigDefaults::animProfileDurationKey()].toInt(),
                   ConfigDefaults::animProfileDurationMax());
    if (obj.contains(ConfigDefaults::animProfileEasingCurveKey())) {
        const QString curve = obj[ConfigDefaults::animProfileEasingCurveKey()].toString();
        // Validate: either a known named curve (with optional :params suffix)
        // or exactly 4 comma-separated doubles (cubic bezier control points)
        static const QStringList knownNamedCurves = {
            QStringLiteral("elastic-in"), QStringLiteral("elastic-out"), QStringLiteral("elastic-in-out"),
            QStringLiteral("bounce-in"),  QStringLiteral("bounce-out"),  QStringLiteral("bounce-in-out"),
        };
        bool valid = false;
        // Extract the curve name (before any ':' params suffix)
        const QString curveName = curve.section(QLatin1Char(':'), 0, 0).trimmed();
        if (knownNamedCurves.contains(curveName)) {
            valid = true;
        } else {
            const auto parts = curve.split(QLatin1Char(','));
            if (parts.size() == 4) {
                valid = true;
                for (const auto& part : parts) {
                    bool ok = false;
                    part.trimmed().toDouble(&ok);
                    if (!ok) {
                        valid = false;
                        break;
                    }
                }
            }
        }
        if (valid)
            p.easingCurve = curve;
    }
    if (obj.contains(ConfigDefaults::animProfileSpringKey()))
        p.spring = springParamsFromJson(obj[ConfigDefaults::animProfileSpringKey()].toObject());
    if (obj.contains(ConfigDefaults::animProfileStyleKey()))
        p.style = animationStyleFromString(obj[ConfigDefaults::animProfileStyleKey()].toString());
    if (obj.contains(ConfigDefaults::animProfileStyleParamKey())) {
        const double sp = obj[ConfigDefaults::animProfileStyleParamKey()].toDouble();
        p.styleParam = std::isfinite(sp)
            ? qBound(ConfigDefaults::animProfileStyleParamMin(), sp, ConfigDefaults::animProfileStyleParamMax())
            : ConfigDefaults::animProfileStyleParamMin();
    }
    if (obj.contains(ConfigDefaults::animProfileShaderPathKey())) {
        const QString sp = obj[ConfigDefaults::animProfileShaderPathKey()].toString();
        // Reject empty, path-traversal (".."), and absolute paths (starting with "/")
        if (!sp.isEmpty() && !sp.contains(QLatin1String("..")) && !sp.startsWith(QLatin1Char('/')))
            p.shaderPath = sp;
    }
    if (obj.contains(ConfigDefaults::animProfileShaderParamsKey()))
        p.shaderParams = obj[ConfigDefaults::animProfileShaderParamsKey()].toObject().toVariantMap();
    if (obj.contains(ConfigDefaults::animProfileGeometryKey())) {
        const QString geo = obj[ConfigDefaults::animProfileGeometryKey()].toString();
        // Validate against known geometry modes (shared list in animationtreedata.h)
        for (int i = 0; i < ValidGeometryModeCount; ++i) {
            if (geo == QLatin1String(ValidGeometryModes[i])) {
                p.geometryMode = geo;
                break;
            }
        }
    }
    return p;
}

bool AnimationProfile::operator==(const AnimationProfile& other) const
{
    if (enabled != other.enabled || timingMode != other.timingMode || duration != other.duration
        || easingCurve != other.easingCurve || spring != other.spring || style != other.style
        || shaderPath != other.shaderPath || shaderParams != other.shaderParams || geometryMode != other.geometryMode)
        return false;

    // Fuzzy compare for optional qreal
    if (styleParam.has_value() != other.styleParam.has_value())
        return false;
    if (styleParam.has_value() && !qFuzzyCompare(1.0 + *styleParam, 1.0 + *other.styleParam))
        return false;

    return true;
}

// ═══════════════════════════════════════════════════════════════════════════════
// AnimationProfileTree — static tree structure
// ═══════════════════════════════════════════════════════════════════════════════

// Tree hierarchy data is in src/common/animationtreedata.h (shared with kwin-effect).

static const QHash<QString, QString>& parentMap()
{
    static const QHash<QString, QString> map = [] {
        QHash<QString, QString> m;
        for (const auto& edge : AnimationTreeEdges)
            m[QString::fromLatin1(edge.child)] = QString::fromLatin1(edge.parent);
        return m;
    }();
    return map;
}

static const QHash<QString, QStringList>& childrenMap()
{
    static const QHash<QString, QStringList> map = [] {
        QHash<QString, QStringList> m;
        for (const auto& edge : AnimationTreeEdges)
            m[QString::fromLatin1(edge.parent)].append(QString::fromLatin1(edge.child));
        return m;
    }();
    return map;
}

static const QStringList& allNames()
{
    static const QStringList names = [] {
        QStringList list;
        list.append(QStringLiteral("global"));
        for (const auto& edge : AnimationTreeEdges)
            list.append(QString::fromLatin1(edge.child));
        return list;
    }();
    return names;
}

// Resolve a profile from a tree by walking the chain WITHOUT default-fill.
// Used internally to avoid infinite recursion in resolvedProfile().
static AnimationProfile resolveFromTreeRaw(const AnimationProfileTree& tree, const QString& eventName)
{
    if (!AnimationProfileTree::isValidEventName(eventName))
        return {};

    QStringList chain;
    QString current = eventName;
    while (!current.isEmpty() && chain.size() < 10) {
        chain.prepend(current);
        current = AnimationProfileTree::parentOf(current);
    }

    AnimationProfile resolved;
    for (const auto& name : chain)
        resolved.mergeFrom(tree.rawProfile(name));
    return resolved;
}

// Cached default tree at file scope to avoid per-call allocation.
static const AnimationProfileTree& cachedDefaultTree()
{
    static const AnimationProfileTree tree = AnimationProfileTree::defaultTree();
    return tree;
}

// ═══════════════════════════════════════════════════════════════════════════════
// AnimationProfileTree — implementation
// ═══════════════════════════════════════════════════════════════════════════════

AnimationProfileTree::AnimationProfileTree() = default;

AnimationProfile AnimationProfileTree::resolvedProfile(const QString& eventName) const
{
    if (!isValidEventName(eventName))
        return {};

    // Walk from root down to eventName, merging profiles
    QStringList chain;
    QString current = eventName;
    while (!current.isEmpty() && chain.size() < 10) {
        chain.prepend(current);
        current = parentOf(current);
    }

    AnimationProfile resolved;
    for (const auto& name : chain) {
        auto it = m_profiles.find(name);
        if (it != m_profiles.end())
            resolved.mergeFrom(*it);
    }

    // Fill remaining nullopt fields from the built-in default tree.
    // This runs unconditionally so that a user who sets only enabled=true
    // on "global" still gets defaults for timingMode, duration, etc.
    // Uses resolveFromTreeRaw() to avoid infinite recursion (the default tree
    // doesn't need its own defaults filled).
    const auto def = resolveFromTreeRaw(cachedDefaultTree(), eventName);
    resolved.fillDefaultsFrom(def);

    return resolved;
}

AnimationProfile AnimationProfileTree::rawProfile(const QString& eventName) const
{
    return m_profiles.value(eventName);
}

void AnimationProfileTree::setProfile(const QString& eventName, const AnimationProfile& profile)
{
    if (!isValidEventName(eventName))
        return;
    if (profile.isEmpty())
        m_profiles.remove(eventName);
    else
        m_profiles[eventName] = profile;
}

void AnimationProfileTree::clearProfile(const QString& eventName)
{
    m_profiles.remove(eventName);
}

QJsonObject AnimationProfileTree::toJson() const
{
    QJsonObject obj;
    for (const auto& name : allEventNames()) {
        auto it = m_profiles.find(name);
        if (it != m_profiles.end() && !it->isEmpty())
            obj[name] = it->toJson();
    }
    return obj;
}

AnimationProfileTree AnimationProfileTree::fromJson(const QJsonObject& obj)
{
    AnimationProfileTree tree;
    for (const auto& name : allEventNames()) {
        if (obj.contains(name)) {
            auto profile = AnimationProfile::fromJson(obj[name].toObject());
            if (!profile.isEmpty())
                tree.m_profiles[name] = profile;
        }
    }
    return tree;
}

QString AnimationProfileTree::parentOf(const QString& eventName)
{
    return parentMap().value(eventName);
}

QStringList AnimationProfileTree::childrenOf(const QString& eventName)
{
    return childrenMap().value(eventName);
}

QStringList AnimationProfileTree::allEventNames()
{
    return allNames();
}

bool AnimationProfileTree::isValidEventName(const QString& name)
{
    static const QSet<QString> nameSet(allNames().constBegin(), allNames().constEnd());
    return nameSet.contains(name);
}

bool AnimationProfileTree::operator==(const AnimationProfileTree& other) const
{
    return m_profiles == other.m_profiles;
}

AnimationProfileTree AnimationProfileTree::defaultTree()
{
    AnimationProfileTree tree;

    // Global defaults — all other nodes inherit from here
    AnimationProfile global;
    global.enabled = true;
    global.timingMode = TimingMode::Easing;
    global.duration = 300;
    global.easingCurve = QStringLiteral("0.33,1.00,0.68,1.00"); // ease-out cubic
    global.spring = SpringParams{ConfigDefaults::springDampingRatio(), ConfigDefaults::springStiffness(),
                                 ConfigDefaults::springEpsilon()};
    global.style = AnimationStyle::Morph;
    tree.setProfile(AnimationEvents::global(), global);

    // Window geometry domain — inherits global, sets Morph as default style
    AnimationProfile windowGeo;
    windowGeo.style = AnimationStyle::Morph;
    tree.setProfile(AnimationEvents::windowGeometry(), windowGeo);

    // Overlay domain — shorter duration, ScaleIn as default style
    AnimationProfile overlayDefaults;
    overlayDefaults.duration = 150;
    overlayDefaults.style = AnimationStyle::ScaleIn;
    overlayDefaults.easingCurve = QStringLiteral("0.16,1.00,0.30,1.00"); // ease-out quart
    tree.setProfile(AnimationEvents::overlay(), overlayDefaults);

    // Zone highlight uses FadeIn (opacity-only transitions)
    AnimationProfile zoneHl;
    zoneHl.style = AnimationStyle::FadeIn;
    tree.setProfile(AnimationEvents::zoneHighlight(), zoneHl);

    // Dim uses FadeIn
    AnimationProfile dimDefaults;
    dimDefaults.style = AnimationStyle::FadeIn;
    dimDefaults.duration = 200;
    tree.setProfile(AnimationEvents::dim(), dimDefaults);

    return tree;
}

AnimationProfile resolvedProfileOrDefault(ISettings* settings, const QString& eventName)
{
    if (settings) {
        return settings->animationProfileTree().resolvedProfile(eventName);
    }
    return AnimationProfileTree::defaultTree().resolvedProfile(eventName);
}

} // namespace PlasmaZones
