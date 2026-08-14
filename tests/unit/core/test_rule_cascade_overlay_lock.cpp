// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_rule_cascade_overlay_lock.cpp
 * @brief The overlay / lock / mode-routing half of the context-slot cascade
 *        proofs for the window-rule model.
 *
 * Split out of test_rule_cascade_context.cpp at the overlay banner when that
 * file passed the 1150-line ceiling. Covers the overlay shader / style
 * per-slot composition, the overlay APPEARANCE colour and opacity overrides,
 * context locks (resolution, slot-conflict, and composition with an
 * assignment), the OSD and drag-selector overrides (resolution, structural
 * field exclusions and the single-slot priority contest, shared driver), the
 * per-mode gap routing through the
 * context `Mode` field, the window-field negation-polarity guard that keeps
 * `none{AppId == x}` rules off windowless context queries, the
 * per-monitor-beats-per-mode specificity order, the colour-scheme cache-key
 * fold across every cached resolver, the slot-carrying admit gate that keeps a
 * terminal Exclude from dropping unrelated context overrides, and the
 * tiling-param payload type gates.
 *
 * The gap overrides, the orientation and ActiveLayout stamping (including the
 * scrolling template's prefixed stamp), the exactContextEntry pair, and the
 * autotile / scrolling context params stay in the original file. Both halves
 * share the harness in RuleCascadeFixture.h.
 */

#include <QColor>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QTest>
#include <QUuid>

#include <functional>

#include "RuleCascadeFixture.h"

class TestRuleCascadeOverlayLock : public QObject, public RuleCascadeFixture
{
    Q_OBJECT

private Q_SLOTS:

    // ─── Context overlay-property resolution (OverlayShader / OverlayStyle) ──
    // resolveContextOverlay is a per-slot read across all matching context
    // rules (mirrors resolveContextGaps): independent shader / style rules
    // compose, and the style wire token maps to the OverlayDisplayMode int.

    void testContextOverlay_perSlotComposition()
    {
        const auto overlayRule = [](const QString& name, int priority, const QString& screenId,
                                    const QList<PWR::RuleAction>& actions) {
            PWR::Rule r;
            r.id = QUuid::createUuid();
            r.name = name;
            r.enabled = true;
            r.priority = priority;
            r.match = PWR::MatchExpression::makeLeaf(PWR::Field::ScreenId, PWR::Operator::Equals, screenId);
            r.actions = actions;
            return r;
        };
        const auto shaderAction = [](const QString& id) {
            PWR::RuleAction a;
            a.type = QString(PWR::ActionType::OverrideOverlayShader);
            a.params.insert(QString(PWR::ActionParam::EffectId), id);
            return a;
        };
        const auto styleAction = [](const QString& token) {
            PWR::RuleAction a;
            a.type = QString(PWR::ActionType::OverrideOverlayStyle);
            a.params.insert(QString(PWR::ActionParam::Value), token);
            return a;
        };

        RegistryFixture f = makeRegistryFixture();
        // One rule sets ONLY the overlay shader; a separate rule sets ONLY the
        // overlay style. Different slots → both compose (no shadowing).
        const PWR::Rule sh = overlayRule(QStringLiteral("sh"), 400, QStringLiteral("DP-1"),
                                         {shaderAction(QStringLiteral("plasma-glow"))});
        const PWR::Rule st = overlayRule(QStringLiteral("st"), 300, QStringLiteral("DP-1"),
                                         {styleAction(QString(PWR::OverlayStyleToken::Preview))});
        QVERIFY(f.store->setAllRules({sh, st}));

        const PhosphorZones::ContextOverlayOverride resolved =
            f.registry->resolveContextOverlay(QStringLiteral("DP-1"), 0, QString());
        QVERIFY(resolved.shaderId.has_value());
        QCOMPARE(*resolved.shaderId, QStringLiteral("plasma-glow"));
        QVERIFY(resolved.style.has_value());
        QCOMPARE(*resolved.style, 1); // "preview" → OverlayDisplayMode::LayoutPreview

        // A context the rules do not pin → no override (falls through to layout).
        QVERIFY(f.registry->resolveContextOverlay(QStringLiteral("DP-2"), 0, QString()).isEmpty());

        // The "rectangles" token maps to OverlayDisplayMode::ZoneRectangles (0).
        const PWR::Rule rect = overlayRule(QStringLiteral("rect"), 500, QStringLiteral("HDMI-1"),
                                           {styleAction(QString(PWR::OverlayStyleToken::Rectangles))});
        QVERIFY(f.store->setAllRules({rect}));
        const PhosphorZones::ContextOverlayOverride r2 =
            f.registry->resolveContextOverlay(QStringLiteral("HDMI-1"), 0, QString());
        QVERIFY(r2.style.has_value());
        QCOMPARE(*r2.style, 0);
        QVERIFY(!r2.shaderId.has_value());

        // shaderParams round-trip: a shader override carrying a params object
        // resolves it into ContextOverlayOverride::shaderParams (the headline
        // shader-uniform-override feature). The nested object is stored as JSON
        // and decoded via toObject().toVariantMap().
        const auto shaderWithParams = [](const QString& id, const QJsonObject& params) {
            PWR::RuleAction a;
            a.type = QString(PWR::ActionType::OverrideOverlayShader);
            a.params.insert(QString(PWR::ActionParam::EffectId), id);
            a.params.insert(QString(PWR::ActionParam::Params), params);
            return a;
        };
        QJsonObject uniforms;
        uniforms.insert(QStringLiteral("intensity"), 0.5);
        const PWR::Rule shp = overlayRule(QStringLiteral("shp"), 600, QStringLiteral("DVI-1"),
                                          {shaderWithParams(QStringLiteral("plasma-glow"), uniforms)});
        QVERIFY(f.store->setAllRules({shp}));
        const PhosphorZones::ContextOverlayOverride r3 =
            f.registry->resolveContextOverlay(QStringLiteral("DVI-1"), 0, QString());
        QVERIFY(r3.shaderId.has_value());
        QCOMPARE(*r3.shaderId, QStringLiteral("plasma-glow"));
        QCOMPARE(r3.shaderParams.value(QStringLiteral("intensity")).toDouble(), 0.5);
    }

    // ─── Context overlay-APPEARANCE resolution (SetOverlay* colours / opacities
    //     / border dimensions / zone-number visibility) ────────────────────────
    // The appearance actions layer over the global Snapping.Zones.* config: each
    // fills its own optional on ContextOverlayOverride, and an unmatched context
    // leaves them all unset so the consumer falls through to config.
    void testContextOverlay_appearanceOverrides()
    {
        const auto valueAction = [](QLatin1StringView type, const QVariant& value) {
            PWR::RuleAction a;
            a.type = QString(type);
            a.params.insert(QString(PWR::ActionParam::Value), QJsonValue::fromVariant(value));
            return a;
        };
        PWR::Rule r;
        r.id = QUuid::createUuid();
        r.name = QStringLiteral("overlay appearance");
        r.enabled = true;
        r.priority = 400;
        r.match = PWR::MatchExpression::makeLeaf(PWR::Field::ScreenId, PWR::Operator::Equals, QStringLiteral("DP-1"));
        r.actions = {
            valueAction(PWR::ActionType::SetOverlayHighlightColor, QStringLiteral("#FF112233")),
            valueAction(PWR::ActionType::SetOverlayInactiveColor, QStringLiteral("#80445566")),
            valueAction(PWR::ActionType::SetOverlayBorderColor, QStringLiteral("#FFEEDDCC")),
            valueAction(PWR::ActionType::SetOverlayActiveOpacity, 0.5),
            valueAction(PWR::ActionType::SetOverlayInactiveOpacity, 0.25),
            valueAction(PWR::ActionType::SetOverlayBorderWidth, 3),
            valueAction(PWR::ActionType::SetOverlayBorderRadius, 12),
            valueAction(PWR::ActionType::SetOverlayShowZoneNumbers, false),
        };

        RegistryFixture f = makeRegistryFixture();
        QVERIFY(f.store->setAllRules({r}));

        const PhosphorZones::ContextOverlayOverride resolved =
            f.registry->resolveContextOverlay(QStringLiteral("DP-1"), 0, QString());
        QVERIFY(resolved.highlightColor.has_value());
        QCOMPARE(*resolved.highlightColor, QColor(QStringLiteral("#FF112233")));
        QVERIFY(resolved.inactiveColor.has_value());
        QCOMPARE(*resolved.inactiveColor, QColor(QStringLiteral("#80445566")));
        QVERIFY(resolved.borderColor.has_value());
        QCOMPARE(*resolved.borderColor, QColor(QStringLiteral("#FFEEDDCC")));
        QVERIFY(resolved.activeOpacity.has_value());
        QCOMPARE(*resolved.activeOpacity, 0.5);
        QVERIFY(resolved.inactiveOpacity.has_value());
        QCOMPARE(*resolved.inactiveOpacity, 0.25);
        QVERIFY(resolved.borderWidth.has_value());
        QCOMPARE(*resolved.borderWidth, 3);
        QVERIFY(resolved.borderRadius.has_value());
        QCOMPARE(*resolved.borderRadius, 12);
        QVERIFY(resolved.showZoneNumbers.has_value());
        QCOMPARE(*resolved.showZoneNumbers, false);

        // An unpinned context leaves every appearance field unset (config wins).
        QVERIFY(f.registry->resolveContextOverlay(QStringLiteral("DP-2"), 0, QString()).isEmpty());
    }

    // ─── Context lock resolution (ActionSlot::Locked) ─────────────────────
    // resolveContextLocked reads the boolean Locked slot off a matching
    // context rule. Mode-agnostic, never persisted — the daemon's
    // isContextLocked ORs it over the manual lock store.

    void testContextLock_resolution()
    {
        const auto lockRule = [](const QString& name, PWR::Field field, const QVariant& value, bool locked) {
            PWR::RuleAction a;
            a.type = QString(PWR::ActionType::LockContext);
            a.params.insert(QString(PWR::ActionParam::Value), locked);
            PWR::Rule r;
            r.id = QUuid::createUuid();
            r.name = name;
            r.enabled = true;
            r.priority = 400;
            r.match = PWR::MatchExpression::makeLeaf(field, PWR::Operator::Equals, value);
            r.actions = {a};
            return r;
        };

        RegistryFixture f = makeRegistryFixture();
        // A monitor lock (value = true) on DP-1, and an activity lock scoped to
        // "work". An explicit value = false lock on DP-3 must NOT lock.
        const PWR::Rule lockMonitor =
            lockRule(QStringLiteral("lock DP-1"), PWR::Field::ScreenId, QStringLiteral("DP-1"), true);
        const PWR::Rule lockActivity =
            lockRule(QStringLiteral("lock work"), PWR::Field::Activity, QStringLiteral("work-uuid"), true);
        const PWR::Rule unlockMonitor =
            lockRule(QStringLiteral("unlock DP-3"), PWR::Field::ScreenId, QStringLiteral("DP-3"), false);
        // A desktop-scoped lock: fires on virtual desktop 2 regardless of
        // screen/activity, proving the desktop axis of the context match.
        const PWR::Rule lockDesktop = lockRule(QStringLiteral("lock desktop 2"), PWR::Field::VirtualDesktop, 2, true);
        // A mixed (context + window-property) lock rule: All{ScreenId == DP-4,
        // AppId == firefox} carrying a LockContext action at a far-above band.
        // Against the windowless context query the AppId leaf evaluates false,
        // so the All{} fails and DP-4 must NOT lock — symmetric to the
        // assignment-path mixed-rule inertness proof (the testMixedRule* slots
        // in test_rule_cascade_fidelity.cpp).
        PWR::RuleAction mixedLockAction;
        mixedLockAction.type = QString(PWR::ActionType::LockContext);
        mixedLockAction.params.insert(QString(PWR::ActionParam::Value), true);
        PWR::Rule mixedLock;
        mixedLock.id = QUuid::createUuid();
        mixedLock.name = QStringLiteral("mixed lock DP-4");
        mixedLock.enabled = true;
        mixedLock.priority = 999;
        mixedLock.match = PWR::MatchExpression::makeAll(
            {PWR::MatchExpression::makeLeaf(PWR::Field::ScreenId, PWR::Operator::Equals, QStringLiteral("DP-4")),
             PWR::MatchExpression::makeLeaf(PWR::Field::AppId, PWR::Operator::Equals, QStringLiteral("firefox"))});
        mixedLock.actions = {mixedLockAction};
        QVERIFY(f.store->setAllRules({lockMonitor, lockActivity, unlockMonitor, lockDesktop, mixedLock}));

        // DP-1 is locked regardless of desktop/activity (screen-only match).
        QVERIFY(f.registry->resolveContextLocked(QStringLiteral("DP-1"), 0, QString()));
        QVERIFY(f.registry->resolveContextLocked(QStringLiteral("DP-1"), 3, QStringLiteral("anything")));
        // The activity lock fires only inside "work".
        QVERIFY(f.registry->resolveContextLocked(QStringLiteral("DP-2"), 0, QStringLiteral("work-uuid")));
        QVERIFY(!f.registry->resolveContextLocked(QStringLiteral("DP-2"), 0, QStringLiteral("play-uuid")));
        // The desktop lock fires on desktop 2 (any screen, no activity) and
        // not on other desktops.
        QVERIFY(f.registry->resolveContextLocked(QStringLiteral("HDMI-2"), 2, QString()));
        QVERIFY(!f.registry->resolveContextLocked(QStringLiteral("HDMI-2"), 1, QString()));
        // value = false resolves to not-locked (explicit no-op overlay).
        QVERIFY(!f.registry->resolveContextLocked(QStringLiteral("DP-3"), 0, QString()));
        // The mixed rule's AppId leaf can't match a windowless query → DP-4 not
        // locked, even though its band (999) would dominate if it leaked.
        QVERIFY(!f.registry->resolveContextLocked(QStringLiteral("DP-4"), 0, QString()));
        // A context no rule pins → not locked.
        QVERIFY(!f.registry->resolveContextLocked(QStringLiteral("HDMI-1"), 0, QString()));

        // Disabling the lock rule drops the lock (revision-invalidated cache):
        // DP-1 was primed locked above, so a stale cache would keep returning
        // true here — the post-mutation false proves the revision bump evicts.
        PWR::Rule disabled = lockMonitor;
        disabled.enabled = false;
        QVERIFY(f.store->setAllRules({disabled, lockActivity, unlockMonitor, lockDesktop, mixedLock}));
        QVERIFY(!f.registry->resolveContextLocked(QStringLiteral("DP-1"), 0, QString()));
        // The eviction is a whole-cache drop, not a zeroing: a lock left intact
        // (lockDesktop, also primed above) must still resolve true after the
        // revision bump rebuilds the cache.
        QVERIFY(f.registry->resolveContextLocked(QStringLiteral("HDMI-2"), 2, QString()));
    }

    // ─── Context OSD override — resolution ────────────────────────────────
    // resolveContextOsdEnabled reads the boolean OsdEnabled slot off a
    // matching context rule. Tri-state: nullopt with no rule (follow the
    // global toggles), explicit true forces OSDs on, explicit false
    // suppresses them.

    void testContextOsd_resolution()
    {
        const auto osdRule = [](const QString& name, PWR::Field field, const QVariant& value, bool enabled) {
            PWR::RuleAction a;
            a.type = QString(PWR::ActionType::SetOsdEnabled);
            a.params.insert(QString(PWR::ActionParam::Value), enabled);
            PWR::Rule r;
            r.id = QUuid::createUuid();
            r.name = name;
            r.enabled = true;
            r.priority = 400;
            r.match = PWR::MatchExpression::makeLeaf(field, PWR::Operator::Equals, value);
            r.actions = {a};
            return r;
        };

        RegistryFixture f = makeRegistryFixture();
        const PWR::Rule hideMonitor =
            osdRule(QStringLiteral("hide on DP-1"), PWR::Field::ScreenId, QStringLiteral("DP-1"), false);
        const PWR::Rule forceDesktop =
            osdRule(QStringLiteral("force on desktop 2"), PWR::Field::VirtualDesktop, 2, true);
        QVERIFY(f.store->setAllRules({hideMonitor, forceDesktop}));

        // DP-1 resolves an explicit false (suppress) regardless of activity.
        QCOMPARE(f.registry->resolveContextOsdEnabled(QStringLiteral("DP-1"), 0, QString()),
                 std::optional<bool>(false));
        // Desktop 2 resolves an explicit true (force) on any other screen.
        QCOMPARE(f.registry->resolveContextOsdEnabled(QStringLiteral("HDMI-2"), 2, QString()),
                 std::optional<bool>(true));
        // A context no rule pins resolves nullopt — follow the global toggles.
        QCOMPARE(f.registry->resolveContextOsdEnabled(QStringLiteral("HDMI-1"), 0, QString()), std::nullopt);

        // Revision invalidation: disabling the suppress rule drops the primed
        // false verdict, while the force rule survives the cache rebuild.
        PWR::Rule disabled = hideMonitor;
        disabled.enabled = false;
        QVERIFY(f.store->setAllRules({disabled, forceDesktop}));
        QCOMPARE(f.registry->resolveContextOsdEnabled(QStringLiteral("DP-1"), 0, QString()), std::nullopt);
        QCOMPARE(f.registry->resolveContextOsdEnabled(QStringLiteral("HDMI-2"), 2, QString()),
                 std::optional<bool>(true));
    }

    // ─── Context drag-selector override — resolution ─────────────────────
    // resolveContextDragSelectorEnabled is the twin of the OSD resolver above,
    // reading the boolean DragSelectorEnabled slot off a matching context rule.
    // Same tri-state: nullopt with no rule (follow the global selector
    // toggle), explicit true forces the popup on, explicit false suppresses it.

    void testContextDragSelector_resolution()
    {
        const auto selectorRule = [](const QString& name, PWR::Field field, const QVariant& value, bool enabled) {
            PWR::RuleAction a;
            a.type = QString(PWR::ActionType::SetDragSelectorEnabled);
            a.params.insert(QString(PWR::ActionParam::Value), enabled);
            PWR::Rule r;
            r.id = QUuid::createUuid();
            r.name = name;
            r.enabled = true;
            r.priority = 400;
            r.match = PWR::MatchExpression::makeLeaf(field, PWR::Operator::Equals, value);
            r.actions = {a};
            return r;
        };

        RegistryFixture f = makeRegistryFixture();
        const PWR::Rule hideMonitor =
            selectorRule(QStringLiteral("hide on DP-1"), PWR::Field::ScreenId, QStringLiteral("DP-1"), false);
        const PWR::Rule forceDesktop =
            selectorRule(QStringLiteral("force on desktop 2"), PWR::Field::VirtualDesktop, 2, true);
        QVERIFY(f.store->setAllRules({hideMonitor, forceDesktop}));

        // DP-1 resolves an explicit false (suppress) regardless of activity.
        QCOMPARE(f.registry->resolveContextDragSelectorEnabled(QStringLiteral("DP-1"), 0, QString()),
                 std::optional<bool>(false));
        // Desktop 2 resolves an explicit true (force) on any other screen.
        QCOMPARE(f.registry->resolveContextDragSelectorEnabled(QStringLiteral("HDMI-2"), 2, QString()),
                 std::optional<bool>(true));
        // A context no rule pins resolves nullopt — follow the global toggle.
        QCOMPARE(f.registry->resolveContextDragSelectorEnabled(QStringLiteral("HDMI-1"), 0, QString()), std::nullopt);

        // The two overrides are INDEPENDENT slots: neither resolver may read
        // the other's action. A shared slot id (the copy-paste failure this
        // verb's descriptor is most exposed to) would show up here as an OSD
        // verdict leaking out of the selector resolver.
        QCOMPARE(f.registry->resolveContextOsdEnabled(QStringLiteral("DP-1"), 0, QString()), std::nullopt);
        QCOMPARE(f.registry->resolveContextOsdEnabled(QStringLiteral("HDMI-2"), 2, QString()), std::nullopt);

        // Revision invalidation: disabling the suppress rule drops the primed
        // false verdict, while the force rule survives the cache rebuild.
        PWR::Rule disabled = hideMonitor;
        disabled.enabled = false;
        QVERIFY(f.store->setAllRules({disabled, forceDesktop}));
        QCOMPARE(f.registry->resolveContextDragSelectorEnabled(QStringLiteral("DP-1"), 0, QString()), std::nullopt);
        QCOMPARE(f.registry->resolveContextDragSelectorEnabled(QStringLiteral("HDMI-2"), 2, QString()),
                 std::optional<bool>(true));
    }

    // ─── ColorScheme match field — provider stamping + key self-heal ──────
    // A context rule matching Field::ColorScheme resolves against the
    // registry's colour-scheme provider, and a provider FLIP re-resolves
    // through the cache-key fold (no rule-set revision bump involved) —
    // the exact contract that makes day/night rules live.

    void testContextLock_colorSchemeRuleFollowsProviderFlips()
    {
        PWR::RuleAction a;
        a.type = QString(PWR::ActionType::LockContext);
        a.params.insert(QString(PWR::ActionParam::Value), true);
        PWR::Rule darkLock;
        darkLock.id = QUuid::createUuid();
        darkLock.name = QStringLiteral("lock in dark");
        darkLock.enabled = true;
        darkLock.priority = 400;
        darkLock.match =
            PWR::MatchExpression::makeLeaf(PWR::Field::ColorScheme, PWR::Operator::Equals, QStringLiteral("dark"));
        darkLock.actions = {a};

        RegistryFixture f = makeRegistryFixture();
        QVERIFY(f.store->setAllRules({darkLock}));

        QString scheme = QStringLiteral("light");
        f.registry->setColorSchemeProvider([&scheme]() -> std::optional<QString> {
            return scheme;
        });

        // Light: the dark-scoped rule does not match (and the verdict caches).
        QVERIFY(!f.registry->resolveContextLocked(QStringLiteral("DP-1"), 0, QString()));
        // Flip to dark: the cache key changes with the token, so the cached
        // light verdict cannot be returned stale.
        scheme = QStringLiteral("dark");
        QVERIFY(f.registry->resolveContextLocked(QStringLiteral("DP-1"), 0, QString()));
        // And back.
        scheme = QStringLiteral("light");
        QVERIFY(!f.registry->resolveContextLocked(QStringLiteral("DP-1"), 0, QString()));

        // Provider cleared: the token empties, the predicate goes inert, and
        // the rule stops matching rather than latching its last verdict.
        f.registry->setColorSchemeProvider({});
        QVERIFY(!f.registry->resolveContextLocked(QStringLiteral("DP-1"), 0, QString()));
    }

    // ─── Context lock — slot conflict resolution ──────────────────────────
    // When two LockContext rules pin the SAME context with opposing values,
    // the single Locked slot is won by the highest-priority rule (then list
    // order on a tie), exactly like the layout slot (testTieBreakIsListOrder).
    // The value itself does not bias the contest — proven by running it both
    // directions so neither "true always wins" nor "false always wins" passes.

    void testContextLock_priorityResolution()
    {
        const auto lockRuleAt = [](const QString& name, const QString& screenId, bool locked, int priority) {
            PWR::RuleAction a;
            a.type = QString(PWR::ActionType::LockContext);
            a.params.insert(QString(PWR::ActionParam::Value), locked);
            PWR::Rule r;
            r.id = QUuid::createUuid();
            r.name = name;
            r.enabled = true;
            r.priority = priority;
            r.match = PWR::MatchExpression::makeLeaf(PWR::Field::ScreenId, PWR::Operator::Equals, screenId);
            r.actions = {a};
            return r;
        };

        RegistryFixture f = makeRegistryFixture();
        // DP-9: higher-priority rule says NOT locked → wins over a lower one
        // that says locked.
        const PWR::Rule dp9High = lockRuleAt(QStringLiteral("dp9 unlock"), QStringLiteral("DP-9"), false, 500);
        const PWR::Rule dp9Low = lockRuleAt(QStringLiteral("dp9 lock"), QStringLiteral("DP-9"), true, 400);
        // DP-10: the inverse — higher-priority rule says locked → wins.
        const PWR::Rule dp10High = lockRuleAt(QStringLiteral("dp10 lock"), QStringLiteral("DP-10"), true, 500);
        const PWR::Rule dp10Low = lockRuleAt(QStringLiteral("dp10 unlock"), QStringLiteral("DP-10"), false, 400);
        // DP-11: equal priority, lock=true first — first-listed rule wins.
        const PWR::Rule dp11First = lockRuleAt(QStringLiteral("dp11 a"), QStringLiteral("DP-11"), true, 400);
        const PWR::Rule dp11Second = lockRuleAt(QStringLiteral("dp11 b"), QStringLiteral("DP-11"), false, 400);
        // DP-12: the inverse tie-break — lock=false first at the same priority.
        // Run both directions so the tie-break is proven to be list-order, not
        // value-bias: a "true always wins on a tie" bug would pass DP-11 alone.
        const PWR::Rule dp12First = lockRuleAt(QStringLiteral("dp12 a"), QStringLiteral("DP-12"), false, 400);
        const PWR::Rule dp12Second = lockRuleAt(QStringLiteral("dp12 b"), QStringLiteral("DP-12"), true, 400);
        QVERIFY(
            f.store->setAllRules({dp9High, dp9Low, dp10High, dp10Low, dp11First, dp11Second, dp12First, dp12Second}));

        // Highest priority wins regardless of the value it carries.
        QVERIFY(!f.registry->resolveContextLocked(QStringLiteral("DP-9"), 0, QString()));
        QVERIFY(f.registry->resolveContextLocked(QStringLiteral("DP-10"), 0, QString()));
        // Equal priority → first-listed wins, in both directions.
        QVERIFY(f.registry->resolveContextLocked(QStringLiteral("DP-11"), 0, QString()));
        QVERIFY(!f.registry->resolveContextLocked(QStringLiteral("DP-12"), 0, QString()));
    }

    // ─── Context lock composes with a layout/engine assignment ────────────
    // LockContext is terminal=false and fills the dedicated Locked slot, so a
    // lock-only rule must co-exist with a separate context-assignment rule on
    // the SAME context: the lock surfaces via resolveContextLocked AND the
    // layout still surfaces via assignmentEntryForScreen. Neither slot shadows
    // the other — the whole reason the action is non-terminal.

    void testContextLock_composesWithAssignment()
    {
        RegistryFixture f = makeRegistryFixture();

        // A lock-only rule (Locked slot) and a separate layout-assignment rule
        // (engine/layout slots), both pinned to DP-7 (screen-only match).
        PWR::RuleAction lockAction;
        lockAction.type = QString(PWR::ActionType::LockContext);
        lockAction.params.insert(QString(PWR::ActionParam::Value), true);
        PWR::Rule lock;
        lock.id = QUuid::createUuid();
        lock.name = QStringLiteral("lock DP-7");
        lock.enabled = true;
        lock.priority = 400;
        lock.match =
            PWR::MatchExpression::makeLeaf(PWR::Field::ScreenId, PWR::Operator::Equals, QStringLiteral("DP-7"));
        lock.actions = {lockAction};

        const PWR::Rule assign = CRB::makeAssignmentRule(QStringLiteral("layout DP-7"), QStringLiteral("DP-7"), 0,
                                                         QString(), QStringLiteral("snapping"),
                                                         QStringLiteral("{ctx-layout}"), QString(), 301, QString());
        QVERIFY(f.store->setAllRules({lock, assign}));

        // The lock surfaces (Locked slot) ...
        QVERIFY(f.registry->resolveContextLocked(QStringLiteral("DP-7"), 1, QString()));
        // ... and the layout assignment still surfaces (engine/layout slots),
        // unshadowed by the non-terminal lock rule.
        const PhosphorZones::AssignmentEntry entry =
            f.registry->assignmentEntryForScreen(QStringLiteral("DP-7"), 1, QString());
        QCOMPARE(entry.mode, PhosphorZones::AssignmentEntry::Snapping);
        QCOMPARE(entry.snappingLayout, QStringLiteral("{ctx-layout}"));
    }

    // ─── Per-mode gap rule resolves only for its mode ─────────────────────
    // A `Mode Equals "tiling"` gap rule is context-only (Mode is a context
    // field), so it participates in the gap cascade. resolveContextGaps must
    // pick up its inner gap when the asking engine is tiling, and ignore it
    // when the asking engine is snapping — the whole point of routing per-mode
    // gaps through the context `Mode` field instead of window-property IsTiled.

    void testPerModeGapRuleResolvesForMatchingModeOnly()
    {
        RegistryFixture f = makeRegistryFixture();

        PWR::RuleAction gapAction;
        gapAction.type = QString(PWR::ActionType::SetInnerGap);
        gapAction.params.insert(QString(PWR::ActionParam::Value), 14);
        PWR::Rule tilingGap;
        tilingGap.id = QUuid::createUuid();
        tilingGap.name = QStringLiteral("Tiling inner gap");
        tilingGap.enabled = true;
        tilingGap.priority = 500;
        tilingGap.match =
            PWR::MatchExpression::makeLeaf(PWR::Field::Mode, PWR::Operator::Equals, QStringLiteral("tiling"));
        tilingGap.actions = {gapAction};
        QVERIFY(tilingGap.match.isContextOnly());
        QVERIFY(f.store->setAllRules({tilingGap}));

        // Tiling engine asks → the per-mode gap applies.
        const PhosphorZones::ContextGapOverride tiled =
            f.registry->resolveContextGaps(QStringLiteral("DP-9"), 1, QString(), QStringLiteral("tiling"));
        QVERIFY(tiled.innerGap.has_value());
        QCOMPARE(*tiled.innerGap, 14);

        // Snapping engine asks → the Mode leaf is a non-match, so no override.
        const PhosphorZones::ContextGapOverride snapped =
            f.registry->resolveContextGaps(QStringLiteral("DP-9"), 1, QString(), QStringLiteral("snapping"));
        QVERIFY(!snapped.innerGap.has_value());

        // No mode supplied (mode-agnostic caller) → also a non-match.
        const PhosphorZones::ContextGapOverride none =
            f.registry->resolveContextGaps(QStringLiteral("DP-9"), 1, QString());
        QVERIFY(!none.innerGap.has_value());

        // NEGATED Mode leaf, mode-agnostic caller. This is the polarity trap:
        // an unstamped mode reads back as an ENGAGED empty string, so
        // `None{Mode Equals "tiling"}` evaluates TRUE and the rule would fire
        // on every context — silently overriding gaps for a caller that asked
        // about no mode at all. The resolver excludes Field::Mode structurally
        // for a mode-agnostic call, so the rule stays inert here while still
        // firing for a caller that names a different mode.
        PWR::RuleAction negatedGapAction;
        negatedGapAction.type = QString(PWR::ActionType::SetInnerGap);
        negatedGapAction.params.insert(QString(PWR::ActionParam::Value), 33);
        PWR::Rule negatedGap;
        negatedGap.id = QUuid::createUuid();
        negatedGap.name = QStringLiteral("Not-tiling inner gap");
        negatedGap.enabled = true;
        negatedGap.priority = 500;
        negatedGap.match = PWR::MatchExpression::makeNone(
            {PWR::MatchExpression::makeLeaf(PWR::Field::Mode, PWR::Operator::Equals, QStringLiteral("tiling"))});
        negatedGap.actions = {negatedGapAction};
        QVERIFY(f.store->setAllRules({negatedGap}));

        const PhosphorZones::ContextGapOverride negatedAgnostic =
            f.registry->resolveContextGaps(QStringLiteral("DP-9"), 1, QString());
        QVERIFY2(!negatedAgnostic.innerGap.has_value(), "a negated Mode rule must not fire for a mode-agnostic caller");

        // Positive control: the same rule DOES fire for a caller that names a
        // mode other than the negated one, so the exclusion above is scoped to
        // the agnostic call and has not simply disabled the rule.
        const PhosphorZones::ContextGapOverride negatedSnapping =
            f.registry->resolveContextGaps(QStringLiteral("DP-9"), 1, QString(), QStringLiteral("snapping"));
        QVERIFY(negatedSnapping.innerGap.has_value());
        QCOMPARE(*negatedSnapping.innerGap, 33);

        // Scrolling arm: a `Mode Equals "scrolling"` gap rule fires for the
        // scrolling asker and stays inert for tiling — the third engine's
        // gap provider resolves with the "scrolling" token.
        PWR::RuleAction scrollGapAction;
        scrollGapAction.type = QString(PWR::ActionType::SetInnerGap);
        scrollGapAction.params.insert(QString(PWR::ActionParam::Value), 8);
        PWR::Rule scrollGap;
        scrollGap.id = QUuid::createUuid();
        scrollGap.name = QStringLiteral("Scrolling inner gap");
        scrollGap.enabled = true;
        scrollGap.priority = 500;
        scrollGap.match =
            PWR::MatchExpression::makeLeaf(PWR::Field::Mode, PWR::Operator::Equals, QStringLiteral("scrolling"));
        scrollGap.actions = {scrollGapAction};
        QVERIFY(scrollGap.match.isContextOnly());
        QVERIFY(f.store->setAllRules({scrollGap}));

        const PhosphorZones::ContextGapOverride scrolled =
            f.registry->resolveContextGaps(QStringLiteral("DP-9"), 1, QString(), QStringLiteral("scrolling"));
        QVERIFY(scrolled.innerGap.has_value());
        QCOMPARE(*scrolled.innerGap, 8);
        const PhosphorZones::ContextGapOverride tiledAgain =
            f.registry->resolveContextGaps(QStringLiteral("DP-9"), 1, QString(), QStringLiteral("tiling"));
        QVERIFY(!tiledAgain.innerGap.has_value());

        // COEXISTING per-mode rules (not replacement): a tiling-pinned and a
        // scrolling-pinned gap rule in the SAME rule set must each fire only
        // for their own mode token.
        PWR::RuleAction tilingGapAction2;
        tilingGapAction2.type = QString(PWR::ActionType::SetInnerGap);
        tilingGapAction2.params.insert(QString(PWR::ActionParam::Value), 14);
        PWR::Rule tilingGap2;
        tilingGap2.id = QUuid::createUuid();
        tilingGap2.name = QStringLiteral("Tiling inner gap");
        tilingGap2.enabled = true;
        tilingGap2.priority = 400;
        tilingGap2.match =
            PWR::MatchExpression::makeLeaf(PWR::Field::Mode, PWR::Operator::Equals, QStringLiteral("tiling"));
        tilingGap2.actions = {tilingGapAction2};
        QVERIFY(f.store->setAllRules({scrollGap, tilingGap2}));

        const PhosphorZones::ContextGapOverride scrolledBoth =
            f.registry->resolveContextGaps(QStringLiteral("DP-9"), 1, QString(), QStringLiteral("scrolling"));
        QVERIFY(scrolledBoth.innerGap.has_value());
        QCOMPARE(*scrolledBoth.innerGap, 8);
        const PhosphorZones::ContextGapOverride tiledBoth =
            f.registry->resolveContextGaps(QStringLiteral("DP-9"), 1, QString(), QStringLiteral("tiling"));
        QVERIFY(tiledBoth.innerGap.has_value());
        QCOMPARE(*tiledBoth.innerGap, 14);
    }

    // ─── Window-field negation polarity on the context resolvers ─────────────
    // The window-field negation-polarity guard: a windowless context query
    // leaves every Window-sourced field ABSENT, which makes a positive leaf
    // evaluate false (inert, by design) but makes a leaf under `none{}` match
    // unconditionally — `none{AppId == firefox}` on a gap or lock rule would
    // fire on EVERY context. The resolvers exclude rules that NEGATE a window
    // field (negatesAnyField over the table-derived windowSourcedFields), and
    // deliberately do NOT exclude positive references, because an `any{}`
    // rule's context branch may legitimately fire. Both polarities pinned.
    void testNegatedWindowFieldStaysInertOnContextResolvers()
    {
        RegistryFixture f = makeRegistryFixture();

        // A `none{AppId == firefox}` gap rule: without the guard it gaps every
        // context (the absent AppId leaf is false, so the None matches).
        PWR::RuleAction gapAction;
        gapAction.type = QString(PWR::ActionType::SetInnerGap);
        gapAction.params.insert(QString(PWR::ActionParam::Value), 44);
        PWR::Rule negatedApp;
        negatedApp.id = QUuid::createUuid();
        negatedApp.name = QStringLiteral("Gap everywhere except firefox");
        negatedApp.enabled = true;
        negatedApp.priority = 500;
        negatedApp.match = PWR::MatchExpression::makeNone(
            {PWR::MatchExpression::makeLeaf(PWR::Field::AppId, PWR::Operator::Equals, QStringLiteral("firefox"))});
        negatedApp.actions = {gapAction};
        QVERIFY(f.store->setAllRules({negatedApp}));

        const PhosphorZones::ContextGapOverride negated =
            f.registry->resolveContextGaps(QStringLiteral("DP-9"), 1, QString(), QStringLiteral("snapping"));
        QVERIFY2(!negated.innerGap.has_value(),
                 "a rule negating a window field must not fire on a windowless context query");

        // Same shape on the LOCK resolver, whose spurious match would lock
        // every context.
        PWR::RuleAction lockAction;
        lockAction.type = QString(PWR::ActionType::LockContext);
        lockAction.params.insert(QString(PWR::ActionParam::Value), true);
        PWR::Rule negatedLock = negatedApp;
        negatedLock.id = QUuid::createUuid();
        negatedLock.actions = {lockAction};
        QVERIFY(f.store->setAllRules({negatedLock}));
        QVERIFY2(!f.registry->resolveContextLocked(QStringLiteral("DP-9"), 1, QString()),
                 "a rule negating a window field must not lock a windowless context");

        // Lock-resolver positive control: a plain ScreenId lock rule DOES lock
        // the same context, so the false above is the guard and not a resolver
        // that never reports locked.
        PWR::Rule plainLock;
        plainLock.id = QUuid::createUuid();
        plainLock.name = QStringLiteral("Lock DP-9");
        plainLock.enabled = true;
        plainLock.priority = 500;
        plainLock.match =
            PWR::MatchExpression::makeLeaf(PWR::Field::ScreenId, PWR::Operator::Equals, QStringLiteral("DP-9"));
        plainLock.actions = {lockAction};
        QVERIFY(f.store->setAllRules({plainLock}));
        QVERIFY2(f.registry->resolveContextLocked(QStringLiteral("DP-9"), 1, QString()),
                 "a plain context lock rule must lock the context");

        // POSITIVE CONTROL 1: the guard is negation-scoped, not a blanket
        // window-field ban. An `any{ScreenId == DP-9, AppId == firefox}` gap
        // rule still fires through its context branch.
        PWR::Rule anyMixed;
        anyMixed.id = QUuid::createUuid();
        anyMixed.name = QStringLiteral("DP-9 or firefox gap");
        anyMixed.enabled = true;
        anyMixed.priority = 500;
        anyMixed.match = PWR::MatchExpression::makeAny(
            {PWR::MatchExpression::makeLeaf(PWR::Field::ScreenId, PWR::Operator::Equals, QStringLiteral("DP-9")),
             PWR::MatchExpression::makeLeaf(PWR::Field::AppId, PWR::Operator::Equals, QStringLiteral("firefox"))});
        anyMixed.actions = {gapAction};
        QVERIFY(f.store->setAllRules({anyMixed}));
        const PhosphorZones::ContextGapOverride positive =
            f.registry->resolveContextGaps(QStringLiteral("DP-9"), 1, QString(), QStringLiteral("snapping"));
        QVERIFY2(positive.innerGap.has_value() && *positive.innerGap == 44,
                 "a POSITIVE window-field reference must not be excluded — the context branch fires");

        // POSITIVE CONTROL 2: a context-only rule on the same store still
        // resolves, so the negative arms above failed because of the guard,
        // not a dead fixture.
        PWR::Rule plain;
        plain.id = QUuid::createUuid();
        plain.name = QStringLiteral("Plain DP-9 gap");
        plain.enabled = true;
        plain.priority = 500;
        plain.match =
            PWR::MatchExpression::makeLeaf(PWR::Field::ScreenId, PWR::Operator::Equals, QStringLiteral("DP-9"));
        plain.actions = {gapAction};
        QVERIFY(f.store->setAllRules({plain}));
        const PhosphorZones::ContextGapOverride control =
            f.registry->resolveContextGaps(QStringLiteral("DP-9"), 1, QString(), QStringLiteral("snapping"));
        QVERIFY(control.innerGap.has_value());
        QCOMPARE(*control.innerGap, 44);
    }

    // ─── Per-monitor gap beats a global per-mode gap (specificity, not priority) ─
    // A per-monitor (ScreenId-pinned) gap override and a global per-mode
    // (Mode-pinned) gap rule can both match the same window/slot. A hand-authored
    // per-mode gap rule can even carry a HIGHER raw priority (500) than a
    // per-screen rule (300). resolveContextGaps must therefore order the slot by
    // MATCH SPECIFICITY (ScreenId-pinned > Mode-pinned), so the per-monitor
    // override wins despite its lower priority, while a slot the per-monitor rule
    // does NOT carry still falls through to the per-mode rule. (Appearance/gaps are
    // config-backed now, so migration creates no gap rules; these are authored
    // directly to pin the cascade contract.)
    void testPerScreenGapBeatsPerModeGap()
    {
        const auto intGapAction = [](QLatin1StringView type, int value) {
            PWR::RuleAction a;
            a.type = QString(type);
            a.params.insert(QString(PWR::ActionParam::Value), value);
            return a;
        };

        RegistryFixture f = makeRegistryFixture();

        // Global per-mode gap rule: higher raw priority, carries both inner and
        // outer gap.
        PWR::Rule perMode;
        perMode.id = QUuid::createUuid();
        perMode.name = QStringLiteral("Tiling gaps");
        perMode.enabled = true;
        perMode.priority = 500; // the per-mode rule's priority — deliberately higher
        perMode.match =
            PWR::MatchExpression::makeLeaf(PWR::Field::Mode, PWR::Operator::Equals, QStringLiteral("tiling"));
        perMode.actions = {intGapAction(PWR::ActionType::SetInnerGap, 14),
                           intGapAction(PWR::ActionType::SetOuterGap, 30)};

        // Per-monitor override for DP-1: lower raw priority, carries ONLY inner gap.
        PWR::Rule perScreen;
        perScreen.id = QUuid::createUuid();
        perScreen.name = QStringLiteral("Gaps (DP-1)");
        perScreen.enabled = true;
        perScreen.priority = 300; // the per-monitor rule's priority — deliberately lower
        perScreen.match =
            PWR::MatchExpression::makeLeaf(PWR::Field::ScreenId, PWR::Operator::Equals, QStringLiteral("DP-1"));
        perScreen.actions = {intGapAction(PWR::ActionType::SetInnerGap, 20)};

        QVERIFY(f.store->setAllRules({perMode, perScreen}));

        // DP-1 in tiling mode: both rules match the inner-gap slot. The
        // ScreenId-pinned rule is more specific, so its value (20) wins even
        // though the Mode-pinned rule has the higher priority (500 > 300).
        const PhosphorZones::ContextGapOverride dp1 =
            f.registry->resolveContextGaps(QStringLiteral("DP-1"), 0, QString(), QStringLiteral("tiling"));
        QVERIFY(dp1.innerGap.has_value());
        QCOMPARE(*dp1.innerGap, 20);
        // The outer-gap slot is carried only by the per-mode rule, so it still
        // surfaces from there (per-slot composition is preserved).
        QVERIFY(dp1.outerGap.has_value());
        QCOMPARE(*dp1.outerGap, 30);

        // DP-2 in tiling mode: no per-monitor rule, so the per-mode gap applies.
        const PhosphorZones::ContextGapOverride dp2 =
            f.registry->resolveContextGaps(QStringLiteral("DP-2"), 0, QString(), QStringLiteral("tiling"));
        QVERIFY(dp2.innerGap.has_value());
        QCOMPARE(*dp2.innerGap, 14);
    }

    // ─── ColorScheme key-fold across EVERY cached resolver ────────────────
    // testContextLock_colorSchemeRuleFollowsProviderFlips proves the fold for
    // ONE of the seven cached resolvers. The scheme token is a non-rule-set
    // input, so each cache that omitted it from its key would latch the first
    // verdict and never re-resolve on a palette switch — a per-resolver bug
    // the lock test cannot see. Drive the same provider flip through the other
    // six (assignment, gaps, default-assignment, OSD, overlay, drag selector).
    void testColorSchemeKeyFold_appliesToEveryCachedResolver()
    {
        const auto darkRule = [](const QString& name, const QList<PWR::RuleAction>& actions) {
            PWR::Rule r;
            r.id = QUuid::createUuid();
            r.name = name;
            r.enabled = true;
            r.priority = 400;
            r.match =
                PWR::MatchExpression::makeLeaf(PWR::Field::ColorScheme, PWR::Operator::Equals, QStringLiteral("dark"));
            r.actions = actions;
            return r;
        };
        const auto valueAction = [](QLatin1StringView type, const QJsonValue& value) {
            PWR::RuleAction a;
            a.type = QString(type);
            a.params.insert(QString(PWR::ActionParam::Value), value);
            return a;
        };

        RegistryFixture f = makeRegistryFixture();
        PWR::RuleAction shader;
        shader.type = QString(PWR::ActionType::OverrideOverlayShader);
        shader.params.insert(QString(PWR::ActionParam::EffectId), QStringLiteral("night-glow"));
        // A dark-scoped SNAPPING LAYOUT rule, so the assignment cache (whose
        // key carries the scheme token in its own "twc:|or:|cs:" composite
        // rather than through contextCacheKeyToken) is exercised too.
        PWR::RuleAction darkLayout;
        darkLayout.type = QString(PWR::ActionType::SetSnappingLayout);
        darkLayout.params.insert(QString(PWR::ActionParam::LayoutId), QStringLiteral("{dark-layout}"));

        QVERIFY(f.store->setAllRules({
            darkRule(QStringLiteral("dark gap"), {valueAction(PWR::ActionType::SetInnerGap, 21)}),
            darkRule(QStringLiteral("dark osd"), {valueAction(PWR::ActionType::SetOsdEnabled, false)}),
            darkRule(QStringLiteral("dark selector"), {valueAction(PWR::ActionType::SetDragSelectorEnabled, false)}),
            darkRule(QStringLiteral("dark default"), {valueAction(PWR::ActionType::DefaultLayoutAssignment, false)}),
            darkRule(QStringLiteral("dark overlay"), {shader}),
            darkRule(QStringLiteral("dark layout"), {darkLayout}),
        }));

        QString scheme = QStringLiteral("light");
        f.registry->setColorSchemeProvider([&scheme]() -> std::optional<QString> {
            return scheme;
        });

        // Prime every cache under "light", where the dark-scoped rules miss.
        QVERIFY(!f.registry->resolveContextGaps(QStringLiteral("DP-1"), 0, QString(), QStringLiteral("snapping"))
                     .innerGap.has_value());
        QCOMPARE(f.registry->resolveContextOsdEnabled(QStringLiteral("DP-1"), 0, QString()), std::nullopt);
        QCOMPARE(f.registry->resolveContextDragSelectorEnabled(QStringLiteral("DP-1"), 0, QString()), std::nullopt);
        QCOMPARE(f.registry->resolveContextDefaultAssignment(QStringLiteral("DP-1"), 0, QString()), std::nullopt);
        QVERIFY(!f.registry->resolveContextOverlay(QStringLiteral("DP-1"), 0, QString()).shaderId.has_value());
        QVERIFY(f.registry->assignmentEntryForScreen(QStringLiteral("DP-1"), 0, QString()).snappingLayout
                != QStringLiteral("{dark-layout}"));

        // Flip the palette. No rule-set revision moves, so only a scheme token
        // folded into each cache KEY can produce a fresh verdict here.
        scheme = QStringLiteral("dark");
        const PhosphorZones::ContextGapOverride gaps =
            f.registry->resolveContextGaps(QStringLiteral("DP-1"), 0, QString(), QStringLiteral("snapping"));
        QVERIFY2(gaps.innerGap.has_value() && *gaps.innerGap == 21, "the gap cache must fold the colour-scheme token");
        QCOMPARE(f.registry->resolveContextOsdEnabled(QStringLiteral("DP-1"), 0, QString()),
                 std::optional<bool>(false));
        QCOMPARE(f.registry->resolveContextDragSelectorEnabled(QStringLiteral("DP-1"), 0, QString()),
                 std::optional<bool>(false));
        QCOMPARE(f.registry->resolveContextDefaultAssignment(QStringLiteral("DP-1"), 0, QString()),
                 std::optional<bool>(false));
        const PhosphorZones::ContextOverlayOverride overlay =
            f.registry->resolveContextOverlay(QStringLiteral("DP-1"), 0, QString());
        QVERIFY2(overlay.shaderId.has_value() && *overlay.shaderId == QStringLiteral("night-glow"),
                 "the overlay cache must fold the colour-scheme token");
        QCOMPARE(f.registry->assignmentEntryForScreen(QStringLiteral("DP-1"), 0, QString()).snappingLayout,
                 QStringLiteral("{dark-layout}"));

        // And back to light, so each cache is shown re-resolving in BOTH
        // directions rather than latching the second verdict.
        scheme = QStringLiteral("light");
        QVERIFY(!f.registry->resolveContextGaps(QStringLiteral("DP-1"), 0, QString(), QStringLiteral("snapping"))
                     .innerGap.has_value());
        QCOMPARE(f.registry->resolveContextOsdEnabled(QStringLiteral("DP-1"), 0, QString()), std::nullopt);
        QCOMPARE(f.registry->resolveContextDragSelectorEnabled(QStringLiteral("DP-1"), 0, QString()), std::nullopt);
        QCOMPARE(f.registry->resolveContextDefaultAssignment(QStringLiteral("DP-1"), 0, QString()), std::nullopt);
        QVERIFY(!f.registry->resolveContextOverlay(QStringLiteral("DP-1"), 0, QString()).shaderId.has_value());
        QVERIFY(f.registry->assignmentEntryForScreen(QStringLiteral("DP-1"), 0, QString()).snappingLayout
                != QStringLiteral("{dark-layout}"));
    }

    // ─── Single-slot bool resolvers: structural exclusions + priority ─────
    // The OSD and drag-selector resolvers carry the same structural
    // exclusions as the lock resolver (Mode and TiledWindowCount unstamped,
    // window-sourced fields negation-guarded) and the same single-slot
    // priority contest. Without this driver a dropped field in either
    // predicate would let one negated leaf gate the popup on every context
    // and nothing would fail. Shared driver (plain member — it takes
    // parameters, so it must not be a test slot), one slot per resolver below.
private:
    void runContextBoolStructuralContract(
        QLatin1StringView actionType,
        const std::function<std::optional<bool>(RegistryFixture&, const QString&)>& resolve)
    {
        const auto osdAction = [&actionType](bool enabled) {
            PWR::RuleAction a;
            a.type = QString(actionType);
            a.params.insert(QString(PWR::ActionParam::Value), enabled);
            return a;
        };
        const auto negatedRule = [&osdAction](const QString& name, PWR::Field field, const QVariant& value) {
            PWR::Rule r;
            r.id = QUuid::createUuid();
            r.name = name;
            r.enabled = true;
            r.priority = 900;
            r.match =
                PWR::MatchExpression::makeNone({PWR::MatchExpression::makeLeaf(field, PWR::Operator::Equals, value)});
            r.actions = {osdAction(false)};
            return r;
        };

        RegistryFixture f = makeRegistryFixture();

        // None{Mode == "tiling"}: mode is unstamped on an OSD query and reads
        // back as an ENGAGED empty string, so the inner leaf is false and the
        // None matches EVERY context. Excluded structurally, so no verdict.
        QVERIFY(f.store->setAllRules(
            {negatedRule(QStringLiteral("not tiling"), PWR::Field::Mode, QStringLiteral("tiling"))}));
        QCOMPARE(resolve(f, QStringLiteral("DP-1")), std::nullopt);

        // None{TiledWindowCount == 0}: the count is stamped only on the
        // assignment query, so the same inversion applies here.
        QVERIFY(f.store->setAllRules({negatedRule(QStringLiteral("not zero tiled"), PWR::Field::TiledWindowCount, 0)}));
        QCOMPARE(resolve(f, QStringLiteral("DP-1")), std::nullopt);

        // None{AppId == firefox}: the window-sourced negation guard.
        QVERIFY(f.store->setAllRules(
            {negatedRule(QStringLiteral("not firefox"), PWR::Field::AppId, QStringLiteral("firefox"))}));
        QCOMPARE(resolve(f, QStringLiteral("DP-1")), std::nullopt);

        // Positive control: a plain ScreenId rule on the same fixture DOES
        // resolve, so the three nullopts above are the guards and not a dead
        // resolver.
        const auto screenRule = [&osdAction](const QString& name, const QString& screenId, bool enabled, int priority) {
            PWR::Rule r;
            r.id = QUuid::createUuid();
            r.name = name;
            r.enabled = true;
            r.priority = priority;
            r.match = PWR::MatchExpression::makeLeaf(PWR::Field::ScreenId, PWR::Operator::Equals, screenId);
            r.actions = {osdAction(enabled)};
            return r;
        };
        QVERIFY(f.store->setAllRules({screenRule(QStringLiteral("plain"), QStringLiteral("DP-1"), false, 400)}));
        QCOMPARE(resolve(f, QStringLiteral("DP-1")), std::optional<bool>(false));

        // Single-slot priority contest, run in BOTH directions so the winner is
        // proven to be the priority and not a bias toward either value.
        QVERIFY(f.store->setAllRules({
            screenRule(QStringLiteral("dp20 high on"), QStringLiteral("DP-20"), true, 500),
            screenRule(QStringLiteral("dp20 low off"), QStringLiteral("DP-20"), false, 400),
            screenRule(QStringLiteral("dp21 high off"), QStringLiteral("DP-21"), false, 500),
            screenRule(QStringLiteral("dp21 low on"), QStringLiteral("DP-21"), true, 400),
        }));
        QCOMPARE(resolve(f, QStringLiteral("DP-20")), std::optional<bool>(true));
        QCOMPARE(resolve(f, QStringLiteral("DP-21")), std::optional<bool>(false));
    }

private Q_SLOTS:
    void testContextOsd_structuralExclusionsAndPriority()
    {
        runContextBoolStructuralContract(PWR::ActionType::SetOsdEnabled,
                                         [](RegistryFixture& f, const QString& screenId) {
                                             return f.registry->resolveContextOsdEnabled(screenId, 0, QString());
                                         });
    }

    void testContextDragSelector_structuralExclusionsAndPriority()
    {
        runContextBoolStructuralContract(
            PWR::ActionType::SetDragSelectorEnabled, [](RegistryFixture& f, const QString& screenId) {
                return f.registry->resolveContextDragSelectorEnabled(screenId, 0, QString());
            });
    }

    // ─── A terminal Exclude must not drop unrelated context overrides ─────
    // The evaluator's walk STOPS at the first ADMITTED rule carrying an
    // in-scope terminal action, so every context resolver admits only rules
    // that carry one of ITS OWN slots. Without that gate a single
    // higher-priority context Exclude rule silently drops every overlay /
    // tiling-param / scrolling-param override beneath it — the shape
    // resolveContextGaps has always guarded and its three siblings did not.
    void testTerminalExcludeDoesNotDropContextOverrides()
    {
        RegistryFixture f = makeRegistryFixture();

        // A high-priority context Exclude on DP-1, carrying no context slot.
        PWR::RuleAction excludeAction;
        excludeAction.type = QString(PWR::ActionType::Exclude);
        PWR::Rule exclude;
        exclude.id = QUuid::createUuid();
        exclude.name = QStringLiteral("exclude on DP-1");
        exclude.enabled = true;
        exclude.priority = 900;
        exclude.match =
            PWR::MatchExpression::makeLeaf(PWR::Field::ScreenId, PWR::Operator::Equals, QStringLiteral("DP-1"));
        exclude.actions = {excludeAction};

        const auto lowerRule = [](const QString& name, const QList<PWR::RuleAction>& actions) {
            PWR::Rule r;
            r.id = QUuid::createUuid();
            r.name = name;
            r.enabled = true;
            r.priority = 300; // below the Exclude, so the walk reaches it only if it continues
            r.match =
                PWR::MatchExpression::makeLeaf(PWR::Field::ScreenId, PWR::Operator::Equals, QStringLiteral("DP-1"));
            r.actions = actions;
            return r;
        };

        PWR::RuleAction shader;
        shader.type = QString(PWR::ActionType::OverrideOverlayShader);
        shader.params.insert(QString(PWR::ActionParam::EffectId), QStringLiteral("plasma-glow"));
        PWR::RuleAction maxWindows;
        maxWindows.type = QString(PWR::ActionType::SetMaxWindows);
        maxWindows.params.insert(QString(PWR::ActionParam::Value), 4);
        PWR::RuleAction smartGaps;
        smartGaps.type = QString(PWR::ActionType::SetScrollSmartGaps);
        smartGaps.params.insert(QString(PWR::ActionParam::Value), true);
        PWR::RuleAction innerGap;
        innerGap.type = QString(PWR::ActionType::SetInnerGap);
        innerGap.params.insert(QString(PWR::ActionParam::Value), 17);

        QVERIFY(f.store->setAllRules({
            exclude,
            lowerRule(QStringLiteral("overlay"), {shader}),
            lowerRule(QStringLiteral("tiling params"), {maxWindows}),
            lowerRule(QStringLiteral("scrolling params"), {smartGaps}),
            lowerRule(QStringLiteral("gaps"), {innerGap}),
        }));

        const PhosphorZones::ContextOverlayOverride overlay =
            f.registry->resolveContextOverlay(QStringLiteral("DP-1"), 0, QString());
        QVERIFY2(overlay.shaderId.has_value() && *overlay.shaderId == QStringLiteral("plasma-glow"),
                 "a context Exclude must not terminate the overlay walk");

        const PhosphorZones::ContextTilingParams tiling =
            f.registry->resolveContextTilingParams(QStringLiteral("DP-1"), 0, QString());
        QVERIFY2(tiling.maxWindows.has_value() && *tiling.maxWindows == 4,
                 "a context Exclude must not terminate the tiling-param walk");

        const PhosphorZones::ContextScrollingParams scrolling =
            f.registry->resolveContextScrollingParams(QStringLiteral("DP-1"), 0, QString());
        QVERIFY2(scrolling.smartGaps.has_value() && *scrolling.smartGaps,
                 "a context Exclude must not terminate the scrolling-param walk");

        // The gap resolver already had the gate; it is here as the control that
        // the Exclude rule is genuinely matching this context.
        const PhosphorZones::ContextGapOverride gaps =
            f.registry->resolveContextGaps(QStringLiteral("DP-1"), 0, QString(), QStringLiteral("snapping"));
        QVERIFY(gaps.innerGap.has_value());
        QCOMPARE(*gaps.innerGap, 17);
    }

    // ─── Tiling-param payload type gates ──────────────────────────────────
    // maxWindows / masterCount / splitRatio REJECT AND FALL THROUGH on a
    // payload of the wrong JSON type, and a fractional int likewise falls
    // through rather than being rounded or read as QJsonValue::toInt()'s zero
    // default (the body's own comment explains why rounding would be wrong).
    // A hand-edited rules.json is the only way to author these, and applying
    // them would mean a maxWindows of 0 or a split ratio of 0.0 the user
    // never wrote. Note the fall-throughs asserted here are produced by the
    // STORE gate, not the resolver: setAllRules returns save()'s bool, and
    // RuleSet::setRules drops a rule whose action fails its descriptor
    // validator, so the malformed rules never reach the resolver at all.
    // That is the real (and sufficient) guarantee; the resolver's own reads
    // are unreachable hardening behind it.
    void testContextTilingParams_payloadTypeGates()
    {
        const auto paramRule = [](const QString& name, QLatin1StringView type, const QJsonValue& value) {
            PWR::RuleAction a;
            a.type = QString(type);
            a.params.insert(QString(PWR::ActionParam::Value), value);
            PWR::Rule r;
            r.id = QUuid::createUuid();
            r.name = name;
            r.enabled = true;
            r.priority = 400;
            r.match =
                PWR::MatchExpression::makeLeaf(PWR::Field::ScreenId, PWR::Operator::Equals, QStringLiteral("DP-1"));
            r.actions = {a};
            return r;
        };

        RegistryFixture f = makeRegistryFixture();

        // A string payload leaves every slot unset (config wins).
        QVERIFY(f.store->setAllRules({
            paramRule(QStringLiteral("bad max"), PWR::ActionType::SetMaxWindows, QStringLiteral("four")),
            paramRule(QStringLiteral("bad ratio"), PWR::ActionType::SetSplitRatio, QStringLiteral("half")),
        }));
        const PhosphorZones::ContextTilingParams bad =
            f.registry->resolveContextTilingParams(QStringLiteral("DP-1"), 0, QString());
        QVERIFY2(!bad.maxWindows.has_value(), "a string maxWindows payload must fall through, not resolve 0");
        QVERIFY2(!bad.splitRatio.has_value(), "a string splitRatio payload must fall through, not resolve 0.0");

        // A fractional count falls through rather than collapsing to toInt()'s
        // zero OR being silently rounded. Rounding would be wrong here, not
        // merely unnecessary: SetMaxWindows' own validator refuses a
        // non-integral payload ("reject rather than silently truncating a
        // hand-edited fractional count"), so a resolver that rounded would
        // resolve a count the store would never have admitted.
        QVERIFY(f.store->setAllRules({paramRule(QStringLiteral("frac max"), PWR::ActionType::SetMaxWindows, 3.5)}));
        const PhosphorZones::ContextTilingParams frac =
            f.registry->resolveContextTilingParams(QStringLiteral("DP-1"), 0, QString());
        QVERIFY2(!frac.maxWindows.has_value(),
                 "a fractional maxWindows payload must fall through, not round or resolve 0");

        // Positive control: a well-formed payload still resolves.
        QVERIFY(f.store->setAllRules({
            paramRule(QStringLiteral("good max"), PWR::ActionType::SetMaxWindows, 5),
            paramRule(QStringLiteral("good ratio"), PWR::ActionType::SetSplitRatio, 0.6),
        }));
        const PhosphorZones::ContextTilingParams good =
            f.registry->resolveContextTilingParams(QStringLiteral("DP-1"), 0, QString());
        QVERIFY(good.maxWindows.has_value());
        QCOMPARE(*good.maxWindows, 5);
        QVERIFY(good.splitRatio.has_value());
        QCOMPARE(*good.splitRatio, 0.6);
    }
};

QTEST_MAIN(TestRuleCascadeOverlayLock)
#include "test_rule_cascade_overlay_lock.moc"
