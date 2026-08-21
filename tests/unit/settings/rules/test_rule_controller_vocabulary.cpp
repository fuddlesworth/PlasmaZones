// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_rule_controller_vocabulary.cpp
 * @brief Coverage for RuleController's authoring vocabulary — the engine-mode
 *        picker tokens, the per-param input hints, the rule templates, the
 *        action-type domains, and the default payload seeding — plus the
 *        SnapToZone zone-name list helpers the editor parses and formats
 *        through.
 *
 * Split out of test_rule_controller_overview.cpp for file-size; the monitor
 * overview projections and the curve-label resolver bridge stay there, the
 * staging CRUD / dirty-tracking contract stays with TestRuleController. Like
 * both, every test here constructs its own RuleController — in a headless
 * unit run the daemon is absent, so the model starts empty and the surfaces
 * are exercised against locally-staged rules.
 */

#include <QSet>
#include <QTest>

#include "settings/rules/rulecontroller.h"

#include <PhosphorRules/ActionParams.h>

using namespace PlasmaZones;

class TestRuleControllerVocabulary : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void engineModePickerExposesAllVocabularyTokens();
    void inputHints();
    void templateCatalogueCarriesUiFields();
    void templatesProduceSeededRules_context();
    void templatesProduceSeededRules_window();
    void templatesProduceSeededRules_placement();
    void zoneNameListParsesAndFormatsRoundTrip();
    void actionTypesCarryDomain();
    void defaultPayloadForSeedsParams();
};

void TestRuleControllerVocabulary::engineModePickerExposesAllVocabularyTokens()
{
    // Pin that the SetEngineMode + DisableEngine pickers expose exactly
    // three options — snapping / autotile / scrolling — with non-empty
    // localised labels. A regression that dropped the Scrolling enum
    // option from `engineModeOptions()` or the GPL settings-layer label
    // map would surface here.
    RuleController controller;
    const QVariantList types = controller.actionTypes();
    // Each entry in `actionTypes()` carries its own `params` list (see the
    // descriptor docstring in rulecontroller.h:330-344). For each
    // param of kind="enum", the `options` list contains `{value, label}`
    // pairs. We walk to the `mode` param of each action and extract its
    // options to verify the closed engine-mode vocabulary.
    const auto findModeOptions = [&](const QString& typeWire) -> QVariantList {
        for (const QVariant& t : types) {
            const QVariantMap tm = t.toMap();
            if (tm.value(QStringLiteral("value")).toString() != typeWire) {
                continue;
            }
            for (const QVariant& p : tm.value(QStringLiteral("params")).toList()) {
                const QVariantMap pm = p.toMap();
                if (pm.value(QStringLiteral("key")).toString() == QLatin1String("mode")) {
                    return pm.value(QStringLiteral("options")).toList();
                }
            }
        }
        return {};
    };
    for (const QString& actionWire : {QStringLiteral("setEngineMode"), QStringLiteral("disableEngine")}) {
        const QVariantList options = findModeOptions(actionWire);
        // QVERIFY2 with the action name rather than a bare QCOMPARE: the
        // failure aborts the slot either way, so this buys the report, not
        // the second iteration — without the message neither the line nor
        // the count says WHICH action was short.
        QVERIFY2(options.size() == 3,
                 qPrintable(QStringLiteral("%1 exposes %2 mode options").arg(actionWire).arg(options.size())));
        QStringList wireValues;
        for (const QVariant& opt : options) {
            const QVariantMap om = opt.toMap();
            wireValues.append(om.value(QStringLiteral("value")).toString());
            QVERIFY2(!om.value(QStringLiteral("label")).toString().isEmpty(),
                     qPrintable(QStringLiteral("empty label for %1 / %2")
                                    .arg(actionWire, om.value(QStringLiteral("value")).toString())));
        }
        QVERIFY2(wireValues.contains(QStringLiteral("snapping")), qPrintable(wireValues.join(QLatin1Char(','))));
        QVERIFY2(wireValues.contains(QStringLiteral("autotile")), qPrintable(wireValues.join(QLatin1Char(','))));
        QVERIFY2(wireValues.contains(QStringLiteral("scrolling")), qPrintable(wireValues.join(QLatin1Char(','))));
    }
}

