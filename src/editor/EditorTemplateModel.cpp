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
#include <QUuid>

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

// qFuzzyCompare is documented unsuitable against zero, and a stored default
// width value CAN be 0.0 (a file may write it explicitly under the kinds
// whose value normalize() leaves alone), so every equality check on it
// routes through this zero-aware helper.
bool fuzzyEqual(qreal a, qreal b)
{
    if (qFuzzyIsNull(a) || qFuzzyIsNull(b)) {
        return qFuzzyIsNull(a) && qFuzzyIsNull(b);
    }
    return qFuzzyCompare(a, b);
}

// Convert a QVariantList of fractions into the QList<qreal> the shared
// preset normalizer consumes, and back. Non-numeric entries fall out inside
// normalizePresetList's qIsFinite gate.
QList<qreal> fractionsFromVariants(const QVariantList& fractions)
{
    QList<qreal> out;
    out.reserve(fractions.size());
    for (const QVariant& value : fractions) {
        out.append(value.toDouble());
    }
    return out;
}

QVariantList fractionsToVariants(const QList<qreal>& fractions)
{
    QVariantList out;
    out.reserve(fractions.size());
    for (qreal value : fractions) {
        out.append(value);
    }
    return out;
}

} // anonymous namespace

EditorTemplateModel::EditorTemplateModel(EditorController* controller, QObject* parent)
    : QObject(parent)
    , m_controller(controller)
{
    // A null controller is legal only for unit tests driving applyState /
    // snapshot directly; every mutation path asserts because pushCommand
    // needs the controller's undo stack.
    // Seed the defaults from a default-constructed ScrollingTemplate so the
    // pre-load values cannot drift from the struct's own (every load path
    // calls resetState before QML reads anything, so these are belt and
    // braces, not behaviour).
    const PhosphorZones::ScrollingTemplate defaults;
    m_defaultWidthKind = defaults.defaultColumnWidthKind;
    m_defaultWidthValue = defaults.defaultColumnWidthValue;
    m_defaultPresetIndex = defaults.defaultColumnWidthPresetIndex;
    m_defaultDisplay = defaults.defaultColumnDisplay;
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
    if (m_defaultWidthKind != widthKind || !fuzzyEqual(m_defaultWidthValue, widthValue)
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

    // Guarded so a unit test can drive undo/redo on a controller-less model;
    // in the app the controller always exists.
    if (m_controller) {
        m_controller->markUnsaved();
    }
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
    // Build a ScrollingTemplate and let its toJson spell the wire keys: the
    // file format is written and read in one place (ScrollingTemplate::toJson
    // / fromJson, per that header's contract), so a renamed or added key on
    // the library side cannot leave this payload writing a stale spelling.
    // toJson omits an empty description and a zero display, which is exactly
    // what fromJson defaults them to.
    PhosphorZones::ScrollingTemplate templ;
    templ.id = QUuid::fromString(id);
    templ.name = name.trimmed();
    templ.description = m_description.trimmed();
    templ.columns.reserve(m_columns.size());
    for (const QVariant& columnVar : m_columns) {
        const QVariantMap column = columnVar.toMap();
        PhosphorZones::ScrollingTemplateColumn entry;
        entry.width = column.value(KeyWidth).toDouble();
        entry.display = column.value(KeyDisplay).toInt();
        templ.columns.append(entry);
    }
    templ.defaultColumnWidthKind = m_defaultWidthKind;
    templ.defaultColumnWidthValue = m_defaultWidthValue;
    templ.defaultColumnWidthPresetIndex = m_defaultPresetIndex;
    templ.defaultColumnDisplay = m_defaultDisplay;
    templ.presetColumnWidths = fractionsFromVariants(m_presetWidths);
    templ.presetWindowHeights = fractionsFromVariants(m_presetHeights);
    return templ.toJson();
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

void EditorTemplateModel::pushCommand(const QString& label, const QVariantMap& oldState, const QVariantMap& newState,
                                      const QString& mergeKey)
{
    // Both conditions are construction errors, not runtime states: the
    // controller and its undo stack exist for the model's whole lifetime.
    // The release guard still refuses rather than crashing, but a silently
    // dropped user edit is a bug worth asserting on in debug builds.
    Q_ASSERT(m_controller);
    UndoController* undo = m_controller ? m_controller->undoController() : nullptr;
    Q_ASSERT(undo);
    if (!undo) {
        qCWarning(lcEditor) << "EditorTemplateModel::pushCommand: undo controller is null";
        return;
    }
    if (oldState == newState) {
        return;
    }
    auto* command = new UpdateTemplateCommand(QPointer<EditorTemplateModel>(this), oldState, newState, label, mergeKey);
    undo->push(command);
}

void EditorTemplateModel::setDescription(const QString& description)
{
    // clampName's trailing-whitespace trim was written for names, but it is
    // deliberately accepted for the description too: toWirePayload trims on
    // save anyway, so refusing a trailing-space-only edit here just spares an
    // undo entry that could never persist.
    const QString clamped = clampName(description, MaxTemplateDescriptionLength);
    if (m_description == clamped) {
        return;
    }
    const QVariantMap oldState = snapshot();
    QVariantMap state = oldState;
    state[KeyDescription] = clamped;
    pushCommand(PhosphorI18n::tr("Change Description"), oldState, state);
}

void EditorTemplateModel::setDefaultWidthKind(int kind)
{
    if (!ConfigDefaults::isValidScrollingWidthKind(kind)) {
        qCWarning(lcEditor) << "setDefaultWidthKind: rejecting unknown kind" << kind;
        return;
    }
    if (m_defaultWidthKind == kind) {
        return;
    }
    const QVariantMap oldState = snapshot();
    QVariantMap state = oldState;
    state[KeyWidthKind] = kind;
    // The width VALUE is shared across kinds, so entering a kind whose unit
    // system cannot hold the stored number re-seeds the value in the SAME
    // snapshot (one undo entry), the shape of
    // Settings::setScrollingDefaultColumnWidthKind plus a Proportion floor
    // the settings side lacks. Without this, a 0.5
    // fraction saved under kind Fixed normalizes to a 1-pixel column and a
    // pixel count saved under Proportion normalizes to the full screen. The
    // hop through ClientDecides/Preset is covered because the check runs on
    // entry into Fixed or Proportion, not on exit.
    if (kind == ConfigDefaults::scrollingWidthKindFixed()
        && m_defaultWidthValue < ConfigDefaults::scrollingDefaultColumnWidthFixedMin()) {
        state[KeyWidthValue] = ConfigDefaults::scrollingDefaultColumnWidthFixedPx();
    } else if (kind == ConfigDefaults::scrollingWidthKindProportion()
               && (m_defaultWidthValue > ConfigDefaults::scrollingDefaultColumnWidthProportionMax()
                   || m_defaultWidthValue < ConfigDefaults::scrollingDefaultColumnWidthProportionMin())) {
        // Both bounds: a file can park a sub-minimum (even 0.0) value under
        // ClientDecides/Preset, which normalize() leaves alone for those
        // kinds.
        state[KeyWidthValue] = ConfigDefaults::scrollingDefaultColumnWidthValue();
    }
    pushCommand(PhosphorI18n::tr("Change Default Width"), oldState, state);
}

void EditorTemplateModel::setDefaultWidthValue(qreal value)
{
    if (fuzzyEqual(m_defaultWidthValue, value)) {
        return;
    }
    const QVariantMap oldState = snapshot();
    QVariantMap state = oldState;
    state[KeyWidthValue] = value;
    // Gesture-scoped like setColumnWidth: the spin bumps the token when it
    // takes focus, so one editing session coalesces into one undo entry and
    // separate sessions stay separate entries.
    pushCommand(PhosphorI18n::tr("Change Default Width"), oldState, state,
                QStringLiteral("defaultWidthValue:%1").arg(m_gesture));
}

void EditorTemplateModel::setDefaultPresetIndex(int index)
{
    // Boundary clamp like the sibling setters: the combo can only produce a
    // valid row index, but a non-QML caller must not seat a value the store
    // would rewrite at save.
    const int highest = int(qMax<qsizetype>(0, m_presetWidths.size() - 1));
    index = std::clamp(index, 0, highest);
    if (m_defaultPresetIndex == index) {
        return;
    }
    const QVariantMap oldState = snapshot();
    QVariantMap state = oldState;
    state[KeyPresetIndex] = index;
    // No merge key: each combo pick is a discrete choice and its own undo
    // entry.
    pushCommand(PhosphorI18n::tr("Change Default Width"), oldState, state);
}

void EditorTemplateModel::setDefaultDisplay(int display)
{
    if (!ConfigDefaults::isValidScrollingColumnDisplay(display)) {
        qCWarning(lcEditor) << "setDefaultDisplay: rejecting unknown display" << display;
        return;
    }
    if (m_defaultDisplay == display) {
        return;
    }
    const QVariantMap oldState = snapshot();
    QVariantMap state = oldState;
    state[KeyDefaultDisplay] = display;
    pushCommand(PhosphorI18n::tr("Change Default Display"), oldState, state);
}

void EditorTemplateModel::setPresetWidths(const QVariantList& widths)
{
    // Enforce the store's preset contract at the write boundary instead of
    // trusting the caller: the chip editor applies the same rules for live
    // preview, but a non-QML caller (test, future rules path) must not seat a
    // list the store would rewrite on save.
    const QVariantList normalized =
        fractionsToVariants(PhosphorZones::ScrollingTemplate::normalizePresetList(fractionsFromVariants(widths)));
    if (m_presetWidths == normalized) {
        return;
    }
    const QVariantMap oldState = snapshot();
    QVariantMap state = oldState;
    state[KeyPresetWidths] = normalized;
    // A shrunk vocabulary re-clamps the default preset index in the SAME
    // snapshot (one undo entry), the way setDefaultWidthKind re-seeds the
    // width value — the store applies the identical clamp at save.
    if (normalized.size() <= m_defaultPresetIndex) {
        state[KeyPresetIndex] = int(qMax<qsizetype>(0, normalized.size() - 1));
    }
    pushCommand(PhosphorI18n::tr("Change Width Presets"), oldState, state);
}

void EditorTemplateModel::setPresetHeights(const QVariantList& heights)
{
    const QVariantList normalized =
        fractionsToVariants(PhosphorZones::ScrollingTemplate::normalizePresetList(fractionsFromVariants(heights)));
    if (m_presetHeights == normalized) {
        return;
    }
    const QVariantMap oldState = snapshot();
    QVariantMap state = oldState;
    state[KeyPresetHeights] = normalized;
    pushCommand(PhosphorI18n::tr("Change Height Presets"), oldState, state);
}

void EditorTemplateModel::addColumn()
{
    if (m_columns.size() >= PhosphorZones::MaxTemplateColumns) {
        return;
    }
    const QVariantMap oldState = snapshot();
    QVariantMap state = oldState;
    QVariantList columns = state.value(KeyColumns).toList();
    QVariantMap column;
    // Seed through the same named default and clamp authority every resize
    // path uses, so a seeded width can never sit outside what setColumnWidth
    // could store.
    column[KeyWidth] = std::clamp<qreal>(ConfigDefaults::scrollingDefaultColumnWidthValue(),
                                         ConfigDefaults::scrollingDefaultColumnWidthProportionMin(),
                                         ConfigDefaults::scrollingDefaultColumnWidthProportionMax());
    column[KeyDisplay] = ConfigDefaults::scrollingColumnDisplayNormal();
    columns.append(column);
    state[KeyColumns] = columns;
    pushCommand(PhosphorI18n::tr("Add Column"), oldState, state);
}

void EditorTemplateModel::removeColumn(int index)
{
    if (index < 0 || index >= m_columns.size()) {
        return;
    }
    const QVariantMap oldState = snapshot();
    QVariantMap state = oldState;
    QVariantList columns = state.value(KeyColumns).toList();
    columns.removeAt(index);
    state[KeyColumns] = columns;
    pushCommand(PhosphorI18n::tr("Remove Column"), oldState, state);
}

void EditorTemplateModel::moveColumn(int from, int to)
{
    if (from == to || from < 0 || to < 0 || from >= m_columns.size() || to >= m_columns.size()) {
        return;
    }
    const QVariantMap oldState = snapshot();
    QVariantMap state = oldState;
    QVariantList columns = state.value(KeyColumns).toList();
    columns.move(from, to);
    state[KeyColumns] = columns;
    pushCommand(PhosphorI18n::tr("Move Column"), oldState, state);
}

void EditorTemplateModel::setColumnWidth(int index, qreal width, bool interactive)
{
    if (index < 0 || index >= m_columns.size()) {
        return;
    }
    // Single clamp authority for the column-width entry points (mouse commit,
    // keyboard steps, the addColumn seed): the same proportion bounds
    // scrollingConstants() exports, so no width path can store a value
    // another width path could not reach. The default-width VALUE setter has
    // no clamp here on purpose — its legal range depends on the kind, and
    // setDefaultWidthKind owns the kind-aware re-seeding.
    const qreal clamped = std::clamp<qreal>(width, ConfigDefaults::scrollingDefaultColumnWidthProportionMin(),
                                            ConfigDefaults::scrollingDefaultColumnWidthProportionMax());
    QVariantMap column = m_columns.at(index).toMap();
    if (qFuzzyCompare(column.value(KeyWidth).toDouble(), clamped)) {
        return;
    }
    const QVariantMap oldState = snapshot();
    QVariantMap state = oldState;
    QVariantList columns = state.value(KeyColumns).toList();
    column[KeyWidth] = clamped;
    columns[index] = column;
    state[KeyColumns] = columns;
    const QString mergeKey = interactive ? QStringLiteral("columnWidth:%1:%2").arg(index).arg(m_gesture) : QString();
    pushCommand(PhosphorI18n::tr("Resize Column"), oldState, state, mergeKey);
}

void EditorTemplateModel::setColumnDisplay(int index, int display)
{
    if (index < 0 || index >= m_columns.size()) {
        return;
    }
    if (!ConfigDefaults::isValidScrollingColumnDisplay(display)) {
        qCWarning(lcEditor) << "setColumnDisplay: rejecting unknown display" << display;
        return;
    }
    QVariantMap column = m_columns.at(index).toMap();
    if (column.value(KeyDisplay).toInt() == display) {
        return;
    }
    const QVariantMap oldState = snapshot();
    QVariantMap state = oldState;
    QVariantList columns = state.value(KeyColumns).toList();
    column[KeyDisplay] = display;
    columns[index] = column;
    state[KeyColumns] = columns;
    pushCommand(PhosphorI18n::tr("Change Column Display"), oldState, state);
}

void EditorTemplateModel::beginWidthDrag()
{
    ++m_gesture;
}

QVariantMap EditorTemplateModel::scrollingConstants() const
{
    // Editor-process sibling of SettingsController::scrollingConstants(),
    // trimmed to the keys the template canvas and panel actually read. Same
    // spellings as the settings-side map so form logic stays portable
    // between the two processes.
    return {
        {QStringLiteral("kindProportion"), ConfigDefaults::scrollingWidthKindProportion()},
        {QStringLiteral("kindFixed"), ConfigDefaults::scrollingWidthKindFixed()},
        {QStringLiteral("kindClientDecides"), ConfigDefaults::scrollingWidthKindClientDecides()},
        {QStringLiteral("kindPreset"), ConfigDefaults::scrollingWidthKindPreset()},
        {QStringLiteral("proportionMin"), ConfigDefaults::scrollingDefaultColumnWidthProportionMin()},
        {QStringLiteral("proportionMax"), ConfigDefaults::scrollingDefaultColumnWidthProportionMax()},
        {QStringLiteral("fixedMin"), ConfigDefaults::scrollingDefaultColumnWidthFixedMin()},
        {QStringLiteral("fixedMax"), ConfigDefaults::scrollingDefaultColumnWidthFixedMax()},
        {QStringLiteral("maxTemplateColumns"), PhosphorZones::MaxTemplateColumns},
        {QStringLiteral("fractionDedupeEpsilon"), PhosphorZones::FractionDedupeEpsilon},
        {QStringLiteral("descriptionMaxLength"), MaxTemplateDescriptionLength},
        // Keyboard resize increment for a blueprint column (Shift+Arrow on
        // the strip canvas). Finer than the settings sliders' 5% notch on
        // purpose; this map is its single authority.
        {QStringLiteral("keyboardResizeStep"), 0.01},
    };
}

} // namespace PlasmaZones
