// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace PlasmaZones::WindowRuleAuthoring {

/// Match fields suitable for the leaf-editor field dropdown. Each entry:
/// `{ value: int (Field enum), wire: QString (JSON wire string),
///    label, valueKind: "string"|"number"|"bool"|"screen"|"activity" }`.
/// QML keys off `wire` so it never has to reconstruct the enum↔wire-string
/// table.
QVariantList matchFields();

/// Operators valid for @p fieldValue (a `PhosphorWindowRules::Field` enum int).
/// Each entry: `{ value: int (Operator enum), wire: QString, label }`.
QVariantList operatorsForField(int fieldValue);

/// Every operator with its translated label, independent of any field. Same
/// entry shape as operatorsForField. The leaf editor sizes the operator
/// dropdown to the widest of these so the operator column lines up across
/// condition rows and doesn't resize when the field — and thus its valid
/// operator subset — changes.
QVariantList allOperators();

/// Registered action types for the action-editor dropdown. Each entry:
/// `{ value: QString (action type id), label, params: [ ... ],
///    domain: "context"|"window" }`. See `WindowRuleController::actionTypes`
/// doc-comment for the full descriptor schema — the controller delegates
/// here and surfaces this list verbatim.
QVariantList actionTypes();

/// A complete, default-seeded action payload for @p typeWire — a JSON map of
/// the form `{ type: <typeWire>, ...defaults }` ready to drop into a rule's
/// `actions` list. See `WindowRuleController::defaultPayloadFor` for the
/// seeding contract (the controller delegates here).
QVariantMap defaultPayloadFor(const QString& typeWire);

} // namespace PlasmaZones::WindowRuleAuthoring