void TestRuleControllerVocabulary::inputHints()
{
    RuleController controller;

    // Match-condition value hints are keyed on the operator wire token: the
    // operators whose value editor is a plain text box AND whose syntax /
    // matching semantics aren't obvious carry a hint; the self-explanatory ones
    // (and the picker / spin-box operators) carry none. Pins the QML contract —
    // MatchLeafEditor calls matchValueHint(node.op) and shows the result beneath
    // the value field — so a regression that drops or widens it is caught.
    QVERIFY2(!controller.matchValueHint(QStringLiteral("regex")).isEmpty(),
             "the regex operator must carry an input hint");
    QVERIFY2(!controller.matchValueHint(QStringLiteral("appIdMatches")).isEmpty(),
             "the app-id-match operator must carry an input hint");
    QVERIFY2(controller.matchValueHint(QStringLiteral("equals")).isEmpty(),
             "equals is self-explanatory and must carry no hint");
    QVERIFY2(controller.matchValueHint(QStringLiteral("contains")).isEmpty(),
             "contains is self-explanatory and must carry no hint");
    QVERIFY2(controller.matchValueHint(QString()).isEmpty(), "an empty operator token yields no hint");

    // The SnapToZone action's `zones` param carries an input hint (its free-text
    // ordinal-list syntax isn't discoverable from the field). The hint rides on
    // the param descriptor in actionTypes() — ActionRow reads `param.hint`.
    const QVariantList actions = controller.actionTypes();
    bool sawSnapToZoneZones = false;
    bool sawSnapToZoneNames = false;
    for (const QVariant& a : actions) {
        const QVariantMap action = a.toMap();
        if (action.value(QStringLiteral("value")).toString() != QLatin1String("snapToZone")) {
            continue;
        }
        for (const QVariant& p : action.value(QStringLiteral("params")).toList()) {
            const QVariantMap param = p.toMap();
            const QString key = param.value(QStringLiteral("key")).toString();
            if (key == QLatin1String("zones")) {
                sawSnapToZoneZones = true;
                QCOMPARE(param.value(QStringLiteral("kind")).toString(), QStringLiteral("zoneOrdinals"));
                QVERIFY2(!param.value(QStringLiteral("hint")).toString().isEmpty(),
                         "SnapToZone's zones param must carry an input hint");
            } else if (key == QLatin1String("zoneNames")) {
                // The zone-name twin (discussion #924) is free text too, so it
                // carries its own hint and its own kind for the editor dispatch.
                sawSnapToZoneNames = true;
                QCOMPARE(param.value(QStringLiteral("kind")).toString(), QStringLiteral("zoneNames"));
                QVERIFY2(!param.value(QStringLiteral("hint")).toString().isEmpty(),
                         "SnapToZone's zoneNames param must carry an input hint");
            }
        }
    }
    QVERIFY2(sawSnapToZoneZones, "actionTypes() must expose the SnapToZone zones param");
    QVERIFY2(sawSnapToZoneNames, "actionTypes() must expose the SnapToZone zoneNames param");
}

