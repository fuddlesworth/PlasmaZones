<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: LGPL-2.1-or-later -->

# PhosphorWindowRule

The unified window/context rule engine for Phosphor: one composable
match-expression language, one pluggable action set, one serialization
format, and one evaluation pipeline with a match cache.

Both the GPL KWin effect and the GPL daemon link this LGPL-2.1+ library, so
there is exactly **one** match-code implementation (GPL→LGPL linking is
permitted).

## Components

| Header | Purpose |
|---|---|
| `MatchTypes.h` | `Field` / `Operator` enums + strict string conversion |
| `WindowQuery.h` | the attribute bag an expression is evaluated against |
| `MatchExpression.h` | composable leaf/composite predicate tree, JSON, cached regex |
| `RuleAction.h` | pluggable slot-based action descriptors + `ActionRegistry` |
| `WindowRule.h` | `{ id, name, enabled, priority, match, actions }` |
| `WindowRuleSet.h` | ordered collection, monotonic revision, `windowrules.json` I/O |
| `RuleEvaluator.h` | descending-priority resolution + `(windowId, revision)` match cache |

## Evaluation model

`RuleEvaluator::resolve()` walks the rule set in **descending priority**
(ties broken by list order via a stable sort), and for each matching enabled
rule accumulates the **first action that fills each slot**. Actions in
different slots stack, and a second action for an already-filled slot is ignored.
A matching rule with a terminal `Exclude` action stops the walk.

`ResolvedActions` distinguishes a **slot-unfilled** result (`std::nullopt`)
from a **slot-filled-with-empty-params** result. The animation engaged-empty
`effectId` sentinel depends on exactly this distinction.

An empty `All{}` match expression is the **always-true catch-all**, which is
the migrated provider default.

## Serialization

`WindowRuleSet` reads and writes `windowrules.json` at `"_version": 4`.
`fromJson` **refuses** any other version. Schema migration is the config
layer's job, never the library's. Loaders follow strict-validation
discipline: malformed rules/actions are dropped with a logged diagnostic, and the
set still loads.

## Dependencies

- `Qt6::Core` (no `Qt6::Gui`)
- `PhosphorProtocol::Types` — PUBLIC, for `WindowType`
- `PhosphorIdentity` — PRIVATE, backs the `AppIdMatches` operator

No QObjects, QML, or D-Bus. Those belong to the higher-level GPL targets.
