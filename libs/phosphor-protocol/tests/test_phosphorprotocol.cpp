// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorProtocol/AutotileMarshalling.h>
#include <PhosphorProtocol/BridgeMarshalling.h>
#include <PhosphorProtocol/DragMarshalling.h>
#include <PhosphorProtocol/Registration.h>
#include <PhosphorProtocol/ScrollAxisEnum.h>
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorProtocol/WindowMarshalling.h>
#include <PhosphorProtocol/WindowTypeEnum.h>
#include <PhosphorProtocol/ZoneMarshalling.h>

#include <QSet>
#include <QTest>

using namespace PhosphorProtocol;

class TestPhosphorProtocol : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        registerWireTypes();
    }

    // D-Bus round-trip tests are in test_compositor_common (requires full message transport).

    void testWindowGeometryToRect()
    {
        WindowGeometryEntry e{QStringLiteral("w"), 5, 10, 100, 200, QString()};
        QCOMPARE(e.toRect(), QRect(5, 10, 100, 200));
    }

    void testWindowGeometryFromRect()
    {
        auto e = WindowGeometryEntry::fromRect(QStringLiteral("w"), QRect(1, 2, 3, 4));
        QCOMPARE(e.windowId, QStringLiteral("w"));
        QCOMPARE(e.x, 1);
        QCOMPARE(e.y, 2);
        QCOMPARE(e.width, 3);
        QCOMPARE(e.height, 4);
    }

    // DragPolicy and DragOutcome have NO wire round-trip coverage anywhere.
    // This comment used to claim test_compositor_common covered them; it does
    // not — tests/unit/compositor-common/test_wire_types.cpp does not include
    // DragMarshalling.h and names neither type. The gap matters more for these
    // two than for their neighbours, because DragPolicy's marshaller is the
    // one that does a real transform (enum to legacy wire string) and its
    // declared shape lives only in a code comment. Covering them needs full
    // D-Bus message transport, for the nested types they carry.

    void testBypassReasonWireStringRoundTrip()
    {
        const QVector<DragBypassReason> all{
            DragBypassReason::None,
            DragBypassReason::EngineOwnedScreen,
            DragBypassReason::SnappingDisabled,
            DragBypassReason::ContextDisabled,
            DragBypassReason::LayoutSuppressed,
        };
        // Hand-maintained list pinned against the declared value count, so a
        // new enumerator fails here rather than shipping without coverage in
        // the library's own suite.
        QCOMPARE(all.size(), DragBypassReasonCount);
        for (auto r : all) {
            QCOMPARE(bypassReasonFromWireString(toWireString(r)), r);
        }
    }

    void testBypassReasonUnknownFallback()
    {
        QCOMPARE(bypassReasonFromWireString(QStringLiteral("bogus")), DragBypassReason::None);
    }

    void testTileRequestValidationEmpty()
    {
        TileRequestEntry e;
        QVERIFY(!e.validationError().isEmpty());
    }

    void testTileRequestValidationValid()
    {
        TileRequestEntry e;
        e.windowId = QStringLiteral("w");
        e.screenId = QStringLiteral("s");
        e.width = 100;
        e.height = 100;
        QVERIFY(e.validationError().isEmpty());
    }

    void testTileRequestValidationFloatingZeroSize()
    {
        TileRequestEntry e;
        e.windowId = QStringLiteral("w");
        e.screenId = QStringLiteral("s");
        e.floating = true;
        e.width = 0;
        e.height = 0;
        QVERIFY(e.validationError().isEmpty());
    }

    void testTileRequestValidationStacking()
    {
        TileRequestEntry e;
        e.windowId = QStringLiteral("w");
        e.screenId = QStringLiteral("s");
        e.width = 100;
        e.height = 100;
        e.stacking = QStringLiteral("firstOnTop");
        QVERIFY(e.validationError().isEmpty());
        e.stacking = QStringLiteral("lastOnTop");
        QVERIFY(e.validationError().isEmpty());
        e.stacking = QStringLiteral("sideways");
        QVERIFY(e.validationError().contains(QStringLiteral("stacking")));
    }

    void testTileRequestValidationScrollEdge()
    {
        TileRequestEntry e;
        e.windowId = QStringLiteral("w");
        e.screenId = QStringLiteral("s");
        e.width = 100;
        e.height = 100;
        // Four values since v12: the strip's axis is per-screen, and a
        // departure names the SCREEN EDGE the column left through, so the pair
        // widens with the axis.
        e.scrollEdge = QStringLiteral("left");
        QVERIFY(e.validationError().isEmpty());
        e.scrollEdge = QStringLiteral("right");
        QVERIFY(e.validationError().isEmpty());
        e.scrollEdge = QStringLiteral("top");
        QVERIFY(e.validationError().isEmpty());
        e.scrollEdge = QStringLiteral("bottom");
        QVERIFY(e.validationError().isEmpty());
        // Still a CLOSED set. "up"/"down" are the within-column direction
        // vocabulary, not screen edges, and the effect re-anchors an animation
        // origin on any non-empty value — so an unrecognised string would
        // silently move a window's apparent entry side rather than failing.
        e.scrollEdge = QStringLiteral("up");
        QVERIFY(e.validationError().contains(QStringLiteral("scrollEdge")));
        e.scrollEdge = QStringLiteral("sideways");
        QVERIFY(e.validationError().contains(QStringLiteral("scrollEdge")));
    }

    void testTileRequestValidationTabFrom()
    {
        // tabFrom names the OTHER window of a tab swap: any foreign id is
        // accepted (the effect resolves or drops unknown ids itself), only a
        // self-reference is rejected as garbling.
        TileRequestEntry e;
        e.windowId = QStringLiteral("w");
        e.screenId = QStringLiteral("s");
        e.width = 100;
        e.height = 100;
        e.tabFrom = QStringLiteral("other");
        QVERIFY(e.validationError().isEmpty());
        e.tabFrom = e.windowId;
        QVERIFY(e.validationError().contains(QStringLiteral("tabFrom")));
    }

    void testTileRequestValidationWindowedFullscreen()
    {
        // The lib's own coverage of its newest cross-field invariants (the
        // app tree pins the same rules at its boundary): the flag is legal
        // on a plain tiled entry and rejected beside either contradictory
        // placement action.
        TileRequestEntry e;
        e.windowId = QStringLiteral("w");
        e.screenId = QStringLiteral("s");
        e.width = 100;
        e.height = 100;
        e.windowedFullscreen = true;
        QVERIFY(e.validationError().isEmpty());
        // Discriminating substrings on both arms: "windowedFullscreen" alone
        // is shared by the two rejection messages, so each arm also pins the
        // partner flag its message names.
        e.floating = true;
        QVERIFY(e.validationError().contains(QStringLiteral("windowedFullscreen")));
        QVERIFY(e.validationError().contains(QStringLiteral("floating")));
        e.floating = false;
        e.monocle = true;
        QVERIFY(e.validationError().contains(QStringLiteral("windowedFullscreen")));
        QVERIFY(e.validationError().contains(QStringLiteral("monocle")));
    }

    void testDragPolicyValidationAutotileNoScreen()
    {
        DragPolicy p;
        p.bypassReason = DragBypassReason::EngineOwnedScreen;
        p.screenId.clear();
        QVERIFY(!p.validationError().isEmpty());
    }

    void testDragOutcomeValidationApplySnapNoZone()
    {
        DragOutcome o;
        o.action = DragOutcome::ApplySnap;
        o.windowId = QStringLiteral("w");
        o.zoneId.clear();
        QVERIFY(!o.validationError().isEmpty());
    }

    void testDragOutcomeValidationNoOp()
    {
        DragOutcome o;
        o.action = DragOutcome::NoOp;
        QVERIFY(o.validationError().isEmpty());
    }

    void testBridgeRegistrationValidation()
    {
        BridgeRegistrationResult r;
        r.apiVersion = QStringLiteral("2");
        r.bridgeName = QStringLiteral("kwin");
        r.sessionId = QStringLiteral("abc");
        QVERIFY(r.validationError().isEmpty());
    }

    void testBridgeRegistrationRejectedSentinelIsNotAValidationError()
    {
        // "REJECTED" is the documented version-mismatch sentinel for
        // `sessionId`, not an invariant violation: `validationError()` must
        // stay empty so the sentinel survives the validity gate. Callers
        // detect the rejection by checking the sessionId value separately —
        // see BridgeRegistrationResult::validationError() docs.
        BridgeRegistrationResult r;
        r.sessionId = QStringLiteral("REJECTED");
        QVERIFY(r.validationError().isEmpty());
    }

    void testServiceConstants()
    {
        QCOMPARE(Service::Name, QLatin1String("org.plasmazones"));
        QCOMPARE(Service::ObjectPath, QLatin1String("/PlasmaZones"));
        // Bumped to 12 for the per-screen strip axis: viewDeltaX became
        // viewDelta (a signed scalar along that screen's own axis) and
        // scrollEdge widened to {left,right,top,bottom}.
        //
        // UNLIKE v6 THROUGH v11, THIS BUMP WIDENED NO SIGNATURE. Those relied
        // on Qt's signature matching as a second line of defence — a stale
        // peer's slot simply never fired. Here a v11 effect would demarshal a
        // v12 batch perfectly and then drop every vertical park as an invalid
        // scrollEdge while reading a vertical delta as a horizontal slide. The
        // handshake is the only thing rejecting that pairing, which is why the
        // bump must not be "optimized away" as unnecessary later.
        //
        // Bumped to 13 when the scrolling tab indicators moved into the KWin
        // effect: org.plasmazones.Scrolling lost its surface-id API and
        // org.plasmazones.Tiling gained the strips / paint-override / colour
        // transport the effect now requires. Again no signature widens, so the
        // handshake alone refuses a mismatched daemon/effect pair.
        QCOMPARE(Service::ApiVersion, 13);
        QCOMPARE(Service::MinPeerApiVersion, 13);
    }

    // SnapAssistCandidate round-trip is covered by test_compositor_common.

    // ── WindowType enum ──────────────────────────────────────────────────

    void testWindowTypeStringRoundTrip()
    {
        QSet<QString> tokens;
        for (int v = windowTypeMinValue; v <= windowTypeMaxValue; ++v) {
            const auto type = static_cast<WindowType>(v);
            const QString token = windowTypeToString(type);
            const auto parsed = windowTypeFromString(token);
            QVERIFY(parsed.has_value());
            QVERIFY(*parsed == type);
            tokens.insert(token);
        }
        // Every enum value must map to a DISTINCT wire token — a copy-paste
        // bug returning the same token for two values would round-trip one of
        // them to the wrong enum. The set size equals the enum value count.
        QCOMPARE(tokens.size(), windowTypeMaxValue - windowTypeMinValue + 1);
    }

    void testWindowTypeFromStringCaseInsensitive()
    {
        const auto upper = windowTypeFromString(QStringLiteral("DIALOG"));
        QVERIFY(upper.has_value() && *upper == WindowType::Dialog);
        const auto mixed = windowTypeFromString(QStringLiteral("DiAlOg"));
        QVERIFY(mixed.has_value() && *mixed == WindowType::Dialog);
    }

    void testWindowTypeFromStringUnknownTokenIsNullopt()
    {
        QVERIFY(!windowTypeFromString(QStringLiteral("not-a-type")).has_value());
        QVERIFY(!windowTypeFromString(QString()).has_value());
    }

    void testWindowTypeAppletPopupIsWireStableAndInRange()
    {
        // Plasma applet popups (Kickoff, tray flyouts) were unrepresentable
        // until AppletPopup was appended, so every one of them crossed the wire
        // as Unknown and no rule could name them.
        QVERIFY(isValidWindowType(static_cast<int>(WindowType::AppletPopup)));
        QVERIFY(windowTypeFromInt(13) == WindowType::AppletPopup);
        const auto parsed = windowTypeFromString(QStringLiteral("appletpopup"));
        QVERIFY(parsed.has_value() && *parsed == WindowType::AppletPopup);

        // The underlying ints ARE the D-Bus representation, so they are frozen.
        // Renumbering an existing value would silently reinterpret every rule
        // and every cached window snapshot a peer already stored; this pins the
        // pre-existing tail of the enum against exactly that.
        QCOMPARE(static_cast<int>(WindowType::Unknown), 0);
        QCOMPARE(static_cast<int>(WindowType::Popup), 12);
        QCOMPARE(static_cast<int>(WindowType::AppletPopup), 13);
        // windowTypeMaxValue must track the LAST enumerator, or a newly added
        // type is rejected on arrival and degrades to Unknown — the exact
        // failure mode this whole test guards.
        QCOMPARE(windowTypeMaxValue, static_cast<int>(WindowType::AppletPopup));
    }

    void testWindowTypeFromIntClampsOutOfRange()
    {
        QVERIFY(windowTypeFromInt(static_cast<int>(WindowType::Dialog)) == WindowType::Dialog);
        QVERIFY(windowTypeFromInt(-1) == WindowType::Unknown);
        QVERIFY(windowTypeFromInt(9999) == WindowType::Unknown);
        QVERIFY(!isValidWindowType(-1));
        QVERIFY(!isValidWindowType(9999));
        QVERIFY(isValidWindowType(static_cast<int>(WindowType::Popup)));
    }

    /// The ScrollAxis wire values are frozen: Horizontal is 0 and Vertical 1,
    /// and both the daemon and the KWin effect cast ints that crossed the bus
    /// against exactly these numbers. Pinned as literals — a renumber would
    /// silently transpose every strip on the wire.
    void testScrollAxisWireStability()
    {
        QCOMPARE(static_cast<int>(ScrollAxis::Horizontal), 0);
        QCOMPARE(static_cast<int>(ScrollAxis::Vertical), 1);
        QVERIFY(isValidScrollAxis(0));
        QVERIFY(isValidScrollAxis(1));
        QVERIFY(!isValidScrollAxis(-1));
        QVERIFY(!isValidScrollAxis(2));
        // Out-of-range ints from a skewed or malformed peer fall back to
        // Horizontal, the same answer an absent value gives.
        QCOMPARE(scrollAxisFromInt(1), ScrollAxis::Vertical);
        QCOMPARE(scrollAxisFromInt(7), ScrollAxis::Horizontal);
        QCOMPARE(scrollAxisFromInt(-3), ScrollAxis::Horizontal);
    }

    /// The Auto rule in the library BOTH processes resolve it from (the
    /// engine per layout pass, the editor and settings previews per draw):
    /// strictly taller than wide is Vertical; square and degenerate are
    /// Horizontal, the historical answer.
    void testAutoScrollAxisRule()
    {
        QCOMPARE(autoScrollAxisFor(1200, 800), ScrollAxis::Horizontal);
        QCOMPARE(autoScrollAxisFor(800, 1200), ScrollAxis::Vertical);
        QCOMPARE(autoScrollAxisFor(1000, 1000), ScrollAxis::Horizontal); // square: historical answer
        QCOMPARE(autoScrollAxisFor(0, 0), ScrollAxis::Horizontal); // degenerate: geometry says nothing
        QCOMPARE(autoScrollAxisFor(0, 1), ScrollAxis::Vertical); // strictly greater, even degenerate-thin
        QCOMPARE(autoScrollAxisFor(1000, 1001), ScrollAxis::Vertical); // strictly greater, no threshold
    }
};

QTEST_GUILESS_MAIN(TestPhosphorProtocol)
#include "test_phosphorprotocol.moc"
