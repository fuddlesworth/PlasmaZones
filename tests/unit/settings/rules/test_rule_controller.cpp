// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_rule_controller.cpp
 * @brief Coverage for RuleController — the staging controller behind
 *        the unified Rules page.
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

#include <QJSEngine>
#include <QJsonObject>
#include <QSet>
#include <QSignalSpy>
#include <QTest>
#include <QUuid>

#include "settings/rules/rulecontroller.h"
#include "settings/rules/rulemodel.h"

#include <PhosphorRules/MatchExpression.h>
#include <PhosphorRules/RuleAction.h>
#include <PhosphorRules/Rule.h>

using namespace PlasmaZones;
using namespace PhosphorRules;

class TestRuleController : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void newEmptyRuleShapesBySubject();
    void addUpdateRemoveByUuid();
    void dirtyTrackingAndRevert();
    void userAuthorableFilterHidesInternalActions();
    void moveRuleReorders();
    void flatPriorityIgnoresBandsOnReorder();
    void authoringMetadata();
    void matchIsContextOnlyClassifies();
    void validationIssuesForJsonFlags();
    void asyncCommitAndRevertAreInvokable();
    void stageUserRulesEnforcesTheAddRuleBoundary();
};

void TestRuleController::newEmptyRuleShapesBySubject()
{
    RuleController controller;

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

void TestRuleController::addUpdateRemoveByUuid()
{
    RuleController controller;

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

void TestRuleController::dirtyTrackingAndRevert()
{
    RuleController controller;
    QVERIFY(!controller.isDirty());

    QSignalSpy dirtySpy(&controller, &RuleController::dirtyChanged);

    QVariantMap rule = controller.newEmptyRule(QStringLiteral("application"));
    rule[QStringLiteral("actions")] = QVariantList{QVariantMap{{QStringLiteral("type"), QStringLiteral("float")}}};
    const QString id = controller.addRuleFromJson(rule);
    QVERIFY(!id.isEmpty());

    // Adding a rule flips the dirty bit.
    QVERIFY(controller.isDirty());
    QVERIFY(dirtySpy.count() >= 1);

    // revert() re-fetches the daemon's authoritative set asynchronously and
    // only clears the dirty bit if the re-fetch succeeded. The contract this
    // test guards is the linkage between the async outcome and the dirty-state
    // transition: a successful revert (rulesLoaded fires) MUST clear dirty, a
    // failed revert MUST preserve it. The earlier bug was a failed revert
    // silently dropping staged edits while reporting success.
    //
    // The check is symmetric, so it passes whether or not a daemon answers —
    // but only one arm is ever a real assertion on a given run, and CI runs the
    // failure arm exclusively: there is no daemon on the session bus, so
    // `reverted` is always false there and only "a failed revert preserves
    // dirty" is exercised. The success arm runs only on a dev box with the
    // daemon up. That asymmetry is accepted rather than mocked away: the
    // controller reaches the bus directly through `QDBusConnection::sessionBus()`
    // (see rulecontroller.cpp's fetchAndLoad), so making the success arm
    // hermetic means standing up a real `org.plasmazones.Rules` service on a
    // private bus — a fixture no other test in this file needs, for one
    // transition the daemon-side rule tests already cover from the other end.
    // Do NOT read a green CI run as evidence that a successful revert clears
    // dirty; that leg is covered on demand by running this test under
    // `dbus-run-session` with the daemon started.
    QSignalSpy loadedSpy(&controller, &RuleController::rulesLoaded);
    controller.revert();
    // Pump the event loop briefly so the QDBusPendingCall reply (success or
    // error) lands. A timeout fall-through is acceptable — that's the
    // daemon-absent path and dirty must stay set.
    loadedSpy.wait(500);
    const bool reverted = loadedSpy.count() > 0;
    QCOMPARE(controller.isDirty(), !reverted);
}

void TestRuleController::userAuthorableFilterHidesInternalActions()
{
    // Pin that the controller's actionTypes() picker honours the
    // `userAuthorable=false` flag on ActionDescriptor. Without this test the
    // filter is dead code — every shipped descriptor currently defaults to
    // userAuthorable=true, so a regression that bypasses the filter (e.g.
    // re-introducing a hand-maintained allow-list) would slip through CI.
    //
    // Register a sentinel descriptor flagged as non-authorable, walk the
    // picker, then restore the descriptor to its prior state so the rest
    // of the test suite isn't disturbed.
    using PhosphorRules::ActionDescriptor;
    using PhosphorRules::ActionDomain;
    using PhosphorRules::ActionRegistry;

    static const QString kSentinelType = QStringLiteral("test-sentinel-internal-action");
    auto& registry = ActionRegistry::instance();
    const bool prevExists = registry.isRegistered(kSentinelType);
    const std::optional<ActionDescriptor> prev = registry.descriptor(kSentinelType);

    // RAII cleanup: restore the prior descriptor (or unregister the sentinel
    // entirely) even if an assertion throws / fails mid-test. Without this,
    // a QVERIFY2 failure between the two registerAction calls would skip
    // the trailing cleanup and leak the sentinel into the registry for the
    // remainder of the test binary's lifetime.
    struct RegistryGuard
    {
        ActionRegistry& registry;
        QString type;
        bool prevExists;
        std::optional<ActionDescriptor> prev;
        ~RegistryGuard()
        {
            if (prevExists && prev.has_value()) {
                registry.registerAction(*prev);
            } else {
                registry.unregisterAction(type);
            }
        }
    };
    RegistryGuard guard{registry, kSentinelType, prevExists, prev};

    ActionDescriptor sentinel;
    sentinel.type = kSentinelType;
    sentinel.slotFor = [](const QJsonObject&) {
        return QStringLiteral("test-sentinel-slot");
    };
    sentinel.validate = [](const QJsonObject&) {
        return true;
    };
    sentinel.terminal = false;
    sentinel.domain = ActionDomain::Window;
    sentinel.userAuthorable = false;
    registry.registerAction(sentinel);

    RuleController controller;
    const QVariantList types = controller.actionTypes();
    bool found = false;
    for (const QVariant& t : types) {
        const QVariantMap tm = t.toMap();
        if (tm.value(QStringLiteral("value")).toString() == kSentinelType) {
            found = true;
            break;
        }
    }
    QVERIFY2(!found, "actionTypes() must exclude descriptors with userAuthorable=false");

    // Now flip the descriptor to userAuthorable=true and confirm the same
    // sentinel surfaces — the filter is the only thing keeping it hidden.
    sentinel.userAuthorable = true;
    registry.registerAction(sentinel);
    const QVariantList typesAuthorable = controller.actionTypes();
    bool foundAuthorable = false;
    for (const QVariant& t : typesAuthorable) {
        const QVariantMap tm = t.toMap();
        if (tm.value(QStringLiteral("value")).toString() == kSentinelType) {
            foundAuthorable = true;
            break;
        }
    }
    QVERIFY2(foundAuthorable, "actionTypes() must include descriptors with userAuthorable=true");
    // RegistryGuard's dtor handles cleanup.
}

void TestRuleController::moveRuleReorders()
{
    RuleController controller;

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

    // Moving C above A is a pure list-order reorder, renormalized so list order
    // maps onto priority order: the list becomes C, A, B.
    QVERIFY(controller.moveRule(c, a));
    RuleModel* model = controller.model();
    QCOMPARE(model->index(0, 0).data(RuleModel::IdRole).toString(), c);
    QCOMPARE(model->index(1, 0).data(RuleModel::IdRole).toString(), a);
    QCOMPARE(model->index(2, 0).data(RuleModel::IdRole).toString(), b);

    // Earlier list index maps to higher (global) priority.
    const int prioFirst = model->index(0, 0).data(RuleModel::PriorityRole).toInt();
    const int prioLast = model->index(2, 0).data(RuleModel::PriorityRole).toInt();
    QVERIFY(prioFirst > prioLast);
}

void TestRuleController::flatPriorityIgnoresBandsOnReorder()
{
    // Priority is one flat global sequence — section "bands" only seed a new
    // rule's default position, they do NOT cap precedence. A cross-band drag
    // must let a lower-band rule outrank a higher-band one.
    RuleController controller;

    // Application rule (float action → Applications band) and a Monitor rule
    // (context match + lockContext → Context band, which seeds higher).
    QVariantMap appRule = controller.newEmptyRule(QStringLiteral("application"));
    {
        QVariantMap match = appRule.value(QStringLiteral("match")).toMap();
        match[QStringLiteral("value")] = QStringLiteral("firefox");
        appRule[QStringLiteral("match")] = match;
        appRule[QStringLiteral("actions")] =
            QVariantList{QVariantMap{{QStringLiteral("type"), QStringLiteral("float")}}};
    }
    const QString appId = controller.addRuleFromJson(appRule);
    QVERIFY(!appId.isEmpty());

    QVariantMap monRule = controller.newEmptyRule(QStringLiteral("monitor"));
    {
        QVariantMap match = monRule.value(QStringLiteral("match")).toMap();
        match[QStringLiteral("value")] = QStringLiteral("DP-1");
        monRule[QStringLiteral("match")] = match;
        monRule[QStringLiteral("actions")] = QVariantList{
            QVariantMap{{QStringLiteral("type"), QStringLiteral("lockContext")}, {QStringLiteral("value"), true}}};
    }
    const QString monId = controller.addRuleFromJson(monRule);
    QVERIFY(!monId.isEmpty());

    RuleModel* model = controller.model();
    const auto rowOf = [&](const QString& id) {
        for (int i = 0; i < model->rowCount(); ++i)
            if (model->index(i, 0).data(RuleModel::IdRole).toString() == id)
                return i;
        return -1;
    };
    const auto prioOf = [&](const QString& id) {
        return model->index(rowOf(id), 0).data(RuleModel::PriorityRole).toInt();
    };

    // Band-seeded insert: the Monitor rule (higher band) seeds ABOVE the
    // Application rule even though it was added second.
    QVERIFY(rowOf(monId) < rowOf(appId));
    QVERIFY(prioOf(monId) > prioOf(appId));

    // Cross-band drag: drop the Application rule above the Monitor rule. Position
    // now decides — the Application rule outranks the Monitor rule despite its
    // lower band. (The old banded scheme would have snapped it back below.)
    QVERIFY(controller.moveRule(appId, monId));
    QVERIFY(rowOf(appId) < rowOf(monId));
    QVERIFY(prioOf(appId) > prioOf(monId));
}

void TestRuleController::authoringMetadata()
{
    RuleController controller;

    const QVariantList fields = controller.matchFields();
    QVERIFY(!fields.isEmpty());
    // Every field entry carries value / label / valueKind. The `screen` and
    // `activity` kinds drive the dedicated picker editors in QML — assert
    // at least one of each is present so a regression that reverts those
    // fields back to `string` (silently breaking the picker UX) is caught.
    bool sawScreenKind = false;
    bool sawActivityKind = false;
    bool sawWindowTypeKind = false;
    bool sawModeKind = false;
    bool sawOrientationKind = false;
    bool sawLayoutKind = false;
    bool sawVirtualDesktopKind = false;
    for (const QVariant& v : fields) {
        const QVariantMap f = v.toMap();
        QVERIFY(f.contains(QStringLiteral("value")));
        QVERIFY(!f.value(QStringLiteral("label")).toString().isEmpty());
        const QString kind = f.value(QStringLiteral("valueKind")).toString();
        QVERIFY(kind == QLatin1String("string") || kind == QLatin1String("number") || kind == QLatin1String("bool")
                || kind == QLatin1String("screen") || kind == QLatin1String("activity")
                || kind == QLatin1String("windowType") || kind == QLatin1String("virtualDesktop")
                || kind == QLatin1String("mode") || kind == QLatin1String("orientation")
                || kind == QLatin1String("layout"));
        if (kind == QLatin1String("screen")) {
            sawScreenKind = true;
        }
        if (kind == QLatin1String("activity")) {
            sawActivityKind = true;
        }
        if (kind == QLatin1String("layout")) {
            sawLayoutKind = true;
        }
        if (kind == QLatin1String("virtualDesktop")) {
            sawVirtualDesktopKind = true;
        }
        // Closed-vocab dropdown fields must carry an `options` array of {value, wire,
        // label} triples so the editor can render the dropdown. ScreenOrientation
        // (orientation) is one of these — it mirrors mode — so it is validated here
        // too, guarding against a regression that reverts it to a bare string field.
        if (kind == QLatin1String("windowType") || kind == QLatin1String("mode")
            || kind == QLatin1String("orientation")) {
            if (kind == QLatin1String("windowType")) {
                sawWindowTypeKind = true;
            } else if (kind == QLatin1String("mode")) {
                sawModeKind = true;
            } else {
                sawOrientationKind = true;
            }
            const QVariantList options = f.value(QStringLiteral("options")).toList();
            QVERIFY2(!options.isEmpty(), "enum valueKind must expose options for the dropdown");
            for (const QVariant& opt : options) {
                const QVariantMap m = opt.toMap();
                QVERIFY(m.contains(QStringLiteral("value")));
                QVERIFY(m.contains(QStringLiteral("wire")));
                QVERIFY(!m.value(QStringLiteral("label")).toString().isEmpty());
            }
        }
    }
    QVERIFY(sawScreenKind);
    QVERIFY(sawActivityKind);
    QVERIFY(sawWindowTypeKind);
    QVERIFY(sawModeKind);
    // The two match fields this expansion adds must keep their editor-driving kinds:
    // ScreenOrientation → "orientation" dropdown, ActiveLayout → "layout" picker. A
    // regression reverting either to "string" would silently break the editor.
    QVERIFY(sawOrientationKind);
    QVERIFY(sawLayoutKind);
    // VirtualDesktop keeps its dedicated "virtualDesktop" kind, which drives the
    // desktop-name picker in the editor and the name resolution in the summaries.
    QVERIFY(sawVirtualDesktopKind);

    // Picker categories drive the fly-out submenu grouping. Every field carries
    // a non-empty category label + a categoryOrder int. The Field enum
    // interleaves state/context, so assert grouping is by CATEGORY (via the
    // language-independent order), not by enum position. The (formerly single,
    // 19-entry) State bucket is split into fine-grained categories:
    // Alphabetical by label: Context=0, Identity=1, Size=2, State=3,
    // Taskbar & switcher=4, Tiling=5, Type=6.
    QHash<QString, int> fieldCategoryOrder;
    for (const QVariant& v : fields) {
        const QVariantMap f = v.toMap();
        QVERIFY(!f.value(QStringLiteral("category")).toString().isEmpty());
        QVERIFY(f.contains(QStringLiteral("categoryOrder")));
        // Every field carries one-line help (the leaf editor's info-icon
        // tooltip) — a missing description would render the icon mute again.
        QVERIFY2(
            !f.value(QStringLiteral("description")).toString().isEmpty(),
            qPrintable(QStringLiteral("field %1 has no description").arg(f.value(QStringLiteral("wire")).toString())));
        fieldCategoryOrder.insert(f.value(QStringLiteral("wire")).toString(),
                                  f.value(QStringLiteral("categoryOrder")).toInt());
    }
    QCOMPARE(fieldCategoryOrder.value(QStringLiteral("appId")), 1); // Identity
    QCOMPARE(fieldCategoryOrder.value(QStringLiteral("windowType")), 6); // Type
    QCOMPARE(fieldCategoryOrder.value(QStringLiteral("isTransient")), 6); // Type
    QCOMPARE(fieldCategoryOrder.value(QStringLiteral("isFullscreen")), 3); // State
    QCOMPARE(fieldCategoryOrder.value(QStringLiteral("isMaximized")), 3); // State
    QCOMPARE(fieldCategoryOrder.value(QStringLiteral("skipTaskbar")), 4); // Taskbar & switcher
    QCOMPARE(fieldCategoryOrder.value(QStringLiteral("skipSwitcher")), 4); // Taskbar & switcher
    QCOMPARE(fieldCategoryOrder.value(QStringLiteral("isFloating")), 5); // Tiling
    QCOMPARE(fieldCategoryOrder.value(QStringLiteral("zone")), 5); // Tiling
    QCOMPARE(fieldCategoryOrder.value(QStringLiteral("width")), 2); // Size
    QCOMPARE(fieldCategoryOrder.value(QStringLiteral("height")), 2); // Size
    QCOMPARE(fieldCategoryOrder.value(QStringLiteral("screenId")), 0); // Context

    // The four match conditions (IsTransient/IsNotification/Width/Height) must be
    // authorable: present in the picker with the correct value kind, and with
    // the operators their category implies (bool -> Equals only; numeric ->
    // Equals/GreaterThan/LessThan). Guards the category-driven editor wiring
    // against a future deny-set or classifier regression.
    QHash<QString, QString> kindByWire;
    QHash<QString, int> valueByWire;
    for (const QVariant& v : fields) {
        const QVariantMap f = v.toMap();
        const QString wire = f.value(QStringLiteral("wire")).toString();
        kindByWire.insert(wire, f.value(QStringLiteral("valueKind")).toString());
        valueByWire.insert(wire, f.value(QStringLiteral("value")).toInt());
    }
    QCOMPARE(kindByWire.value(QStringLiteral("isTransient")), QStringLiteral("bool"));
    QCOMPARE(kindByWire.value(QStringLiteral("isNotification")), QStringLiteral("bool"));
    QCOMPARE(kindByWire.value(QStringLiteral("width")), QStringLiteral("number"));
    QCOMPARE(kindByWire.value(QStringLiteral("height")), QStringLiteral("number"));

    const auto opWires = [&](const QString& wire) {
        QSet<QString> s;
        for (const QVariant& v : controller.operatorsForField(valueByWire.value(wire))) {
            s.insert(v.toMap().value(QStringLiteral("wire")).toString());
        }
        return s;
    };
    const QSet<QString> widthOps = opWires(QStringLiteral("width"));
    QVERIFY(widthOps.contains(QStringLiteral("lessThan")));
    QVERIFY(widthOps.contains(QStringLiteral("greaterThan")));
    QVERIFY(widthOps.contains(QStringLiteral("equals")));
    QCOMPARE(opWires(QStringLiteral("isTransient")), QSet<QString>{QStringLiteral("equals")});

    // AppId (Field enum 0) supports the AppIdMatches operator.
    const QVariantList appOps = controller.operatorsForField(0);
    QVERIFY(!appOps.isEmpty());

    // allOperators() surfaces the FULL operator vocabulary (not a field
    // subset). The leaf editor sizes the operator dropdown to the widest
    // allOperators() label so the operator column lines up across condition
    // rows — that sizing is only correct if allOperators() is a SUPERSET of
    // every field's operator set (otherwise a field operator wider than any
    // measured label would size the column too narrow and elide). Assert the
    // {value, wire, label} shape with non-empty labels and the superset
    // relationship so a regression that drops an operator is caught.
    const QVariantList allOps = controller.allOperators();
    QVERIFY(!allOps.isEmpty());
    QSet<QString> allOperatorWires;
    for (const QVariant& v : allOps) {
        const QVariantMap m = v.toMap();
        QVERIFY(m.contains(QStringLiteral("value")));
        QVERIFY(!m.value(QStringLiteral("wire")).toString().isEmpty());
        QVERIFY(!m.value(QStringLiteral("label")).toString().isEmpty());
        allOperatorWires.insert(m.value(QStringLiteral("wire")).toString());
    }
    for (const QVariant& v : appOps) {
        QVERIFY2(allOperatorWires.contains(v.toMap().value(QStringLiteral("wire")).toString()),
                 "operatorsForField returned an operator absent from allOperators()");
    }

    const QVariantList actions = controller.actionTypes();
    QVERIFY(!actions.isEmpty());
    bool sawFloat = false;
    // Every action carries a picker category; collect the order per wire so the
    // grouping can be spot-checked. Context-domain categories come first
    // (Gaps=0, Engine=1, Snapping=2, Tiling=3, Overlay=4), then the
    // window-domain categories (Animation=5, Appearance=6, Window=7). The old
    // flat "Layout & engine" category was split into Engine / Snapping / Tiling.
    QHash<QString, int> actionCategoryOrder;
    for (const QVariant& v : actions) {
        const QVariantMap a = v.toMap();
        if (a.value(QStringLiteral("value")).toString() == QLatin1String("float"))
            sawFloat = true;
        QVERIFY(!a.value(QStringLiteral("category")).toString().isEmpty());
        QVERIFY(a.contains(QStringLiteral("categoryOrder")));
        actionCategoryOrder.insert(a.value(QStringLiteral("value")).toString(),
                                   a.value(QStringLiteral("categoryOrder")).toInt());
    }
    QVERIFY(sawFloat);
    QCOMPARE(actionCategoryOrder.value(QStringLiteral("setInnerGap")), 0); // Gaps (context)
    QCOMPARE(actionCategoryOrder.value(QStringLiteral("setEngineMode")), 1); // Engine (context)
    QCOMPARE(actionCategoryOrder.value(QStringLiteral("setSnappingLayout")), 2); // Snapping (context)
    QCOMPARE(actionCategoryOrder.value(QStringLiteral("setTilingAlgorithm")), 3); // Tiling (context)
    QCOMPARE(actionCategoryOrder.value(QStringLiteral("setAlgorithmParam")), 3); // Tiling (context)
    QCOMPARE(actionCategoryOrder.value(QStringLiteral("overrideOverlayShader")), 4); // Overlay (context)
    QCOMPARE(actionCategoryOrder.value(QStringLiteral("excludeAnimations")), 5); // Animation (window)
    QCOMPARE(actionCategoryOrder.value(QStringLiteral("setOpacity")), 6); // Appearance (window)
    QCOMPARE(actionCategoryOrder.value(QStringLiteral("exclude")), 7); // Window (window)
}

void TestRuleController::matchIsContextOnlyClassifies()
{
    RuleController controller;

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

void TestRuleController::validationIssuesForJsonFlags()
{
    RuleController controller;

    // Clean rule: window match + Float action → no issues.
    QVariantMap clean = controller.newEmptyRule(QStringLiteral("application"));
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

void TestRuleController::asyncCommitAndRevertAreInvokable()
{
    // Pin the QML-facing commit contract: asyncCommit(bool) is the
    // escape hatch the daemonChangedWhileDirty banner uses, and
    // revert() backs its "Discard and reload" action. Both must
    // stay Q_INVOKABLE or the banner breaks at runtime.
    RuleController controller;
    const QMetaObject* mo = controller.metaObject();
    QVERIFY2(mo->indexOfMethod("asyncCommit(bool)") >= 0,
             "RuleController::asyncCommit must remain Q_INVOKABLE — QML's daemon-changed banner depends on it");
    QVERIFY2(mo->indexOfMethod("revert()") >= 0,
             "RuleController::revert must remain Q_INVOKABLE — the daemon-changed banner's "
             "'Discard and reload' action calls it directly from QML");
}

QTEST_MAIN(TestRuleController)

/// stageUserRules is the profile-activation staging path — a public entry
/// that bypasses addRule. It must enforce the same boundary: an invalid rule
/// (constructed directly; Rule::fromJson cannot produce one) is dropped
/// rather than staged, because one invalid rule in the model poisons the
/// eventual Save whole. The valid rule replaces the previous user subset.
void TestRuleController::stageUserRulesEnforcesTheAddRuleBoundary()
{
    RuleController controller;

    // Seed one user rule through the normal CRUD path.
    QVariantMap seeded = controller.newEmptyRule(QStringLiteral("application"));
    seeded[QStringLiteral("actions")] = QVariantList{QVariantMap{{QStringLiteral("type"), QStringLiteral("float")}}};
    QVERIFY(!controller.addRuleFromJson(seeded).isEmpty());
    QCOMPARE(controller.model()->rowCount(), 1);

    // A rule isValid() rejects (zero actions) but with an otherwise sound
    // id + match — only constructible programmatically.
    Rule bad;
    bad.id = QUuid::createUuid();
    bad.name = QStringLiteral("invalid");
    bad.match = MatchExpression::makeLeaf(Field::AppId, Operator::Equals, QStringLiteral("x"));

    Rule good;
    good.id = QUuid::createUuid();
    good.name = QStringLiteral("valid");
    good.match = MatchExpression::makeLeaf(Field::AppId, Operator::Equals, QStringLiteral("y"));
    RuleAction floatAction;
    floatAction.type = QString(ActionType::Float);
    good.actions = {floatAction};
    QVERIFY(!bad.isValid());
    QVERIFY(good.isValid());

    controller.stageUserRules({bad, good});

    // The invalid rule was dropped at the boundary; the valid one replaced
    // the seeded user subset wholesale.
    QCOMPARE(controller.model()->rowCount(), 1);
    QCOMPARE(controller.model()->rules().first().id, good.id);
}

#include "test_rule_controller.moc"
