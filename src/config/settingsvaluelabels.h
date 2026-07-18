// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"

#include <QString>
#include <QVariant>
#include <QVariantList>

namespace PlasmaZones {

/// How a stored config value should be presented to a reader.
///
/// The config schema says what values a key accepts; this says what they MEAN
/// to a user. The split matters because the two answers live in different
/// places: the legal value set is semantics and belongs with the key (see
/// PhosphorConfig::ChoiceDef), while the words for those values are
/// presentation and belong here, where translation is available.
///
/// The kinds mirror the rule editor's param `kind` vocabulary
/// (ruleauthoring.cpp), and for the same reason: a caller cannot format a value
/// correctly from its C++ type alone. An int is a count, a pixel width, a
/// millisecond duration, a virtual-desktop number, or an enum, and each reads
/// differently.
enum class ValueKind {
    /// No special handling — render the value as-is.
    Plain,
    /// One of a closed set. Resolve through the schema's choices to a token,
    /// then through enumLabel() to a word.
    Enum,
    /// A number, optionally scaled for display and carrying a unit.
    Number,
    /// A colour string, which the view also renders as a swatch.
    Color,
    /// Ids resolved against LIVE runtime data, which the C++ side cannot see:
    /// the view resolves these the way ActionListView does, and every one of
    /// them must fall back to the raw id, since a monitor can be unplugged, a
    /// layout deleted, or a pack uninstalled.
    LayoutId,
    ScreenId,
    VirtualDesktop,
    TilingAlgorithm,
    ShaderPack,
    DecorationPack,
    OverlayShader,
    /// A modifier + mouse-button pair, or a list of them.
    Trigger,
    /// A portable key sequence string ("Ctrl+Alt+E"), already readable.
    Shortcut,
};

/// How to present one config key's value.
struct ValueDescriptor
{
    ValueKind kind = ValueKind::Plain;

    /// Unit shown after the number, e.g. "px". Empty for a bare count.
    QString unit = {};

    /// Stored value multiplied by this to get the displayed number. 100 for the
    /// opacity/ratio keys, which persist 0.0-1.0 but read as a percentage.
    double displayScale = 1.0;

    /// True where the UI presents 0 as "Off" rather than as a zero quantity
    /// (the minimum window size filters, the animation move threshold).
    bool zeroMeansOff = false;
};

/// Presentation rules for config values.
///
/// Lookups are by (group, key) because a token's meaning is key-local: "float"
/// is "Float on drag" for the tiling drag behaviour and "Float excess" for its
/// overflow behaviour, and "auto" is "Auto" for the zone-selector size but
/// "Automatic" for the rendering backend. A token table keyed on the token
/// alone would collapse those.
namespace SettingsValueLabels {

/// How @p key in @p group should be presented. Returns a Plain descriptor for
/// any key without a declared presentation, which renders the raw value — the
/// same fallback discipline the rule views use, so an undescribed key shows
/// something rather than nothing.
PLASMAZONES_EXPORT ValueDescriptor descriptorFor(const QString& group, const QString& key);

/// The translated label for one choice token of a key, or an empty string when
/// the key or token has no declared label. Callers fall back to the raw value.
PLASMAZONES_EXPORT QString enumLabel(const QString& group, const QString& key, const QString& token);

/// Display text for a stored value, or an EMPTY string when this side cannot
/// produce one.
///
/// Empty is a real answer, not a failure: an id kind resolves against live
/// runtime state (connected monitors, installed packs) that only the view can
/// see, and a trigger needs the view's modifier tables. The caller renders the
/// raw value in those cases, or resolves it itself by @c kind.
PLASMAZONES_EXPORT QString displayText(const QString& group, const QString& key, const QVariant& value);

/// Stable name for a kind, for handing to QML where the enum is not available.
PLASMAZONES_EXPORT QString kindName(ValueKind kind);

/// Every (group, key, token) triple that carries a label, as
/// `{group, key, token, label}` maps. Exists so a test can assert that every
/// choice declared in the config schema has a word for it, which is what keeps
/// the two halves from drifting apart.
PLASMAZONES_EXPORT QVariantList allEnumLabels();

/// Every key carrying a declared presentation, as `{group, key}` maps. Exists so
/// a test can assert each one names a key the schema actually declares — a
/// mistyped pair would otherwise never match and silently render raw forever.
PLASMAZONES_EXPORT QVariantList allDescribedKeys();

} // namespace SettingsValueLabels

} // namespace PlasmaZones