void TestRuleControllerVocabulary::templateCatalogueCarriesUiFields()
{
    RuleController controller;

    // The catalogue surfaced to the AddRuleSheet — every template entry
    // must carry the four UI fields the QML grid binds against. A missing
    // field would render a tile with a blank label or no icon.
    const QVariantList templates = controller.ruleTemplates();
    QVERIFY(!templates.isEmpty());
    QSet<QString> seenIcons;
    for (const QVariant& v : templates) {
        const QVariantMap t = v.toMap();
        // QVERIFY2 with the id so a failing entry names ITSELF — a bare
        // QVERIFY here reported only a line number, and the abort hid any
        // further bad entries behind the first.
        const QByteArray id = t.value(QStringLiteral("id")).toString().toUtf8();
        QVERIFY2(!t.value(QStringLiteral("id")).toString().isEmpty(), "template with empty id");
        QVERIFY2(!t.value(QStringLiteral("label")).toString().isEmpty(), id.constData());
        QVERIFY2(!t.value(QStringLiteral("description")).toString().isEmpty(), id.constData());
        const QString icon = t.value(QStringLiteral("icon")).toString();
        QVERIFY2(!icon.isEmpty(), id.constData());
        // No two tiles may share an icon. Three of them once shared the
        // generic edit-delete-remove red X, which made that whole block of the
        // grid read as one card repeated — the reason the catalogue was culled.
        QVERIFY2(!seenIcons.contains(icon), qPrintable(QString::fromUtf8(id) + QLatin1String(" reuses icon ") + icon));
        seenIcons.insert(icon);
        // Every catalogued id must actually materialise. `newRuleFromTemplate`
        // returns an empty map for an unknown id, so a tile added to the
        // catalogue without its matching branch below would render, be
        // clickable, and hand the sheet nothing.
        QVERIFY2(!controller.newRuleFromTemplate(t.value(QStringLiteral("id")).toString()).isEmpty(), id.constData());
    }
}

// The context-domain templates (monitor / desktop / orientation keyed):
// each seeds the match leaf and action shape the rule editor's contract
// expects. Split per family so the first failing template hides only its
// own siblings, not the whole catalogue.
void TestRuleControllerVocabulary::templatesProduceSeededRules_context()
{
    RuleController controller;

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

    // `layoutOnDesktop` is the desktop twin of layoutOnMonitor: a
    // `VirtualDesktop == 1` leaf (0 is the all-desktops sentinel, so the seed
    // must be a real desktop number) + the same SetEngineMode("snapping") +
    // SetSnappingLayout pair.
    const QVariantMap desktopRule = controller.newRuleFromTemplate(QStringLiteral("layoutOnDesktop"));
    const QVariantMap desktopMatch = desktopRule.value(QStringLiteral("match")).toMap();
    QCOMPARE(desktopMatch.value(QStringLiteral("field")).toString(), QStringLiteral("virtualDesktop"));
    QCOMPARE(desktopMatch.value(QStringLiteral("value")).toInt(), 1);
    const QVariantList desktopActions = desktopRule.value(QStringLiteral("actions")).toList();
    QCOMPARE(desktopActions.size(), 2);
    QCOMPARE(desktopActions.at(0).toMap().value(QStringLiteral("mode")).toString(), QStringLiteral("snapping"));
    QCOMPARE(desktopActions.at(1).toMap().value(QStringLiteral("type")).toString(),
             QStringLiteral("setSnappingLayout"));

    // `scrollingOnMonitor` is mode-only on purpose: ScreenId leaf +
    // SetEngineMode("scrolling") and nothing else, so the quick-start is
    // immediately savable without forcing a scrolling template choice.
    const QVariantMap scrollRule = controller.newRuleFromTemplate(QStringLiteral("scrollingOnMonitor"));
    QCOMPARE(scrollRule.value(QStringLiteral("match")).toMap().value(QStringLiteral("field")).toString(),
             QStringLiteral("screenId"));
    const QVariantList scrollActions = scrollRule.value(QStringLiteral("actions")).toList();
    QCOMPARE(scrollActions.size(), 1);
    QCOMPARE(scrollActions.at(0).toMap().value(QStringLiteral("mode")).toString(), QStringLiteral("scrolling"));
}

