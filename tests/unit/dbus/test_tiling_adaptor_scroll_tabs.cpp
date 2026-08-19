// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_tiling_adaptor_scroll_tabs.cpp
 *
 * Pins the tab-indicator transport TilingAdaptor exposes to the KWin effect:
 * the relay that turns the scroll engine's per-screen model into
 * scrollTabStripsChanged plus the scrollTabStrips replay cache behind it, the
 * "[]" retraction that drops a screen from that cache, the all-windows
 * scrollTabColorsChanged broadcast, and the scrollTabColors pass-through to
 * the WindowTrackingAdaptor's rule resolution.
 *
 * NOTE: the adaptor is parented to a plain QObject rather than a
 * D-Bus-registered object, same as the panel-gate and active-layouts tests —
 * QDBusAbstractAdaptor walks its parent's meta-object at construction and a
 * vanilla QObject parent sidesteps the D-Bus registration assumptions.
 */

#include <QCoreApplication>
#include <QObject>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTest>
#include <QUuid>
#include <memory>

#include <PhosphorEngine/WindowRegistry.h>
#include <PhosphorRules/ActionTypes.h>
#include <PhosphorRules/MatchExpression.h>
#include <PhosphorRules/MatchTypes.h>
#include <PhosphorRules/RuleAction.h>
#include <PhosphorRules/RuleSet.h>
#include <PhosphorRules/RuleStore.h>

#include "config/configdefaults.h"
#include "dbus/tilingadaptor/tilingadaptor.h"
#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"
#include "helpers/IsolatedConfigGuard.h"
#include "helpers/LayoutRegistryTestHelpers.h"
#include "helpers/StubSettings.h"
#include "helpers/StubZoneDetector.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

namespace {

/// One tabbed column, shaped like the engine's payload (rects absolute
/// logical, position 0 Left / 1 Right / 2 Top / 3 Bottom).
QString stripsJsonFor(const QString& tabId)
{
    return QStringLiteral(
               "[{\"x\":0,\"y\":0,\"width\":800,\"height\":600,\"position\":0,"
               "\"activeIndex\":0,\"tabs\":[\"%1\"]}]")
        .arg(tabId);
}

} // namespace

