// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

/**
 * @file PhosphorWindowRule.h
 * @brief Umbrella header for the phosphor-windowrule library.
 *
 * phosphor-windowrule is the LGPL rule engine linked by both the GPL KWin
 * effect and the GPL daemon, so there is exactly one match-code
 * implementation. It provides:
 *
 *   - MatchTypes      — Field / Operator enums + strict string conversion
 *   - WindowQuery     — the attribute bag an expression is evaluated against
 *   - MatchExpression — the composable leaf/composite predicate tree
 *   - RuleAction      — pluggable slot-based action descriptors + registry
 *   - WindowRule      — { id, name, enabled, priority, match, actions }
 *   - WindowRuleSet   — ordered collection; revision counter; (de)serialization
 *   - RuleEvaluator   — descending-priority resolution + match cache
 *
 * No QObjects, no QML, no D-Bus — those belong to higher-level GPL targets.
 */

#include "MatchExpression.h"
#include "MatchTypes.h"
#include "RuleAction.h"
#include "RuleEvaluator.h"
#include "WindowQuery.h"
#include "WindowRule.h"
#include "WindowRuleSet.h"