// The window-domain templates (per-app exclusion and float).
void TestRuleControllerVocabulary::templatesProduceSeededRules_window()
{
    RuleController controller;

    // `excludeApp` is the per-app exclusion template (Application subject +
    // ExcludePlacement action — placement-only, as the template's title and
    // description promise; decorations and animations stay). Single action,
    // no params required.
    const QVariantMap excludeRule = controller.newRuleFromTemplate(QStringLiteral("excludeApp"));
    QCOMPARE(excludeRule.value(QStringLiteral("match")).toMap().value(QStringLiteral("field")).toString(),
             QStringLiteral("appId"));
    const QVariantList excludeActions = excludeRule.value(QStringLiteral("actions")).toList();
    QCOMPARE(excludeActions.size(), 1);
    QCOMPARE(excludeActions.at(0).toMap().value(QStringLiteral("type")).toString(), QStringLiteral("excludePlacement"));

    // `floatApp`: AppId leaf + a single param-less Float action.
    const QVariantMap floatRule = controller.newRuleFromTemplate(QStringLiteral("floatApp"));
    QCOMPARE(floatRule.value(QStringLiteral("match")).toMap().value(QStringLiteral("field")).toString(),
             QStringLiteral("appId"));
    const QVariantList floatActions = floatRule.value(QStringLiteral("actions")).toList();
    QCOMPARE(floatActions.size(), 1);
    QCOMPARE(floatActions.at(0).toMap().value(QStringLiteral("type")).toString(), QStringLiteral("float"));
}

// The per-app placement templates (SnapToZone, RouteToScreen) and the
// unknown-id contract.
void TestRuleControllerVocabulary::templatesProduceSeededRules_placement()
{
    RuleController controller;

    // `snapAppToZone` is the flagship per-app placement rule: AppId leaf +
    // SnapToZone seeded with ordinal 1. The template deliberately seeds an
    // ordinal rather than a name (ordinals need no layout to exist), and the
    // seed must survive the round-trip so the action has a target.
    const QVariantMap snapRule = controller.newRuleFromTemplate(QStringLiteral("snapAppToZone"));
    QCOMPARE(snapRule.value(QStringLiteral("match")).toMap().value(QStringLiteral("field")).toString(),
             QStringLiteral("appId"));
    const QVariantList snapActions = snapRule.value(QStringLiteral("actions")).toList();
    QCOMPARE(snapActions.size(), 1);
    QCOMPARE(snapActions.at(0).toMap().value(QStringLiteral("type")).toString(), QStringLiteral("snapToZone"));
    const QVariantList seededZones = snapActions.at(0).toMap().value(QStringLiteral("zones")).toList();
    QCOMPARE(seededZones.size(), 1);
    QCOMPARE(seededZones.at(0).toInt(), 1);
    QVERIFY2(!snapActions.at(0).toMap().contains(QStringLiteral("zoneNames")),
             "the template seeds ordinals only; names are the user's to add");

    // `routeAppToScreen`: AppId leaf + RouteToScreen with an empty target
    // (the user picks the monitor in the editor).
    const QVariantMap routeRule = controller.newRuleFromTemplate(QStringLiteral("routeAppToScreen"));
    QCOMPARE(routeRule.value(QStringLiteral("match")).toMap().value(QStringLiteral("field")).toString(),
             QStringLiteral("appId"));
    const QVariantList routeActions = routeRule.value(QStringLiteral("actions")).toList();
    QCOMPARE(routeActions.size(), 1);
    QCOMPARE(routeActions.at(0).toMap().value(QStringLiteral("type")).toString(), QStringLiteral("routeToScreen"));

    // An unknown id must return an empty map — the AddRuleSheet would
    // otherwise commit a UUID-less rule on a typo in the template id.
    const QVariantMap bogus = controller.newRuleFromTemplate(QStringLiteral("nonexistentTemplate"));
    QVERIFY(bogus.isEmpty());
}

void TestRuleControllerVocabulary::actionTypesCarryDomain()
{
    // The action row keys off this field to flag context-domain actions when
    // the match references window-property leaves — a regression that drops
    // the domain would silently lose the warning for the silently-never-fires
    // combination.
    RuleController controller;
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
    QCOMPARE(domainOf.value(QStringLiteral("lockContext")), QStringLiteral("context"));
    QCOMPARE(domainOf.value(QStringLiteral("float")), QStringLiteral("window"));
    QCOMPARE(domainOf.value(QStringLiteral("exclude")), QStringLiteral("window"));
    // The scoped exclusion siblings. These rows are the REAL domain guard —
    // the generic loop above is unfalsifiable by construction (the
    // controller's two-way enum map can only ever produce "context" or
    // "window") — and `domainOf.value()` returns an empty string for an
    // absent key, so each row also pins that the action is surfaced by
    // actionTypes() at all.
    QCOMPARE(domainOf.value(QStringLiteral("excludePlacement")), QStringLiteral("window"));
    QCOMPARE(domainOf.value(QStringLiteral("excludeDecorations")), QStringLiteral("window"));
}

