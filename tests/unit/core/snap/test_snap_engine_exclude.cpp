// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "helpers/SnapEngineTestFixture.h"

/**
 * @brief SnapEngine exclusion + cross-mode handoff coverage: setExcludeRuleSet
 *        wiring, isWindowExcluded full-query/size checks, the resolveWindowRestore
 *        size-exclusion consumer, and entryZoneForCrossing.
 */
class TestSnapEngineExclude : public SnapEngineTestFixture
{
    Q_OBJECT

private Q_SLOTS:

    // =========================================================================
    // setExcludeRuleSet + isAppIdExcluded wiring tests
    //
    // Daemon owns the filtered Exclude rule set and pushes its address into
    // SnapEngine via `setExcludeRuleSet`. SnapEngine lazily binds a
    // `RuleEvaluator` to that set on first `isAppIdExcluded` call and
    // re-binds it whenever the pointer changes. An in-place edit through
    // `RuleSet::setRules` bumps the revision counter; the evaluator's
    // per-revision sort index + match cache invalidate automatically.
    //
    // These tests pin the contract the daemon's `refilterExcludeRules`
    // lambda + `Daemon::stop()` `setExcludeRuleSet(nullptr)` teardown
    // rely on:
    //   - nullptr borrow ⇒ isAppIdExcluded == false (early-init fast path)
    //   - empty set      ⇒ isAppIdExcluded == false (no-exclusions fast path)
    //   - matching rule  ⇒ isAppIdExcluded == true
    //   - pointer change ⇒ cached evaluator drops + rebinds against new set
    //   - in-place edit  ⇒ revision bump invalidates the eval cache
    // =========================================================================

    void testExcludeWiring_nullptrBorrowReturnsFalse()
    {
        SnapEngine engine(nullptr, m_wts, nullptr, nullptr, nullptr);
        // No setExcludeRuleSet call — m_excludeRuleSet starts nullptr.
        QVERIFY(!engine.isAppIdExcluded(QStringLiteral("firefox")));
    }

    void testExcludeWiring_emptySetReturnsFalse()
    {
        SnapEngine engine(nullptr, m_wts, nullptr, nullptr, nullptr);
        PhosphorRules::RuleSet emptySet;
        engine.setExcludeRuleSet(&emptySet);
        QVERIFY(!engine.isAppIdExcluded(QStringLiteral("firefox")));
    }

    void testExcludeWiring_matchingRuleReturnsTrue()
    {
        SnapEngine engine(nullptr, m_wts, nullptr, nullptr, nullptr);

        PhosphorRules::RuleSet set;
        PhosphorRules::Rule rule;
        rule.id = QUuid::createUuid();
        rule.name = QStringLiteral("exclude-firefox");
        rule.enabled = true;
        rule.match = PhosphorRules::MatchExpression::makeLeaf(
            PhosphorRules::Field::AppId, PhosphorRules::Operator::AppIdMatches, QStringLiteral("firefox"));
        PhosphorRules::RuleAction action;
        action.type = QString(PhosphorRules::ActionType::Exclude);
        rule.actions.append(action);
        QVERIFY(set.addRule(rule));

        engine.setExcludeRuleSet(&set);
        QVERIFY(engine.isAppIdExcluded(QStringLiteral("firefox")));
        // Non-matching appId resolves to not-excluded against the same
        // bound set — the evaluator differentiates correctly.
        QVERIFY(!engine.isAppIdExcluded(QStringLiteral("konsole")));
    }

