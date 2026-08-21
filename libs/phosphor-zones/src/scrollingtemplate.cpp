// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorZones/ScrollingTemplate.h>

#include <QJsonArray>

#include <algorithm>

namespace PhosphorZones {

namespace {

QJsonArray fractionsToJson(const QList<qreal>& values)
{
    QJsonArray out;
    for (qreal v : values) {
        out.append(v);
    }
    return out;
}

QList<qreal> fractionsFromJson(const QJsonArray& array)
{
    QList<qreal> out;
    out.reserve(array.size());
    for (const auto& v : array) {
        out.append(v.toDouble());
    }
    return out;
}

} // namespace

// Clamp to [MinTemplateFraction, 1.0] dropping sub-floor entries, sort ascending,
// dedupe within kEps — the same normalization the settings preset parser
// applies, so a template list and a settings list obey one contract. The result
// is capped at MaxTemplateColumns, the same limit the blueprint obeys: the
// engine keeps only that many entries out of a pushed vocabulary, so anything
// past it could never be cycled to. Truncating AFTER the sort and dedupe makes
// the kept set deterministic and identical to what the engine keeps, the
// smallest N distinct fractions.
QList<qreal> ScrollingTemplate::normalizePresetList(QList<qreal> values)
{
    // Dedupe tolerance, matching the settings parser's treatment of editor
    // float dust (thirds arrive as 0.333333/0.333334).
    constexpr qreal kEps = FractionDedupeEpsilon;
    QList<qreal> kept;
    kept.reserve(values.size());
    for (qreal v : values) {
        if (!qIsFinite(v) || v < MinTemplateFraction) {
            continue;
        }
        kept.append(qMin(v, 1.0));
    }
    std::sort(kept.begin(), kept.end());
    QList<qreal> out;
    out.reserve(qMin<qsizetype>(kept.size(), MaxTemplateColumns));
    for (qreal v : kept) {
        if (out.size() == MaxTemplateColumns) {
            break;
        }
        if (out.isEmpty() || qAbs(out.last() - v) >= kEps) {
            out.append(v);
        }
    }
    return out;
}

bool ScrollingTemplate::normalize()
{
    QList<ScrollingTemplateColumn> keptColumns;
    keptColumns.reserve(qMin<qsizetype>(columns.size(), MaxTemplateColumns));
    for (ScrollingTemplateColumn column : columns) {
        if (keptColumns.size() == MaxTemplateColumns) {
            break;
        }
        if (!qIsFinite(column.width) || column.width < MinTemplateFraction) {
            continue;
        }
        column.width = qMin(column.width, 1.0);
        if (column.display != 0 && column.display != 1) {
            column.display = 0;
        }
        keptColumns.append(column);
    }
    columns = keptColumns;

    // An out-of-vocabulary kind coerces to Proportion, not Preset: Proportion
    // is the one kind whose payload (defaultColumnWidthValue) is always
    // meaningful on its own, so garbage in the kind field degrades to a plain
    // fraction. Coercing to Preset instead handed a template with a populated
    // preset vocabulary a concrete preset width the file never asked for.
    if (defaultColumnWidthKind < 0 || defaultColumnWidthKind > DefaultWidthKindPreset) {
        defaultColumnWidthKind = DefaultWidthKindProportion;
    }
    if (defaultColumnWidthPresetIndex < 0) {
        defaultColumnWidthPresetIndex = 0;
    }
    if (defaultColumnDisplay != 0 && defaultColumnDisplay != 1) {
        defaultColumnDisplay = 0;
    }

    presetColumnWidths = normalizePresetList(presetColumnWidths);
    presetWindowHeights = normalizePresetList(presetWindowHeights);
    if (presetColumnWidths.isEmpty()) {
        // No vocabulary to index into, whatever the kind. Zero is the only
        // defensible value: the trio is pushed as a unit and round-trips
        // through JSON, so leaving a stale index on a non-Preset kind would
        // hand the engine a dangling index the moment presets are added back.
        defaultColumnWidthPresetIndex = 0;
    } else if (defaultColumnWidthPresetIndex >= presetColumnWidths.size()) {
        defaultColumnWidthPresetIndex = presetColumnWidths.size() - 1;
    }
    // Kind Preset with no preset list points at nothing, and the daemon pushes
    // the width trio as a unit — the engine would read a preset index against
    // an empty vocabulary. This is the defaults shape (kind 3, no presets), so
    // demote to Proportion and let defaultColumnWidthValue answer instead.
    // The index is already 0 by the clause above.
    if (defaultColumnWidthKind == DefaultWidthKindPreset && presetColumnWidths.isEmpty()) {
        defaultColumnWidthKind = DefaultWidthKindProportion;
    }

    if (!qIsFinite(defaultColumnWidthValue) || defaultColumnWidthValue < 0.0) {
        defaultColumnWidthValue = 0.5;
    }
    // Kind-aware bound: a Proportion is a work-area fraction and obeys the same
    // floor/ceiling as every other fraction here. Fixed is a pixel count with
    // no meaningful upper bound, so it only gets a floor — one physical pixel,
    // since a zero-width column is not a column.
    if (defaultColumnWidthKind == DefaultWidthKindProportion) {
        defaultColumnWidthValue = qBound<qreal>(MinTemplateFraction, defaultColumnWidthValue, 1.0);
    } else if (defaultColumnWidthKind == DefaultWidthKindFixed) {
        defaultColumnWidthValue = qMax<qreal>(defaultColumnWidthValue, 1.0);
    }
    return isValid();
}

QJsonObject ScrollingTemplate::toJson() const
{
    QJsonObject json;
    json.insert(TemplateJsonKeys::Id, id.toString());
    json.insert(QLatin1String("name"), name);
    if (!description.isEmpty()) {
        json.insert(QLatin1String("description"), description);
    }
    QJsonArray columnArray;
    for (const ScrollingTemplateColumn& column : columns) {
        QJsonObject columnJson;
        columnJson.insert(QLatin1String("width"), column.width);
        if (column.display != 0) {
            columnJson.insert(QLatin1String("display"), column.display);
        }
        columnArray.append(columnJson);
    }
    json.insert(QLatin1String("columns"), columnArray);
    QJsonObject defaultWidth;
    defaultWidth.insert(QLatin1String("kind"), defaultColumnWidthKind);
    defaultWidth.insert(QLatin1String("value"), defaultColumnWidthValue);
    defaultWidth.insert(QLatin1String("presetIndex"), defaultColumnWidthPresetIndex);
    json.insert(QLatin1String("defaultColumnWidth"), defaultWidth);
    json.insert(QLatin1String("defaultColumnDisplay"), defaultColumnDisplay);
    json.insert(QLatin1String("presetColumnWidths"), fractionsToJson(presetColumnWidths));
    json.insert(QLatin1String("presetWindowHeights"), fractionsToJson(presetWindowHeights));
    return json;
}

ScrollingTemplate ScrollingTemplate::fromJson(const QJsonObject& json)
{
    ScrollingTemplate templ;
    templ.id = QUuid::fromString(json.value(TemplateJsonKeys::Id).toString());
    templ.name = json.value(QLatin1String("name")).toString();
    templ.description = json.value(QLatin1String("description")).toString();
    const QJsonArray columnArray = json.value(QLatin1String("columns")).toArray();
    templ.columns.reserve(columnArray.size());
    for (const auto& value : columnArray) {
        // A non-object element is malformed input, not a column. Without this
        // gate `"columns": [1, 2, 3]` parsed as three phantom columns at the
        // default width, because toObject() answers an empty object and every
        // field then took its default.
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject columnJson = value.toObject();
        ScrollingTemplateColumn column;
        column.width = columnJson.value(QLatin1String("width")).toDouble(0.5);
        column.display = columnJson.value(QLatin1String("display")).toInt(0);
        templ.columns.append(column);
    }
    const QJsonObject defaultWidth = json.value(QLatin1String("defaultColumnWidth")).toObject();
    templ.defaultColumnWidthKind = defaultWidth.value(QLatin1String("kind")).toInt(DefaultWidthKindPreset);
    templ.defaultColumnWidthValue = defaultWidth.value(QLatin1String("value")).toDouble(0.5);
    templ.defaultColumnWidthPresetIndex = defaultWidth.value(QLatin1String("presetIndex")).toInt(1);
    templ.defaultColumnDisplay = json.value(QLatin1String("defaultColumnDisplay")).toInt(0);
    templ.presetColumnWidths = fractionsFromJson(json.value(QLatin1String("presetColumnWidths")).toArray());
    templ.presetWindowHeights = fractionsFromJson(json.value(QLatin1String("presetWindowHeights")).toArray());
    templ.normalize();
    return templ;
}

} // namespace PhosphorZones
