// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "phosphorzones_export.h"

#include <QJsonObject>
#include <QLatin1String>
#include <QList>
#include <QString>
#include <QUuid>
#include <QtGlobal>

namespace PhosphorZones {

/// Smallest fraction of the work area a template may express, mirroring the
/// scroll engine's PhosphorScrollEngine::MinColumnWidthFraction (ScrollTypes.h).
/// The dependency runs the other way, so the value is repeated here the same
/// way ConfigDefaults and PhosphorRules repeat it. Every place this library
/// clamps or floors a template fraction reads this constant.
inline constexpr qreal MinTemplateFraction = 0.05;

/// Most blueprint columns a template may carry, mirroring the scroll engine's
/// kMaxTemplateEntries (enginelimits.h) the same hand-written way
/// MinTemplateFraction mirrors its floor. The engine truncates a pushed
/// blueprint at this many entries, so columns beyond it could never seed
/// anything, and it keeps the same number out of a pushed preset vocabulary.
/// Normalization drops the excess from all three lists, and the value is
/// exported to the settings UI through SettingsController::scrollingConstants().
inline constexpr int MaxTemplateColumns = 16;

/// The engine's DefaultWidthKind wire values, mirrored the same hand-written
/// way MinTemplateFraction mirrors its floor: this library deliberately does
/// not depend on phosphor-scroll-engine, so the vocabulary is repeated by
/// documented value and is append-only on both sides. Every site in this
/// library that compares or coerces a default-width kind reads these rather
/// than a bare integer.
inline constexpr int DefaultWidthKindProportion = 0;
inline constexpr int DefaultWidthKindFixed = 1;
inline constexpr int DefaultWidthKindClientDecides = 2;
inline constexpr int DefaultWidthKindPreset = 3;

/// Two preset stops closer together than this are the same stop: the
/// normalization dedupe drops the later one. Exported to the settings UI
/// through SettingsController::scrollingConstants() so the template editor's
/// parse mirrors the store exactly.
inline constexpr qreal FractionDedupeEpsilon = 0.01;

/**
 * @brief One column of a scrolling template's seed blueprint
 *
 * Widths are plain fractions of the work area (0..1), deliberately NOT the
 * engine's full ColumnWidth kind vocabulary: a Fixed-pixel width in a
 * template would mean different things on different monitors, and a Preset
 * width would point into the template's own preset list — both traps for an
 * object meant to be portable across screens. The display value mirrors the
 * scroll engine's ColumnDisplay wire values (0 = Normal stacked, 1 = Tabbed);
 * the two libraries deliberately do not include each other, so the mirror is
 * by documented value, append-only on both sides.
 */
struct PHOSPHORZONES_EXPORT ScrollingTemplateColumn
{
    /// Fraction of the work-area width (gap-aware, same contract as the
    /// engine's Proportion kind).
    qreal width = 0.5;
    /// Mirrors PhosphorScrollEngine::ColumnDisplay: 0 Normal, 1 Tabbed.
    int display = 0;