void TestRuleControllerVocabulary::defaultPayloadForSeedsParams()
{
    // The QML action row uses `defaultPayloadFor` when the user switches the
    // type picker to a new action — a stale regression that returned a bare
    // `{type: X}` map would leave SpinBoxes anchored at 0 and `canSave`
    // would gate the rule on params the user never had a chance to fill.
    RuleController controller;

    // Float carries no params — payload is exactly `{type: float}`.
    const QVariantMap floatPayload = controller.defaultPayloadFor(QStringLiteral("float"));
    QCOMPARE(floatPayload.value(QStringLiteral("type")).toString(), QStringLiteral("float"));
    QCOMPARE(floatPayload.size(), 1);

    // The scoped exclusions share Float's param-less shape — the type picker
    // can now land on either, and neither had ever been driven through this
    // path before they shipped.
    for (const QString& wire : {QStringLiteral("excludePlacement"), QStringLiteral("excludeDecorations")}) {
        const QVariantMap payload = controller.defaultPayloadFor(wire);
        QCOMPARE(payload.value(QStringLiteral("type")).toString(), wire);
        QCOMPARE(payload.size(), 1);
    }

    // SetOpacity stores `display * scale`; the descriptor declares
    // defaultDisplay=100 with scale=0.01, so the seeded wire value is
    // 1.0 (100% — no visible change). A future change to the descriptor's
    // defaultDisplay would automatically flow through here. The earlier
    // seed-at-`min`=0 behaviour was a bug: a SetOpacity rule was savable
    // immediately at 0% (invisible window) before the user adjusted.
    const QVariantMap opacityPayload = controller.defaultPayloadFor(QStringLiteral("setOpacity"));
    QCOMPARE(opacityPayload.value(QStringLiteral("type")).toString(), QStringLiteral("setOpacity"));
    QVERIFY(opacityPayload.contains(QStringLiteral("value")));
    QCOMPARE(opacityPayload.value(QStringLiteral("value")).toDouble(), 1.0);

    // SetEngineMode's `mode` is an enum — seeded to the first option's wire
    // value. The engineModeOptions list begins with snapping, so the default
    // pre-selects that. Changing the order in the descriptor would
    // automatically change this default.
    const QVariantMap modePayload = controller.defaultPayloadFor(QStringLiteral("setEngineMode"));
    QCOMPARE(modePayload.value(QStringLiteral("type")).toString(), QStringLiteral("setEngineMode"));
    QCOMPARE(modePayload.value(QStringLiteral("mode")).toString(), QStringLiteral("snapping"));

    // SetSnappingLayout uses the `snappingLayout` picker kind — no implicit
    // default (the user must pick a layout), so the seeded value is an
    // empty string. `canSave` will then explicitly surface "layout missing".
    const QVariantMap layoutPayload = controller.defaultPayloadFor(QStringLiteral("setSnappingLayout"));
    QVERIFY(layoutPayload.contains(QStringLiteral("layoutId")));
    QCOMPARE(layoutPayload.value(QStringLiteral("layoutId")).toString(), QString());

    // OverrideAnimationCurve has two picker-kind params — both empty.
    const QVariantMap curvePayload = controller.defaultPayloadFor(QStringLiteral("overrideAnimationCurve"));
    QVERIFY(curvePayload.contains(QStringLiteral("event")));
    QVERIFY(curvePayload.contains(QStringLiteral("curve")));
    QCOMPARE(curvePayload.value(QStringLiteral("event")).toString(), QString());
    QCOMPARE(curvePayload.value(QStringLiteral("curve")).toString(), QString());

    // SetBorderColorActive / SetBorderColorInactive each carry a single
    // `color`-kind param keyed `value`, seeded with the accent sentinel so a
    // fresh border-colour rule follows the system accent until the user picks a
    // concrete colour.
    const QVariantMap borderColorActivePayload = controller.defaultPayloadFor(QStringLiteral("setBorderColorActive"));
    QCOMPARE(borderColorActivePayload.value(QStringLiteral("type")).toString(), QStringLiteral("setBorderColorActive"));
    QCOMPARE(borderColorActivePayload.value(QStringLiteral("value")).toString(), QStringLiteral("accent"));
    const QVariantMap borderColorInactivePayload =
        controller.defaultPayloadFor(QStringLiteral("setBorderColorInactive"));
    QCOMPARE(borderColorInactivePayload.value(QStringLiteral("type")).toString(),
             QStringLiteral("setBorderColorInactive"));
    QCOMPARE(borderColorInactivePayload.value(QStringLiteral("value")).toString(), QStringLiteral("accent"));

    // LockContext's `value` is a bool with defaultDisplay=1.0, so a freshly
    // type-switched lock action seeds to `value: true` — the picker opens
    // value-on (the meaningful "lock" default). A descriptor regression that
    // dropped defaultDisplay would seed `false` here, silently authoring a
    // lock action that doesn't lock.
    const QVariantMap lockPayload = controller.defaultPayloadFor(QStringLiteral("lockContext"));
    QCOMPARE(lockPayload.value(QStringLiteral("type")).toString(), QStringLiteral("lockContext"));
    QVERIFY(lockPayload.contains(QStringLiteral("value")));
    QCOMPARE(lockPayload.value(QStringLiteral("value")).toBool(), true);

    // The bool actions added for the scrolling and unfloat parity work seed
    // DELIBERATELY ASYMMETRIC polarities: each one opens on the value a user
    // authoring that rule is actually reaching for, which is the opposite of
    // whatever its governing global setting defaults to. That asymmetry is
    // the contract (the unfloat seed is spelled out in the changelog), and it
    // is exactly the kind of thing a descriptor edit flips by accident, so
    // pin both polarities rather than a sample of one. Presence is asserted
    // before the value because QVariant().toBool() also reads false.
    struct BoolSeed
    {
        QString wire;
        bool seed;
    };
    for (const BoolSeed& s : {
             // Global snapUnfloatFallbackToZone defaults OFF, so the rule opts in.
             BoolSeed{QStringLiteral("setUnfloatFallbackToZone"), true},
             // No global counterpart — the rule exists to maximize, so it opens on.
             BoolSeed{QStringLiteral("openMaximized"), true},
             // Global focus-new-windows defaults ON, so the rule opts out.
             BoolSeed{QStringLiteral("openFocused"), false},
             BoolSeed{QStringLiteral("openFullscreen"), true},
             // The per-context scrolling toggles, each seeded against its own
             // global default.
             BoolSeed{QStringLiteral("setScrollAlwaysCenterSingleColumn"), true},
             BoolSeed{QStringLiteral("setScrollRespectMinimumSize"), false},
             BoolSeed{QStringLiteral("setScrollCropStraddlers"), true},
             BoolSeed{QStringLiteral("setScrollFocusNewWindows"), false},
             BoolSeed{QStringLiteral("setScrollSmartGaps"), false},
             BoolSeed{QStringLiteral("setScrollFocusFollowsMouse"), true},
         }) {
        const QVariantMap payload = controller.defaultPayloadFor(s.wire);
        QCOMPARE(payload.value(QStringLiteral("type")).toString(), s.wire);
        // QVERIFY2 over QVERIFY so a failing row names itself: an abort here
        // hides every later row, and the rows differ only by wire string.
        QVERIFY2(payload.contains(QStringLiteral("value")), qPrintable(s.wire));
        QVERIFY2(payload.value(QStringLiteral("value")).toBool() == s.seed, qPrintable(s.wire));
    }

    // SnapToZone seeds ONE valid ordinal (so the fresh action has a target the
    // validator accepts) and an EMPTY name list (a valid starting shape beside
    // the ordinal, and the array the editor's guards expect to find).
    const QVariantMap snapPayload = controller.defaultPayloadFor(QStringLiteral("snapToZone"));
    QCOMPARE(snapPayload.value(QStringLiteral("type")).toString(), QStringLiteral("snapToZone"));
    QCOMPARE(snapPayload.value(QStringLiteral("zones")).toList(), QVariantList{1});
    QVERIFY(snapPayload.contains(QStringLiteral("zoneNames")));
    QVERIFY(snapPayload.value(QStringLiteral("zoneNames")).toList().isEmpty());

    // Unknown type → bare `{type: X}` map. The QML side will never call this
    // with an unknown wire (the picker only offers registered types), but
    // returning a sane shape keeps the contract total.
    const QVariantMap unknownPayload = controller.defaultPayloadFor(QStringLiteral("bogusActionType"));
    QCOMPARE(unknownPayload.value(QStringLiteral("type")).toString(), QStringLiteral("bogusActionType"));
    QCOMPARE(unknownPayload.size(), 1);
}

