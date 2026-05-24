// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_window_rule_controller.cpp
 * @brief Coverage for WindowRuleController — the staging controller behind
 *        the unified Window Rules page.
 *
 * The controller talks to the daemon over D-Bus; in a headless unit run the
 * daemon is absent, so `daemonReachable` is false and the model starts empty.
 * The staging contract (in-memory CRUD by UUID, dirty-tracking, revert) is
 * fully exercisable without a live daemon.
 *
 * Pins:
 *   - `newEmptyRule` produces a valid, subject-shaped rule with a fresh UUID,
 *   - add / update / remove by UUID flip the dirty bit,
 *   - `monitorOverview` summarises rules per connected monitor,
 *   - `moveRule` reorders and renormalizes priorities,
 *   - the field / operator / action authoring metadata is well-formed.
 */

#include <QSignalSpy>
#include <QTest>
#include <QUuid>

#include "settings/windowrulecontroller.h"
#include "settings/windowrulemodel.h"

using namespace PlasmaZones;

class TestWindowRuleController : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void newEmptyRuleShapesBySubject();
    void addUpdateRemoveByUuid();
    void dirtyTrackingAndRevert();
    void monitorOverviewSummarises();
    void moveRuleReorders();
    void authoringMetadata();
    void templatesProduceSeededRules();
    void actionTypesCarryDomain();
    void matchIsContextOnlyClassifies();
    void validationIssuesForJsonFlags();
};

void TestWindowRuleController::newEmptyRuleShapesBySubject()
{
    WindowRuleController controller;

    const QVariantMap monitor = controller.newEmptyRule(QStringLiteral("monitor"));
    QVERIFY(!monitor.value(QStringLiteral("id")).toString().isEmpty());
    QVERIFY(monitor.contains(QStringLiteral("match")));
    // The monitor subject starts with a ScreenId leaf.
    const QVariantMap monitorMatch = monitor.value(QStringLiteral("match")).toMap();
    QCOMPARE(monitorMatch.value(QStringLiteral("field")).toString(), QStringLiteral("screenId"));

    const QVariantMap app = controller.newEmptyRule(QStringLiteral("application"));
    const QVariantMap appMatch = app.value(QStringLiteral("match")).toMap();
    QCOMPARE(appMatch.value(QStringLiteral("field")).toString(), QStringLiteral("appId"));

    const QVariantMap activity = controller.newEmptyRule(QStringLiteral("activity"));
    const QVariantMap activityMatch = activity.value(QStringLiteral("match")).toMap();
    QCOMPARE(activityMatch.value(QStringLiteral("field")).toString(), QStringLiteral("activity"));

    // Custom starts from the catch-all All{} composite.
    const QVariantMap custom = controller.newEmptyRule(QStringLiteral("custom"));
    QVERIFY(custom.value(QStringLiteral("match")).toMap().contains(QStringLiteral("all")));

    // Each fresh rule carries a distinct UUID.
    QVERIFY(monitor.value(QStringLiteral("id")).toString() != app.value(QStringLiteral("id")).toString());
}

void TestWindowRuleController::addUpdateRemoveByUuid()
{
    WindowRuleController controller;

    // Build a monitor rule, give it a usable action, and add it.
    QVariantMap rule = controller.newEmptyRule(QStringLiteral("application"));
    rule[QStringLiteral("actions")] = QVariantList{QVariantMap{{QStringLiteral("type"), QStringLiteral("float")}}};
    const QString id = controller.addRuleFromJson(rule);
    QVERIFY(!id.isEmpty());
    QCOMPARE(controller.model()->rowCount(), 1);

    // Update by UUID — rename it.
    QVariantMap fetched = controller.ruleJson(id);
    QCOMPARE(fetched.value(QStringLiteral("id")).toString(), id);
    fetched[QStringLiteral("name")] = QStringLiteral("Renamed");
    QVERIFY(controller.updateRuleFromJson(fetched));
    QCOMPARE(controller.ruleJson(id).value(QStringLiteral("name")).toString(), QStringLiteral("Renamed"));

    // setRuleEnabled toggles the flag.
    QVERIFY(controller.setRuleEnabled(id, false));
    QCOMPARE(controller.ruleJson(id).value(QStringLiteral("enabled")).toBool(), false);

    // Remove by UUID.
    QVERIFY(controller.removeRule(id));
    QCOMPARE(controller.model()->rowCount(), 0);
    QVERIFY(!controller.removeRule(id));
}

