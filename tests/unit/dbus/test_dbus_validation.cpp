// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_dbus_validation.cpp
 * @brief Pins the validationError() methods on D-Bus struct types.
 *
 * The D-Bus types carry payloads across a process boundary. Qt's
 * marshaller will decode any byte sequence that matches the signature
 * without checking semantic invariants — so an out-of-range action
 * enum, an empty zoneId on an ApplySnap outcome, or a zero-size tiled
 * request unmarshals silently and corrupts state downstream.
 *
 * validationError() methods enforce the cross-field invariants at the
 * unmarshal boundary. This test fixture pins the accept/reject table
 * so the invariants can't drift as producer code evolves.
 */

#include <QCoreApplication>
#include <QObject>
#include <QTest>

#include <PhosphorProtocol/AutotileMarshalling.h>
#include <PhosphorProtocol/BridgeMarshalling.h>
#include <PhosphorProtocol/DragMarshalling.h>

using namespace PhosphorProtocol;

class TestDbusValidation : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ═════════════════════════════════════════════════════════════════════
    // PhosphorProtocol::DragOutcome
    // ═════════════════════════════════════════════════════════════════════

    void dragOutcome_noOp_noRequirements()
    {
        PhosphorProtocol::DragOutcome o;
        o.action = PhosphorProtocol::DragOutcome::NoOp;
        QVERIFY(o.validationError().isEmpty());
    }

    void dragOutcome_actionOutOfRange_rejected()
    {
        PhosphorProtocol::DragOutcome o;
        // Simulates a garbled/out-of-range wire value; the typed field forces
        // the cast the unmarshal boundary performs.
        o.action = static_cast<PhosphorProtocol::DragOutcome::Action>(999);
        o.windowId = QStringLiteral("win-1");
        const QString err = o.validationError();
        QVERIFY(!err.isEmpty());
        QVERIFY(err.contains(QStringLiteral("out of range")));
    }

    void dragOutcome_negativeAction_rejected()
    {
        PhosphorProtocol::DragOutcome o;
        o.action = static_cast<PhosphorProtocol::DragOutcome::Action>(-1);
        const QString err = o.validationError();
        QVERIFY(!err.isEmpty());
        QVERIFY(err.contains(QStringLiteral("out of range")));
    }

    void dragOutcome_applyFloatRequiresWindowId()
    {
        PhosphorProtocol::DragOutcome o;
        o.action = PhosphorProtocol::DragOutcome::ApplyFloat;
        // windowId empty
        const QString err = o.validationError();
        QVERIFY(!err.isEmpty());
        QVERIFY(err.contains(QStringLiteral("windowId required")));
    }

    void dragOutcome_applyFloat_valid()
    {
        PhosphorProtocol::DragOutcome o;
        o.action = PhosphorProtocol::DragOutcome::ApplyFloat;
        o.windowId = QStringLiteral("win-1");
        // x/y can be zero (top-left corner), targetScreenId optional
        QVERIFY(o.validationError().isEmpty());
    }

    void dragOutcome_applySnapRequiresZoneId()
    {
        PhosphorProtocol::DragOutcome o;
        o.action = PhosphorProtocol::DragOutcome::ApplySnap;
        o.windowId = QStringLiteral("win-1");
        o.width = 800;
        o.height = 600;
        // zoneId empty
        const QString err = o.validationError();
        QVERIFY(!err.isEmpty());
        QVERIFY(err.contains(QStringLiteral("ApplySnap requires non-empty zoneId")));
    }

    void dragOutcome_applySnapRequiresNonZeroSize()
    {
        PhosphorProtocol::DragOutcome o;
        o.action = PhosphorProtocol::DragOutcome::ApplySnap;
        o.windowId = QStringLiteral("win-1");
        o.zoneId = QStringLiteral("zone-1");
        o.width = 0;
        o.height = 600;
        const QString err = o.validationError();
        QVERIFY(!err.isEmpty());
        QVERIFY(err.contains(QStringLiteral("non-zero size")));
    }

    void dragOutcome_applySnap_valid()
    {
        PhosphorProtocol::DragOutcome o;
        o.action = PhosphorProtocol::DragOutcome::ApplySnap;
        o.windowId = QStringLiteral("win-1");
        o.zoneId = QStringLiteral("zone-1");
        o.targetScreenId = QStringLiteral("DP-1");
        o.x = 100;
        o.y = 200;
        o.width = 800;
        o.height = 600;
        QVERIFY(o.validationError().isEmpty());
    }

    void dragOutcome_restoreSizeRequiresNonZeroSize()
    {
        PhosphorProtocol::DragOutcome o;
        o.action = PhosphorProtocol::DragOutcome::RestoreSize;
        o.windowId = QStringLiteral("win-1");
        o.width = 800;
        o.height = 0;
        const QString err = o.validationError();
        QVERIFY(!err.isEmpty());
        QVERIFY(err.contains(QStringLiteral("non-zero size")));
    }

    void dragOutcome_cancelSnap_noSizeNeeded()
    {
        PhosphorProtocol::DragOutcome o;
        o.action = PhosphorProtocol::DragOutcome::CancelSnap;
        o.windowId = QStringLiteral("win-1");
        // CancelSnap has no geometry requirement
        QVERIFY(o.validationError().isEmpty());
    }

    void dragOutcome_notifyDragOutUnsnap_requiresWindowId()
    {
        PhosphorProtocol::DragOutcome o;
        o.action = PhosphorProtocol::DragOutcome::NotifyDragOutUnsnap;
        // windowId empty
        const QString err = o.validationError();
        QVERIFY(!err.isEmpty());
        QVERIFY(err.contains(QStringLiteral("windowId required")));
    }

    // ═════════════════════════════════════════════════════════════════════
    // PhosphorProtocol::DragPolicy
    // ═════════════════════════════════════════════════════════════════════

    void dragPolicy_canonicalSnap_valid()
    {
        PhosphorProtocol::DragPolicy p;
        p.streamDragMoved = true;
        p.showOverlay = true;
        p.grabKeyboard = true;
        p.captureGeometry = true;
        p.screenId = QStringLiteral("DP-1");
        p.bypassReason = PhosphorProtocol::DragBypassReason::None;
        QVERIFY(p.validationError().isEmpty());
    }

    void dragPolicy_autotileBypassRequiresScreenId()
    {
        PhosphorProtocol::DragPolicy p;
        p.bypassReason = PhosphorProtocol::DragBypassReason::EngineOwnedScreen;
        p.captureGeometry = true;
        // screenId empty
        const QString err = p.validationError();
        QVERIFY(!err.isEmpty());
        QVERIFY(err.contains(QStringLiteral("EngineOwnedScreen bypass requires non-empty screenId")));
    }

    void dragPolicy_autotileBypass_valid()
    {
        PhosphorProtocol::DragPolicy p;
        p.bypassReason = PhosphorProtocol::DragBypassReason::EngineOwnedScreen;
        p.screenId = QStringLiteral("HP-1");
        p.captureGeometry = true;
        QVERIFY(p.validationError().isEmpty());
    }

    void dragPolicy_snappingDisabledEmptyScreen_tolerated()
    {
        // beginDrag with empty startScreenId returns SnappingDisabled with
        // empty screenId. That path is deliberately tolerated — the drag
        // still needs to complete via endDrag NoOp.
        PhosphorProtocol::DragPolicy p;
        p.bypassReason = PhosphorProtocol::DragBypassReason::SnappingDisabled;
        p.screenId = QString();
        QVERIFY(p.validationError().isEmpty());
    }

    // ═════════════════════════════════════════════════════════════════════
    // PhosphorProtocol::TileRequestEntry
    // ═════════════════════════════════════════════════════════════════════

    void tileRequestEntry_emptyWindowId_rejected()
    {
        PhosphorProtocol::TileRequestEntry e;
        e.screenId = QStringLiteral("DP-1");
        e.width = 800;
        e.height = 600;
        const QString err = e.validationError();
        QVERIFY(!err.isEmpty());
        QVERIFY(err.contains(QStringLiteral("empty windowId")));
    }

    void tileRequestEntry_emptyScreenId_rejected()
    {
        PhosphorProtocol::TileRequestEntry e;
        e.windowId = QStringLiteral("win-1");
        e.width = 800;
        e.height = 600;
        const QString err = e.validationError();
        QVERIFY(!err.isEmpty());
        QVERIFY(err.contains(QStringLiteral("empty screenId")));
    }

    void tileRequestEntry_negativeSize_rejected()
    {
        PhosphorProtocol::TileRequestEntry e;
        e.windowId = QStringLiteral("win-1");
        e.screenId = QStringLiteral("DP-1");
        e.width = -10;
        e.height = 600;
        const QString err = e.validationError();
        QVERIFY(!err.isEmpty());
        QVERIFY(err.contains(QStringLiteral("negative size")));
    }

    void tileRequestEntry_tiledZeroSize_rejected()
    {
        PhosphorProtocol::TileRequestEntry e;
        e.windowId = QStringLiteral("win-1");
        e.screenId = QStringLiteral("DP-1");
        e.floating = false;
        e.width = 0;
        e.height = 0;
        const QString err = e.validationError();
        QVERIFY(!err.isEmpty());
        QVERIFY(err.contains(QStringLiteral("tiled request requires non-zero size")));
    }

    void tileRequestEntry_floatingZeroSize_tolerated()
    {
        // Floating requests legitimately carry zero size — the plugin
        // resolves geometry from the current frame.
        PhosphorProtocol::TileRequestEntry e;
        e.windowId = QStringLiteral("win-1");
        e.screenId = QStringLiteral("DP-1");
        e.floating = true;
        e.width = 0;
        e.height = 0;
        QVERIFY(e.validationError().isEmpty());
    }

    void tileRequestEntry_tiledValid()
    {
        PhosphorProtocol::TileRequestEntry e;
        e.windowId = QStringLiteral("win-1");
        e.screenId = QStringLiteral("DP-1");
        e.zoneId = QStringLiteral("zone-1");
        e.x = 0;
        e.y = 0;
        e.width = 1920;
        e.height = 1080;
        QVERIFY(e.validationError().isEmpty());
    }

    void tileRequestEntry_invalidStacking_rejected()
    {
        PhosphorProtocol::TileRequestEntry e;
        e.windowId = QStringLiteral("win-1");
        e.screenId = QStringLiteral("DP-1");
        e.width = 1920;
        e.height = 1080;
        e.stacking = QStringLiteral("sideways");
        const QString err = e.validationError();
        QVERIFY(!err.isEmpty());
        QVERIFY(err.contains(QStringLiteral("invalid stacking")));
    }

    void tileRequestEntry_stackingValues_tolerated()
    {
        // Empty (non-overlap layout) and the two declared directions are the
        // only legal values.
        PhosphorProtocol::TileRequestEntry e;
        e.windowId = QStringLiteral("win-1");
        e.screenId = QStringLiteral("DP-1");
        e.width = 1920;
        e.height = 1080;
        QVERIFY(e.validationError().isEmpty());
        e.stacking = QStringLiteral("firstOnTop");
        QVERIFY(e.validationError().isEmpty());
        e.stacking = QStringLiteral("lastOnTop");
        QVERIFY(e.validationError().isEmpty());
    }

    void tileRequestEntry_invalidScrollEdge_rejected()
    {
        // The effect treats any non-"left" value as right, so an unvalidated
        // unknown string would silently flip an entry's side — the whitelist
        // is what makes that a dropped entry instead.
        PhosphorProtocol::TileRequestEntry e;
        e.windowId = QStringLiteral("win-1");
        e.screenId = QStringLiteral("DP-1");
        e.width = 1920;
        e.height = 1080;
        e.scrollEdge = QStringLiteral("up");
        const QString err = e.validationError();
        QVERIFY(!err.isEmpty());
        QVERIFY(err.contains(QStringLiteral("scrollEdge")));
    }

    void tileRequestEntry_windowedFullscreenOnFloating_rejected()
    {
        // The pair is contradictory (the flag means "keeps its column slot")
        // and the effect would flip KWin fullscreen state on a free window.
        // The engine never emits it, so its presence is garbling.
        PhosphorProtocol::TileRequestEntry e;
        e.windowId = QStringLiteral("win-1");
        e.screenId = QStringLiteral("DP-1");
        e.floating = true;
        e.windowedFullscreen = true;
        const QString err = e.validationError();
        QVERIFY(!err.isEmpty());
        QVERIFY(err.contains(QStringLiteral("windowedFullscreen")));
        // On a tiled entry the flag is legal.
        e.floating = false;
        e.width = 1920;
        e.height = 1080;
        QVERIFY(e.validationError().isEmpty());
    }

    void tileRequestEntry_scrollEdgeValues_tolerated()
    {
        // Empty (no strip motion) and the two screen edges are the only
        // legal values.
        PhosphorProtocol::TileRequestEntry e;
        e.windowId = QStringLiteral("win-1");
        e.screenId = QStringLiteral("DP-1");
        e.width = 1920;
        e.height = 1080;
        QVERIFY(e.validationError().isEmpty());
        e.scrollEdge = QStringLiteral("left");
        QVERIFY(e.validationError().isEmpty());
        e.scrollEdge = QStringLiteral("right");
        QVERIFY(e.validationError().isEmpty());
    }

    // ═════════════════════════════════════════════════════════════════════
    // PhosphorProtocol::BridgeRegistrationResult
    // ═════════════════════════════════════════════════════════════════════

    void bridgeRegistration_valid()
    {
        PhosphorProtocol::BridgeRegistrationResult r;
        r.apiVersion = QStringLiteral("2");
        r.bridgeName = QStringLiteral("kwin");
        r.sessionId = QStringLiteral("abcd-1234");
        QVERIFY(r.validationError().isEmpty());
    }

    void bridgeRegistration_rejectedSentinel_tolerated()
    {
        // "REJECTED" is how the daemon signals version mismatch. It's
        // not an invalid result — callers branch on it explicitly.
        PhosphorProtocol::BridgeRegistrationResult r;
        r.apiVersion = QStringLiteral("999");
        r.bridgeName = QString(); // sentinel path may leave these empty
        r.sessionId = QStringLiteral("REJECTED");
        QVERIFY(r.validationError().isEmpty());
    }

    void bridgeRegistration_emptyApiVersion_rejected()
    {
        PhosphorProtocol::BridgeRegistrationResult r;
        r.bridgeName = QStringLiteral("kwin");
        r.sessionId = QStringLiteral("abcd-1234");
        const QString err = r.validationError();
        QVERIFY(!err.isEmpty());
        QVERIFY(err.contains(QStringLiteral("empty apiVersion")));
    }

    void bridgeRegistration_nonNumericApiVersion_rejected()
    {
        PhosphorProtocol::BridgeRegistrationResult r;
        r.apiVersion = QStringLiteral("one");
        r.bridgeName = QStringLiteral("kwin");
        r.sessionId = QStringLiteral("abcd-1234");
        const QString err = r.validationError();
        QVERIFY(!err.isEmpty());
        QVERIFY(err.contains(QStringLiteral("not an integer")));
    }

    void bridgeRegistration_emptyBridgeName_rejected()
    {
        PhosphorProtocol::BridgeRegistrationResult r;
        r.apiVersion = QStringLiteral("2");
        r.sessionId = QStringLiteral("abcd-1234");
        const QString err = r.validationError();
        QVERIFY(!err.isEmpty());
        QVERIFY(err.contains(QStringLiteral("empty bridgeName")));
    }

    void bridgeRegistration_emptySessionId_rejected()
    {
        PhosphorProtocol::BridgeRegistrationResult r;
        r.apiVersion = QStringLiteral("2");
        r.bridgeName = QStringLiteral("kwin");
        const QString err = r.validationError();
        QVERIFY(!err.isEmpty());
        QVERIFY(err.contains(QStringLiteral("empty sessionId")));
    }

    // ═════════════════════════════════════════════════════════════════════
    // PhosphorProtocol::DragBypassReason wire round-trip
    // ═════════════════════════════════════════════════════════════════════

    void dragBypassReason_wireRoundTrip_all()
    {
        // Every enum value must round-trip through the wire format.
        const QVector<PhosphorProtocol::DragBypassReason> all{
            PhosphorProtocol::DragBypassReason::None,
            PhosphorProtocol::DragBypassReason::EngineOwnedScreen,
            PhosphorProtocol::DragBypassReason::SnappingDisabled,
            PhosphorProtocol::DragBypassReason::ContextDisabled,
            PhosphorProtocol::DragBypassReason::LayoutSuppressed,
        };
        // Coverage guard: this list is hand-maintained, so pin it against the
        // declared value count — a new enumerator then fails here instead of
        // silently shipping without round-trip coverage.
        QCOMPARE(all.size(), PhosphorProtocol::DragBypassReasonCount);
        for (auto r : all) {
            QCOMPARE(bypassReasonFromWireString(toWireString(r)), r);
        }
    }

    void dragBypassReason_wireStrings_arePinned()
    {
        // The wire strings are the daemon↔plugin contract. A round-trip alone
        // still passes if a value's string is typo'd (both directions use the
        // same constant), so pin the literals themselves.
        QCOMPARE(toWireString(PhosphorProtocol::DragBypassReason::None), QString());
        QCOMPARE(toWireString(PhosphorProtocol::DragBypassReason::EngineOwnedScreen),
                 QStringLiteral("autotile_screen"));
        QCOMPARE(toWireString(PhosphorProtocol::DragBypassReason::SnappingDisabled),
                 QStringLiteral("snapping_disabled"));
        QCOMPARE(toWireString(PhosphorProtocol::DragBypassReason::ContextDisabled), QStringLiteral("context_disabled"));
        QCOMPARE(toWireString(PhosphorProtocol::DragBypassReason::LayoutSuppressed),
                 QStringLiteral("layout_suppressed"));
    }

    void dragPolicy_layoutSuppressedEmptyScreen_tolerated()
    {
        // Same tolerance as the other non-autotile bypass reasons: beginDrag
        // called with an empty startScreenId may emit one.
        PhosphorProtocol::DragPolicy p;
        p.bypassReason = PhosphorProtocol::DragBypassReason::LayoutSuppressed;
        p.screenId = QString();
        QVERIFY(p.validationError().isEmpty());
    }

    void dragBypassReason_unknownWire_mapsToNone()
    {
        // Unknown wire strings map to None to preserve the legacy
        // fall-through behavior where unrecognized values didn't match
        // the autotile branch.
        QCOMPARE(bypassReasonFromWireString(QStringLiteral("future_reason")), PhosphorProtocol::DragBypassReason::None);
        QCOMPARE(bypassReasonFromWireString(QString()), PhosphorProtocol::DragBypassReason::None);
    }
};

QTEST_MAIN(TestDbusValidation)
#include "test_dbus_validation.moc"
