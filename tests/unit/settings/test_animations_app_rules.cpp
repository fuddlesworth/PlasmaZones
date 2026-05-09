// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_animations_app_rules.cpp
 * @brief AnimationsPageController app-rule list (Phase 3) tests.
 *
 * Pins the QML-facing CRUD surface for AnimationAppRule:
 *   - appRules() returns the persisted list as QVariantList of
 *     QVariantMaps in the AnimationAppRule::toJson() shape
 *   - addAppRule appends after validating classPattern / eventPath /
 *     kind, rejecting malformed entries
 *   - removeAppRule / moveAppRule respect bounds and emit
 *     pendingChangesChanged
 *   - setAppRule replaces in place and short-circuits on no-op
 *   - appRulesChanged forwards from ISettings::animationAppRulesChanged
 *   - animationAppRuleEvents surfaces only window.* concrete events
 */

#include <QSignalSpy>
#include <QTest>

#include <PhosphorAnimation/AnimationShaderRegistry.h>

#include "config/settings.h"
#include "settings/animationspagecontroller.h"
#include "../helpers/IsolatedConfigGuard.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

namespace {

QVariantMap makeShaderRuleMap(const QString& pattern, const QString& event, const QString& effectId,
                              const QVariantMap& params = {})
{
    QVariantMap m;
    m.insert(QStringLiteral("classPattern"), pattern);
    m.insert(QStringLiteral("eventPath"), event);
    m.insert(QStringLiteral("kind"), QStringLiteral("shader"));
    m.insert(QStringLiteral("effectId"), effectId);
    if (!params.isEmpty())
        m.insert(QStringLiteral("shaderParams"), params);
    return m;
}

QVariantMap makeTimingRuleMap(const QString& pattern, const QString& event, int durationMs, const QString& curve = {})
{
    QVariantMap m;
    m.insert(QStringLiteral("classPattern"), pattern);
    m.insert(QStringLiteral("eventPath"), event);
    m.insert(QStringLiteral("kind"), QStringLiteral("timing"));
    m.insert(QStringLiteral("durationMs"), durationMs);
    if (!curve.isEmpty())
        m.insert(QStringLiteral("curve"), curve);
    return m;
}

} // namespace