void TestWindowRuleController::dirtyTrackingAndRevert()
{
    WindowRuleController controller;
    QVERIFY(!controller.isDirty());

    QSignalSpy dirtySpy(&controller, &WindowRuleController::dirtyChanged);

    QVariantMap rule = controller.newEmptyRule(QStringLiteral("application"));
    rule[QStringLiteral("actions")] = QVariantList{QVariantMap{{QStringLiteral("type"), QStringLiteral("float")}}};
    const QString id = controller.addRuleFromJson(rule);
    QVERIFY(!id.isEmpty());

    // Adding a rule flips the dirty bit.
    QVERIFY(controller.isDirty());
    QVERIFY(controller.hasPendingChanges());
    QVERIFY(dirtySpy.count() >= 1);

    // revert() re-fetches the daemon's authoritative set and only clears the
    // dirty bit if the re-fetch succeeded. The contract this test guards is
    // the linkage between revert()'s return value and the dirty-state
    // transition: a successful revert MUST clear dirty, a failed revert MUST
    // preserve it. The earlier bug was a failed revert silently dropping
    // staged edits while reporting success. The check is symmetric so it
    // passes both in a fully headless run (daemon absent → revert fails →
    // dirty stays) and on a dev machine with a live daemon (revert succeeds
    // → dirty clears).
    const bool reverted = controller.revert();
    QCOMPARE(controller.isDirty(), !reverted);
}

void TestWindowRuleController::monitorOverviewSummarises()
{
    WindowRuleController controller;

    // One rule pinned to DP-2 with an engine action.
    QVariantMap rule = controller.newEmptyRule(QStringLiteral("monitor"));
    QVariantMap match = rule.value(QStringLiteral("match")).toMap();
    match[QStringLiteral("value")] = QStringLiteral("DP-2");
    rule[QStringLiteral("match")] = match;
    rule[QStringLiteral("actions")] =
        QVariantList{QVariantMap{{QStringLiteral("type"), QStringLiteral("setEngineMode")},
                                 {QStringLiteral("mode"), QStringLiteral("autotile")}}};
    QVERIFY(!controller.addRuleFromJson(rule).isEmpty());

    // Two monitors connected — DP-2 has the rule, eDP-1 has none.
    const QVariantList screens{QVariantMap{{QStringLiteral("name"), QStringLiteral("DP-2")}},
                               QVariantMap{{QStringLiteral("name"), QStringLiteral("eDP-1")}}};
    const QVariantList overview = controller.monitorOverview(screens);
    QCOMPARE(overview.size(), 2);

    bool sawDp2 = false;
    bool sawEdp1 = false;
    for (const QVariant& v : overview) {
        const QVariantMap tile = v.toMap();
        if (tile.value(QStringLiteral("screenId")).toString() == QLatin1String("DP-2")) {
            sawDp2 = true;
            QCOMPARE(tile.value(QStringLiteral("ruleCount")).toInt(), 1);
            QCOMPARE(tile.value(QStringLiteral("assigned")).toBool(), true);
        }
        if (tile.value(QStringLiteral("screenId")).toString() == QLatin1String("eDP-1")) {
            sawEdp1 = true;
            QCOMPARE(tile.value(QStringLiteral("ruleCount")).toInt(), 0);
            QCOMPARE(tile.value(QStringLiteral("assigned")).toBool(), false);
        }
    }
    QVERIFY(sawDp2);
    QVERIFY(sawEdp1);
}

