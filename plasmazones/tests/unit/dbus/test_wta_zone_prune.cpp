// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_wta_zone_prune.cpp
 * @brief The stale-zone prune: what triggers it, what it drops, in which
 *        store, and what it tells the effect.
 *
 * A layout change used to prune only on an active-layout switch, through the
 * window's primary store, without telling the effect anything. An editor
 * delete or a layout removal left windows in zones that no longer exist, a
 * span that lost a zone kept its old rect, and a window the prune unsnapped
 * kept its snapped mark in the effect.
 */

#include "wta_convenience_fixture.h"

#include <PhosphorEngine/GeometryUtils.h>

class TestWtaZonePrune : public QObject, protected WtaConvenienceFixture
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        initFixture();
        m_desktop = m_layoutManager->currentVirtualDesktopForScreen(m_screenId);
    }

    void cleanup()
    {
        cleanupFixture();
    }

    // An editor delete of a zone unsnaps the window in it and says so (F363).
    void editorDeletingAZoneUnsnapsItsWindow()
    {
        const QString w = QStringLiteral("app|prune-delete");
        service()->assignWindowToZone(w, m_zoneIds[2], m_screenId, m_desktop);
        QSignalSpy states(m_wta, &WindowTrackingAdaptor::windowStateChanged);

        deleteZone(m_testLayout, m_zoneIds[2]);

        QVERIFY(service()->zoneForWindow(w).isEmpty());
        QVERIFY(hasState(states, w, QStringLiteral("unsnapped")));
    }

    // Mid-batch nothing is pruned: the editor's batch removes and re-adds
    // zones, and only the end of it says what the layout is.
    void midBatchZoneRemovalUnassignsNothing()
    {
        const QString w = QStringLiteral("app|prune-midbatch");
        service()->assignWindowToZone(w, m_zoneIds[2], m_screenId, m_desktop);
        PhosphorZones::Zone* zone = m_testLayout->zoneById(QUuid::fromString(m_zoneIds[2]));
        m_testLayout->beginBatchModify();
        m_testLayout->removeZone(zone);
        QCOMPARE(service()->zoneForWindow(w), m_zoneIds[2]);
        m_testLayout->endBatchModify();
    }

    // A span that loses a zone keeps the rest and moves to them (F442).
    void spanLosingAMemberNarrowsAndMoves()
    {
        const QString w = QStringLiteral("app|prune-span");
        service()->assignWindowToZones(w, {m_zoneIds[1], m_zoneIds[2]}, m_screenId, m_desktop);
        QSignalSpy batch(m_wta, &WindowTrackingAdaptor::applyGeometriesBatch);

        deleteZone(m_testLayout, m_zoneIds[2]);

        QCOMPARE(service()->zonesForWindow(w), QStringList{m_zoneIds[1]});
        const QRect zoneRect = service()->resolveZoneGeometry({m_zoneIds[1]}, m_screenId);
        bool moved = false;
        for (const QList<QVariant>& args : std::as_const(batch)) {
            for (const auto& e : args.at(0).value<PhosphorProtocol::WindowGeometryList>()) {
                moved |= e.windowId == w && QRect(e.x, e.y, e.width, e.height) == zoneRect;
            }
        }
        QVERIFY2(moved, "the window must be moved to the zone it kept");
    }

    // An active-layout switch that drops a window's zone tells the effect
    // (F328).
    void activeLayoutSwitchTellsTheEffect()
    {
        const QString w = QStringLiteral("app|prune-switch");
        service()->assignWindowToZone(w, m_zoneIds[0], m_screenId, m_desktop);
        QSignalSpy states(m_wta, &WindowTrackingAdaptor::windowStateChanged);

        switchDefaultTo(addLayout(2));

        QVERIFY(service()->zoneForWindow(w).isEmpty());
        QVERIFY(hasState(states, w, QStringLiteral("unsnapped")));
    }

    // A context that runs no layout (the None opt-out) keeps its windows'
    // zones across a layout switch (F264).
    void noneContextKeepsItsMemory()
    {
        const QString w = QStringLiteral("app|prune-none");
        service()->assignWindowToZone(w, m_zoneIds[0], m_screenId, m_desktop);
        setNone();

        switchDefaultTo(addLayout(2));

        QCOMPARE(service()->zoneForWindow(w), m_zoneIds[0]);
    }

    // ...but a zone whose layout was deleted exists nowhere, and goes even
    // there (F431).
    void noneContextDropsAZoneWhoseLayoutWasDeleted()
    {
        PhosphorZones::Layout* gone = addLayout(2);
        const QString goneZone = gone->zones().first()->id().toString();
        const QString w = QStringLiteral("app|prune-none-dead");
        service()->assignWindowToZone(w, goneZone, m_screenId, m_desktop);
        setNone();

        QVERIFY(m_layoutManager->removeLayout(gone));

        QVERIFY(service()->zoneForWindow(w).isEmpty());
    }

    // A window the prune unsnaps gives up its record's snap slot, so the
    // next switch's buffer does not put it back (F567).
    void unsnappedRecordIsNotResnappedLater()
    {
        const QString w = QStringLiteral("app|prune-record");
        service()->assignWindowToZone(w, m_zoneIds[2], m_screenId, m_desktop);
        m_wta->captureWindowPlacement(w);

        switchDefaultTo(addLayout(2));
        QVERIFY(service()->zoneForWindow(w).isEmpty());
        switchDefaultTo(m_testLayout);
        service()->populateResnapBufferForAllScreens({}, {m_screenId});

        for (const PhosphorEngine::ResnapEntry& e : service()->takeResnapBuffer()) {
            QVERIFY2(e.windowId != w, "the unsnapped window must not be resnapped from its record");
        }
        const auto rec = service()->placementStore().peekExact(w);
        QVERIFY(rec);
        QVERIFY(rec->slotFor(PhosphorEngine::WindowPlacement::snapEngineId()).state
                != PhosphorEngine::WindowPlacement::stateSnapped());
    }

    // A screen configured for autotile whose engine is off runs snapping,
    // and its stale zones are pruned like any snapping screen's (F112).
    void configuredTilingModeWithTheEngineOffIsJudged()
    {
        const QString w = QStringLiteral("app|prune-tiling-off");
        service()->assignWindowToZone(w, m_zoneIds[2], m_screenId, m_desktop);
        PhosphorZones::AssignmentEntry entry;
        entry.mode = PhosphorZones::AssignmentEntry::Autotile;
        entry.tilingAlgorithm = QStringLiteral("bsp");
        m_layoutManager->setAssignmentEntryDirect(m_screenId, m_desktop, QString(), entry);

        switchDefaultTo(addLayout(2));

        QVERIFY(service()->zoneForWindow(w).isEmpty());
    }

    // A span in another activity's store narrows in THAT store: the rewrite
    // went through the service's assign, which pins the activity in view and
    // planted a phantom membership there.
    void backgroundActivityRewriteMakesNoPhantom()
    {
        installPerScreenResolver();
        const QString w = QStringLiteral("app|prune-activity");
        m_snapEngine->setCurrentDesktopForScreen(m_screenId, m_desktop);
        switchActivity(QStringLiteral("act-X"));
        service()->assignWindowToZones(w, {m_zoneIds[1], m_zoneIds[2]}, m_screenId, m_desktop);
        switchActivity(QStringLiteral("act-Y"));

        deleteZone(m_testLayout, m_zoneIds[2]);

        for (SnapState* state : m_snapEngine->allSnapStates()) {
            const auto key = m_snapEngine->keyForState(state);
            if (!key) {
                continue;
            }
            if (key->activity == QStringLiteral("act-X")) {
                QCOMPARE(state->zonesForWindow(w), QStringList{m_zoneIds[1]});
            } else {
                QVERIFY2(state->zonesForWindow(w).isEmpty(), "no other store may hold the window");
            }
        }
        switchActivity(QString());
    }

