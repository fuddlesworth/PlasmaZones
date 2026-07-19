// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "settingsvaluelabels.h"

#include "../phosphor_i18n.h"
#include "configdefaults.h"
#include "configkeys.h"
#include "settingsschema.h"

#include <QHash>
#include <QVariantMap>

namespace PlasmaZones {
namespace SettingsValueLabels {

namespace {

using CD = ConfigDefaults;

/// (group, key) joined for hash lookup. The separator is a character no group
/// or key contains, so a joined pair cannot collide with a differently-split
/// one ("A.B" + "C" must not equal "A" + "B.C").
QString pairKey(const QString& group, const QString& key)
{
    return group + QLatin1Char('\x1f') + key;
}

/// Labels for every declared choice, keyed (group, key) then token.
///
/// Built once. The strings go through PhosphorI18n::tr at build time rather
/// than being stored untranslated, which is why this is a function-local static
/// rather than a namespace-scope table: it must not run before the translator
/// is installed.
const QHash<QString, QHash<QString, QString>>& enumLabelTable()
{
    static const QHash<QString, QHash<QString, QString>> table = [] {
        QHash<QString, QHash<QString, QString>> t;

        // ── Rendering / audio: token-valued keys ─────────────────────────────
        t.insert(pairKey(CD::renderingGroup(), CD::backendKey()),
                 {
                     {QStringLiteral("auto"), PhosphorI18n::tr("Automatic")},
                     {QStringLiteral("vulkan"), PhosphorI18n::tr("Vulkan")},
                     {QStringLiteral("opengl"), PhosphorI18n::tr("OpenGL")},
                 });
        t.insert(pairKey(CD::shadersAudioGroup(), CD::channelModeKey()),
                 {
                     {QStringLiteral("stereo"), PhosphorI18n::tr("Stereo")},
                     {QStringLiteral("mono-average"), PhosphorI18n::tr("Mono (average)")},
                     {QStringLiteral("mono-left"), PhosphorI18n::tr("Mono (left)")},
                     {QStringLiteral("mono-right"), PhosphorI18n::tr("Mono (right)")},
                 });
        // Ten legal tokens, of which the picker offers three. The rest are
        // reachable only by hand-editing the config, so naming them here is the
        // only way a reader ever sees what they mean.
        t.insert(pairKey(CD::shadersAudioGroup(), CD::inputMethodKey()),
                 {
                     {QStringLiteral("auto"), PhosphorI18n::tr("Automatic")},
                     {QStringLiteral("pipewire"), PhosphorI18n::tr("PipeWire")},
                     {QStringLiteral("pulse"), PhosphorI18n::tr("PulseAudio")},
                     {QStringLiteral("alsa"), PhosphorI18n::tr("ALSA")},
                     {QStringLiteral("jack"), PhosphorI18n::tr("JACK")},
                     {QStringLiteral("sndio"), PhosphorI18n::tr("sndio")},
                     {QStringLiteral("oss"), PhosphorI18n::tr("OSS")},
                     {QStringLiteral("portaudio"), PhosphorI18n::tr("PortAudio")},
                     {QStringLiteral("fifo"), PhosphorI18n::tr("FIFO")},
                     {QStringLiteral("shmem"), PhosphorI18n::tr("Shared memory")},
                 });

        // ── Window appearance: the three scopes share one token set ──────────
        const QHash<QString, QString> scopeLabels{
            {QStringLiteral("tiled"), PhosphorI18n::tr("Tiled and snapped windows")},
            {QStringLiteral("normal"), PhosphorI18n::tr("All normal windows")},
            {QStringLiteral("all"), PhosphorI18n::tr("All windows")},
        };
        t.insert(pairKey(CD::windowsAppearanceGroup(), CD::borderScopeKey()), scopeLabels);
        t.insert(pairKey(CD::windowsAppearanceGroup(), CD::titleBarScopeKey()), scopeLabels);
        t.insert(pairKey(CD::windowsAppearanceGroup(), CD::opacityTintScopeKey()), scopeLabels);

        // ── Snapping overlay ────────────────────────────────────────────────
        t.insert(pairKey(CD::snappingEffectsGroup(), CD::osdStyleKey()),
                 {
                     {QStringLiteral("none"), PhosphorI18n::tr("None")},
                     {QStringLiteral("text"), PhosphorI18n::tr("Text only")},
                     {QStringLiteral("preview"), PhosphorI18n::tr("Visual preview")},
                 });
        t.insert(pairKey(CD::snappingEffectsGroup(), CD::overlayDisplayModeKey()),
                 {
                     {QStringLiteral("zoneRectangles"), PhosphorI18n::tr("Full zone highlight")},
                     {QStringLiteral("layoutPreview"), PhosphorI18n::tr("Compact preview")},
                 });

        // ── Zone selector ───────────────────────────────────────────────────
        t.insert(pairKey(CD::snappingZoneSelectorGroup(), CD::positionKey()),
                 {
                     {QStringLiteral("topLeft"), PhosphorI18n::tr("Top-Left")},
                     {QStringLiteral("top"), PhosphorI18n::tr("Top")},
                     {QStringLiteral("topRight"), PhosphorI18n::tr("Top-Right")},
                     {QStringLiteral("left"), PhosphorI18n::tr("Left")},
                     {QStringLiteral("center"), PhosphorI18n::tr("Center")},
                     {QStringLiteral("right"), PhosphorI18n::tr("Right")},
                     {QStringLiteral("bottomLeft"), PhosphorI18n::tr("Bottom-Left")},
                     {QStringLiteral("bottom"), PhosphorI18n::tr("Bottom")},
                     {QStringLiteral("bottomRight"), PhosphorI18n::tr("Bottom-Right")},
                 });
        t.insert(pairKey(CD::snappingZoneSelectorGroup(), CD::layoutModeKey()),
                 {
                     {QStringLiteral("grid"), PhosphorI18n::tr("Grid")},
                     {QStringLiteral("horizontal"), PhosphorI18n::tr("Horizontal")},
                     {QStringLiteral("vertical"), PhosphorI18n::tr("Vertical")},
                 });
        // Only two values, though the picker shows five buttons: four of them
        // write Manual and differ by the sibling PreviewWidth. Naming the
        // stored value honestly beats guessing at which button was pressed.
        t.insert(pairKey(CD::snappingZoneSelectorGroup(), CD::sizeModeKey()),
                 {
                     {QStringLiteral("auto"), PhosphorI18n::tr("Auto")},
                     {QStringLiteral("manual"), PhosphorI18n::tr("Manual")},
                 });

        // ── Drag modifiers. No settings page exposes these, so the enumerator
        //    names are the only prior art for the wording. ──────────────────
        const QHash<QString, QString> dragModifierLabels{
            {QStringLiteral("disabled"), PhosphorI18n::tr("Disabled")},
            {QStringLiteral("shift"), PhosphorI18n::tr("Shift")},
            {QStringLiteral("ctrl"), PhosphorI18n::tr("Ctrl")},
            {QStringLiteral("alt"), PhosphorI18n::tr("Alt")},
            {QStringLiteral("meta"), PhosphorI18n::tr("Meta")},
            {QStringLiteral("ctrlAlt"), PhosphorI18n::tr("Ctrl + Alt")},
            {QStringLiteral("ctrlShift"), PhosphorI18n::tr("Ctrl + Shift")},
            {QStringLiteral("altShift"), PhosphorI18n::tr("Alt + Shift")},
            {QStringLiteral("alwaysActive"), PhosphorI18n::tr("Always active")},
            {QStringLiteral("altMeta"), PhosphorI18n::tr("Alt + Meta")},
            {QStringLiteral("ctrlAltMeta"), PhosphorI18n::tr("Ctrl + Alt + Meta")},
        };
        // Only the zone-span key uses this enum. Editor.FillOnDrop/Modifier has
        // the same accessor name but stores a Qt::KeyboardModifier bitmask, so
        // it resolves as a trigger instead (see the descriptor table below).
        t.insert(pairKey(CD::snappingBehaviorZoneSpanGroup(), CD::modifierKey()), dragModifierLabels);

        // ── Sticky window handling, declared on two independent groups ───────
        const QHash<QString, QString> stickyLabels{
            {QStringLiteral("treatAsNormal"), PhosphorI18n::tr("Treat as normal")},
            {QStringLiteral("restoreOnly"), PhosphorI18n::tr("Restore only")},
            {QStringLiteral("ignoreAll"), PhosphorI18n::tr("Ignore all")},
        };
        t.insert(pairKey(CD::snappingBehaviorWindowHandlingGroup(), CD::stickyWindowHandlingKey()), stickyLabels);
        t.insert(pairKey(CD::tilingBehaviorGroup(), CD::stickyWindowHandlingKey()), stickyLabels);

        // ── Tiling. "float" appears under two keys meaning different things,
        //    which is why this table is keyed by (group, key) and not token. ──
        t.insert(pairKey(CD::tilingBehaviorGroup(), CD::insertPositionKey()),
                 {
                     {QStringLiteral("end"), PhosphorI18n::tr("After existing")},
                     {QStringLiteral("afterFocused"), PhosphorI18n::tr("After focused")},
                     {QStringLiteral("asMaster"), PhosphorI18n::tr("As main window")},
                 });
        t.insert(pairKey(CD::tilingBehaviorGroup(), CD::dragBehaviorKey()),
                 {
                     {QStringLiteral("float"), PhosphorI18n::tr("Float on drag")},
                     {QStringLiteral("reorder"), PhosphorI18n::tr("Reorder on drag")},
                 });
        t.insert(pairKey(CD::tilingBehaviorGroup(), CD::overflowBehaviorKey()),
                 {
                     {QStringLiteral("float"), PhosphorI18n::tr("Float excess")},
                     {QStringLiteral("unlimited"), PhosphorI18n::tr("Unlimited")},
                 });
        return t;
    }();
    return table;
}

/// Presentation for the non-enum keys: units, display scaling, and the id kinds
/// the view resolves against live data.
const QHash<QString, ValueDescriptor>& descriptorTable()
{
    static const QHash<QString, ValueDescriptor> table = [] {
        QHash<QString, ValueDescriptor> t;

        const QString px = QStringLiteral("px");
        const QString ms = QStringLiteral("ms");
        const QString pct = QStringLiteral("%");

        const auto number = [](const QString& unit, double scale = 1.0, bool zeroOff = false) {
            return ValueDescriptor{ValueKind::Number, unit, scale, zeroOff};
        };
        const auto idKind = [](ValueKind kind) {
            return ValueDescriptor{kind, {}, 1.0, false};
        };

        // ── Ratios persisted 0.0-1.0 that read as a percentage ──────────────
        t.insert(pairKey(CD::snappingZonesOpacityGroup(), CD::activeKey()), number(pct, 100.0));
        t.insert(pairKey(CD::snappingZonesOpacityGroup(), CD::inactiveKey()), number(pct, 100.0));
        t.insert(pairKey(CD::snappingZonesLabelsGroup(), CD::fontSizeScaleKey()), number(pct, 100.0));
        t.insert(pairKey(CD::windowsAppearanceGroup(), CD::opacityKey()), number(pct, 100.0));
        t.insert(pairKey(CD::windowsAppearanceGroup(), CD::tintStrengthKey()), number(pct, 100.0));
        t.insert(pairKey(CD::tilingAlgorithmGroup(), CD::splitRatioKey()), number(pct, 100.0));
        t.insert(pairKey(CD::tilingAlgorithmGroup(), CD::splitRatioStepKey()), number(pct, 100.0));

        // ── Pixel quantities ────────────────────────────────────────────────
        t.insert(pairKey(CD::snappingZonesBorderGroup(), CD::widthKey()), number(px));
        t.insert(pairKey(CD::snappingZonesBorderGroup(), CD::radiusKey()), number(px));
        t.insert(pairKey(CD::snappingGapsGroup(), CD::adjacentThresholdKey()), number(px));
        t.insert(pairKey(CD::windowsAppearanceGroup(), CD::widthKey()), number(px));
        t.insert(pairKey(CD::windowsAppearanceGroup(), CD::radiusKey()), number(px));
        t.insert(pairKey(CD::snappingZoneSelectorGroup(), CD::triggerDistanceKey()), number(px));
        t.insert(pairKey(CD::snappingZoneSelectorGroup(), CD::previewWidthKey()), number(px));

        // ── Durations. IdleTimeoutSec is the one stored in seconds. ─────────
        t.insert(pairKey(CD::windowsAppearanceGroup(), CD::focusFadeDurationKey()), number(ms));
        t.insert(pairKey(CD::decorationsPerformanceGroup(), CD::idleTimeoutSecKey()), number(QStringLiteral("s")));

        // ── Audio / shader scalars ──────────────────────────────────────────
        t.insert(pairKey(CD::shadersGroup(), CD::frameRateKey()), number(QStringLiteral("fps")));
        t.insert(pairKey(CD::shadersAudioGroup(), CD::sensitivityKey()), number(pct));
        t.insert(pairKey(CD::shadersAudioGroup(), CD::extraSmoothingKey()), number(pct));
        t.insert(pairKey(CD::shadersAudioGroup(), CD::lowerCutoffHzKey()), number(QStringLiteral("Hz")));
        t.insert(pairKey(CD::shadersAudioGroup(), CD::higherCutoffHzKey()), number(QStringLiteral("Hz")));

        // ── Window-size filters, where 0 disables rather than meaning zero ──
        for (const QString& group :
             {CD::exclusionsGroup(), CD::animationsWindowFilteringGroup(), CD::decorationsWindowFilteringGroup()}) {
            t.insert(pairKey(group, CD::minimumWindowWidthKey()), number(px, 1.0, true));
            t.insert(pairKey(group, CD::minimumWindowHeightKey()), number(px, 1.0, true));
        }

        // ── Ids resolved against live runtime data ──────────────────────────
        t.insert(pairKey(CD::snappingBehaviorWindowHandlingGroup(), CD::defaultLayoutIdKey()),
                 idKind(ValueKind::LayoutId));
        t.insert(pairKey(CD::tilingAlgorithmGroup(), CD::defaultKey()), idKind(ValueKind::TilingAlgorithm));
        t.insert(pairKey(CD::tilingBehaviorGroup(), CD::lockedScreensKey()), idKind(ValueKind::ScreenId));
        t.insert(pairKey(CD::animationsGroup(), CD::shaderProfileTreeKey()), idKind(ValueKind::ShaderPack));
        t.insert(pairKey(CD::decorationsGroup(), CD::decorationProfileTreeKey()), idKind(ValueKind::DecorationPack));

        // ── Triggers ────────────────────────────────────────────────────────
        // Tiling.Behavior.Triggers has a group accessor but declares no keys —
        // the tiling triggers live in Tiling.Behavior itself.
        for (const QString& group : {CD::snappingBehaviorGroup(), CD::snappingBehaviorZoneSpanGroup(),
                                     CD::snappingBehaviorSnapAssistGroup(), CD::tilingBehaviorGroup()}) {
            t.insert(pairKey(group, CD::triggersKey()), idKind(ValueKind::Trigger));
        }
        t.insert(pairKey(CD::editorSnappingGroup(), CD::overrideModifierKey()), idKind(ValueKind::Trigger));
        t.insert(pairKey(CD::editorFillOnDropGroup(), CD::modifierKey()), idKind(ValueKind::Trigger));

        return t;
    }();
    return table;
}

} // namespace

QStringList uiChoiceSubset(const QString& group, const QString& key)
{
    // Shaders.Audio/InputMethod declares ten legal tokens so a hand-edited
    // config still resolves to a name in the diff, but the picker offers only
    // the supported three — the contract configdefaults.h documents. Every
    // other enum key offers its full legal set.
    if (group == CD::shadersAudioGroup() && key == CD::inputMethodKey()) {
        return {QStringLiteral("auto"), QStringLiteral("pipewire"), QStringLiteral("pulse")};
    }
    return {};
}

QString kindName(ValueKind kind)
{
    switch (kind) {
    case ValueKind::Enum:
        return QStringLiteral("enum");
    case ValueKind::Number:
        return QStringLiteral("number");
    case ValueKind::Color:
        return QStringLiteral("color");
    case ValueKind::LayoutId:
        return QStringLiteral("layoutId");
    case ValueKind::ScreenId:
        return QStringLiteral("screenId");
    case ValueKind::VirtualDesktop:
        return QStringLiteral("virtualDesktop");
    case ValueKind::TilingAlgorithm:
        return QStringLiteral("tilingAlgorithm");
    case ValueKind::ShaderPack:
        return QStringLiteral("shaderPack");
    case ValueKind::DecorationPack:
        return QStringLiteral("decorationPack");
    case ValueKind::OverlayShader:
        return QStringLiteral("overlayShader");
    case ValueKind::Trigger:
        return QStringLiteral("trigger");
    case ValueKind::Shortcut:
        return QStringLiteral("shortcut");
    case ValueKind::Plain:
        break;
    }
    return QStringLiteral("plain");
}

QString displayText(const QString& group, const QString& key, const QVariant& value)
{
    return displayText(group, key, value, descriptorFor(group, key));
}

QString displayText(const QString& group, const QString& key, const QVariant& value, const ValueDescriptor& descriptor)
{
    switch (descriptor.kind) {
    case ValueKind::Enum: {
        // The schema owns the value → token mapping; this owns token → word.
        for (const PhosphorConfig::ChoiceDef& choice : cachedSettingsSchema().choicesFor(group, key)) {
            if (choice.value == value) {
                return enumLabel(group, key, choice.token);
            }
        }
        // A value outside the declared set — hand-edited config, or a choice
        // list that has fallen behind. The caller shows the raw value.
        return {};
    }
    case ValueKind::Number: {
        bool ok = false;
        const double raw = value.toDouble(&ok);
        if (!ok) {
            return {};
        }
        if (descriptor.zeroMeansOff && qFuzzyIsNull(raw)) {
            return PhosphorI18n::tr("Off");
        }
        const double scaled = raw * descriptor.displayScale;
        // Whole numbers read as integers; a scaled ratio can land on a fraction
        // (0.335 → 33.5%), so keep one decimal rather than rounding it away.
        const QString number =
            qFuzzyCompare(scaled, qRound(scaled)) ? QString::number(qRound(scaled)) : QString::number(scaled, 'f', 1);
        if (descriptor.unit.isEmpty()) {
            return number;
        }
        // "80%" but "2 px": a percent sign sits tight against its number, a
        // unit word does not.
        if (descriptor.unit == QLatin1String("%")) {
            return number + descriptor.unit;
        }
        return PhosphorI18n::tr("%1 %2", "a number followed by its unit").arg(number, descriptor.unit);
    }
    default:
        // Ids, triggers, shortcuts, colours and plain values are the view's to
        // render — it has the live data and the swatch.
        return {};
    }
}

ValueDescriptor descriptorFor(const QString& group, const QString& key)
{
    const QString pair = pairKey(group, key);
    if (const auto it = descriptorTable().constFind(pair); it != descriptorTable().constEnd()) {
        return *it;
    }
    // An enum key needs no descriptor entry of its own — carrying choices in
    // the schema is what makes it an enum.
    if (enumLabelTable().contains(pair)) {
        return ValueDescriptor{ValueKind::Enum, {}, 1.0, false};
    }
    return {};
}

QString enumLabel(const QString& group, const QString& key, const QString& token)
{
    const auto it = enumLabelTable().constFind(pairKey(group, key));
    if (it == enumLabelTable().constEnd()) {
        return {};
    }
    return it->value(token);
}

QVariantList allDescribedKeys()
{
    QVariantList out;
    const auto emit = [&out](const QString& pair) {
        const QStringList parts = pair.split(QLatin1Char('\x1f'));
        if (parts.size() == 2) {
            out.append(QVariantMap{
                {QStringLiteral("group"), parts.at(0)},
                {QStringLiteral("key"), parts.at(1)},
            });
        }
    };
    for (auto it = descriptorTable().constBegin(); it != descriptorTable().constEnd(); ++it) {
        emit(it.key());
    }
    for (auto it = enumLabelTable().constBegin(); it != enumLabelTable().constEnd(); ++it) {
        emit(it.key());
    }
    return out;
}

QVariantList allEnumLabels()
{
    QVariantList out;
    for (auto git = enumLabelTable().constBegin(); git != enumLabelTable().constEnd(); ++git) {
        const QStringList pair = git.key().split(QLatin1Char('\x1f'));
        if (pair.size() != 2) {
            continue;
        }
        for (auto tit = git.value().constBegin(); tit != git.value().constEnd(); ++tit) {
            out.append(QVariantMap{
                {QStringLiteral("group"), pair.at(0)},
                {QStringLiteral("key"), pair.at(1)},
                {QStringLiteral("token"), tit.key()},
                {QStringLiteral("label"), tit.value()},
            });
        }
    }
    return out;
}

} // namespace SettingsValueLabels
} // namespace PlasmaZones
