// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorZones/ScrollingTemplate.h>

#include <QJsonArray>

#include <algorithm>

namespace PhosphorZones {

namespace {

// Hand-mirrored floors: the authoritative constants live in the scroll
// engine (PhosphorScrollEngine::MinColumnWidthFraction /
// MinWindowHeightFraction, ScrollTypes.h) and the dependency runs the other
// way, so this library repeats the values like ConfigDefaults and
// PhosphorRules do. Keep all mirrors in sync.
constexpr qreal kMinFraction = 0.05;

// Dedupe tolerance for the preset lists, matching the settings parser's
// treatment of editor float dust (thirds arrive as 0.333333/0.333334).
constexpr qreal kEps = 0.01;

// Blueprint cap, mirroring the scroll engine's kMaxTemplateEntries
// (engine_core.cpp): the engine truncates the pushed blueprint at 16
// entries anyway, so a hand-written or bus-supplied file with thousands of
// columns would only burn memory on entries nothing can ever seed from.
constexpr int kMaxColumns = 16;

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

/// Clamp to [kMinFraction, 1.0] dropping sub-floor entries, sort ascending,
/// dedupe within kEps — the same normalization the settings preset parser
/// applies, so a template list and a settings list obey one contract.
QList<qreal> normalizeFractionList(QList<qreal> values)
{
    QList<qreal> kept;
    kept.reserve(values.size());
    for (qreal v : values) {
        if (!qIsFinite(v) || v < kMinFraction) {
            continue;
        }
        kept.append(qMin(v, 1.0));
    }
    std::sort(kept.begin(), kept.end());
    QList<qreal> out;
    out.reserve(kept.size());
    for (qreal v : kept) {
        if (out.isEmpty() || qAbs(out.last() - v) >= kEps) {
            out.append(v);
        }
    }
    return out;
}

} // namespace

bool ScrollingTemplate::normalize()
{
    QList<ScrollingTemplateColumn> keptColumns;
    keptColumns.reserve(qMin(int(columns.size()), kMaxColumns));
    for (ScrollingTemplateColumn column : columns) {
        if (keptColumns.size() == kMaxColumns) {
            break;
        }
        if (!qIsFinite(column.width) || column.width < kMinFraction) {
            continue;
        }
        column.width = qMin(column.width, 1.0);
        if (column.display != 0 && column.display != 1) {
            column.display = 0;
        }
        keptColumns.append(column);
    }
    columns = keptColumns;

    if (defaultColumnWidthKind < 0 || defaultColumnWidthKind > 3) {
        defaultColumnWidthKind = 3;
    }
    if (defaultColumnWidthPresetIndex < 0) {
        defaultColumnWidthPresetIndex = 0;
    }
    if (defaultColumnDisplay != 0 && defaultColumnDisplay != 1) {
        defaultColumnDisplay = 0;
    }

    presetColumnWidths = normalizeFractionList(presetColumnWidths);
    presetWindowHeights = normalizeFractionList(presetWindowHeights);
    if (defaultColumnWidthPresetIndex >= presetColumnWidths.size() && !presetColumnWidths.isEmpty()) {
        defaultColumnWidthPresetIndex = presetColumnWidths.size() - 1;
    }
    // Kind Preset with no preset list points at nothing, and the daemon pushes
    // the width trio as a unit — the engine would read a preset index against
    // an empty vocabulary. This is the defaults shape (kind 3, no presets), so
    // demote to Proportion and let defaultColumnWidthValue answer instead.
    if (defaultColumnWidthKind == 3 && presetColumnWidths.isEmpty()) {
        defaultColumnWidthKind = 0;
        defaultColumnWidthPresetIndex = 0;
    }

    if (!qIsFinite(defaultColumnWidthValue) || defaultColumnWidthValue < 0.0) {
        defaultColumnWidthValue = 0.5;
    }
    // Kind-aware bound: a Proportion is a work-area fraction and obeys the same
    // floor/ceiling as every other fraction here. Fixed is a pixel count with
    // no meaningful upper bound, so it keeps the lower-bound repair only.
    if (defaultColumnWidthKind == 0) {
        defaultColumnWidthValue = qBound(kMinFraction, defaultColumnWidthValue, 1.0);
    }
    return isValid();
}

QJsonObject ScrollingTemplate::toJson() const
{
    QJsonObject json;
    json.insert(QLatin1String("id"), id.toString());
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
    templ.id = QUuid::fromString(json.value(QLatin1String("id")).toString());
    templ.name = json.value(QLatin1String("name")).toString();
    templ.description = json.value(QLatin1String("description")).toString();
    const QJsonArray columnArray = json.value(QLatin1String("columns")).toArray();
    templ.columns.reserve(columnArray.size());
    for (const auto& value : columnArray) {
        const QJsonObject columnJson = value.toObject();
        ScrollingTemplateColumn column;
        column.width = columnJson.value(QLatin1String("width")).toDouble(0.5);
        column.display = columnJson.value(QLatin1String("display")).toInt(0);
        templ.columns.append(column);
    }
    const QJsonObject defaultWidth = json.value(QLatin1String("defaultColumnWidth")).toObject();
    templ.defaultColumnWidthKind = defaultWidth.value(QLatin1String("kind")).toInt(3);
    templ.defaultColumnWidthValue = defaultWidth.value(QLatin1String("value")).toDouble(0.5);
    templ.defaultColumnWidthPresetIndex = defaultWidth.value(QLatin1String("presetIndex")).toInt(1);
    templ.defaultColumnDisplay = json.value(QLatin1String("defaultColumnDisplay")).toInt(0);
    templ.presetColumnWidths = fractionsFromJson(json.value(QLatin1String("presetColumnWidths")).toArray());
    templ.presetWindowHeights = fractionsFromJson(json.value(QLatin1String("presetWindowHeights")).toArray());
    templ.normalize();
    return templ;
}

} // namespace PhosphorZones
