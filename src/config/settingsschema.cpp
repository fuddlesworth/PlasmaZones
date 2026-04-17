// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "settingsschema.h"

#include "configdefaults.h"
#include "configmigration.h"

#include <QtGlobal>

namespace PlasmaZones {

PhosphorConfig::Schema buildSettingsSchema()
{
    PhosphorConfig::Schema s;
    s.version = ConfigSchemaVersion;
    s.versionKey = ConfigKeys::versionKey();

    appendShadersSchema(s);
    appendAppearanceSchema(s);

    return s;
}

// ─── Validator helpers ──────────────────────────────────────────────────────
// Common coercion patterns factored to keep group schemas readable. Return
// the same function-object type as KeyDef::validator.

namespace {
auto clampInt(int minVal, int maxVal)
{
    return [minVal, maxVal](const QVariant& v) -> QVariant {
        return qBound(minVal, v.toInt(), maxVal);
    };
}

auto clampDouble(double minVal, double maxVal)
{
    return [minVal, maxVal](const QVariant& v) -> QVariant {
        return qBound(minVal, v.toDouble(), maxVal);
    };
}

/// Fall back to @p fallback when the value is an invalid color. Defaults
/// in the schema are already valid, so this mostly protects against
/// garbage in the on-disk file.
auto validColorOr(QColor fallback)
{
    return [fallback](const QVariant& v) -> QVariant {
        const QColor c = v.value<QColor>();
        return c.isValid() ? QVariant::fromValue(c) : QVariant::fromValue(fallback);
    };
}
} // namespace

// ─── Shaders ────────────────────────────────────────────────────────────────
// Controls: overall effect toggle, frame rate, audio visualizer, bar count.

void appendShadersSchema(PhosphorConfig::Schema& schema)
{
    using CD = ConfigDefaults;
    schema.groups[CD::shadersGroup()] = {
        {CD::enabledKey(), CD::enableShaderEffects(), QMetaType::Bool},
        {CD::frameRateKey(),
         CD::shaderFrameRate(),
         QMetaType::Int,
         {},
         clampInt(CD::shaderFrameRateMin(), CD::shaderFrameRateMax())},
        {CD::audioVisualizerKey(), CD::enableAudioVisualizer(), QMetaType::Bool},
        {CD::audioSpectrumBarCountKey(),
         CD::audioSpectrumBarCount(),
         QMetaType::Int,
         {},
         clampInt(CD::audioSpectrumBarCountMin(), CD::audioSpectrumBarCountMax())},
    };
}

// ─── Appearance ─────────────────────────────────────────────────────────────
// Five sub-groups under Snapping.Appearance.*: Colors (system toggle + 3
// zone colors), Labels (font family/color/scale/weight + italic/underline/
// strikeout toggles), Opacity (active + inactive), Border (width + radius),
// plus Effects.Blur which shares the load function.

void appendAppearanceSchema(PhosphorConfig::Schema& schema)
{
    using CD = ConfigDefaults;

    schema.groups[CD::snappingAppearanceColorsGroup()] = {
        {CD::useSystemKey(), CD::useSystemColors(), QMetaType::Bool},
        {CD::highlightKey(), CD::highlightColor(), QMetaType::QColor, {}, validColorOr(CD::highlightColor())},
        {CD::inactiveKey(), CD::inactiveColor(), QMetaType::QColor, {}, validColorOr(CD::inactiveColor())},
        {CD::borderKey(), CD::borderColor(), QMetaType::QColor, {}, validColorOr(CD::borderColor())},
    };

    schema.groups[CD::snappingAppearanceLabelsGroup()] = {
        {CD::fontColorKey(), CD::labelFontColor(), QMetaType::QColor, {}, validColorOr(CD::labelFontColor())},
        {CD::fontFamilyKey(), CD::labelFontFamily(), QMetaType::QString},
        {CD::fontSizeScaleKey(),
         CD::labelFontSizeScale(),
         QMetaType::Double,
         {},
         clampDouble(CD::labelFontSizeScaleMin(), CD::labelFontSizeScaleMax())},
        {CD::fontWeightKey(),
         CD::labelFontWeight(),
         QMetaType::Int,
         {},
         clampInt(CD::labelFontWeightMin(), CD::labelFontWeightMax())},
        {CD::fontItalicKey(), CD::labelFontItalic(), QMetaType::Bool},
        {CD::fontUnderlineKey(), CD::labelFontUnderline(), QMetaType::Bool},
        {CD::fontStrikeoutKey(), CD::labelFontStrikeout(), QMetaType::Bool},
    };

    schema.groups[CD::snappingAppearanceOpacityGroup()] = {
        {CD::activeKey(),
         CD::activeOpacity(),
         QMetaType::Double,
         {},
         clampDouble(CD::activeOpacityMin(), CD::activeOpacityMax())},
        {CD::inactiveKey(),
         CD::inactiveOpacity(),
         QMetaType::Double,
         {},
         clampDouble(CD::inactiveOpacityMin(), CD::inactiveOpacityMax())},
    };

    schema.groups[CD::snappingAppearanceBorderGroup()] = {
        {CD::widthKey(), CD::borderWidth(), QMetaType::Int, {}, clampInt(CD::borderWidthMin(), CD::borderWidthMax())},
        {CD::radiusKey(),
         CD::borderRadius(),
         QMetaType::Int,
         {},
         clampInt(CD::borderRadiusMin(), CD::borderRadiusMax())},
    };

    // Effects group currently holds only the blur toggle — sharing the
    // "Appearance" load function on the PZ side made it convenient to
    // migrate here together.
    schema.groups[CD::snappingEffectsGroup()] = {
        {CD::blurKey(), CD::enableBlur(), QMetaType::Bool},
    };
}

} // namespace PlasmaZones
