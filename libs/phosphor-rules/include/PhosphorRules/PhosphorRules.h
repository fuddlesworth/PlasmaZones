// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

/**
 * @file PhosphorRules.h
 * @brief Umbrella header for the phosphor-rules library.
 *
 * phosphor-rules is the LGPL rule engine linked by both the GPL KWin
 * effect and the GPL daemon, so there is exactly one match-code
 * implementation. It provides:
 *
 *   - MatchTypes        — Field / Operator enums + strict string conversion
 *   - WindowQuery       — the attribute bag an expression is evaluated against
 *   - MatchExpression   — the composable leaf/composite predicate tree
 *   - RuleAction        — pluggable slot-based action descriptors + registry
 *   - Rule        — { id, name, enabled, priority, match, actions, managed }
 *   - RuleSet     — ordered collection; revision counter; (de)serialization
 *   - RuleEvaluator     — descending-priority resolution + match cache
 *   - RuleStore   — QObject persistent store over rules.json
 *   - ContextRuleBridge — header-only context-rule helpers (per-desktop /
 *                         per-activity layer-rule fan-out)
 *   - ExclusionRules    — slicers for the Exclude / ExcludeAnimations
 *                         action shapes (declarations in
 *                         ExclusionRules.h, bodies in
 *                         src/exclusionrules.cpp)
 *   - IdentityKey       — length-prefixed key encoder (UUIDv5 namespace
 *                         derivation, dedup keys)
 *   - RuleLogging — Q_DECLARE_LOGGING_CATEGORY re-exports for
 *                         header-only consumers
 *
 * The only QObject is RuleStore (Qt6::Core only). No QML, no D-Bus —
 * those belong to higher-level GPL targets.
 */

#include "ContextRuleBridge.h"
#include "ExclusionRules.h"
#include "IdentityKey.h"
#include "MatchExpression.h"
#include "MatchTypes.h"
#include "RuleAction.h"
#include "RuleEvaluator.h"
#include "WindowQuery.h"
#include "Rule.h"
#include "RuleLogging.h"
#include "RuleSet.h"
#include "RuleStore.h"