    void testExcludeWiring_pointerChangeRebindsEvaluator()
    {
        SnapEngine engine(nullptr, m_wts, nullptr, nullptr, nullptr);

        PhosphorRules::RuleSet firefoxSet;
        PhosphorRules::Rule firefoxRule;
        firefoxRule.id = QUuid::createUuid();
        firefoxRule.enabled = true;
        firefoxRule.match = PhosphorRules::MatchExpression::makeLeaf(
            PhosphorRules::Field::AppId, PhosphorRules::Operator::AppIdMatches, QStringLiteral("firefox"));
        PhosphorRules::RuleAction firefoxAction;
        firefoxAction.type = QString(PhosphorRules::ActionType::Exclude);
        firefoxRule.actions.append(firefoxAction);
        QVERIFY(firefoxSet.addRule(firefoxRule));

        PhosphorRules::RuleSet konsoleSet;
        PhosphorRules::Rule konsoleRule;
        konsoleRule.id = QUuid::createUuid();
        konsoleRule.enabled = true;
        konsoleRule.match = PhosphorRules::MatchExpression::makeLeaf(
            PhosphorRules::Field::AppId, PhosphorRules::Operator::AppIdMatches, QStringLiteral("konsole"));
        PhosphorRules::RuleAction konsoleAction;
        konsoleAction.type = QString(PhosphorRules::ActionType::Exclude);
        konsoleRule.actions.append(konsoleAction);
        QVERIFY(konsoleSet.addRule(konsoleRule));

        // Wire the firefox set, prime the cached evaluator.
        engine.setExcludeRuleSet(&firefoxSet);
        QVERIFY(engine.isAppIdExcluded(QStringLiteral("firefox")));
        QVERIFY(!engine.isAppIdExcluded(QStringLiteral("konsole")));

        // Re-wire to the konsole set — the cached evaluator was bound to
        // firefoxSet by reference; without the pointer-change rebind in
        // setExcludeRuleSet, this would still resolve "firefox" as
        // excluded and "konsole" as not.
        engine.setExcludeRuleSet(&konsoleSet);
        QVERIFY(!engine.isAppIdExcluded(QStringLiteral("firefox")));
        QVERIFY(engine.isAppIdExcluded(QStringLiteral("konsole")));

        // Clear: nullptr borrow short-circuits to false again.
        engine.setExcludeRuleSet(nullptr);
        QVERIFY(!engine.isAppIdExcluded(QStringLiteral("firefox")));
        QVERIFY(!engine.isAppIdExcluded(QStringLiteral("konsole")));
    }

    // isWindowExcluded — full-query exclusion (parity with autotile)
    //
    // With the daemon's exclusion query provider wired, a window's FULL
    // attributes are evaluated, so an Exclude rule keyed on a non-appId field
    // (here Title) matches — where the appId-only path silently would not.
    void testWindowExcluded_fullQueryMatchesNonAppIdRule()
    {
        SnapEngine engine(nullptr, m_wts, nullptr, nullptr, nullptr);

        PhosphorRules::RuleSet set;
        PhosphorRules::Rule rule;
        rule.id = QUuid::createUuid();
        rule.enabled = true;
        rule.match = PhosphorRules::MatchExpression::makeLeaf(
            PhosphorRules::Field::Title, PhosphorRules::Operator::Contains, QStringLiteral("scratch"));
        PhosphorRules::RuleAction action;
        action.type = QString(PhosphorRules::ActionType::Exclude);
        rule.actions.append(action);
        QVERIFY(set.addRule(rule));
        engine.setExcludeRuleSet(&set);

        // Without the provider, the appId-only query can't see the title — the
        // Title rule stays inert (the historical gap).
        QVERIFY(!engine.isWindowExcluded(QStringLiteral("app|win")));

        // With the provider supplying a full query, the Title rule matches.
        engine.setExclusionQueryProvider([](const QString&) {
            PhosphorRules::WindowQuery q;
            q.appId = QStringLiteral("editor");
            q.title = QStringLiteral("scratchpad - notes");
            return std::optional<PhosphorRules::WindowQuery>(q);
        });
        QVERIFY(engine.isWindowExcluded(QStringLiteral("app|win")));

        // A non-matching title is not excluded.
        engine.setExclusionQueryProvider([](const QString&) {
            PhosphorRules::WindowQuery q;
            q.title = QStringLiteral("main window");
            return std::optional<PhosphorRules::WindowQuery>(q);
        });
        QVERIFY(!engine.isWindowExcluded(QStringLiteral("app|win")));
    }