void TestWindowRuleController::moveRuleReorders()
{
    WindowRuleController controller;

    auto makeApp = [&](const QString& appId) {
        QVariantMap rule = controller.newEmptyRule(QStringLiteral("application"));
        QVariantMap match = rule.value(QStringLiteral("match")).toMap();
        match[QStringLiteral("value")] = appId;
        rule[QStringLiteral("match")] = match;
        rule[QStringLiteral("actions")] = QVariantList{QVariantMap{{QStringLiteral("type"), QStringLiteral("float")}}};
        return controller.addRuleFromJson(rule);
    };

    const QString a = makeApp(QStringLiteral("a"));
    const QString b = makeApp(QStringLiteral("b"));
    const QString c = makeApp(QStringLiteral("c"));
    QVERIFY(!a.isEmpty() && !b.isEmpty() && !c.isEmpty());

    // Move C before A — order becomes C, A, B.
    QVERIFY(controller.moveRule(c, a));
    WindowRuleModel* model = controller.model();
    QCOMPARE(model->index(0, 0).data(WindowRuleModel::IdRole).toString(), c);
    QCOMPARE(model->index(1, 0).data(WindowRuleModel::IdRole).toString(), a);
    QCOMPARE(model->index(2, 0).data(WindowRuleModel::IdRole).toString(), b);

    // moveRule renormalizes priorities — earlier list index ⇒ higher priority.
    const int prioFirst = model->index(0, 0).data(WindowRuleModel::PriorityRole).toInt();
    const int prioLast = model->index(2, 0).data(WindowRuleModel::PriorityRole).toInt();
    QVERIFY(prioFirst > prioLast);
}

void TestWindowRuleController::authoringMetadata()
{
    WindowRuleController controller;

    const QVariantList fields = controller.matchFields();
    QVERIFY(!fields.isEmpty());
    // Every field entry carries value / label / valueKind. The `screen` and
    // `activity` kinds drive the dedicated picker editors in QML — assert
    // at least one of each is present so a regression that reverts those
    // fields back to `string` (silently breaking the picker UX) is caught.
    bool sawScreenKind = false;
    bool sawActivityKind = false;
    for (const QVariant& v : fields) {
        const QVariantMap f = v.toMap();
        QVERIFY(f.contains(QStringLiteral("value")));
        QVERIFY(!f.value(QStringLiteral("label")).toString().isEmpty());
        const QString kind = f.value(QStringLiteral("valueKind")).toString();
        QVERIFY(kind == QLatin1String("string") || kind == QLatin1String("number") || kind == QLatin1String("bool")
                || kind == QLatin1String("screen") || kind == QLatin1String("activity"));
        if (kind == QLatin1String("screen")) {
            sawScreenKind = true;
        }
        if (kind == QLatin1String("activity")) {
            sawActivityKind = true;
        }
    }
    QVERIFY(sawScreenKind);
    QVERIFY(sawActivityKind);

    // AppId (Field enum 0) supports the AppIdMatches operator.
    const QVariantList appOps = controller.operatorsForField(0);
    QVERIFY(!appOps.isEmpty());

    const QVariantList actions = controller.actionTypes();
    QVERIFY(!actions.isEmpty());
    bool sawFloat = false;
    for (const QVariant& v : actions) {
        if (v.toMap().value(QStringLiteral("value")).toString() == QLatin1String("float"))
            sawFloat = true;
    }
    QVERIFY(sawFloat);
}