void TestRuleControllerVocabulary::zoneNameListParsesAndFormatsRoundTrip()
{
    // The SnapToZone "Zone names" free-text contract the editor's field and
    // picker share: `,` / `;` separated, trimmed, blank and over-long entries
    // dropped, deduplicated case-insensitively keeping the first spelling, and
    // a name containing a separator or a quote rides inside straight double
    // quotes so the formatted string parses back to the same list.
    RuleController controller;

    QCOMPARE(controller.parseZoneNameList(QStringLiteral("Editor, Terminal ;Browser")),
             (QStringList{QStringLiteral("Editor"), QStringLiteral("Terminal"), QStringLiteral("Browser")}));
    QCOMPARE(controller.parseZoneNameList(QStringLiteral("  ")), QStringList{});
    QCOMPARE(controller.parseZoneNameList(QStringLiteral(",;,")), QStringList{});
    // Case-insensitive dedupe keeps the first spelling and order.
    QCOMPARE(controller.parseZoneNameList(QStringLiteral("Editor, editor, EDITOR, Terminal")),
             (QStringList{QStringLiteral("Editor"), QStringLiteral("Terminal")}));
    // A quoted token may carry separators; a doubled quote is one literal quote.
    QCOMPARE(controller.parseZoneNameList(QStringLiteral("\"Left, Right\", Main")),
             (QStringList{QStringLiteral("Left, Right"), QStringLiteral("Main")}));
    QCOMPARE(controller.parseZoneNameList(QStringLiteral("\"Say \"\"hi\"\"\"")),
             QStringList{QStringLiteral("Say \"hi\"")});
    // Over the per-name bound: dropped, the rest survive.
    const QString tooLong(PhosphorRules::MaxZoneNameLength + 1, QLatin1Char('a'));
    const QString atBound(PhosphorRules::MaxZoneNameLength, QLatin1Char('b'));
    QCOMPARE(controller.parseZoneNameList(tooLong + QStringLiteral(", ") + atBound), QStringList{atBound});

    // Formatting quotes only what needs it, and round-trips.
    QCOMPARE(controller.formatZoneNameList({QStringLiteral("Editor"), QStringLiteral("Terminal")}),
             QStringLiteral("Editor, Terminal"));
    QCOMPARE(controller.formatZoneNameList(
                 {QStringLiteral("Left, Right"), QStringLiteral("A;B"), QStringLiteral("Say \"hi\"")}),
             QStringLiteral("\"Left, Right\", \"A;B\", \"Say \"\"hi\"\"\""));
    const QStringList awkward{QStringLiteral("Left, Right"), QStringLiteral("Plain"), QStringLiteral("Say \"hi\""),
                              QStringLiteral("A;B")};
    QCOMPARE(controller.parseZoneNameList(controller.formatZoneNameList(awkward)), awkward);
    // Blank entries are dropped on format as well.
    QCOMPARE(controller.formatZoneNameList({QStringLiteral("  "), QStringLiteral("Editor")}), QStringLiteral("Editor"));
}

QTEST_MAIN(TestRuleControllerVocabulary)

#include "test_rule_controller_vocabulary.moc"
