// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace PlasmaZones::RuleAuthoring {

/// Match fields suitable for the leaf-editor field dropdown. Each entry:
/// `{ value: int (Field enum), wire: QString (JSON wire string),
///    label, valueKind: "string"|"number"|"bool"|"screen"|"activity" }`.
/// QML keys off `wire` so it never has to reconstruct the enum↔wire-string
/// table.
QVariantList matchFields();

/// Operators valid for @p fieldValue (a `PhosphorRules::Field` enum int).
/// Each entry: `{ value: int (Operator enum), wire: QString, label }`.
QVariantList operatorsForField(int fieldValue);

/// Every operator with its translated label, independent of any field. Same
/// entry shape as operatorsForField. The leaf editor sizes the operator
/// dropdown to the widest of these so the operator column lines up across
/// condition rows and doesn't resize when the field — and thus its valid
/// operator subset — changes.
QVariantList allOperators();

/// Optional translated input hint for a match condition's value editor, keyed on
/// the operator wire token @p op (the leaf's `node.op`). Non-empty only for
/// operators whose value editor is a free-text box and whose syntax / matching
/// semantics aren't obvious (regex, app-id match); empty otherwise. The
/// match-side counterpart to the action-param hints.
QString matchValueHint(const QString& op);

/// Registered action types for the action-editor dropdown. Each entry:
/// `{ value: QString (action type id), label, params: [ ... ],
///    domain: "context"|"window" }`. See `RuleController::actionTypes`
/// doc-comment for the full descriptor schema — the controller delegates
/// here and surfaces this list verbatim.
QVariantList actionTypes();

/// Polarity-aware phrase for a boolean action's current value — e.g.
/// `SetBorderVisible` → "Show border" when @p on, "Hide border" when off. The
/// single source of truth shared by the rule-list summary (`RuleModel`) and the
/// editor toggle caption (`ActionRow`), so the two never drift. Returns an empty
/// string for a non-boolean or unknown action type.
QString boolActionStateLabel(const QString& typeWire, bool on);

/// Translated label for one enum wire value on action @p typeWire, param @p key.
/// Structural enum membership lives on the descriptor; the human-facing label is
/// per `(type, key, wireValue)`. Shared by the action editor's enum options and
/// the rule-list summary (`RuleModel`) so the picker and the summary never drift.
/// Returns @p wireValue unchanged for an action/param without a translated vocab.
QString enumOptionLabel(const QString& typeWire, const QString& key, const QString& wireValue);

/// Translated label for a WindowType match value (the int underlying the
/// PhosphorProtocol::WindowType enum) — e.g. 2 → "Dialog". Single source shared by
/// the editor dropdown (matchFields) and the collapsed rule-list summary (RuleModel)
/// so the two never drift. Returns the raw int as a string for an unknown value.
QString windowTypeLabel(int windowTypeValue);

/// A complete, default-seeded action payload for @p typeWire — a JSON map of
/// the form `{ type: <typeWire>, ...defaults }` ready to drop into a rule's
/// `actions` list. See `RuleController::defaultPayloadFor` for the
/// seeding contract (the controller delegates here).
QVariantMap defaultPayloadFor(const QString& typeWire);

} // namespace PlasmaZones::RuleAuthoring