    // isWindowExcluded — minimum-window-size exclusion (parity with autotile)
    void testWindowExcluded_minimumWindowSize()
    {
        SnapEngine engine(nullptr, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_settings->setMinimumWindowWidth(200);
        m_settings->setMinimumWindowHeight(150);

        // Sub-threshold frame → excluded (when the full query carries the size).
        engine.setExclusionQueryProvider([](const QString&) {
            PhosphorRules::WindowQuery q;
            q.width = 120;
            q.height = 90;
            return std::optional<PhosphorRules::WindowQuery>(q);
        });
        QVERIFY(engine.isWindowExcluded(QStringLiteral("app|tiny")));

        // At/above threshold → not excluded.
        engine.setExclusionQueryProvider([](const QString&) {
            PhosphorRules::WindowQuery q;
            q.width = 800;
            q.height = 600;
            return std::optional<PhosphorRules::WindowQuery>(q);
        });
        QVERIFY(!engine.isWindowExcluded(QStringLiteral("app|big")));

        // Exactly AT the threshold → not excluded (the comparison is strict `<`).
        engine.setExclusionQueryProvider([](const QString&) {
            PhosphorRules::WindowQuery q;
            q.width = 200; // == minW
            q.height = 150; // == minH
            return std::optional<PhosphorRules::WindowQuery>(q);
        });
        QVERIFY(!engine.isWindowExcluded(QStringLiteral("app|exact")));

        // OR semantics: width under but height over → excluded (pins that the
        // check is OR, not AND, and that each dimension is evaluated).
        engine.setExclusionQueryProvider([](const QString&) {
            PhosphorRules::WindowQuery q;
            q.width = 120; // under 200
            q.height = 600; // over 150
            return std::optional<PhosphorRules::WindowQuery>(q);
        });
        QVERIFY(engine.isWindowExcluded(QStringLiteral("app|narrow")));

        // Symmetric: height under but width over → excluded.
        engine.setExclusionQueryProvider([](const QString&) {
            PhosphorRules::WindowQuery q;
            q.width = 800; // over 200
            q.height = 90; // under 150
            return std::optional<PhosphorRules::WindowQuery>(q);
        });
        QVERIFY(engine.isWindowExcluded(QStringLiteral("app|short")));

        // A zero threshold disables that dimension: a 1x1 window is NOT excluded
        // by size when both thresholds are 0 (guards the `> 0` enable check).
        m_settings->setMinimumWindowWidth(0);
        m_settings->setMinimumWindowHeight(0);
        engine.setExclusionQueryProvider([](const QString&) {
            PhosphorRules::WindowQuery q;
            q.width = 1;
            q.height = 1;
            return std::optional<PhosphorRules::WindowQuery>(q);
        });
        QVERIFY(!engine.isWindowExcluded(QStringLiteral("app|zero-thresh")));

        // No provider → no frame size is known, so the size threshold is not
        // applied (the appId-only fallback, preserving historical behaviour).
        m_settings->setMinimumWindowWidth(200);
        m_settings->setMinimumWindowHeight(150);
        engine.setExclusionQueryProvider({});
        QVERIFY(!engine.isWindowExcluded(QStringLiteral("app|unknown")));
    }

    // resolveWindowRestore — full-query/size exclusion path (parity consumer)
    //
    // isWindowExcluded is exercised in isolation above; this pins the production
    // wiring in resolveWindowRestore (lifecycle.cpp), which switched from the
    // appId-only isAppIdExcluded to the full-query isWindowExcluded. A window
    // with no stored record falls through to the legacy chain and must be
    // refused (noSnap) when its full query is sub-threshold, with the distinct
    // "excluded by rule or size" log identifying the branch that produced it.
    void testResolveWindowRestore_excludedBySize_returnsNoSnap()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());

