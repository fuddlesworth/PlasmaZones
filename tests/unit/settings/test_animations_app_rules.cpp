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
        QCOMPARE(c.appRules().size(), 0);
    }

    void addAppRule_emptyEventPath_isRejected()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        AnimationsPageController c(nullptr, &settings);

        QVERIFY(!c.addAppRule(makeShaderRuleMap(QStringLiteral("firefox"), QString(), QStringLiteral("dissolve"))));
        QCOMPARE(c.appRules().size(), 0);
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
        QCOMPARE(c.appRules().size(), 0);
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
        QVERIFY(!c.moveAppRule(0, 5));
        QVERIFY(!c.moveAppRule(-1, 0));
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
        // Empty pattern in the replacement is rejected, original survives.
        QVERIFY(
            !c.setAppRule(0, makeShaderRuleMap(QString(), QStringLiteral("window.open"), QStringLiteral("dissolve"))));
        QCOMPARE(c.appRules().first().toMap().value(QStringLiteral("classPattern")).toString(),
                 QStringLiteral("firefox"));
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
        AnimationsPageController c;
        const auto events = c.animationAppRuleEvents();
        QVERIFY(!events.isEmpty());
        for (const QVariant& entry : events) {
            const QVariantMap m = entry.toMap();
            const QString path = m.value(QStringLiteral("path")).toString();
            QVERIFY2(path.startsWith(QLatin1String("window.")) || path == QLatin1String("window"),
                     qPrintable(QStringLiteral("non-window path: ") + path));
            QVERIFY(!m.value(QStringLiteral("label")).toString().isEmpty());
        }
    }
};

QTEST_MAIN(TestAnimationsAppRules)
#include "test_animations_app_rules.moc"