    bool operator==(const ScrollingTemplateColumn&) const = default;
};

/**
 * @brief The template JSON keys other modules have to spell.
 *
 * `id` is the persisted identity key. `isSystem` and `sourcePath` are NOT
 * part of the file format (the store stamps both at load); they are the
 * spellings the settings app's editing map and the D-Bus wire enrichment
 * add on top, and they live here so the sides cannot drift apart. The rest
 * of the file format is written and read in one place,
 * ScrollingTemplate::toJson / fromJson, so those keys stay local to that
 * translation unit.
 */
namespace TemplateJsonKeys {
inline constexpr QLatin1String Id{"id"};
inline constexpr QLatin1String IsSystem{"isSystem"};
inline constexpr QLatin1String SourcePath{"sourcePath"};
} // namespace TemplateJsonKeys

/**
 * @brief A scrolling screen's first-class sizing template
 *
 * Scrolling's own template object — the peer of snapping's zone Layout and
 * autotile's algorithm. It describes how a scrolling screen's strip behaves:
 *
 * - @ref columns is the SEED BLUEPRINT: a column materializing at strip
 *   index i < columns.size() takes the blueprint entry's width and display.
 *   The blueprint never resizes an existing column (a template change
 *   affects only columns created after it — the niri principle that opening
 *   or configuring never reshapes what you have).
 * - The default width trio and @ref defaultColumnDisplay answer for columns
 *   BEYOND the blueprint. They mirror the engine's settings-channel wire
 *   values (DefaultWidthKind: 0 Proportion, 1 Fixed, 2 ClientDecides,
 *   3 Preset) so the daemon push writes them into the existing per-screen
 *   keys verbatim.
 * - @ref presetColumnWidths / @ref presetWindowHeights are the cycle stops
 *   the preset shortcuts step through while this template is assigned; they
 *   replace the compiled defaults WHOLESALE (no merge).
 *
 * Version-free additive is the PARSER's policy: an absent key reads as its
 * default, so an older build loads a newer file, matching the layout JSON
 * policy. The authoring schema (data/schemas/scrolling-template.schema.json)
 * is deliberately CLOSED so a typo in a bundled file fails validation, and a
 * later axis (per-column scroll direction, overflow policies) adds a field to
 * the parser and the schema in the same change.
 *
 * Assignment rides the existing cascade: AssignmentEntry's
 * scrollingTemplateLayout field stores this object's id (QUuid-shaped, same
 * canonical braced form as layout ids).
 */
class PHOSPHORZONES_EXPORT ScrollingTemplate
{
public:
    QUuid id;
    QString name;
    /// Optional user-facing description (plain prose).
    QString description;

    /// Seed blueprint, ordered left to right. May be empty: a template can
    /// carry only vocabularies and defaults.
    QList<ScrollingTemplateColumn> columns;

    /// Default column width for columns beyond the blueprint, spelled in the
    /// engine's settings-channel trio (see class doc for the kind values).
    int defaultColumnWidthKind = DefaultWidthKindPreset;
    qreal defaultColumnWidthValue = 0.5; ///< Proportion fraction / Fixed px
    int defaultColumnWidthPresetIndex = 1; ///< Kind Preset: index into presetColumnWidths
    /// Mirrors ColumnDisplay: 0 Normal, 1 Tabbed.
    int defaultColumnDisplay = 0;

    /// Cycle stops for the preset width/height shortcuts. Fractions 0..1,
    /// ascending after normalization.
    QList<qreal> presetColumnWidths;
    QList<qreal> presetWindowHeights;

    /// True when the loaded file came from a read-only system location
    /// (bundled starter). Not serialized; stamped by the store at load.
    /// A user edit shadows the bundled file with a same-id user copy, and
    /// deleting that copy resurfaces the bundled original.
    bool isSystem = false;
    /// Absolute path of the file this template was loaded from. Not
    /// serialized; stamped by the store at load (empty for a template that
    /// was never persisted). Feeds the open-in-text-editor affordance.
    QString sourcePath;

    bool isValid() const
    {
        return !id.isNull() && !name.isEmpty();
    }

    /// Clamp every fraction into legal range, drop degenerate entries, sort
    /// and dedupe the preset lists, and cap the blueprint AND both preset
    /// vocabularies at MaxTemplateColumns. The preset cap applies after the
    /// sort and dedupe, so what survives is the smallest N distinct stops,
    /// matching what the engine keeps out of the same pushed list. A Preset
    /// default width with an empty preset vocabulary
    /// (the bare-defaults shape) demotes to Proportion, so the width trio the
    /// daemon pushes as a unit is always self-consistent. Returns false when
    /// the template is unusable even after normalization (invalid id / empty
    /// name).
    bool normalize();

    QJsonObject toJson() const;
    /// Parses and normalizes. Returns an invalid template (isValid() false)
    /// on a malformed document.
    static ScrollingTemplate fromJson(const QJsonObject& json);

    bool operator==(const ScrollingTemplate&) const = default;
};

} // namespace PhosphorZones