        m_settings->setMinimumWindowWidth(200);
        m_settings->setMinimumWindowHeight(150);
        // Full query carries a sub-threshold frame → isWindowExcluded is true.
        engine.setExclusionQueryProvider([](const QString&) {
            PhosphorRules::WindowQuery q;
            q.width = 80;
            q.height = 60;
            return std::optional<PhosphorRules::WindowQuery>(q);
        });

        // No stored record for this windowId → resolveWindowRestore falls through
        // to the legacy chain and hits the exclusion check.
        PhosphorEngine::SnapResult result;
        const QStringList lines =
            captureResolveLogs(engine, QStringLiteral("app|tiny-restore"), QStringLiteral("DP-1"), &result);

        QVERIFY2(!result.shouldSnap, "a size-excluded window must not be auto-restored");
        QVERIFY2(lines.join(QLatin1Char('\n')).contains(QStringLiteral("excluded by rule or size")),
                 "the exclusion branch (rule or size) must be the one that refused the restore");
        m_wts->setSnapState(nullptr);
    }

    // Honest scope of this test (renamed from the earlier
    // `…InvalidatesEvalCache` name): exercises that across in-place
    // `RuleSet::setRules` edits at the SAME bound pointer, the
    // evaluator surfaces the post-edit rule set (not stale results
    // from the pre-edit rule set). `RuleSet::setRules`
    // unconditionally bumps the revision counter, so the evaluator's
    // revision-equality guard
    // (`m_priorityOrderRevision == revision`) catches every transition
    // here independently of the also-present size guard
    // (`m_priorityOrderRulesSize == rules.size()`) — the size guard
    // only fires under a quint64 revision wraparound the production
    // path effectively never hits (~5.85 billion years of one-per-
    // second edits, see RuleEvaluator's own commentary). This test
    // doesn't pin the size guard in isolation; a fixture that did
    // would need test-only hooks to force a revision collision. The
    // grow-the-list (1 → 2) step is kept because it makes
    // false-negative regressions in the priority-order rebuild
    // visible (cached `[0]` walk would skip rules[1]), even though
    // the revision guard catches it first in the current code.
    void testExcludeWiring_inPlaceSetRulesRespectsRevisionBump()
    {
        SnapEngine engine(nullptr, m_wts, nullptr, nullptr, nullptr);

        PhosphorRules::RuleSet set;
        // Wire BEFORE adding rules so the bound pointer doesn't change
        // when we mutate the set; this is the daemon's actual pattern
        // (setExcludeRuleSet wired once at init, edits happen via
        // setRules from the rulesChanged subscription).
        engine.setExcludeRuleSet(&set);

        const auto excludeRule = [](const QString& pattern) {
            PhosphorRules::Rule r;
            r.id = QUuid::createUuid();
            r.enabled = true;
            r.match = PhosphorRules::MatchExpression::makeLeaf(PhosphorRules::Field::AppId,
                                                               PhosphorRules::Operator::AppIdMatches, pattern);
            PhosphorRules::RuleAction a;
            a.type = QString(PhosphorRules::ActionType::Exclude);
            r.actions.append(a);
            return r;
        };

        // Step 1: rule A exists (size 1). Querying primes the cached
        // evaluator against the bound set at its current revision; the
        // resolved priority-order index is `[0]`.
        set.setRules({excludeRule(QStringLiteral("appA"))});
        QVERIFY(engine.isAppIdExcluded(QStringLiteral("appA")));
        QVERIFY(!engine.isAppIdExcluded(QStringLiteral("appB")));

        // Step 2: swap rule A for rule B at the SAME size (1 → 1).
        // Verifies that the new rule's match is picked up — the
        // priorityOrder cache happens to remain `[0]` which still
        // indexes the only post-swap rule, so this step alone does
        // NOT discriminate revision-bump invalidation from a stale
        // cache (the broken walk visits rules[0] which IS the new
        // rule). It does verify that the engine doesn't latch onto
        // the OLD rule's pattern, which is the load-bearing user-
        // facing property.
        set.setRules({excludeRule(QStringLiteral("appB"))});
        QVERIFY(!engine.isAppIdExcluded(QStringLiteral("appA")));
        QVERIFY(engine.isAppIdExcluded(QStringLiteral("appB")));

        // Step 3: GROW the list (1 → 2). This is the step that
        // discriminates a working `priorityOrder()` rebuild from a
        // broken one. The cached permutation from Step 2 has size 1;
        // the new set has size 2. Without per-revision invalidation,
        // the cached `[0]` walk would only visit rules[0] (the
        // already-known "appB" rule) — rules[1] ("appC") would never
        // be evaluated and `isAppIdExcluded("appC")` would return
        // false (false-negative). The pass verdict requires BOTH
        // pre-existing AND newly-appended rules to resolve correctly.
        set.setRules({excludeRule(QStringLiteral("appB")), excludeRule(QStringLiteral("appC"))});
        QVERIFY(engine.isAppIdExcluded(QStringLiteral("appB")));
        QVERIFY(engine.isAppIdExcluded(QStringLiteral("appC")));
        QVERIFY(!engine.isAppIdExcluded(QStringLiteral("appA")));

        // Step 4: clear via empty setRules. This goes through the
        // `isEmpty()` fast path in `SnapEngine::isAppIdExcluded` (not
        // the evaluator), but verifies the bound pointer survives the
        // mutation cleanly.
        set.setRules({});
        QVERIFY(!engine.isAppIdExcluded(QStringLiteral("appA")));
        QVERIFY(!engine.isAppIdExcluded(QStringLiteral("appB")));
        QVERIFY(!engine.isAppIdExcluded(QStringLiteral("appC")));
    }

    // ── Cross-mode handoff (Phase 3): the entry zone a window enters when it
    //    crosses onto a snap neighbour is the edge zone facing back toward the
    //    source (crossing "right" → the neighbour's LEFT-edge zone). ────────────
    void testEntryZoneForCrossing_facingEdge()
    {
        // Minimal adjacency stub: records the (direction, screen) it is asked for
        // and returns a deterministic zone id so we can assert the opposite-edge
        // mapping entryZoneForCrossing applies.
        struct FakeAdjacency : PhosphorSnapEngine::IZoneAdjacencyResolver
        {
            mutable QString lastDirection;
            mutable QString lastScreen;
            QString getAdjacentZone(const QString&, const QString&, const QString&) const override
            {
                return {};
            }
            QString getFirstZoneInDirection(const QString& direction, const QString& screenId) const override
            {
                lastDirection = direction;
                lastScreen = screenId;
                return QStringLiteral("entry:") + direction;
            }
        };
        FakeAdjacency adj;
        SnapEngine engine(nullptr, m_wts, nullptr, nullptr, nullptr);
        engine.setZoneAdjacencyResolver(&adj);

        // Crossing RIGHT enters the neighbour from its LEFT edge.
        QCOMPARE(engine.entryZoneForCrossing(QStringLiteral("right"), QStringLiteral("DP-2")),
                 QStringLiteral("entry:left"));
        QCOMPARE(adj.lastDirection, QStringLiteral("left"));
        QCOMPARE(adj.lastScreen, QStringLiteral("DP-2"));
        // The other directions invert correctly.
        QCOMPARE(engine.entryZoneForCrossing(QStringLiteral("left"), QStringLiteral("DP-2")),
                 QStringLiteral("entry:right"));
        QCOMPARE(engine.entryZoneForCrossing(QStringLiteral("up"), QStringLiteral("DP-2")),
                 QStringLiteral("entry:down"));
        QCOMPARE(engine.entryZoneForCrossing(QStringLiteral("down"), QStringLiteral("DP-2")),
                 QStringLiteral("entry:up"));
        // An unknown token yields no entry zone.
        QVERIFY(engine.entryZoneForCrossing(QStringLiteral("diagonal"), QStringLiteral("DP-2")).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestSnapEngineExclude)
#include "test_snap_engine_exclude.moc"