void TestWindowRuleController::templatesProduceSeededRules()
{
    WindowRuleController controller;

    // The catalogue surfaced to the AddRuleSheet — every template entry
    // must carry the four UI fields the QML grid binds against. A missing
    // field would render a tile with a blank label or no icon.
    const QVariantList templates = controller.ruleTemplates();
    QVERIFY(!templates.isEmpty());
    for (const QVariant& v : templates) {
        const QVariantMap t = v.toMap();
        QVERIFY(!t.value(QStringLiteral("id")).toString().isEmpty());
        QVERIFY(!t.value(QStringLiteral("label")).toString().isEmpty());
        QVERIFY(!t.value(QStringLiteral("description")).toString().isEmpty());
        QVERIFY(!t.value(QStringLiteral("icon")).toString().isEmpty());
    }

    // `layoutOnMonitor` mirrors the old MonitorStatePage assignment flow:
    // ScreenId leaf + SetEngineMode("snapping") + SetSnappingLayout (empty
    // layoutId — the user fills it in the editor). The seeded action shape
    // is the rule editor's contract; regression there silently breaks the
    // quick-start flow.
    const QVariantMap layoutRule = controller.newRuleFromTemplate(QStringLiteral("layoutOnMonitor"));
    QVERIFY(!layoutRule.value(QStringLiteral("id")).toString().isEmpty());
    QCOMPARE(layoutRule.value(QStringLiteral("match")).toMap().value(QStringLiteral("field")).toString(),
             QStringLiteral("screenId"));
    const QVariantList layoutActions = layoutRule.value(QStringLiteral("actions")).toList();
    QCOMPARE(layoutActions.size(), 2);
    QCOMPARE(layoutActions.at(0).toMap().value(QStringLiteral("type")).toString(), QStringLiteral("setEngineMode"));
    QCOMPARE(layoutActions.at(0).toMap().value(QStringLiteral("mode")).toString(), QStringLiteral("snapping"));
    QCOMPARE(layoutActions.at(1).toMap().value(QStringLiteral("type")).toString(), QStringLiteral("setSnappingLayout"));

    // `algorithmOnMonitor` is the autotile mirror — same screen leaf,
    // SetEngineMode("autotile") + SetTilingAlgorithm.
    const QVariantMap algoRule = controller.newRuleFromTemplate(QStringLiteral("algorithmOnMonitor"));
    const QVariantList algoActions = algoRule.value(QStringLiteral("actions")).toList();
    QCOMPARE(algoActions.size(), 2);
    QCOMPARE(algoActions.at(0).toMap().value(QStringLiteral("mode")).toString(), QStringLiteral("autotile"));
    QCOMPARE(algoActions.at(1).toMap().value(QStringLiteral("type")).toString(), QStringLiteral("setTilingAlgorithm"));

    // `excludeApp` is the per-app exclusion template (Application subject +
    // Exclude action). Single action, no params required.
    const QVariantMap excludeRule = controller.newRuleFromTemplate(QStringLiteral("excludeApp"));
    QCOMPARE(excludeRule.value(QStringLiteral("match")).toMap().value(QStringLiteral("field")).toString(),
             QStringLiteral("appId"));
    const QVariantList excludeActions = excludeRule.value(QStringLiteral("actions")).toList();
    QCOMPARE(excludeActions.size(), 1);
    QCOMPARE(excludeActions.at(0).toMap().value(QStringLiteral("type")).toString(), QStringLiteral("exclude"));

    // An unknown id must return an empty map — the AddRuleSheet would
    // otherwise commit a UUID-less rule on a typo in the template id.
    const QVariantMap bogus = controller.newRuleFromTemplate(QStringLiteral("nonexistentTemplate"));
    QVERIFY(bogus.isEmpty());
}

void TestWindowRuleController::actionTypesCarryDomain()
{
    // The picker keys off this field to disable context-domain actions when
    // the match references window-property leaves — a regression that drops
    // the domain would silently re-enable the silently-never-fires
    // combination.
    WindowRuleController controller;
    const QVariantList actions = controller.actionTypes();
    QVERIFY(!actions.isEmpty());
    QHash<QString, QString> domainOf;
    for (const QVariant& v : actions) {
        const QVariantMap m = v.toMap();
        const QString id = m.value(QStringLiteral("value")).toString();
        const QString domain = m.value(QStringLiteral("domain")).toString();
        QVERIFY2(domain == QLatin1String("context") || domain == QLatin1String("window"),
                 qPrintable(QStringLiteral("action %1 has unexpected domain %2").arg(id, domain)));
        domainOf.insert(id, domain);
    }
    // Spot-check the canonical pairs — the context actions and a sample of
    // window actions. A typo in the descriptor would flip these.
    QCOMPARE(domainOf.value(QStringLiteral("setEngineMode")), QStringLiteral("context"));
    QCOMPARE(domainOf.value(QStringLiteral("setSnappingLayout")), QStringLiteral("context"));
    QCOMPARE(domainOf.value(QStringLiteral("setTilingAlgorithm")), QStringLiteral("context"));
    QCOMPARE(domainOf.value(QStringLiteral("disableEngine")), QStringLiteral("context"));
    QCOMPARE(domainOf.value(QStringLiteral("float")), QStringLiteral("window"));
    QCOMPARE(domainOf.value(QStringLiteral("exclude")), QStringLiteral("window"));
}