class TestTilingAdaptorScrollTabs : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void testRelayEmitsAndCachesPerScreen()
    {
        QObject adaptorParent;
        TilingAdaptor adaptor(nullptr, &adaptorParent);
        QSignalSpy spy(&adaptor, &TilingAdaptor::scrollTabStripsChanged);
        QVERIFY(spy.isValid());

        // No relay yet: the replay query answers "nothing to paint".
        QVERIFY(adaptor.scrollTabStrips().isEmpty());

        const QString dp1 = stripsJsonFor(QStringLiteral("konsole|inst1"));
        adaptor.relayScrollTabStrips(QStringLiteral("DP-1"), dp1);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("DP-1"));
        QCOMPARE(spy.at(0).at(1).toString(), dp1);
        QCOMPARE(adaptor.scrollTabStrips().value(QStringLiteral("DP-1")).toString(), dp1);

        // A second screen is additive — the replay map is per screen, not a
        // single last-payload latch.
        const QString hdmi = stripsJsonFor(QStringLiteral("firefox|inst2"));
        adaptor.relayScrollTabStrips(QStringLiteral("HDMI-1"), hdmi);
        QCOMPARE(spy.count(), 2);
        const QVariantMap both = adaptor.scrollTabStrips();
        QCOMPARE(both.size(), 2);
        QCOMPARE(both.value(QStringLiteral("DP-1")).toString(), dp1);
        QCOMPARE(both.value(QStringLiteral("HDMI-1")).toString(), hdmi);

        // Re-relaying a screen replaces its entry rather than accumulating.
        const QString dp1Next = stripsJsonFor(QStringLiteral("konsole|inst3"));
        adaptor.relayScrollTabStrips(QStringLiteral("DP-1"), dp1Next);
        QCOMPARE(spy.count(), 3);
        QCOMPARE(adaptor.scrollTabStrips().size(), 2);
        QCOMPARE(adaptor.scrollTabStrips().value(QStringLiteral("DP-1")).toString(), dp1Next);
    }

    void testEmptyArrayRetractsTheScreen()
    {
        QObject adaptorParent;
        TilingAdaptor adaptor(nullptr, &adaptorParent);
        QSignalSpy spy(&adaptor, &TilingAdaptor::scrollTabStripsChanged);
        QVERIFY(spy.isValid());

        adaptor.relayScrollTabStrips(QStringLiteral("DP-1"), stripsJsonFor(QStringLiteral("konsole|inst1")));
        QCOMPARE(adaptor.scrollTabStrips().size(), 1);

        // The last tabbed column went away: the screen leaves the replay map
        // entirely, so a replaying effect never sees an empty array it would
        // have to special-case — but the wire still gets the retraction.
        adaptor.relayScrollTabStrips(QStringLiteral("DP-1"), QStringLiteral("[]"));
        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.at(1).at(1).toString(), QStringLiteral("[]"));
        QVERIFY(adaptor.scrollTabStrips().isEmpty());

        // An empty payload carries no columns either, and is canonicalized to
        // "[]" on the wire so the receiver has one retraction spelling.
        adaptor.relayScrollTabStrips(QStringLiteral("HDMI-1"), QString());
        QCOMPARE(spy.count(), 3);
        QCOMPARE(spy.at(2).at(1).toString(), QStringLiteral("[]"));
        QVERIFY(adaptor.scrollTabStrips().isEmpty());

        // An empty screen id is a malformed producer, not a retraction of
        // anything: dropped without reaching the wire.
        adaptor.relayScrollTabStrips(QString(), stripsJsonFor(QStringLiteral("konsole|inst1")));
        QCOMPARE(spy.count(), 3);
    }

    void testClearEngineDropsTheReplayCache()
    {
        QObject adaptorParent;
        TilingAdaptor adaptor(nullptr, &adaptorParent);

        adaptor.relayScrollTabStrips(QStringLiteral("DP-1"), stripsJsonFor(QStringLiteral("konsole|inst1")));
        QCOMPARE(adaptor.scrollTabStrips().size(), 1);

        // Shutdown: the cache names live windows and their column rects, so
        // replaying it across a restart would paint a layout no engine owns.
        adaptor.clearEngine();
        QVERIFY(adaptor.scrollTabStrips().isEmpty());
    }

    void testPaintOverridesAreChangeGatedPerScreenAndReplay()
    {
        QObject adaptorParent;
        TilingAdaptor adaptor(nullptr, &adaptorParent);
        QSignalSpy spy(&adaptor, &TilingAdaptor::scrollTabPaintOverridesChanged);
        QVERIFY(spy.isValid());
        QVERIFY(adaptor.scrollTabPaintOverrides().isEmpty());

        // Clearing a screen that carries nothing is not a change.
        adaptor.setScrollTabPaintOverrides(QStringLiteral("DP-1"), {});
        QCOMPARE(spy.count(), 0);

        QVariantMap ov;
        ov.insert(QStringLiteral("tabStyle"), 0);
        ov.insert(QStringLiteral("activeColor"), QStringLiteral("#ff0000"));
        adaptor.setScrollTabPaintOverrides(QStringLiteral("DP-1"), ov);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("DP-1"));
        QCOMPARE(spy.at(0).at(1).toMap(), ov);
        // The replay getter hands back the per-screen map, nested.
        const QVariantMap replay = adaptor.scrollTabPaintOverrides();
        QCOMPARE(replay.size(), 1);
        QCOMPARE(replay.value(QStringLiteral("DP-1")).toMap(), ov);

        // Same map again: change-gated, no emit.
        adaptor.setScrollTabPaintOverrides(QStringLiteral("DP-1"), ov);
        QCOMPARE(spy.count(), 1);

        // A second screen is independent of the first.
        QVariantMap other;
        other.insert(QStringLiteral("cornerRadius"), -1);
        adaptor.setScrollTabPaintOverrides(QStringLiteral("HDMI-1"), other);
        QCOMPARE(spy.count(), 2);
        QCOMPARE(adaptor.scrollTabPaintOverrides().size(), 2);

        // Clearing emits the empty map and drops the replay entry.
        adaptor.setScrollTabPaintOverrides(QStringLiteral("DP-1"), {});
        QCOMPARE(spy.count(), 3);
        QVERIFY(spy.at(2).at(1).toMap().isEmpty());
        QCOMPARE(adaptor.scrollTabPaintOverrides().size(), 1);
        QVERIFY(!adaptor.scrollTabPaintOverrides().contains(QStringLiteral("DP-1")));

        // Empty screen id is dropped outright.
        adaptor.setScrollTabPaintOverrides(QString(), ov);
        QCOMPARE(spy.count(), 3);

        // Shutdown clears the replay cache like the strips.
        adaptor.clearEngine();
        QVERIFY(adaptor.scrollTabPaintOverrides().isEmpty());
    }

    void testColorsBroadcastIsEmptyOnBothArguments()
    {
        QObject adaptorParent;
        TilingAdaptor adaptor(nullptr, &adaptorParent);
        QSignalSpy spy(&adaptor, &TilingAdaptor::scrollTabColorsChanged);
        QVERIFY(spy.isValid());

        adaptor.relayScrollTabColorsChanged();
        QCOMPARE(spy.count(), 1);
        QVERIFY(spy.at(0).at(0).toString().isEmpty());
        QVERIFY(spy.at(0).at(1).toMap().isEmpty());
    }

    void testColorsPassThroughToRuleResolution()
    {
        IsolatedConfigGuard guard;

        QObject parent;
        auto* layoutManager = PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts"));
        const auto dropRegistry = qScopeGuard([layoutManager] {
            delete layoutManager;
        });
        StubSettings settings(nullptr);
        StubZoneDetector zoneDetector(nullptr);

        auto* wta =
            new WindowTrackingAdaptor(layoutManager, &zoneDetector, nullptr, &settings, nullptr, nullptr, &parent);
        auto* registry = new PhosphorEngine::WindowRegistry(&parent);
        wta->setWindowRegistry(registry);
        wta->setWindowMetadata(QStringLiteral("inst1"), QStringLiteral("tabapp"), QString(), QString(), QString(), 0, 0,
                               QString(), 0, QVariantMap());

        using namespace PhosphorRules;
        Rule rule;
        rule.id = QUuid::createUuid();
        rule.enabled = true;
        rule.match = MatchExpression::makeLeaf(Field::AppId, Operator::AppIdMatches, QStringLiteral("tabapp"));
        RuleAction tabActive;
        tabActive.type = QString(ActionType::TabColorActive);
        tabActive.params.insert(QString(ActionParam::Value), QStringLiteral("#ff8800"));
        rule.actions = {tabActive};
        RuleStore store(ConfigDefaults::rulesFilePath(), &parent);
        QVERIFY(store.addRule(rule));
        wta->setRuleStore(&store);
        const auto teardown = qScopeGuard([wta] {
            wta->setRuleStore(nullptr);
            wta->setWindowRegistry(nullptr);
        });

        QObject adaptorParent;
        TilingAdaptor adaptor(nullptr, &adaptorParent);

        // No WTA wired: the pass-through has nothing to resolve against.
        const QString windowId = QStringLiteral("tabapp|inst1");
        QVERIFY(adaptor.scrollTabColors(windowId).isEmpty());

        adaptor.setWindowTrackingAdaptor(wta);
        const QVariantMap colors = adaptor.scrollTabColors(windowId);
        QCOMPARE(colors.value(QStringLiteral("activeColor")).toString(), QStringLiteral("#ff8800"));
        // Only the slots a rule actually set are present, so a painter reads
        // each key independently rather than assuming all three.
        QVERIFY(!colors.contains(QStringLiteral("inactiveColor")));
        QVERIFY(!colors.contains(QStringLiteral("urgentColor")));

        // Same resolution the WTA itself answers — the adaptor adds no
        // reshaping of its own.
        QCOMPARE(colors, wta->tabColorRuleParams(windowId));

        // An empty id is rejected at the wire boundary rather than handed to
        // the resolver.
        QVERIFY(adaptor.scrollTabColors(QString()).isEmpty());

        adaptor.setWindowTrackingAdaptor(nullptr);
    }
};

QTEST_GUILESS_MAIN(TestTilingAdaptorScrollTabs)
#include "test_tiling_adaptor_scroll_tabs.moc"
