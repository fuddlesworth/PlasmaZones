// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// The open-placement half of the RuleAction wire tests: the RouteToScreen /
// RouteToDesktop / RouteToWorkspace slot mapping and the SnapToZone target
// payload (ordinals and zone names). Split from test_ruleaction.cpp for
// file-size, the way
// test_ruleaction_contextbools.cpp and test_ruleaction_tilingparams.cpp were.

#include <PhosphorRules/Rule.h>
#include <PhosphorRules/RuleAction.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QTest>
#include <QUuid>

using namespace PhosphorRules;

class TestRuleActionPlacement : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void testJson_routeToScreen()
    {
        QJsonObject o;
        o.insert(QStringLiteral("type"), QString(ActionType::RouteToScreen));
        // Missing / empty target screen id is rejected.
        QVERIFY(!RuleAction::fromJson(o).has_value());
        o.insert(QString(ActionParam::TargetScreenId), QString());
        QVERIFY(!RuleAction::fromJson(o).has_value());
        // A non-empty canonical screen id is accepted (not validated against
        // live screen state — a route to a currently-absent monitor is legal).
        o.insert(QString(ActionParam::TargetScreenId), QStringLiteral("LG Electronics:38GN950:688325"));
        const auto loaded = RuleAction::fromJson(o);
        QVERIFY(loaded.has_value());
        QCOMPARE(ActionRegistry::instance().slotFor(*loaded), QString(ActionSlot::RouteScreen));
        // Whitespace-only is blank, not a target — rejected like the empty id.
        o.insert(QString(ActionParam::TargetScreenId), QStringLiteral("   "));
        QVERIFY(!RuleAction::fromJson(o).has_value());
        // The length cap is inclusive and measured on the TRIMMED id: the
        // boundary passes, one over fails, and padding around a boundary-length
        // id does not push it over (mirrors the SnapToZone zone-name pins).
        o.insert(QString(ActionParam::TargetScreenId), QString(MaxScreenIdLength, QLatin1Char('a')));
        QVERIFY(RuleAction::fromJson(o).has_value());
        o.insert(QString(ActionParam::TargetScreenId), QString(MaxScreenIdLength + 1, QLatin1Char('a')));
        QVERIFY(!RuleAction::fromJson(o).has_value());
        o.insert(QString(ActionParam::TargetScreenId),
                 QString(QStringLiteral("  ") + QString(MaxScreenIdLength, QLatin1Char('a')) + QStringLiteral("  ")));
        QVERIFY(RuleAction::fromJson(o).has_value());
        // A padded ordinary id is accepted and stored VERBATIM: trimming is a
        // read-side concern, the same contract the RouteToWorkspace name and
        // the SnapToZone zone names pin. The readers that honour it are the
        // daemon's placement-path screen resolution and the settings rule
        // model's "Open on monitor" summary (rulemodel_labels.cpp), both of
        // which trim before resolving the id against live monitors.
        o.insert(QString(ActionParam::TargetScreenId), QStringLiteral("  LG Electronics:38GN950:688325  "));
        const auto padded = RuleAction::fromJson(o);
        QVERIFY(padded.has_value());
        QCOMPARE(padded->toJson().value(QString(ActionParam::TargetScreenId)).toString(),
                 QStringLiteral("  LG Electronics:38GN950:688325  "));
        // The realistic colon-bearing id loads on its own before the negative
        // case below reuses the object.
        o.insert(QString(ActionParam::TargetScreenId), QStringLiteral("LG Electronics:38GN950:688325"));
        QVERIFY(RuleAction::fromJson(o).has_value());
        // Unknown param key is rejected by the strict loader.
        o.insert(QStringLiteral("bogus"), 1);
        QVERIFY(!RuleAction::fromJson(o).has_value());
    }

    void testJson_routeToDesktop()
    {
        QJsonObject o;
        o.insert(QStringLiteral("type"), QString(ActionType::RouteToDesktop));
        // Missing desktop is rejected.
        QVERIFY(!RuleAction::fromJson(o).has_value());
        // Desktops are 1-based: 0 and negatives are rejected.
        o.insert(QString(ActionParam::TargetDesktop), 0);
        QVERIFY(!RuleAction::fromJson(o).has_value());
        o.insert(QString(ActionParam::TargetDesktop), -1);
        QVERIFY(!RuleAction::fromJson(o).has_value());
        // Non-integral is rejected.
        o.insert(QString(ActionParam::TargetDesktop), 2.5);
        QVERIFY(!RuleAction::fromJson(o).has_value());
        // Out-of-range (above the cap) is rejected.
        o.insert(QString(ActionParam::TargetDesktop), MaxVirtualDesktopOrdinal + 1);
        QVERIFY(!RuleAction::fromJson(o).has_value());
        // A valid 1-based desktop loads and resolves to the route-desktop slot.
        o.insert(QString(ActionParam::TargetDesktop), 3);
        const auto loaded = RuleAction::fromJson(o);
        QVERIFY(loaded.has_value());
        QCOMPARE(ActionRegistry::instance().slotFor(*loaded), QString(ActionSlot::RouteDesktop));
    }

    void testJson_routeToWorkspace()
    {
        QJsonObject o;
        o.insert(QStringLiteral("type"), QString(ActionType::RouteToWorkspace));
        // Missing / empty workspace name is rejected.
        QVERIFY(!RuleAction::fromJson(o).has_value());
        o.insert(QString(ActionParam::TargetWorkspaceName), QString());
        QVERIFY(!RuleAction::fromJson(o).has_value());
        // A non-empty name is accepted (not validated against live
        // declarations — a rule naming a not-yet-declared workspace is
        // legitimate and dormant) and resolves to its own slot, distinct
        // from RouteDesktop.
        o.insert(QString(ActionParam::TargetWorkspaceName), QStringLiteral("chat"));
        const auto loaded = RuleAction::fromJson(o);
        QVERIFY(loaded.has_value());
        QCOMPARE(ActionRegistry::instance().slotFor(*loaded), QString(ActionSlot::RouteWorkspace));
        // Whitespace-only is blank, not a name — rejected like the empty one
        // (the daemon resolves trimmed names, so an all-space name could never
        // match a declaration).
        o.insert(QString(ActionParam::TargetWorkspaceName), QStringLiteral("   "));
        QVERIFY(!RuleAction::fromJson(o).has_value());
        // The length cap is inclusive and measured on the TRIMMED name: the
        // boundary passes, one over fails, and padding around a boundary-length
        // name does not push it over (mirrors the SnapToZone zone-name pins).
        o.insert(QString(ActionParam::TargetWorkspaceName), QString(MaxWorkspaceNameLength, QLatin1Char('a')));
        QVERIFY(RuleAction::fromJson(o).has_value());
        o.insert(QString(ActionParam::TargetWorkspaceName), QString(MaxWorkspaceNameLength + 1, QLatin1Char('a')));
        QVERIFY(!RuleAction::fromJson(o).has_value());
        o.insert(
            QString(ActionParam::TargetWorkspaceName),
            QString(QStringLiteral("  ") + QString(MaxWorkspaceNameLength, QLatin1Char('a')) + QStringLiteral("  ")));
        QVERIFY(RuleAction::fromJson(o).has_value());
        // A padded ordinary name is accepted and stored VERBATIM: trimming is a
        // read-side concern of the daemon's resolver, not the loader's, the
        // same contract the SnapToZone zone names pin.
        o.insert(QString(ActionParam::TargetWorkspaceName), QStringLiteral("  chat  "));
        const auto padded = RuleAction::fromJson(o);
        QVERIFY(padded.has_value());
        QCOMPARE(padded->toJson().value(QString(ActionParam::TargetWorkspaceName)).toString(),
                 QStringLiteral("  chat  "));
        o.insert(QString(ActionParam::TargetWorkspaceName), QStringLiteral("chat"));
        // Unknown param key is rejected by the strict loader.
        o.insert(QStringLiteral("bogus"), 1);
        QVERIFY(!RuleAction::fromJson(o).has_value());
    }

    void testSnapToZone_fromJson()
    {
        // SnapToZone is a window-domain placement action whose `zones` param is a
        // non-empty JSON array of 1-based integer ordinals. Pin the validator at
        // the public fromJson boundary: it is the single line of defence against
        // a hand-edited rules.json carrying a malformed ordinal list, and a
        // widening regression in registerBuiltins would otherwise slip past.
        const auto withZones = [](const QJsonValue& zones) {
            QJsonObject o;
            o.insert(QStringLiteral("type"), QString(ActionType::SnapToZone));
            o.insert(QString(ActionParam::Zones), zones);
            return o;
        };

        // Missing / wrong-typed / empty → rejected.
        QJsonObject missing;
        missing.insert(QStringLiteral("type"), QString(ActionType::SnapToZone));
        QVERIFY(!RuleAction::fromJson(missing).has_value());
        QVERIFY(!RuleAction::fromJson(withZones(QJsonValue(2))).has_value()); // not an array
        QVERIFY(!RuleAction::fromJson(withZones(QJsonArray{})).has_value()); // empty
        QVERIFY(!RuleAction::fromJson(withZones(QJsonArray{0})).has_value()); // 0 not 1-based
        QVERIFY(!RuleAction::fromJson(withZones(QJsonArray{-1})).has_value()); // negative
        QVERIFY(!RuleAction::fromJson(withZones(QJsonArray{1.5})).has_value()); // non-integral
        QVERIFY(!RuleAction::fromJson(withZones(QJsonArray{QStringLiteral("1")})).has_value()); // string
        // Above the shared ordinal cap — named, so a future cap change moves
        // this test with the validator (the RouteToDesktop sibling test uses
        // MaxVirtualDesktopOrdinal the same way).
        QVERIFY(!RuleAction::fromJson(withZones(QJsonArray{MaxZoneOrdinal + 1})).has_value());
        // A double far beyond int range must be rejected by the bound BEFORE any
        // narrowing cast (an out-of-range float-to-int cast is UB) — must not crash.
        QVERIFY(!RuleAction::fromJson(withZones(QJsonArray{1e18})).has_value());

        // Single zone, span, and the inclusive cap boundary are all accepted.
        QVERIFY(RuleAction::fromJson(withZones(QJsonArray{1})).has_value());
        QVERIFY(RuleAction::fromJson(withZones(QJsonArray{1, 2})).has_value());
        QVERIFY(RuleAction::fromJson(withZones(QJsonArray{MaxZoneOrdinal})).has_value());

        // A key outside allowedKeys ({zones, zoneNames}) is rejected.
        QJsonObject stray = withZones(QJsonArray{1});
        stray.insert(QStringLiteral("value"), 3);
        QVERIFY(!RuleAction::fromJson(stray).has_value());
    }

    void testSnapToZone_zoneNames_fromJson()
    {
        // The name-keyed twin of `zones`: a JSON array of non-blank strings.
        // Either array alone is a complete target, the two union, and a payload
        // with neither (or with both present but empty) names nothing and is
        // rejected. Each present key is still shape-checked on its own, so a
        // malformed `zoneNames` cannot ride in on a valid `zones`.
        const auto withNames = [](const QJsonValue& names) {
            QJsonObject o;
            o.insert(QStringLiteral("type"), QString(ActionType::SnapToZone));
            o.insert(QString(ActionParam::ZoneNames), names);
            return o;
        };

        QVERIFY(RuleAction::fromJson(withNames(QJsonArray{QStringLiteral("Editor")})).has_value());
        QVERIFY(RuleAction::fromJson(withNames(QJsonArray{QStringLiteral("Editor"), QStringLiteral("Terminal")}))
                    .has_value());
        QVERIFY(RuleAction::fromJson(withNames(QJsonArray{QString(MaxZoneNameLength, QLatin1Char('a'))})).has_value());

        QVERIFY(!RuleAction::fromJson(withNames(QStringLiteral("Editor"))).has_value()); // not an array
        QVERIFY(!RuleAction::fromJson(withNames(QJsonArray{})).has_value()); // empty, no other target
        QVERIFY(!RuleAction::fromJson(withNames(QJsonArray{QString()})).has_value()); // empty string
        QVERIFY(!RuleAction::fromJson(withNames(QJsonArray{QStringLiteral("   ")})).has_value()); // blank
        QVERIFY(!RuleAction::fromJson(withNames(QJsonArray{1})).has_value()); // number, not string
        QVERIFY(
            !RuleAction::fromJson(withNames(QJsonArray{QString(MaxZoneNameLength + 1, QLatin1Char('a'))})).has_value());

        // Names and ordinals together: the union is accepted, and an empty
        // ordinal array beside a real name list is fine (names are the target).
        QJsonObject both = withNames(QJsonArray{QStringLiteral("Editor")});
        both.insert(QString(ActionParam::Zones), QJsonArray{2});
        QVERIFY(RuleAction::fromJson(both).has_value());
        QJsonObject emptyOrdinals = withNames(QJsonArray{QStringLiteral("Editor")});
        emptyOrdinals.insert(QString(ActionParam::Zones), QJsonArray{});
        QVERIFY(RuleAction::fromJson(emptyOrdinals).has_value());
        QJsonObject namesEmptyWithZones = withNames(QJsonArray{});
        namesEmptyWithZones.insert(QString(ActionParam::Zones), QJsonArray{1});
        QVERIFY(RuleAction::fromJson(namesEmptyWithZones).has_value());

        // Both present, both empty: nothing to snap to.
        QJsonObject bothEmpty = withNames(QJsonArray{});
        bothEmpty.insert(QString(ActionParam::Zones), QJsonArray{});
        QVERIFY(!RuleAction::fromJson(bothEmpty).has_value());

        // A malformed ordinal list is still rejected even when names are valid.
        QJsonObject badOrdinals = withNames(QJsonArray{QStringLiteral("Editor")});
        badOrdinals.insert(QString(ActionParam::Zones), QJsonArray{0});
        QVERIFY(!RuleAction::fromJson(badOrdinals).has_value());

        // Round-trip keeps both arrays verbatim.
        const auto parsed = RuleAction::fromJson(both);
        QVERIFY(parsed.has_value());
        const QJsonObject out = parsed->toJson();
        QCOMPARE(out.value(QString(ActionParam::ZoneNames)).toArray(), QJsonArray{QStringLiteral("Editor")});
        QCOMPARE(out.value(QString(ActionParam::Zones)).toArray(), QJsonArray{2});
    }

    void testSnapToZone_namesOnly_roundTripAndStrayKey()
    {
        // A names-only action (no `zones` key at all) is the payload the editor
        // writes least often and the wire shape most at risk of a key being
        // synthesised: toJson must echo the params verbatim, so no `zones`
        // appears, and the strict loader must still refuse a stray key on this
        // base just as it does on the ordinal base.
        QJsonObject namesOnly;
        namesOnly.insert(QStringLiteral("type"), QString(ActionType::SnapToZone));
        namesOnly.insert(QString(ActionParam::ZoneNames), QJsonArray{QStringLiteral("Editor")});
        const auto parsed = RuleAction::fromJson(namesOnly);
        QVERIFY(parsed.has_value());
        const QJsonObject out = parsed->toJson();
        QVERIFY2(!out.contains(QString(ActionParam::Zones)), "toJson must not synthesise a zones key");
        QCOMPARE(out.value(QString(ActionParam::ZoneNames)).toArray(), QJsonArray{QStringLiteral("Editor")});
        QVERIFY(RuleAction::fromJson(out).has_value());

        QJsonObject stray = namesOnly;
        stray.insert(QStringLiteral("value"), 3);
        QVERIFY2(!RuleAction::fromJson(stray).has_value(),
                 "a key outside allowedKeys is rejected on the names-only base too");

        // A whitespace-padded name is accepted (the validator measures the
        // trimmed form) and stored VERBATIM: trimming is a read-side concern of
        // the daemon's placement reader and the engine's lookup, not the loader's.
        QJsonObject padded;
        padded.insert(QStringLiteral("type"), QString(ActionType::SnapToZone));
        padded.insert(QString(ActionParam::ZoneNames), QJsonArray{QStringLiteral("  Editor  ")});
        const auto paddedParsed = RuleAction::fromJson(padded);
        QVERIFY(paddedParsed.has_value());
        QCOMPARE(paddedParsed->toJson().value(QString(ActionParam::ZoneNames)).toArray(),
                 QJsonArray{QStringLiteral("  Editor  ")});
        // ...and a name whose TRIMMED length is exactly the bound still passes
        // even with padding that would push the raw string over it.
        QJsonObject paddedMax;
        paddedMax.insert(QStringLiteral("type"), QString(ActionType::SnapToZone));
        const QString paddedMaxName =
            QStringLiteral("  ") + QString(MaxZoneNameLength, QLatin1Char('a')) + QStringLiteral("  ");
        paddedMax.insert(QString(ActionParam::ZoneNames), QJsonArray{paddedMaxName});
        QVERIFY(RuleAction::fromJson(paddedMax).has_value());
    }

    void testRule_workspaceAndDesktopRoutesCoexist()
    {
        // ActionSlots.h states that a cascade may legitimately carry BOTH a
        // workspace route and a desktop route, and that the daemon prefers the
        // workspace one. The preference itself is daemon-side behaviour, but
        // its precondition is a phosphor-rules contract: the two must survive
        // one rule's load and occupy DISTINCT slots. If they ever shared a
        // slot, the same-slot collision handling would make the pairing
        // unrepresentable and the daemon's priority unreachable.
        QJsonObject workspace;
        workspace.insert(QStringLiteral("type"), QString(ActionType::RouteToWorkspace));
        workspace.insert(QString(ActionParam::TargetWorkspaceName), QStringLiteral("chat"));
        QJsonObject desktop;
        desktop.insert(QStringLiteral("type"), QString(ActionType::RouteToDesktop));
        desktop.insert(QString(ActionParam::TargetDesktop), 3);

        QJsonObject rule;
        rule.insert(QStringLiteral("id"), QUuid::createUuid().toString());
        rule.insert(QStringLiteral("name"), QStringLiteral("route both"));
        rule.insert(QStringLiteral("match"), QJsonObject{{QStringLiteral("all"), QJsonArray{}}});
        rule.insert(QStringLiteral("actions"), QJsonArray{workspace, desktop});

        const auto loaded = Rule::fromJson(rule);
        QVERIFY(loaded.has_value());
        QCOMPARE(loaded->actions.size(), 2);
        const ActionRegistry& reg = ActionRegistry::instance();
        const QString firstSlot = reg.slotFor(loaded->actions.at(0));
        const QString secondSlot = reg.slotFor(loaded->actions.at(1));
        QCOMPARE(firstSlot, QString(ActionSlot::RouteWorkspace));
        QCOMPARE(secondSlot, QString(ActionSlot::RouteDesktop));
        QVERIFY(firstSlot != secondSlot);
    }
};

QTEST_GUILESS_MAIN(TestRuleActionPlacement)
#include "test_ruleaction_placement.moc"
