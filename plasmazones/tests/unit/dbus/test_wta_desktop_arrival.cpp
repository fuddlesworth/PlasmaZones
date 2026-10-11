// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_wta_desktop_arrival.cpp
 * @brief SnapAdaptor::resolveWindowRestore for a window arriving on a desktop
 *        it was moved to while that desktop was hidden.
 *
 * DesktopArrival is the continuation of an open: it may run the whole restore
 * chain, and it answers a zone the window already holds on the arrival
 * context with that zone's rect (F493 pins that arm). DesktopReapply is every
 * other move: it re-applies a held zone and places nothing new (F415). And an
 * open's continuation resolves its rules against the arrival screen, not a
 * verdict cached at the spawn screen (F332).
 */

#include "wta_convenience_fixture.h"

#include <PhosphorRules/MatchExpression.h>
#include <PhosphorRules/MatchTypes.h>
#include <PhosphorRules/RuleAction.h>
#include <PhosphorRules/RuleStore.h>

using PhosphorEngine::RestoreReason;

namespace {
struct Answer
{
    bool shouldSnap = false;
    QRect rect;
};
} // namespace

class TestWtaDesktopArrival : public QObject, protected WtaConvenienceFixture
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        initFixture();
        installPerScreenResolver();
        m_snapEngine->setCurrentDesktopForScreen(m_screenId, 1);
    }

    void cleanup()
    {
        m_wta->setRuleStore(nullptr);
        m_wta->setWindowRegistry(nullptr);
        m_store.reset();
        m_snapEngine->setLiveModeResolver({});
        cleanupFixture();
    }

    // (a) A zone the window holds in the arrival context is re-applied.
    void heldZoneIsReappliedOnArrival()
    {
        const QString w = QStringLiteral("app|arrival-held");
        m_wta->service()->assignWindowToZone(w, m_zoneIds[1], m_screenId, 1);
        const Answer a = resolve(w, RestoreReason::DesktopArrival);
        QVERIFY(a.shouldSnap);
        QCOMPARE(a.rect, m_wta->service()->zoneGeometry(m_zoneIds[1], m_screenId));
    }

    // (b) A zone held on ANOTHER desktop is not: the primary membership names
    // it, and its rect is a layout the arrival desktop may not run (#1104).
    void anotherDesktopsZoneIsNotReapplied()
    {
        const QString w = QStringLiteral("app|arrival-elsewhere");
        m_snapEngine->setCurrentDesktopForScreen(m_screenId, 2);
        m_wta->service()->assignWindowToZone(w, m_zoneIds[1], m_screenId, 2);
        m_snapEngine->setCurrentDesktopForScreen(m_screenId, 1);
        QVERIFY(!resolve(w, RestoreReason::DesktopReapply).shouldSnap);
    }

    // (d) A screen a tiling engine runs gets nothing from the snap arm.
    void aTilingScreenGetsNothing()
    {
        const QString w = QStringLiteral("app|arrival-tiled");
        m_wta->service()->assignWindowToZone(w, m_zoneIds[1], m_screenId, 1);
        m_snapEngine->setLiveModeResolver([](const QString&) {
            return PhosphorZones::AssignmentEntry::Mode::Autotile;
        });
        QVERIFY(!resolve(w, RestoreReason::DesktopReapply).shouldSnap);
    }

    // (e) Snapping switched off answers nothing either.
    void snappingOffGetsNothing()
    {
        const QString w = QStringLiteral("app|arrival-off");
        m_wta->service()->assignWindowToZone(w, m_zoneIds[1], m_screenId, 1);
        m_settings->setSnappingEnabled(false);
        QVERIFY(!resolve(w, RestoreReason::DesktopReapply).shouldSnap);
        m_settings->setSnappingEnabled(true);
    }

    // A re-apply park re-applies the held zone like the arrival does.
    void reapplyReasonReappliesTheHeldZone()
    {
        const QString w = QStringLiteral("app|reapply-held");
        m_wta->service()->assignWindowToZone(w, m_zoneIds[2], m_screenId, 1);
        const Answer a = resolve(w, RestoreReason::DesktopReapply);
        QVERIFY(a.shouldSnap);
        QCOMPARE(a.rect, m_wta->service()->zoneGeometry(m_zoneIds[2], m_screenId));
    }

    // A window the desktop shortcut moved without a slot to land in is not
    // snapped into an empty zone on arrival: the shortcut left it unsnapped
    // on purpose (F415). The open's continuation still may, which is the
    // control showing the auto-assign would fire.
    void reapplyReasonPlacesNothingNew()
    {
        m_settings->setAutoAssignAllLayouts(true);
        const QString moved = QStringLiteral("app|reapply-nothing");
        QVERIFY(!resolve(moved, RestoreReason::DesktopReapply).shouldSnap);
        QVERIFY(m_wta->service()->zoneForWindow(moved).isEmpty());

        const QString opened = QStringLiteral("other|arrival-open");
        QVERIFY2(resolve(opened, RestoreReason::DesktopArrival).shouldSnap,
                 "control: an open's continuation takes the empty zone");
    }

    // An open's continuation resolves its rules for the screen it arrived on.
    // A SnapToZone rule scoped to the spawn screen was answered from the
    // verdict cached there and snapped the window on another screen (F332).
    void arrivalResolvesRulesOnTheArrivalScreen()
    {
        const QString instance = QStringLiteral("rule-inst");
        const QString w = QStringLiteral("ruleapp|") + instance;
        installScreenScopedSnapRule(instance, QStringLiteral("ruleapp"), m_screenId);
        QVERIFY(!m_wta->placementZonesByRule(w, m_screenId).zoneOrdinals.isEmpty());

        const QString arrival = QStringLiteral("DP-2");
        QVERIFY2(!resolve(w, RestoreReason::DesktopArrival, arrival).shouldSnap,
                 "a rule scoped to the spawn screen must not snap the window on the arrival screen");
    }

private:
    Answer resolve(const QString& windowId, RestoreReason reason, const QString& screenId = QString())
    {
        int x = 0, y = 0, w = 0, h = 0;
        bool shouldSnap = false;
        m_snapAdaptor->resolveWindowRestore(windowId, screenId.isEmpty() ? m_screenId : screenId, false,
                                            static_cast<int>(PhosphorEngine::WindowKind::Normal),
                                            static_cast<int>(reason), 0, 0, x, y, w, h, shouldSnap);
        return {shouldSnap, QRect(x, y, w, h)};
    }

    void installScreenScopedSnapRule(const QString& instance, const QString& appId, const QString& screenId)
    {
        auto* registry = new PhosphorEngine::WindowRegistry(m_parent);
        m_wta->setWindowRegistry(registry);
        m_wta->setWindowMetadata(instance, appId, QString(), QString(), QString(), 0, 0, QString(), 0, QVariantMap());
        using namespace PhosphorRules;
        RuleAction snapTo;
        snapTo.type = QString(ActionType::SnapToZone);
        snapTo.params.insert(QString(ActionParam::Zones), QJsonArray{1});
        Rule rule;
        rule.id = QUuid::createUuid();
        rule.enabled = true;
        rule.match = MatchExpression::makeLeaf(Field::ScreenId, Operator::Equals, screenId);
        rule.actions = {snapTo};
        m_store = std::make_unique<RuleStore>(ConfigDefaults::rulesFilePath(), nullptr);
        QVERIFY(m_store->addRule(rule));
        m_wta->setRuleStore(m_store.get());
    }

    std::unique_ptr<PhosphorRules::RuleStore> m_store;
};

QTEST_MAIN(TestWtaDesktopArrival)
#include "test_wta_desktop_arrival.moc"
