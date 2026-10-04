// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_wta_layout_switch.cpp
 * @brief The one layout-switch helper every switch caller runs:
 *        WindowTrackingAdaptor::resnapScreensToTheirLayouts. It populates
 *        the buffer for the switched screens, resnaps silently, then prunes
 *        and relays what the switch could not carry.
 */

#include "wta_convenience_fixture.h"

#include <PhosphorEngine/GeometryUtils.h>
#include <PhosphorZones/LayoutUtils.h>

class TestWtaLayoutSwitch : public QObject, protected WtaConvenienceFixture
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        initFixture();
        m_desktop = m_layoutManager->currentVirtualDesktopForScreen(m_screenId);
        m_twoZones = createTestLayout(2, m_layoutManager);
        m_layoutManager->addLayout(m_twoZones);
    }

    void cleanup()
    {
        cleanupFixture();
    }

    // A switch on one screen leaves the other screens' windows alone: the
    // quick-layout key populated every screen, and a span on another
    // monitor collapsed to its first zone (F474).
    void switchOnOneScreenLeavesTheOtherScreensSpans()
    {
        const QString other = QStringLiteral("DP-2");
        const QString span = QStringLiteral("app|switch-span");
        service()->assignWindowToZones(span, {m_zoneIds[0], m_zoneIds[1]}, other, m_desktop);
        service()->assignWindowToZone(QStringLiteral("app|switch-here"), m_zoneIds[0], m_screenId, m_desktop);
        m_layoutManager->assignLayout(m_screenId, m_desktop, QString(), m_twoZones);
        QSignalSpy batch(m_wta, &WindowTrackingAdaptor::applyGeometriesBatch);

        m_wta->resnapScreensToTheirLayouts({}, {m_screenId}, m_desktop);

        QCOMPARE(service()->zonesForWindow(span), (QStringList{m_zoneIds[0], m_zoneIds[1]}));
        QVERIFY2(!batchCarries(batch, span), "the other screen's span must not be resnapped");
    }

    // A window past the new layout's zone count with no float spot is
    // unsnapped and the effect told, instead of staying in a zone of the old
    // layout at its old rect (F475).
    void excessWindowWithoutFloatBackIsUnsnapped()
    {
        const QString w = QStringLiteral("app|switch-excess");
        service()->assignWindowToZone(w, m_zoneIds[2], m_screenId, m_desktop);
        m_layoutManager->assignLayout(m_screenId, m_desktop, QString(), m_twoZones);
        QSignalSpy states(m_wta, &WindowTrackingAdaptor::windowStateChanged);

        m_wta->resnapScreensToTheirLayouts({}, {m_screenId}, m_desktop);

        QVERIFY(service()->zoneForWindow(w).isEmpty());
        bool unsnapped = false;
        for (const QList<QVariant>& args : std::as_const(states)) {
            unsnapped |= args.at(0).toString() == w
                && args.at(1).value<PhosphorProtocol::WindowStateEntry>().changeType == QStringLiteral("unsnapped");
        }
        QVERIFY(unsnapped);
    }

    // The windows the new layout can hold move to its same-numbered zones.
    void windowsMoveToTheSameNumberedZone()
    {
        const QString w = QStringLiteral("app|switch-second");
        service()->assignWindowToZone(w, m_zoneIds[1], m_screenId, m_desktop);
        m_layoutManager->assignLayout(m_screenId, m_desktop, QString(), m_twoZones);

        m_wta->resnapScreensToTheirLayouts({}, {m_screenId}, m_desktop);

        QVector<PhosphorZones::Zone*> zones = m_twoZones->zones();
        QCOMPARE(service()->zoneForWindow(w), zones.at(1)->id().toString());
    }

    // A switch with nothing to move says nothing: the switch's own OSD is
    // the feedback, and the "no windows to resnap" card used to follow it
    // unless a suppression count happened to be armed (F489).
    void layoutSwitchIsSilent()
    {
        m_layoutManager->assignLayout(m_screenId, m_desktop, QString(), m_twoZones);
        QSignalSpy feedback(m_snapEngine, &SnapEngine::navigationFeedback);

        m_wta->resnapScreensToTheirLayouts({}, {m_screenId}, m_desktop);

        QCOMPARE(feedback.count(), 0);
    }

    // The user's own verbs still report an empty resnap.
    void reportedResnapStillSaysNothingMoved()
    {
        QSignalSpy feedback(m_snapEngine, &SnapEngine::navigationFeedback);
        m_snapEngine->resnapToNewLayout();
        QCOMPARE(feedback.count(), 1);
        QCOMPARE(feedback.first().at(2).toString(), QStringLiteral("no_windows_to_resnap"));

        m_snapEngine->resnapCurrentAssignments(m_screenId, {}, SnapEngine::ResnapFeedback::Silent);
        QCOMPARE(feedback.count(), 1);
    }

    // Deleting the layout a screen runs as the default moves its windows in
    // view to the same-numbered zone of the layout the screen falls back to
    // (F403). The removal re-activated another layout with no switch to move
    // by, and the prune unsnapped the window where it was.
    void deletingTheDefaultLayoutMovesItsWindowsBySlot()
    {
        PhosphorZones::Layout* doomed = addThreeZoneLayout();
        m_defaultId = doomed->id().toString();
        m_layoutManager->setActiveLayout(doomed);
        const QString w = QStringLiteral("app|delete-default");
        service()->assignWindowToZone(w, doomed->zones().at(1)->id().toString(), m_screenId, m_desktop);
        QSignalSpy batch(m_wta, &WindowTrackingAdaptor::applyGeometriesBatch);

        QVERIFY(m_layoutManager->removeLayout(doomed));

        PhosphorZones::Layout* fallback = m_layoutManager->resolveLayoutForScreen(m_screenId);
        QVERIFY(fallback);
        QVector<PhosphorZones::Zone*> zones = fallback->zones();
        PhosphorZones::LayoutUtils::sortZonesByNumber(zones);
        QCOMPARE(service()->zoneForWindow(w), zones.at(1)->id().toString());
        QVERIFY(batchCarries(batch, w));
    }

    // Deleting a layout assigned to the context itself leaves the context on
    // the explicit no-layout word (the registry's purge does not hand it the
    // default), so there is nothing to move into: the window in view is
    // unsnapped where it is and the effect told. It used to keep the deleted
    // layout's zone id (F429).
    void deletingAContextLayoutUnsnapsItsWindows()
    {
        PhosphorZones::Layout* doomed = addThreeZoneLayout();
        m_layoutManager->assignLayout(m_screenId, m_desktop, QString(), doomed);
        const QString w = QStringLiteral("app|delete-context");
        service()->assignWindowToZone(w, doomed->zones().at(1)->id().toString(), m_screenId, m_desktop);
        QSignalSpy states(m_wta, &WindowTrackingAdaptor::windowStateChanged);

        QVERIFY(m_layoutManager->removeLayout(doomed));

        QVERIFY(!m_layoutManager->resolveLayoutForScreen(m_screenId));
        QVERIFY(service()->zoneForWindow(w).isEmpty());
        bool unsnapped = false;
        for (const QList<QVariant>& args : std::as_const(states)) {
            unsnapped |= args.at(0).toString() == w
                && args.at(1).value<PhosphorProtocol::WindowStateEntry>().changeType == QStringLiteral("unsnapped");
        }
        QVERIFY(unsnapped);
    }

    // A window holding the deleted layout's zone on a desktop not in view is
    // unsnapped: its zone exists nowhere any more.
    void deletingALayoutUnsnapsItsHiddenWindows()
    {
        PhosphorZones::Layout* doomed = addThreeZoneLayout();
        m_defaultId = doomed->id().toString();
        const QString hidden = QStringLiteral("app|delete-hidden");
        service()->assignWindowToZone(hidden, doomed->zones().at(0)->id().toString(), m_screenId, m_desktop + 1);

        QVERIFY(m_layoutManager->removeLayout(doomed));

        QVERIFY(service()->zoneForWindow(hidden).isEmpty());
    }

private:
    PhosphorZones::Layout* addThreeZoneLayout()
    {
        m_layoutManager->setDefaultLayoutIdProvider([this]() {
            return m_defaultId;
        });
        PhosphorZones::Layout* layout = createTestLayout(3, m_layoutManager);
        m_layoutManager->addLayout(layout);
        return layout;
    }

    PhosphorPlacement::WindowTrackingService* service() const
    {
        return m_wta->service();
    }

    static bool batchCarries(const QSignalSpy& spy, const QString& windowId)
    {
        for (const QList<QVariant>& args : spy) {
            for (const auto& e : args.at(0).value<PhosphorProtocol::WindowGeometryList>()) {
                if (e.windowId == windowId) {
                    return true;
                }
            }
        }
        return false;
    }

    int m_desktop = 1;
    PhosphorZones::Layout* m_twoZones = nullptr;
    QString m_defaultId;
};

QTEST_MAIN(TestWtaLayoutSwitch)
#include "test_wta_layout_switch.moc"