private:
    PhosphorPlacement::WindowTrackingService* service() const
    {
        return m_wta->service();
    }

    PhosphorZones::Layout* addLayout(int zones)
    {
        PhosphorZones::Layout* layout = createTestLayout(zones, m_layoutManager);
        m_layoutManager->addLayout(layout);
        return layout;
    }

    /// Make @p layout the screen's layout (the cascade's default) and the
    /// active one, which runs the prune.
    void switchDefaultTo(PhosphorZones::Layout* layout)
    {
        const QString id = layout->id().toString();
        m_layoutManager->setDefaultLayoutIdProvider([id]() {
            return id;
        });
        m_layoutManager->setActiveLayout(layout);
    }

    /// An editor delete: a batch that removes the zone and ends.
    void deleteZone(PhosphorZones::Layout* layout, const QString& zoneId)
    {
        PhosphorZones::Zone* zone = layout->zoneById(QUuid::fromString(zoneId));
        QVERIFY(zone);
        layout->beginBatchModify();
        layout->removeZone(zone);
        layout->endBatchModify();
    }

    void setNone()
    {
        PhosphorZones::AssignmentEntry entry;
        entry.mode = PhosphorZones::AssignmentEntry::Snapping;
        entry.snappingLayout = PhosphorZones::NoSnappingLayout;
        m_layoutManager->setAssignmentEntryDirect(m_screenId, 0, QString(), entry);
        QVERIFY(!m_layoutManager->resolveLayoutForScreen(m_screenId));
    }

    void switchActivity(const QString& activity)
    {
        m_snapEngine->setCurrentActivity(activity);
        m_layoutManager->setCurrentActivity(activity);
    }

    static bool hasState(const QSignalSpy& spy, const QString& windowId, const QString& changeType)
    {
        for (const QList<QVariant>& args : spy) {
            const auto state = args.at(1).value<PhosphorProtocol::WindowStateEntry>();
            if (args.at(0).toString() == windowId && state.changeType == changeType) {
                return true;
            }
        }
        return false;
    }

    int m_desktop = 1;
};

QTEST_MAIN(TestWtaZonePrune)
#include "test_wta_zone_prune.moc"