void TestWindowRuleController::matchIsContextOnlyClassifies()
{
    WindowRuleController controller;

    // Empty / catch-all match — context-only by definition (no leaves).
    QVERIFY(controller.matchIsContextOnly(QVariantMap{}));

    QVariantMap allEmpty;
    allEmpty[QStringLiteral("all")] = QVariantList{};
    QVERIFY(controller.matchIsContextOnly(allEmpty));

    // Single context leaf — context-only.
    QVariantMap screenLeaf;
    screenLeaf[QStringLiteral("field")] = QStringLiteral("screenId");
    screenLeaf[QStringLiteral("op")] = QStringLiteral("equals");
    screenLeaf[QStringLiteral("value")] = QStringLiteral("DP-1");
    QVERIFY(controller.matchIsContextOnly(screenLeaf));

    // Single window leaf — NOT context-only.
    QVariantMap appLeaf;
    appLeaf[QStringLiteral("field")] = QStringLiteral("appId");
    appLeaf[QStringLiteral("op")] = QStringLiteral("equals");
    appLeaf[QStringLiteral("value")] = QStringLiteral("firefox");
    QVERIFY(!controller.matchIsContextOnly(appLeaf));

    // An All{} carrying a window leaf — NOT context-only.
    QVariantMap mixedAll;
    QVariantList children;
    children.append(screenLeaf);
    children.append(appLeaf);
    mixedAll[QStringLiteral("all")] = children;
    QVERIFY(!controller.matchIsContextOnly(mixedAll));
}

void TestWindowRuleController::validationIssuesForJsonFlags()
{
    WindowRuleController controller;

    // Clean rule: window match + Float action → no issues.
    QVariantMap clean = controller.newEmptyRule(QStringLiteral("application"));
    clean[QStringLiteral("match")].toMap();
    QVariantMap appLeaf;
    appLeaf[QStringLiteral("field")] = QStringLiteral("appId");
    appLeaf[QStringLiteral("op")] = QStringLiteral("equals");
    appLeaf[QStringLiteral("value")] = QStringLiteral("firefox");
    clean[QStringLiteral("match")] = appLeaf;
    QVariantList cleanActions;
    QVariantMap floatAction;
    floatAction[QStringLiteral("type")] = QStringLiteral("float");
    cleanActions.append(floatAction);
    clean[QStringLiteral("actions")] = cleanActions;
    QCOMPARE(controller.validationIssuesForJson(clean).size(), 0);

    // Bad rule: same window match + SetEngineMode action → one issue at
    // index 0, pointing at the offending action.
    QVariantMap bad = clean;
    QVariantList badActions;
    QVariantMap engine;
    engine[QStringLiteral("type")] = QStringLiteral("setEngineMode");
    engine[QStringLiteral("mode")] = QStringLiteral("autotile");
    badActions.append(engine);
    bad[QStringLiteral("actions")] = badActions;
    const QVariantList issues = controller.validationIssuesForJson(bad);
    QCOMPARE(issues.size(), 1);
    const QVariantMap issue = issues.first().toMap();
    QCOMPARE(issue.value(QStringLiteral("actionIndex")).toInt(), 0);
    QCOMPARE(issue.value(QStringLiteral("actionType")).toString(), QStringLiteral("setEngineMode"));
    QVERIFY(!issue.value(QStringLiteral("message")).toString().isEmpty());

    // Partial rule (no actions yet) → zero issues; the editor only flags
    // once the user has picked an action.
    QVariantMap partial = clean;
    partial[QStringLiteral("actions")] = QVariantList{};
    QCOMPARE(controller.validationIssuesForJson(partial).size(), 0);
}

QTEST_MAIN(TestWindowRuleController)

#include "test_window_rule_controller.moc"
