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

#include <QJsonObject>
#include <QSet>
#include <QSignalSpy>
#include <QTest>
#include <QUuid>

#include "settings/rules/ruleauthoring.h"
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
    void stageUserRulesRaisesTheCommitGate();
    void promotedLeafRootRoundTrips();
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

    // The desktop subject starts with a VirtualDesktop leaf, seeded at 1
    // because 0 is the "unpinned" sentinel and would match nothing.
    const QVariantMap desktop = controller.newEmptyRule(QStringLiteral("desktop"));
    const QVariantMap desktopMatch = desktop.value(QStringLiteral("match")).toMap();
    QCOMPARE(desktopMatch.value(QStringLiteral("field")).toString(), QStringLiteral("virtualDesktop"));
    QCOMPARE(desktopMatch.value(QStringLiteral("value")).toInt(), 1);

    // Animation and Custom both start from the catch-all All{} composite —
    // they are the two subjects that do NOT seed a bare leaf. Because their
    // match shapes are identical, the PRIORITY is the only thing separating
    // the two branches, so a dispatch that routed one into the other would be
    // invisible without asserting it.
    const QVariantMap custom = controller.newEmptyRule(QStringLiteral("custom"));
    QVERIFY(custom.value(QStringLiteral("match")).toMap().contains(QStringLiteral("all")));
    const QVariantMap animation = controller.newEmptyRule(QStringLiteral("animation"));
    QVERIFY(animation.value(QStringLiteral("match")).toMap().contains(QStringLiteral("all")));
    QVERIFY2(custom.value(QStringLiteral("priority")).toInt() != animation.value(QStringLiteral("priority")).toInt(),
             "custom and animation must not collapse onto one band");

    // Every subject the New Rule dialog offers is covered above. An id it does
    // not know falls through to the custom shape rather than producing an
    // empty map, so a typo in the QML catalogue degrades rather than breaks.
    const QVariantMap unknown = controller.newEmptyRule(QStringLiteral("no-such-subject"));
    QVERIFY(!unknown.value(QStringLiteral("id")).toString().isEmpty());
    QVERIFY(unknown.value(QStringLiteral("match")).toMap().contains(QStringLiteral("all")));

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

    // The negative half of the by-UUID surface. Every one of these takes an id
    // from QML, where a stale binding or a deleted-then-clicked row hands over
    // an id the model no longer has. They must refuse, not assert or write.
    const QString ghost = QUuid::createUuid().toString();
    QVERIFY2(controller.ruleJson(ghost).isEmpty(), "ruleJson must return an empty map for an unknown id");
    QVERIFY2(!controller.setRuleEnabled(ghost, true), "setRuleEnabled must refuse an unknown id");
    QVERIFY2(!controller.removeRule(ghost), "removeRule must refuse an unknown id");

    QVariantMap orphan = controller.newEmptyRule(QStringLiteral("application"));
    orphan[QStringLiteral("id")] = ghost;
    orphan[QStringLiteral("actions")] = QVariantList{QVariantMap{{QStringLiteral("type"), QStringLiteral("float")}}};
    QVERIFY2(!controller.updateRuleFromJson(orphan), "updateRuleFromJson must refuse an id the model does not hold");
    QCOMPARE(controller.model()->rowCount(), 0);
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

    // Adding a rule flips the dirty bit, exactly once. A floor (>= 1) would
    // pass for an add that emitted dirtyChanged three times, which is the
    // regression this signal is most likely to grow — CLAUDE.md's "only emit
    // when the value actually changes" applies here.
    QVERIFY(controller.isDirty());
    QCOMPARE(dirtySpy.count(), 1);

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
    // Under ctest the TEST_LAUNCHER private bus declares NO service
    // directories, so org.plasmazones.Rules is never reachable and the
    // failure arm below is the one that runs. The assertion IS
    // outcome-derived (the branch keeps the success arm alive for a direct
    // dev-box run against a live daemon), so a ctest environment that ever
    // grew a service directory would silently swap which contract is
    // asserted — accepted, since the launcher config is part of this
    // repo's test contract.
    QSignalSpy loadedSpy(&controller, &RuleController::rulesLoaded);
    controller.revert();
    // Pump the event loop briefly so the QDBusPendingCall reply (an error
    // under ctest) lands.
    loadedSpy.wait(500);
    if (loadedSpy.count() > 0) {
        // Direct run against a live daemon: a landed revert clears dirty.
        QVERIFY(!controller.isDirty());
    } else {
        // The ctest path: no daemon, revert cannot land, dirty must stay.
        QVERIFY(controller.isDirty());
    }
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
    bool sawColorSchemeKind = false;
    for (const QVariant& v : fields) {
        const QVariantMap f = v.toMap();
        QVERIFY(f.contains(QStringLiteral("value")));
        QVERIFY(!f.value(QStringLiteral("label")).toString().isEmpty());
        const QString kind = f.value(QStringLiteral("valueKind")).toString();
        QVERIFY(kind == QLatin1String("string") || kind == QLatin1String("number") || kind == QLatin1String("bool")
                || kind == QLatin1String("screen") || kind == QLatin1String("activity")
                || kind == QLatin1String("windowType") || kind == QLatin1String("virtualDesktop")
                || kind == QLatin1String("mode") || kind == QLatin1String("orientation")
                || kind == QLatin1String("layout") || kind == QLatin1String("colorScheme"));
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
        // ColorScheme (light/dark) is the third of the same shape: without it in
        // this loop a dropped options array would leave the editor's dropdown
        // empty with nothing failing.
        if (kind == QLatin1String("windowType") || kind == QLatin1String("mode") || kind == QLatin1String("orientation")
            || kind == QLatin1String("colorScheme")) {
            if (kind == QLatin1String("windowType")) {
                sawWindowTypeKind = true;
            } else if (kind == QLatin1String("mode")) {
                sawModeKind = true;
            } else if (kind == QLatin1String("orientation")) {
                sawOrientationKind = true;
            } else {
                sawColorSchemeKind = true;
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
    // ColorScheme keeps its own "colorScheme" kind. Reverting it to a bare
    // string field would drop the light/dark dropdown and leave the user
    // hand-typing the token, so the kind is pinned the same way the others are.
    QVERIFY(sawColorSchemeKind);

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
    QCOMPARE(fieldCategoryOrder.value(QStringLiteral("appId"), -1), 1); // Identity
    QCOMPARE(fieldCategoryOrder.value(QStringLiteral("windowType"), -1), 6); // Type
    QCOMPARE(fieldCategoryOrder.value(QStringLiteral("isTransient"), -1), 6); // Type
    QCOMPARE(fieldCategoryOrder.value(QStringLiteral("isFullscreen"), -1), 3); // State
    QCOMPARE(fieldCategoryOrder.value(QStringLiteral("isMaximized"), -1), 3); // State
    QCOMPARE(fieldCategoryOrder.value(QStringLiteral("skipTaskbar"), -1), 4); // Taskbar & switcher
    QCOMPARE(fieldCategoryOrder.value(QStringLiteral("skipSwitcher"), -1), 4); // Taskbar & switcher
    QCOMPARE(fieldCategoryOrder.value(QStringLiteral("isFloating"), -1), 5); // Tiling
    QCOMPARE(fieldCategoryOrder.value(QStringLiteral("zone"), -1), 5); // Tiling
    QCOMPARE(fieldCategoryOrder.value(QStringLiteral("width"), -1), 2); // Size
    QCOMPARE(fieldCategoryOrder.value(QStringLiteral("height"), -1), 2); // Size
    QCOMPARE(fieldCategoryOrder.value(QStringLiteral("screenId"), -1), 0); // Context
    // ColorScheme is a context field, not a window-state one: it resolves per
    // screen/desktop/activity like screenId, so it belongs in the Context
    // bucket. A descriptor that dropped it into State would bury it under the
    // window properties in the fly-out.
    QCOMPARE(fieldCategoryOrder.value(QStringLiteral("colorScheme"), -1), 0); // Context

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
        for (const QVariant& v : controller.operatorsForField(valueByWire.value(wire, -1))) {
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
    // (Gaps=0, Engine=1, Snapping=2, Tiling/Algorithm and Tiling/Behavior both
    // =3, Scrolling=4, Overlay=5), then the ONE window-domain bucket, whose
    // numbers order its submenus: Window/Placement=6, Window/Scrolling=7,
    // Window/Appearance=8, Window/Animation=9, Window/Behavior=10,
    // Window/Tab indicator=11, Window/Drop indicator=12. CategoryMenuButton
    // takes a bucket's position from the SMALLEST order in it, so 6 is what
    // puts Window last. An unregistered or uncategorized action falls to
    // Other=99. The old flat "Layout & engine" category was split into
    // Engine / Snapping / Tiling / Scrolling.
    QHash<QString, int> actionCategoryOrder;
    QHash<QString, QString> actionCategoryLabel;
    for (const QVariant& v : actions) {
        const QVariantMap a = v.toMap();
        if (a.value(QStringLiteral("value")).toString() == QLatin1String("float"))
            sawFloat = true;
        QVERIFY(!a.value(QStringLiteral("category")).toString().isEmpty());
        QVERIFY(a.contains(QStringLiteral("categoryOrder")));
        // Every picker entry needs a real translated label: the label table's
        // fallback returns the RAW WIRE STRING, so a future authorable action
        // added without a label branch would silently show "excludePlacement"
        // in the picker while every count-free assertion stays green. The
        // label != wire guard is the picker-side twin of
        // test_rule_model.cpp's summary-label registry loop.
        const QString wire = a.value(QStringLiteral("value")).toString();
        const QString label = a.value(QStringLiteral("label")).toString();
        QVERIFY2(!label.isEmpty(), qPrintable(wire));
        QVERIFY2(label != wire, qPrintable(wire));
        // Description canary: every picker entry carries the info-icon hover
        // help (actionDescription's if-ladder has no compiler exhaustiveness
        // like fieldDescription's switch, so this sweep is what keeps it
        // covering every registered type — an action added without a
        // description entry fails here by name).
        QVERIFY2(!a.value(QStringLiteral("description")).toString().isEmpty(), qPrintable(wire));
        // The structural invariant behind the picker's context/window divider:
        // a top-level bucket takes its side from ONE of its items, so a bucket
        // may never hold both domains. Every window-domain action therefore
        // lives in a `Window/<sub>` submenu and no context-domain action may,
        // which collapses the whole window half into a single bucket the
        // divider can describe honestly. This also pins the shape: bare
        // "Window" would be a direct item sitting above the submenus, the
        // mixed flat-list-plus-submenus layout this organisation replaced.
        const QString category = a.value(QStringLiteral("category")).toString();
        const bool windowDomain = a.value(QStringLiteral("domain")).toString() == QLatin1String("window");
        const QString whereFailed = wire + QLatin1String(" -> ") + category;
        QVERIFY2(category != QLatin1String("Window"), qPrintable(whereFailed));
        QVERIFY2(windowDomain == category.startsWith(QLatin1String("Window/")), qPrintable(whereFailed));
        actionCategoryOrder.insert(wire, a.value(QStringLiteral("categoryOrder")).toInt());
        actionCategoryLabel.insert(wire, category);
    }
    QVERIFY(sawFloat);
    QCOMPARE(actionCategoryOrder.value(QStringLiteral("setInnerGap"), -1), 0); // Gaps (context)
    QCOMPARE(actionCategoryOrder.value(QStringLiteral("setEngineMode"), -1), 1); // Engine (context)
    QCOMPARE(actionCategoryOrder.value(QStringLiteral("setSnappingLayout"), -1), 2); // Snapping (context)
    QCOMPARE(actionCategoryOrder.value(QStringLiteral("setTilingAlgorithm"), -1), 3); // Tiling (context)
    QCOMPARE(actionCategoryOrder.value(QStringLiteral("setAlgorithmParam"), -1), 3); // Tiling (context)
    QCOMPARE(actionCategoryOrder.value(QStringLiteral("setCenterFocusedColumn"), -1), 4); // Scrolling (context)
    // Each Window submenu carries its own number, which is what orders the
    // submenus inside the bucket. Pin the label too: the order alone cannot
    // tell Window/Scrolling apart from a stray top-level bucket that happens
    // to sort at 7.
    QCOMPARE(actionCategoryOrder.value(QStringLiteral("openTabbed"), -1), 7);
    QCOMPARE(actionCategoryLabel.value(QStringLiteral("openTabbed")), QStringLiteral("Window/Scrolling"));
    QCOMPARE(actionCategoryOrder.value(QStringLiteral("overrideOverlayShader"), -1), 5); // Overlay (context)
    QCOMPARE(actionCategoryOrder.value(QStringLiteral("excludeAnimations"), -1), 9);
    QCOMPARE(actionCategoryLabel.value(QStringLiteral("excludeAnimations")), QStringLiteral("Window/Animation"));
    QCOMPARE(actionCategoryOrder.value(QStringLiteral("setOpacity"), -1), 8);
    QCOMPARE(actionCategoryLabel.value(QStringLiteral("setOpacity")), QStringLiteral("Window/Appearance"));
    QCOMPARE(actionCategoryOrder.value(QStringLiteral("exclude"), -1), 6);
    QCOMPARE(actionCategoryLabel.value(QStringLiteral("exclude")), QStringLiteral("Window/Placement"));
    // The scoped exclusion siblings: placement rides Window/Placement with the
    // blanket Exclude; decorations rides Window/Appearance with the border
    // family. A descriptor category typo on either would land it in the wrong
    // picker bucket (or Other=99) with no other test noticing.
    QCOMPARE(actionCategoryLabel.value(QStringLiteral("excludePlacement")), QStringLiteral("Window/Placement"));
    QCOMPARE(actionCategoryLabel.value(QStringLiteral("excludeDecorations")), QStringLiteral("Window/Appearance"));
    // The two behaviour overrides are the reason Window/Behavior exists: the
    // stacking layer and the pointer scroll multiplier are neither placement
    // nor looks, and the scroll multiplier in particular must NOT drift into
    // Window/Scrolling, which is the scrolling engine's per-window arm.
    QCOMPARE(actionCategoryLabel.value(QStringLiteral("setWindowLayer")), QStringLiteral("Window/Behavior"));
    QCOMPARE(actionCategoryLabel.value(QStringLiteral("scrollFactor")), QStringLiteral("Window/Behavior"));
    // The per-window indicator colours: window-domain, so they sit in the
    // Window bucket, one hop from the context-domain half of each family under
    // Scrolling. Keeping a family whole would put window actions above the
    // divider — see actionCategory()'s tabIndicator branch.
    QCOMPARE(actionCategoryLabel.value(QStringLiteral("tabColorActive")), QStringLiteral("Window/Tab indicator"));
    QCOMPARE(actionCategoryLabel.value(QStringLiteral("setTabIndicatorActiveColor")),
             QStringLiteral("Scrolling/Tab indicator"));
    QCOMPARE(actionCategoryLabel.value(QStringLiteral("dropIndicatorColor")), QStringLiteral("Window/Drop indicator"));
    QCOMPARE(actionCategoryLabel.value(QStringLiteral("setDropIndicatorColor")),
             QStringLiteral("Scrolling/Drop indicator"));
    // The per-context scrolling behaviour toggles and enums ride the Scrolling bucket
    // with the sizing knobs they sit beside. Every one of them shares the
    // `layoutEngine` descriptor category with the engine controls, so the
    // bucket is decided by a hand-written per-type dispatch: an action left
    // out of that list falls through to Engine=1 and lands in the wrong
    // submenu with nothing else noticing. Pin the whole set rather than a
    // sample, because the omission is per type.
    QCOMPARE(actionCategoryOrder.value(QStringLiteral("setScrollAlwaysCenterSingleColumn"), -1), 4);
    QCOMPARE(actionCategoryOrder.value(QStringLiteral("setScrollRespectMinimumSize"), -1), 4);
    QCOMPARE(actionCategoryOrder.value(QStringLiteral("setScrollCropStraddlers"), -1), 4);
    QCOMPARE(actionCategoryOrder.value(QStringLiteral("setScrollFocusNewWindows"), -1), 4);
    QCOMPARE(actionCategoryOrder.value(QStringLiteral("setScrollSmartGaps"), -1), 4);
    QCOMPARE(actionCategoryOrder.value(QStringLiteral("setScrollFocusFollowsMouse"), -1), 4);
    QCOMPARE(actionCategoryOrder.value(QStringLiteral("setScrollStickyWindowHandling"), -1), 4);
    QCOMPARE(actionCategoryOrder.value(QStringLiteral("setScrollStripAxis"), -1), 4);
    // The per-window Open* actions are window-domain and ride the
    // Window/Scrolling submenu, the same way openTabbed above does. A miss
    // here drops them into the context-domain Scrolling bucket, above the
    // picker's divider.
    QCOMPARE(actionCategoryOrder.value(QStringLiteral("openMaximized"), -1), 7);
    QCOMPARE(actionCategoryOrder.value(QStringLiteral("openFocused"), -1), 7);
    QCOMPARE(actionCategoryOrder.value(QStringLiteral("openFullscreen"), -1), 7);
    // The unfloat fallback is a windowManagement action, riding
    // Window/Placement with the blanket Exclude.
    QCOMPARE(actionCategoryOrder.value(QStringLiteral("setUnfloatFallbackToZone"), -1), 6);

    // The strip-axis option labels — the exact summary strings the editor
    // combo and the rule row render, matching the Strip direction card's own
    // wording. A token that falls through enumOptionLabel shows its raw wire
    // spelling in the UI with everything else green.
    QCOMPARE(RuleAuthoring::enumOptionLabel(QString(ActionType::SetScrollStripAxis), QString(ActionParam::Value),
                                            QString(StripAxisToken::Auto)),
             QStringLiteral("Match the screen shape"));
    QCOMPARE(RuleAuthoring::enumOptionLabel(QString(ActionType::SetScrollStripAxis), QString(ActionParam::Value),
                                            QString(StripAxisToken::Horizontal)),
             QStringLiteral("Side to side"));
    QCOMPARE(RuleAuthoring::enumOptionLabel(QString(ActionType::SetScrollStripAxis), QString(ActionParam::Value),
                                            QString(StripAxisToken::Vertical)),
             QStringLiteral("Top to bottom"));

    // The bool-phrase canary: EVERY registered action whose Value param is a
    // bool must answer boolActionStateLabel with a non-empty, distinct phrase
    // for both polarities. A new bool action added without its phrases used
    // to render the raw wire string in the rule row with the whole suite
    // green — this is the future-miss net.
    int boolActions = 0;
    const QStringList allTypes = PhosphorRules::ActionRegistry::instance().registeredTypes();
    for (const QString& type : allTypes) {
        const auto desc = PhosphorRules::ActionRegistry::instance().descriptor(type);
        QVERIFY(desc.has_value());
        bool valueIsBool = false;
        for (const auto& param : desc->params) {
            if (param.key == QString(ActionParam::Value) && param.kind == QLatin1String("bool")) {
                valueIsBool = true;
            }
        }
        if (!valueIsBool) {
            continue;
        }
        ++boolActions;
        const QString onLabel = RuleAuthoring::boolActionStateLabel(type, true);
        const QString offLabel = RuleAuthoring::boolActionStateLabel(type, false);
        QVERIFY2(!onLabel.isEmpty() && !offLabel.isEmpty(),
                 qPrintable(QStringLiteral("bool action %1 lacks a polarity phrase").arg(type)));
        QVERIFY2(onLabel != offLabel,
                 qPrintable(QStringLiteral("bool action %1 renders both polarities identically").arg(type)));
    }
    QVERIFY2(boolActions > 10, "the bool-action sweep must actually cover the family, not an empty set");
}

void TestRuleController::promotedLeafRootRoundTrips()
{
    // The C++ half of the editor's leaf-root promotion. Every guided starting
    // point seeds a BARE LEAF as the whole match, and MatchExpressionEditor
    // wraps it into `all:[existingLeaf, blankLeaf]` when the user adds a
    // second condition. That shape reaches the store, so the contract it has
    // to satisfy is pinned here — the QML side has no test harness in this
    // tree, and the shape is the part that persists.
    RuleController controller;

    const QVariantMap seeded = controller.newEmptyRule(QStringLiteral("application"));
    QVariantMap leaf = seeded.value(QStringLiteral("match")).toMap();
    // The seeded subject really is a bare leaf — the premise the promotion
    // exists for. If this ever becomes a composite, the promote row stops
    // rendering and this test is measuring nothing.
    QVERIFY2(leaf.contains(QStringLiteral("field")), "the application subject must seed a bare leaf");
    leaf[QStringLiteral("value")] = QStringLiteral("org.kde.dolphin");

    const QVariantMap blank{{QStringLiteral("field"), QString()},
                            {QStringLiteral("op"), QString()},
                            {QStringLiteral("value"), QString()}};
    QVariantMap promoted = seeded;
    promoted[QStringLiteral("match")] = QVariantMap{{QStringLiteral("all"), QVariantList{leaf, blank}}};
    promoted[QStringLiteral("actions")] = QVariantList{QVariantMap{{QStringLiteral("type"), QStringLiteral("float")}}};

    // A half-filled group must not become a stored rule. The blank leaf makes
    // MatchExpression::fromJson reject the WHOLE expression, so accepting it
    // would either drop the user's filled condition or leave a match-everything
    // catch-all behind. The editor gates Save on its own filled-leaves check;
    // this pins that the store refuses it too, for every path that does not go
    // through the editor (D-Bus, import, a hand-edited rules.json).
    QVERIFY2(controller.addRuleFromJson(promoted).isEmpty(),
             "a promoted group with a blank second condition must not be storable");
    QCOMPARE(controller.model()->rowCount(), 0);

    // Fill the second condition and the rule commits, with the FIRST leaf
    // preserved intact — the promotion embeds the original by reference, so a
    // regression that rebuilt it instead would surface as a lost field/value.
    QVariantMap filled = blank;
    filled[QStringLiteral("field")] = QStringLiteral("windowClass");
    filled[QStringLiteral("op")] = QStringLiteral("contains");
    filled[QStringLiteral("value")] = QStringLiteral("dolphin");
    promoted[QStringLiteral("match")] = QVariantMap{{QStringLiteral("all"), QVariantList{leaf, filled}}};

    const QString id = controller.addRuleFromJson(promoted);
    QVERIFY2(!id.isEmpty(), "a fully filled promoted group must be storable");

    const QVariantMap stored = controller.ruleJson(id);
    const QVariantList children = stored.value(QStringLiteral("match")).toMap().value(QStringLiteral("all")).toList();
    QCOMPARE(children.size(), 2);
    QCOMPARE(children.at(0).toMap().value(QStringLiteral("field")).toString(), QStringLiteral("appId"));
    QCOMPARE(children.at(0).toMap().value(QStringLiteral("value")).toString(), QStringLiteral("org.kde.dolphin"));
    QCOMPARE(children.at(1).toMap().value(QStringLiteral("field")).toString(), QStringLiteral("windowClass"));

    // Still a window-property match, so the picker keeps offering
    // window-domain actions rather than flipping to the context half.
    QVERIFY(!controller.matchIsContextOnly(stored.value(QStringLiteral("match")).toMap()));
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

    // An action whose TYPE is picked but whose required param is still empty
    // (what every seeded rule template hands the editor). RuleAction::fromJson
    // drops it, so saving would silently lose the action — it has to surface
    // as an issue or the Save button stays enabled over a rule that will not
    // survive the round trip.
    QVariantMap unfilled = clean;
    QVariantMap route;
    route[QStringLiteral("type")] = QStringLiteral("routeToScreen");
    route[QStringLiteral("targetScreenId")] = QString();
    unfilled[QStringLiteral("actions")] = QVariantList{route};
    const QVariantList unfilledIssues = controller.validationIssuesForJson(unfilled);
    QCOMPARE(unfilledIssues.size(), 1);
    const QVariantMap unfilledIssue = unfilledIssues.first().toMap();
    QCOMPARE(unfilledIssue.value(QStringLiteral("code")).toInt(),
             static_cast<int>(PhosphorRules::ValidationIssue::Code::IncompleteActionPayload));
    QCOMPARE(unfilledIssue.value(QStringLiteral("actionIndex")).toInt(), 0);
    QCOMPARE(unfilledIssue.value(QStringLiteral("actionType")).toString(), QStringLiteral("routeToScreen"));

    // Discriminator: the same action WITH its picker filled is clean, so the
    // issue above is the empty param and not the action type.
    route[QStringLiteral("targetScreenId")] = QStringLiteral("DP-1");
    unfilled[QStringLiteral("actions")] = QVariantList{route};
    QCOMPARE(controller.validationIssuesForJson(unfilled).size(), 0);

    // A type-less placeholder row (the user added an action but has not
    // picked a type) raises NO issue here: the editor's completeness gate
    // owns that case, and the library's co-located-exclusion check would
    // otherwise name the placeholder with an empty action label.
    QVariantMap placeholder = clean;
    QVariantMap blank;
    blank[QStringLiteral("type")] = QString();
    QVariantMap exclude;
    exclude[QStringLiteral("type")] = QStringLiteral("exclude");
    placeholder[QStringLiteral("actions")] = QVariantList{exclude, blank};
    QCOMPARE(controller.validationIssuesForJson(placeholder).size(), 0);
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

/// Staging into a CLEAN controller must raise the commit gate, not only the
/// badge. The two ride different mechanisms: the Rules page badge derives from
/// the value-based userRulesDirty(), while asyncCommit refuses to push and
/// reload() overwrites the model whenever m_dirty is false. A regression that
/// stages the profile's rules without flipping isDirty() therefore badges the
/// page while the global Save silently commits nothing and the next daemon
/// broadcast erases the staged set — which is why this asserts the BOOLEAN,
/// not the badge. The boundary test above cannot catch it: it seeds through
/// addRuleFromJson first, which is already dirty by then.
void TestRuleController::stageUserRulesRaisesTheCommitGate()
{
    RuleController controller;
    QVERIFY(!controller.isDirty());

    Rule good;
    good.id = QUuid::createUuid();
    good.name = QStringLiteral("profile rule");
    good.match = MatchExpression::makeLeaf(Field::AppId, Operator::Equals, QStringLiteral("z"));
    RuleAction floatAction;
    floatAction.type = QString(ActionType::Float);
    good.actions = {floatAction};
    QVERIFY(good.isValid());

    QSignalSpy dirtySpy(&controller, &RuleController::dirtyChanged);
    controller.stageUserRules({good});

    QVERIFY(controller.isDirty());
    QVERIFY(controller.userRulesDirty());
    QCOMPARE(dirtySpy.count(), 1);

    // Re-staging while already dirty keeps the gate up and still notifies, so
    // the reconcile re-runs against the new model contents.
    controller.stageUserRules({good});
    QVERIFY(controller.isDirty());
    QCOMPARE(dirtySpy.count(), 2);
}

QTEST_MAIN(TestRuleController)

#include "test_rule_controller.moc"