class TestAnimationsAppRules : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ── Empty state ───────────────────────────────────────────────────

    void appRules_emptyByDefault()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        AnimationsPageController c(nullptr, &settings);

        QCOMPARE(c.appRules(), QVariantList{});
    }

    void appRules_returnsEmptyWhenSettingsIsNull()
    {
        // Defense in depth: even though the body never touches Settings
        // (nullptr arg), wrap with the guard so an accidental future
        // change adding any I/O can't pollute the user's real config.
        IsolatedConfigGuard guard;
        AnimationsPageController c(nullptr, nullptr);
        QCOMPARE(c.appRules(), QVariantList{});
    }

    // ── Add ───────────────────────────────────────────────────────────

    void addAppRule_persistsAndShowsInList()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        AnimationsPageController c(nullptr, &settings);

        QVERIFY(c.addAppRule(
            makeShaderRuleMap(QStringLiteral("firefox"), QStringLiteral("window.open"), QStringLiteral("dissolve"))));
        const auto rules = c.appRules();
        QCOMPARE(rules.size(), 1);
        const QVariantMap entry = rules.first().toMap();
        QCOMPARE(entry.value(QStringLiteral("classPattern")).toString(), QStringLiteral("firefox"));
        QCOMPARE(entry.value(QStringLiteral("eventPath")).toString(), QStringLiteral("window.open"));
        QCOMPARE(entry.value(QStringLiteral("kind")).toString(), QStringLiteral("shader"));
        QCOMPARE(entry.value(QStringLiteral("effectId")).toString(), QStringLiteral("dissolve"));
    }

    void addAppRule_emptyClassPattern_isRejected()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        AnimationsPageController c(nullptr, &settings);

        QVERIFY(!c.addAppRule(makeShaderRuleMap(QString(), QStringLiteral("window.open"), QStringLiteral("dissolve"))));
        QCOMPARE(c.appRules(), QVariantList{});
    }

    void addAppRule_emptyEventPath_isRejected()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        AnimationsPageController c(nullptr, &settings);

        QVERIFY(!c.addAppRule(makeShaderRuleMap(QStringLiteral("firefox"), QString(), QStringLiteral("dissolve"))));
        QCOMPARE(c.appRules(), QVariantList{});
    }

    void addAppRule_unknownKind_isRejected()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        AnimationsPageController c(nullptr, &settings);

        QVariantMap m;
        m.insert(QStringLiteral("classPattern"), QStringLiteral("firefox"));
        m.insert(QStringLiteral("eventPath"), QStringLiteral("window.open"));
        m.insert(QStringLiteral("kind"), QStringLiteral("not-a-kind"));
        QVERIFY(!c.addAppRule(m));
        QCOMPARE(c.appRules(), QVariantList{});
    }

    void addAppRule_emptyEffectIdShaderRule_isAccepted()
    {
        // Engaged-empty effectId is the documented "block default for
        // matching windows" sentinel — the rule list must accept it.
        IsolatedConfigGuard guard;
        Settings settings;
        AnimationsPageController c(nullptr, &settings);

        QVERIFY(c.addAppRule(makeShaderRuleMap(QStringLiteral("noanim"), QStringLiteral("window.open"), QString())));
        QCOMPARE(c.appRules().size(), 1);
        QCOMPARE(c.appRules().first().toMap().value(QStringLiteral("effectId")).toString(), QString());
    }

    void addAppRule_timingRule_roundTripsCurveAndDuration()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        AnimationsPageController c(nullptr, &settings);

        QVERIFY(c.addAppRule(makeTimingRuleMap(QStringLiteral("spotify"), QStringLiteral("window.minimize"), 800,
                                               QStringLiteral("0.33,1.00,0.68,1.00"))));
        const QVariantMap entry = c.appRules().first().toMap();
        QCOMPARE(entry.value(QStringLiteral("kind")).toString(), QStringLiteral("timing"));
        QCOMPARE(entry.value(QStringLiteral("durationMs")).toInt(), 800);
        QCOMPARE(entry.value(QStringLiteral("curve")).toString(), QStringLiteral("0.33,1.00,0.68,1.00"));
    }

    // ── Remove / move ─────────────────────────────────────────────────

    void removeAppRule_byIndex_drops()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        AnimationsPageController c(nullptr, &settings);

        QVERIFY(c.addAppRule(
            makeShaderRuleMap(QStringLiteral("firefox"), QStringLiteral("window.open"), QStringLiteral("dissolve"))));
        QVERIFY(c.addAppRule(
            makeShaderRuleMap(QStringLiteral("spotify"), QStringLiteral("window.open"), QStringLiteral("popin"))));
        QCOMPARE(c.appRules().size(), 2);

        QVERIFY(c.removeAppRule(0));
        QCOMPARE(c.appRules().size(), 1);
        QCOMPARE(c.appRules().first().toMap().value(QStringLiteral("classPattern")).toString(),
                 QStringLiteral("spotify"));
    }

    void removeAppRule_outOfRange_returnsFalse()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        AnimationsPageController c(nullptr, &settings);

        QVERIFY(c.addAppRule(
            makeShaderRuleMap(QStringLiteral("firefox"), QStringLiteral("window.open"), QStringLiteral("dissolve"))));
        QVERIFY(!c.removeAppRule(-1));
        QVERIFY(!c.removeAppRule(99));
        QCOMPARE(c.appRules().size(), 1);
    }

    void removeAppRule_emptyList_returnsFalse()
    {
        // Most common QML-side off-by-one: user deletes the last rule
        // and the click handler fires once more before the list rebinds.
        IsolatedConfigGuard guard;
        Settings settings;
        AnimationsPageController c(nullptr, &settings);

        QVERIFY(!c.removeAppRule(0));
    }

    void moveAppRule_reordersTwoEntries()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        AnimationsPageController c(nullptr, &settings);

        QVERIFY(c.addAppRule(
            makeShaderRuleMap(QStringLiteral("firefox"), QStringLiteral("window.open"), QStringLiteral("dissolve"))));
        QVERIFY(c.addAppRule(
            makeShaderRuleMap(QStringLiteral("spotify"), QStringLiteral("window.open"), QStringLiteral("popin"))));

        QVERIFY(c.moveAppRule(1, 0));
        QCOMPARE(c.appRules().first().toMap().value(QStringLiteral("classPattern")).toString(),
                 QStringLiteral("spotify"));
    }

    void moveAppRule_sameIndex_returnsFalse()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        AnimationsPageController c(nullptr, &settings);

        QVERIFY(c.addAppRule(
            makeShaderRuleMap(QStringLiteral("firefox"), QStringLiteral("window.open"), QStringLiteral("dissolve"))));
        QVERIFY(!c.moveAppRule(0, 0));
    }

    void moveAppRule_outOfRange_returnsFalse()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        AnimationsPageController c(nullptr, &settings);

        QVERIFY(c.addAppRule(
            makeShaderRuleMap(QStringLiteral("firefox"), QStringLiteral("window.open"), QStringLiteral("dissolve"))));
        // Cover both halves of the (from, to) matrix — out-of-range
        // `to` AND out-of-range `from`, plus negative on each side.
        QVERIFY(!c.moveAppRule(0, 5));
        QVERIFY(!c.moveAppRule(5, 0));
        QVERIFY(!c.moveAppRule(-1, 0));
        QVERIFY(!c.moveAppRule(0, -1));
    }

    // ── Set in place ──────────────────────────────────────────────────

    void setAppRule_replacesInPlace()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        AnimationsPageController c(nullptr, &settings);

        QVERIFY(c.addAppRule(
            makeShaderRuleMap(QStringLiteral("firefox"), QStringLiteral("window.open"), QStringLiteral("dissolve"))));
        QVERIFY(c.setAppRule(
            0, makeShaderRuleMap(QStringLiteral("firefox"), QStringLiteral("window.open"), QStringLiteral("popin"))));

        QCOMPARE(c.appRules().size(), 1);
        QCOMPARE(c.appRules().first().toMap().value(QStringLiteral("effectId")).toString(), QStringLiteral("popin"));
    }

    void setAppRule_outOfRange_returnsFalse()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        AnimationsPageController c(nullptr, &settings);

        QVERIFY(!c.setAppRule(
            0,
            makeShaderRuleMap(QStringLiteral("firefox"), QStringLiteral("window.open"), QStringLiteral("dissolve"))));
    }

    void setAppRule_invalidRule_returnsFalse()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        AnimationsPageController c(nullptr, &settings);

        QVERIFY(c.addAppRule(
            makeShaderRuleMap(QStringLiteral("firefox"), QStringLiteral("window.open"), QStringLiteral("dissolve"))));
        // Mirror the add-side rejection coverage — empty pattern, empty
        // event, and unknown kind all rejected at the controller boundary;
        // the original entry must survive each rejected replacement, AND
        // the list size must stay at 1 (a regression that silently grew
        // or shrank the list on rejection wouldn't be caught by the
        // classPattern check alone).
        QVERIFY(
            !c.setAppRule(0, makeShaderRuleMap(QString(), QStringLiteral("window.open"), QStringLiteral("dissolve"))));
        QCOMPARE(c.appRules().size(), 1);
        QVERIFY(!c.setAppRule(0, makeShaderRuleMap(QStringLiteral("firefox"), QString(), QStringLiteral("dissolve"))));
        QCOMPARE(c.appRules().size(), 1);
        QVariantMap unknownKindMap;
        unknownKindMap.insert(QStringLiteral("classPattern"), QStringLiteral("firefox"));
        unknownKindMap.insert(QStringLiteral("eventPath"), QStringLiteral("window.open"));
        unknownKindMap.insert(QStringLiteral("kind"), QStringLiteral("not-a-kind"));
        QVERIFY(!c.setAppRule(0, unknownKindMap));
        QCOMPARE(c.appRules().size(), 1);
        QCOMPARE(c.appRules().first().toMap().value(QStringLiteral("classPattern")).toString(),
                 QStringLiteral("firefox"));
    }

    void appRule_unknownEventPath_isRejected()
    {
        // The page-level event combo only offers valid paths, but
        // a programmatic Q_INVOKABLE caller can persist a stale-across-
        // releases path that silently never matches at resolve time.
        // Both add and set go through the same `appRuleFromVariantMap`
        // gate, so verify both reject — a regression on either path
        // would be a real user-data corruption mode.
        IsolatedConfigGuard guard;
        Settings settings;
        AnimationsPageController c(nullptr, &settings);

        QVERIFY(!c.addAppRule(makeShaderRuleMap(QStringLiteral("firefox"), QStringLiteral("window.notARealEvent"),
                                                QStringLiteral("dissolve"))));
        QCOMPARE(c.appRules(), QVariantList{});

        QVERIFY(c.addAppRule(
            makeShaderRuleMap(QStringLiteral("firefox"), QStringLiteral("window.open"), QStringLiteral("dissolve"))));
        QVERIFY(!c.setAppRule(0,
                              makeShaderRuleMap(QStringLiteral("firefox"), QStringLiteral("window.notARealEvent"),
                                                QStringLiteral("dissolve"))));
        QCOMPARE(c.appRules().first().toMap().value(QStringLiteral("eventPath")).toString(),
                 QStringLiteral("window.open"));
    }

    void setAppRule_noOp_doesNotEmitPendingChanges()
    {
        // QML two-way bindings can re-emit the same rule on every
        // refresh; setAppRule(currentRule) must short-circuit so the
        // dirty bit doesn't oscillate.
        IsolatedConfigGuard guard;
        Settings settings;
        AnimationsPageController c(nullptr, &settings);

        QVERIFY(c.addAppRule(
            makeShaderRuleMap(QStringLiteral("firefox"), QStringLiteral("window.open"), QStringLiteral("dissolve"))));

        QSignalSpy spy(&c, &AnimationsPageController::pendingChangesChanged);
        QVERIFY(c.setAppRule(
            0,
            makeShaderRuleMap(QStringLiteral("firefox"), QStringLiteral("window.open"), QStringLiteral("dissolve"))));
        QCOMPARE(spy.count(), 0);
    }

    // ── Signals ───────────────────────────────────────────────────────

    void addAppRule_emitsPendingChangesChanged()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        AnimationsPageController c(nullptr, &settings);

        QSignalSpy spy(&c, &AnimationsPageController::pendingChangesChanged);
        QVERIFY(c.addAppRule(
            makeShaderRuleMap(QStringLiteral("firefox"), QStringLiteral("window.open"), QStringLiteral("dissolve"))));
        QVERIFY2(spy.count() >= 1, "addAppRule MUST emit pendingChangesChanged");
    }

    void appRulesChanged_forwardsFromSettings()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        AnimationsPageController c(nullptr, &settings);

        QSignalSpy spy(&c, &AnimationsPageController::appRulesChanged);
        QVERIFY(c.addAppRule(
            makeShaderRuleMap(QStringLiteral("firefox"), QStringLiteral("window.open"), QStringLiteral("dissolve"))));
        QVERIFY2(spy.count() >= 1, "appRulesChanged MUST be forwarded from ISettings::animationAppRulesChanged");
    }

    // ── Event dropdown surface ────────────────────────────────────────

    void animationAppRuleEvents_surfacesOnlyWindowConcreteEvents()
    {
        // Wrap with the guard for file-wide convention symmetry — even
        // though this test never touches Settings, a future change
        // adding any I/O would silently pollute the user's real config
        // without it.
        IsolatedConfigGuard guard;
        AnimationsPageController c;
        const auto events = c.animationAppRuleEvents();
        // Lower-bound count check: the canonical Window* set is
        // open/close/minimize/maximize/move/resize/focus = 7 entries.
        // A future taxonomy refactor that accidentally drops some
        // would still satisfy `!isEmpty()`; the count gate catches it.
        QVERIFY2(
            events.size() >= 7,
            qPrintable(QStringLiteral("expected at least 7 window events, got ") + QString::number(events.size())));
        bool sawWindowOpen = false;
        for (const QVariant& entry : events) {
            const QVariantMap m = entry.toMap();
            const QString path = m.value(QStringLiteral("path")).toString();
            // Strict: only `window.<leaf>` paths — the bare `"window"`
            // category is filtered by the controller's `isCategory`
            // skip, so accepting it here would mask a regression.
            QVERIFY2(path.startsWith(QLatin1String("window.")),
                     qPrintable(QStringLiteral("non-window-leaf path: ") + path));
            QVERIFY(!m.value(QStringLiteral("label")).toString().isEmpty());
            if (path == QLatin1String("window.open")) {
                sawWindowOpen = true;
            }
        }
        // Pin the canonical event by name so a regression that swaps
        // the entire taxonomy underneath us still trips the test.
        QVERIFY2(sawWindowOpen, "window.open MUST be in the rule-event surface");
    }
};

QTEST_MAIN(TestAnimationsAppRules)
#include "test_animations_app_rules.moc"
