// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "EditorTemplateModel.h"

#include "EditorController.h"
#include "undo/UndoController.h"
#include "undo/commands/UpdateTemplateCommand.h"
#include "../config/configdefaults.h"
#include "core/types/constants.h"
#include "core/platform/logging.h"
#include <PhosphorZones/ScrollingTemplate.h>

#include "phosphor_i18n.h"
#include <algorithm>
#include <QJsonArray>

namespace PlasmaZones {

namespace {

// Snapshot keys for the template edit state. The columns / description /
// defaultColumnWidth / preset spellings match ScrollingTemplate::toJson so a
// snapshot converts to the wire payload without a translation table; the
// flattened width-trio keys are snapshot-local.
constexpr QLatin1String KeyDescription{"description"};
constexpr QLatin1String KeyColumns{"columns"};
constexpr QLatin1String KeyWidth{"width"};
constexpr QLatin1String KeyDisplay{"display"};
constexpr QLatin1String KeyWidthKind{"widthKind"};
constexpr QLatin1String KeyWidthValue{"widthValue"};
constexpr QLatin1String KeyPresetIndex{"presetIndex"};
constexpr QLatin1String KeyDefaultDisplay{"defaultDisplay"};
constexpr QLatin1String KeyPresetWidths{"presetWidths"};
constexpr QLatin1String KeyPresetHeights{"presetHeights"};

QJsonArray fractionsToJsonArray(const QVariantList& fractions)
{
    QJsonArray array;
    for (const QVariant& value : fractions) {
        array.append(value.toDouble());
    }
    return array;
}

} // anonymous namespace

EditorTemplateModel::EditorTemplateModel(EditorController* controller, QObject* parent)
    : QObject(parent)
    , m_controller(controller)
{
}

QVariantMap EditorTemplateModel::snapshot() const
{
    QVariantMap state;
    state[KeyDescription] = m_description;
    state[KeyColumns] = m_columns;
    state[KeyWidthKind] = m_defaultWidthKind;
    state[KeyWidthValue] = m_defaultWidthValue;
    state[KeyPresetIndex] = m_defaultPresetIndex;
    state[KeyDefaultDisplay] = m_defaultDisplay;
    state[KeyPresetWidths] = m_presetWidths;
    state[KeyPresetHeights] = m_presetHeights;
    return state;
}

void EditorTemplateModel::applyState(const QVariantMap& state)
{
    const QString description = state.value(KeyDescription).toString();
    if (m_description != description) {
        m_description = description;
        Q_EMIT descriptionChanged();
    }

    const QVariantList columns = state.value(KeyColumns).toList();
    if (m_columns != columns) {
        m_columns = columns;
        Q_EMIT columnsChanged();
    }

    const int widthKind = state.value(KeyWidthKind).toInt();
    const qreal widthValue = state.value(KeyWidthValue).toDouble();
    const int presetIndex = state.value(KeyPresetIndex).toInt();
    const int defaultDisplay = state.value(KeyDefaultDisplay).toInt();
    if (m_defaultWidthKind != widthKind || !qFuzzyCompare(m_defaultWidthValue, widthValue)
        || m_defaultPresetIndex != presetIndex || m_defaultDisplay != defaultDisplay) {
        m_defaultWidthKind = widthKind;
        m_defaultWidthValue = widthValue;
        m_defaultPresetIndex = presetIndex;
        m_defaultDisplay = defaultDisplay;
        Q_EMIT defaultsChanged();
    }

    const QVariantList presetWidths = state.value(KeyPresetWidths).toList();
    const QVariantList presetHeights = state.value(KeyPresetHeights).toList();
    if (m_presetWidths != presetWidths || m_presetHeights != presetHeights) {
        m_presetWidths = presetWidths;
        m_presetHeights = presetHeights;
        Q_EMIT presetsChanged();
    }

    m_controller->markUnsaved();
}

void EditorTemplateModel::resetState(const QVariantMap& state, bool isSystem)
{
    m_description = state.value(KeyDescription).toString();
    m_columns = state.value(KeyColumns).toList();
    m_defaultWidthKind = state.value(KeyWidthKind).toInt();
    m_defaultWidthValue = state.value(KeyWidthValue).toDouble();
    m_defaultPresetIndex = state.value(KeyPresetIndex).toInt();
    m_defaultDisplay = state.value(KeyDefaultDisplay).toInt();
    m_presetWidths = state.value(KeyPresetWidths).toList();
    m_presetHeights = state.value(KeyPresetHeights).toList();
    if (m_isSystem != isSystem) {
        m_isSystem = isSystem;
        Q_EMIT isSystemChanged();
    }
    Q_EMIT descriptionChanged();
    Q_EMIT columnsChanged();
    Q_EMIT defaultsChanged();
    Q_EMIT presetsChanged();
}

void EditorTemplateModel::clearSystemFlag()
{
    if (m_isSystem) {
        m_isSystem = false;
        Q_EMIT isSystemChanged();
    }
}

QJsonObject EditorTemplateModel::toWirePayload(const QString& id, const QString& name) const
{
    QJsonObject payload;
    payload.insert(PhosphorZones::TemplateJsonKeys::Id, id);
    payload.insert(QLatin1String("name"), name.trimmed());
    payload.insert(KeyDescription, m_description.trimmed());
    QJsonArray columns;
    for (const QVariant& columnVar : m_columns) {
        const QVariantMap column = columnVar.toMap();
        QJsonObject entry;
        entry.insert(KeyWidth, column.value(KeyWidth).toDouble());
        entry.insert(KeyDisplay, column.value(KeyDisplay).toInt());
        columns.append(entry);
    }
    payload.insert(KeyColumns, columns);
    QJsonObject defaultWidth;
    defaultWidth.insert(QLatin1String("kind"), m_defaultWidthKind);
    defaultWidth.insert(QLatin1String("value"), m_defaultWidthValue);
    defaultWidth.insert(QLatin1String("presetIndex"), m_defaultPresetIndex);
    payload.insert(QLatin1String("defaultColumnWidth"), defaultWidth);
    payload.insert(QLatin1String("defaultColumnDisplay"), m_defaultDisplay);
    payload.insert(QLatin1String("presetColumnWidths"), fractionsToJsonArray(m_presetWidths));
    payload.insert(QLatin1String("presetWindowHeights"), fractionsToJsonArray(m_presetHeights));
    return payload;
}

QVariantMap EditorTemplateModel::stateFromTemplate(const PhosphorZones::ScrollingTemplate& templ)
{
    QVariantMap state;
    state[KeyDescription] = templ.description;
    QVariantList columns;
    columns.reserve(templ.columns.size());
    for (const PhosphorZones::ScrollingTemplateColumn& column : templ.columns) {
        QVariantMap entry;
        entry[KeyWidth] = column.width;
        entry[KeyDisplay] = column.display;
        columns.append(entry);
    }
    state[KeyColumns] = columns;
    state[KeyWidthKind] = templ.defaultColumnWidthKind;
    state[KeyWidthValue] = templ.defaultColumnWidthValue;
    state[KeyPresetIndex] = templ.defaultColumnWidthPresetIndex;
    state[KeyDefaultDisplay] = templ.defaultColumnDisplay;
    QVariantList presetWidths;
    presetWidths.reserve(templ.presetColumnWidths.size());
    for (qreal value : templ.presetColumnWidths) {
        presetWidths.append(value);
    }
    state[KeyPresetWidths] = presetWidths;
    QVariantList presetHeights;
    presetHeights.reserve(templ.presetWindowHeights.size());
    for (qreal value : templ.presetWindowHeights) {
        presetHeights.append(value);
    }
    state[KeyPresetHeights] = presetHeights;
    return state;
}

void EditorTemplateModel::pushCommand(const QString& label, const QVariantMap& newState, const QString& mergeKey)
{
    UndoController* undo = m_controller->undoController();
    if (!undo) {
        qCWarning(lcEditor) << "EditorTemplateModel::pushCommand: undo controller is null";
        return;
    }
    const QVariantMap oldState = snapshot();
    if (oldState == newState) {
        return;
    }
    auto* command = new UpdateTemplateCommand(QPointer<EditorTemplateModel>(this), oldState, newState, label, mergeKey);
    undo->push(command);
}

void EditorTemplateModel::setDescription(const QString& description)
{
    const QString clamped = clampName(description, MaxTemplateDescriptionLength);
    if (m_description == clamped) {
        return;
    }
    QVariantMap state = snapshot();
    state[KeyDescription] = clamped;
    pushCommand(PhosphorI18n::tr("Change Description"), state);
}

void EditorTemplateModel::setDefaultWidthKind(int kind)
{
    if (m_defaultWidthKind == kind) {
        return;
    }
    QVariantMap state = snapshot();
    state[KeyWidthKind] = kind;
    pushCommand(PhosphorI18n::tr("Change Default Width"), state);
}

void EditorTemplateModel::setDefaultWidthValue(qreal value)
{
    if (qFuzzyCompare(m_defaultWidthValue, value)) {
        return;
    }
    QVariantMap state = snapshot();
    state[KeyWidthValue] = value;
    pushCommand(PhosphorI18n::tr("Change Default Width"), state, QStringLiteral("defaultWidthValue"));
}

void EditorTemplateModel::setDefaultPresetIndex(int index)
{
    if (m_defaultPresetIndex == index) {
        return;
    }
    QVariantMap state = snapshot();
    state[KeyPresetIndex] = index;
    pushCommand(PhosphorI18n::tr("Change Default Width"), state, QStringLiteral("defaultPresetIndex"));
}

void EditorTemplateModel::setDefaultDisplay(int display)
{
    if (m_defaultDisplay == display) {
        return;
    }
    QVariantMap state = snapshot();
    state[KeyDefaultDisplay] = display;
    pushCommand(PhosphorI18n::tr("Change Default Display"), state);
}

void EditorTemplateModel::setPresetWidths(const QVariantList& widths)
{
    if (m_presetWidths == widths) {
        return;
    }
    QVariantMap state = snapshot();
    state[KeyPresetWidths] = widths;
    pushCommand(PhosphorI18n::tr("Change Width Presets"), state);
}

void EditorTemplateModel::setPresetHeights(const QVariantList& heights)
{
    if (m_presetHeights == heights) {
        return;
    }
    QVariantMap state = snapshot();
    state[KeyPresetHeights] = heights;
    pushCommand(PhosphorI18n::tr("Change Height Presets"), state);
}

void EditorTemplateModel::addColumn()
{
    if (m_columns.size() >= PhosphorZones::MaxTemplateColumns) {
        return;
    }
    QVariantMap state = snapshot();
    QVariantList columns = state.value(KeyColumns).toList();
    QVariantMap column;
    column[KeyWidth] = 0.5;
    column[KeyDisplay] = 0;
    columns.append(column);
    state[KeyColumns] = columns;
    pushCommand(PhosphorI18n::tr("Add Column"), state);
}

void EditorTemplateModel::removeColumn(int index)
{
    if (index < 0 || index >= m_columns.size()) {
        return;
    }
    QVariantMap state = snapshot();
    QVariantList columns = state.value(KeyColumns).toList();
    columns.removeAt(index);
    state[KeyColumns] = columns;
    pushCommand(PhosphorI18n::tr("Remove Column"), state);
}

void EditorTemplateModel::moveColumn(int from, int to)
{
    if (from == to || from < 0 || to < 0 || from >= m_columns.size() || to >= m_columns.size()) {
        return;
    }
    QVariantMap state = snapshot();
    QVariantList columns = state.value(KeyColumns).toList();
    columns.move(from, to);
    state[KeyColumns] = columns;
    pushCommand(PhosphorI18n::tr("Move Column"), state);
}

void EditorTemplateModel::setColumnWidth(int index, qreal width, bool interactive)
{
    if (index < 0 || index >= m_columns.size()) {
        return;
    }
    // Single clamp authority for every entry point (mouse commit, keyboard
    // steps): the same proportion bounds scrollingConstants() exports, so
    // no path can store a width another path could not reach.
    const qreal clamped = std::clamp<qreal>(width, ConfigDefaults::scrollingDefaultColumnWidthProportionMin(),
                                            ConfigDefaults::scrollingDefaultColumnWidthProportionMax());
    QVariantMap column = m_columns.at(index).toMap();
    if (qFuzzyCompare(column.value(KeyWidth).toDouble(), clamped)) {
        return;
    }
    QVariantMap state = snapshot();
    QVariantList columns = state.value(KeyColumns).toList();
    column[KeyWidth] = clamped;
    columns[index] = column;
    state[KeyColumns] = columns;
    const QString mergeKey = interactive ? QStringLiteral("columnWidth:%1:%2").arg(index).arg(m_gesture) : QString();
    pushCommand(PhosphorI18n::tr("Resize Column"), state, mergeKey);
}

void EditorTemplateModel::setColumnDisplay(int index, int display)
{
    if (index < 0 || index >= m_columns.size()) {
        return;
    }
    QVariantMap column = m_columns.at(index).toMap();
    if (column.value(KeyDisplay).toInt() == display) {
        return;
    }
    QVariantMap state = snapshot();
    QVariantList columns = state.value(KeyColumns).toList();
    column[KeyDisplay] = display;
    columns[index] = column;
    state[KeyColumns] = columns;
    pushCommand(PhosphorI18n::tr("Change Column Display"), state);
}

void EditorTemplateModel::beginWidthDrag()
{
    ++m_gesture;
}

QVariantMap EditorTemplateModel::scrollingConstants() const
{
    // Editor-process sibling of SettingsController::scrollingConstants(),
    // trimmed to the template-authoring keys. Same spellings on purpose so
    // the settings dialog's form logic ported across unchanged.
    return {
        {QStringLiteral("kindProportion"), ConfigDefaults::scrollingWidthKindProportion()},
        {QStringLiteral("kindFixed"), ConfigDefaults::scrollingWidthKindFixed()},
        {QStringLiteral("kindClientDecides"), ConfigDefaults::scrollingWidthKindClientDecides()},
        {QStringLiteral("kindPreset"), ConfigDefaults::scrollingWidthKindPreset()},
        {QStringLiteral("proportionMin"), ConfigDefaults::scrollingDefaultColumnWidthProportionMin()},
        {QStringLiteral("proportionMax"), ConfigDefaults::scrollingDefaultColumnWidthProportionMax()},
        {QStringLiteral("fixedMin"), ConfigDefaults::scrollingDefaultColumnWidthFixedMin()},
        {QStringLiteral("fixedMax"), ConfigDefaults::scrollingDefaultColumnWidthFixedMax()},
        {QStringLiteral("presetIndexMax"), ConfigDefaults::scrollingPresetIndexMax()},
        {QStringLiteral("maxTemplateColumns"), PhosphorZones::MaxTemplateColumns},
        {QStringLiteral("fractionDedupeEpsilon"), PhosphorZones::FractionDedupeEpsilon},
        {QStringLiteral("nameMaxLength"), MaxLayoutNameLength},
        {QStringLiteral("descriptionMaxLength"), MaxTemplateDescriptionLength},
    };
}

} // namespace PlasmaZones
