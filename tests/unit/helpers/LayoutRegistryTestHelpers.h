// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorWindowRules/WindowRuleStore.h>
#include <PhosphorZones/LayoutRegistry.h>

#include "config/configdefaults.h"

namespace PlasmaZones {
namespace TestHelpers {

/**
 * @brief Construct a LayoutRegistry wired to an owned WindowRuleStore.
 *
 * Phase 3b removed the temporary IBackend-taking LayoutRegistry ctor — the
 * registry now resolves its per-context assignment cascade entirely through a
 * @ref PhosphorWindowRules::WindowRuleStore. Tests need a store that outlives
 * the registry (the registry's RuleEvaluator holds a reference to the store's
 * rule set), so this helper creates the store first, hands its pointer to the
 * registry, then re-parents the store to the registry: the store is destroyed
 * by @c ~QObject of the registry, which runs AFTER the registry's member
 * destructors (so the evaluator's reference is already gone).
 *
 * The store points at @c ConfigDefaults::windowRulesFilePath(), which resolves
 * inside the test's @ref IsolatedConfigGuard tempdir — each test gets a fresh,
 * empty rule set on disk.
 *
 * @param layoutSubdirectory XDG-relative layout discovery path (callers pass
 *        @c "plasmazones/layouts" to match production).
 * @param parent Qt parent for the registry.
 */
inline PhosphorZones::LayoutRegistry* makeLayoutRegistry(const QString& layoutSubdirectory, QObject* parent = nullptr)
{
    auto* store = new PhosphorWindowRules::WindowRuleStore(ConfigDefaults::windowRulesFilePath());
    auto* registry = new PhosphorZones::LayoutRegistry(store, layoutSubdirectory, parent);
    store->setParent(registry);
    return registry;
}

} // namespace TestHelpers
} // namespace PlasmaZones
