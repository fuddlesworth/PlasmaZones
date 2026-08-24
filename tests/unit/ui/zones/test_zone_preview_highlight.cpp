// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QColor>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QTest>
#include <QVariantList>
#include <QVariantMap>

#include <QtPlugin>

#include <cmath>
#include <memory>

// Force-link the static QML module's auto-generated init symbol so the test
// binary registers `org.plasmazones.common` types into the QmlEngine. Without
// this the linker drops the init code as dead and the import fails to resolve.
// The symbol name is the qmldir's `classname` field.
Q_IMPORT_PLUGIN(org_plasmazones_commonPlugin)

/**
 * @brief Per-zone highlight semantics of the shared ZonePreview.
 *
 * The zone selector picks one zone out of a layout card while a window is
 * dragged: OverlayService::updateSelectorPosition hit-tests the cursor and
 * writes `selectedZoneIndex` down to the card's ZonePreview. That index is only
 * observable if the singled-out zone renders differently from its siblings.
 *
 * ZonePreview is shared with consumers that pass NO per-zone selection (layout
 * picker, layout OSD, settings thumbnails) and rely on the card-level
 * `isActive` / `isHovered` states lighting every zone at once. Those two
 * behaviours live in one expression, so this pins both halves: card-level state
 * lights everything only while no specific zone is selected.
 *
 * The same component also draws the scrolling strip previews' tab indicators,
 * and the second half of this file pins those: which zones get a band at all,
 * one pill per tab, the edge and length the engine resolved, which pill is lit,
 * and that a column with more tabs than the band has pixels stays inside its
 * own tile. They live here because they are the same shared component, and a
 * layout host sprouting indicators for windows that do not exist is a
 * regression in the highlight surface above as much as in the strip.
 */
class TestZonePreviewHighlight : public QObject
{
    Q_OBJECT

private:
    /// The wire values of PhosphorScrollEngine::TabIndicatorPosition, which
    /// reaches the preview as a bare int (PhosphorProtocol StripPreviewKey).
    /// Spelled here for readability only — nothing in this TU sees the C++
    /// enum, and the QML carries its own copy of the numbering, so this does
    /// NOT catch a renumbering upstream. The static_asserts in
    /// settingsschema_scrolling.cpp catch an enum-only renumbering, and
    /// test_stripzones_tabkeys covers a coordinated one.
    enum TabPosition {
        Left = 0,
        Right = 1,
        Top = 2,
        Bottom = 3
    };

    /// Half a pixel. Wide enough to absorb the fractional gap arithmetic the
    /// delegates apply, far narrower than the tens of pixels separating any
    /// two of the four edges in these fixtures, so a band on the wrong edge
    /// cannot slip through it.
    static constexpr qreal kEdgeEpsilon = 0.51;
    /// The chevron box is arm * sqrt(2) square — see AxisChevron.qml on why
    /// the box is square rather than snug.
    static constexpr qreal kSqrt2 = 1.4142135623730951;
    /// The edge inset the axis fixtures inject, in one place so the fixture
    /// and the assertions that measure against it cannot drift apart.
    static constexpr qreal kFixtureEdgeGap = 3.0;

    QQmlEngine m_engine;

    /// Four side-by-side zones in the flat x/y/width/height wire shape that
    /// layoutpreviewserialize.cpp emits for the selector.
    static QVariantList fourZones()
    {
        QVariantList zones;
        for (int i = 0; i < 4; ++i) {
            QVariantMap zone;
            zone[QLatin1String("x")] = i * 0.25;
            zone[QLatin1String("y")] = 0.0;
            zone[QLatin1String("width")] = 0.25;
            zone[QLatin1String("height")] = 1.0;
            zone[QLatin1String("zoneNumber")] = i + 1;
            // Stable per-zone id for the highlightedZoneIds path; unused by
            // the index-based tests.
            zone[QLatin1String("zoneId")] = QStringLiteral("zone-%1").arg(i + 1);
            zones.append(zone);
        }
        return zones;
    }

    /// Instantiate a ZonePreview with `properties` applied, and return the
    /// per-zone highlight state in zone order.
    QList<bool> highlightStates(const QVariantMap& properties)
    {
        QQmlComponent component(&m_engine);
        component.setData(
            "import QtQuick\n"
            "import org.plasmazones.common as QFZCommon\n"
            "QFZCommon.ZonePreview { width: 180; height: 101 }\n",
            QUrl(QStringLiteral("qrc:/test_zone_preview_highlight.qml")));
        QList<bool> states;
        if (!component.isReady()) {
            qWarning() << "component not ready:" << component.errorString();
            return states;
        }

        // `zones` is a required property, so it has to be supplied at creation
        // rather than assigned afterwards.
        QVariantMap initial = properties;
        initial[QStringLiteral("zones")] = fourZones();

        auto* preview = qobject_cast<QQuickItem*>(component.createWithInitialProperties(initial));
        if (!preview) {
            qWarning() << "create failed:" << component.errorString();
            return states;
        }

        // The Repeater parents each zone delegate to the ZonePreview itself, in
        // model order. Delegates are the only children carrying the property.
        const auto kids = preview->childItems();
        for (QQuickItem* kid : kids) {
            const QVariant highlighted = kid->property("isZoneHighlighted");
            if (highlighted.isValid()) {
                states.append(highlighted.toBool());
            }
        }
        delete preview;
        return states;
    }

    /// Instantiate a ZonePreview over fourZones() with the zone at
    /// @p tabbedIndex carrying strip tab data, and return the preview. It is
    /// parented to the QML engine so its items outlive the call; the whole
    /// tree dies with the fixture. Returns nullptr if creation failed, which
    /// every caller must check — an unchecked null here turns a QML regression
    /// into a crash instead of a failure.
    QQuickItem* tabPreview(int tabCount, int tabbedIndex, int position, qreal lengthProportion, int activeTab = 0)
    {
        QQmlComponent component(&m_engine);
        component.setData(
            "import QtQuick\n"
            "import org.plasmazones.common as QFZCommon\n"
            "QFZCommon.ZonePreview { width: 180; height: 101 }\n",
            QUrl(QStringLiteral("qrc:/test_zone_preview_tabs.qml")));
        if (!component.isReady()) {
            qWarning() << "component not ready:" << component.errorString();
            return nullptr;
        }

        QVariantList zones = fourZones();
        QVariantMap tabbed = zones.at(tabbedIndex).toMap();
        tabbed[QLatin1String("tabCount")] = tabCount;
        tabbed[QLatin1String("activeTab")] = activeTab;
        tabbed[QLatin1String("tabPosition")] = position;
        tabbed[QLatin1String("tabLength")] = lengthProportion;
        zones[tabbedIndex] = tabbed;

        QVariantMap initial;
        initial[QStringLiteral("zones")] = zones;
        auto* preview =
            qobject_cast<QQuickItem*>(component.createWithInitialProperties(initial, m_engine.rootContext()));
        if (!preview) {
            qWarning() << "create failed:" << component.errorString();
            return nullptr;
        }
        preview->setParent(&m_engine);
        return preview;
    }

    /// The tab-indicator bands of a preview. Only a zone carrying tab data has
    /// one, so this is also the count of zones the preview thinks are tabbed.
    static QList<QQuickItem*> bandsOf(QQuickItem* preview)
    {
        QList<QQuickItem*> bands;
        for (QQuickItem* kid : preview->childItems()) {
            if (kid->objectName() == QLatin1String("zonePreviewTabIndicator")) {
                bands.append(kid);
            }
        }
        return bands;
    }

    /// The zone delegates of a preview, in model order. The band's expected
    /// geometry is read off these rather than recomputed from the fixture's
    /// fractions, so the assertions cannot drift with the gap and minimum-size
    /// handling the delegates apply.
    static QList<QQuickItem*> zonesOf(QQuickItem* preview)
    {
        QList<QQuickItem*> zones;
        for (QQuickItem* kid : preview->childItems()) {
            if (kid->objectName() == QLatin1String("zonePreviewZone")) {
                zones.append(kid);
            }
        }
        return zones;
    }

    /// The pills of one tab band: its Rectangle children, told apart from the
    /// band's own Repeater by the `radius` only a Rectangle carries.
    static QList<QQuickItem*> pillsOf(QQuickItem* band)
    {
        QList<QQuickItem*> pills;
        for (QQuickItem* kid : band->childItems()) {
            if (kid->property("radius").isValid()) {
                pills.append(kid);
            }
        }
        return pills;
    }

    /// A bare ZonePreview at @p width by @p height with @p axisHint, no zones.
    /// The ticks are what is under test, so an empty strip is the honest
    /// fixture: they must draw without any zone to hang off.
    ///
    /// The qrc: URL is fixed while the QML text is constant, which is what
    /// keeps the engine's compilation-unit cache harmless here. Parameterising
    /// the QML string without also varying the URL would silently reuse the
    /// first variant's compiled unit for every later call.
    QQuickItem* axisPreview(const QString& axisHint, qreal width, qreal height)
    {
        QQmlComponent component(&m_engine);
        component.setData(
            "import QtQuick\n"
            "import org.plasmazones.common as QFZCommon\n"
            "QFZCommon.ZonePreview { }\n",
            QUrl(QStringLiteral("qrc:/test_zone_preview_axis.qml")));
        if (!component.isReady()) {
            qWarning() << "component not ready:" << component.errorString();
            return nullptr;
        }
        QVariantMap initial;
        initial[QStringLiteral("zones")] = QVariantList();
        initial[QStringLiteral("stripAxisHint")] = axisHint;
        initial[QStringLiteral("width")] = width;
        initial[QStringLiteral("height")] = height;
        initial[QStringLiteral("edgeGap")] = kFixtureEdgeGap;
        auto* preview = qobject_cast<QQuickItem*>(component.createWithInitialProperties(initial));
        if (!preview) {
            qWarning() << "create failed:" << component.errorString();
            return nullptr;
        }
        preview->setParent(&m_engine);
        return preview;
    }

    /// One axis tick by object name, or nullptr.
    static QQuickItem* tickOf(QQuickItem* preview, const char* name)
    {
        for (QQuickItem* kid : preview->childItems()) {
            if (kid->objectName() == QLatin1String(name)) {
                return kid;
            }
        }
        return nullptr;
    }

private Q_SLOTS:
    /// A layout host must draw NO ticks. ZonePreview is shared with the
    /// picker, the OSD and every settings thumbnail, and a chevron on a snap
    /// layout would claim it scrolls.
    void testNoAxisTicksWithoutAHint()
    {
        QQuickItem* preview = axisPreview(QStringLiteral("none"), 200, 120);
        QVERIFY(preview);
        QQuickItem* start = tickOf(preview, "zonePreviewAxisTickStart");
        QQuickItem* end = tickOf(preview, "zonePreviewAxisTickEnd");
        // Checked before dereferencing: an unchecked null here turns a QML
        // regression (a renamed objectName, a deleted tick) into a segfault
        // instead of a failure, which is the one outcome a canary must not
        // produce.
        QVERIFY(start && end);
        QVERIFY(!start->isVisible());
        QVERIFY(!end->isVisible());
    }

    /// The horizontal ticks sit on the LEFT and RIGHT edges, vertically
    /// centred. The chevron is built pointing left and rotated into the other
    /// three directions about its own centre, so a box that is not square
    /// silently offsets the rotated legs — these two cases are what pins the
    /// square-box choice in ZonePreview.
    void testHorizontalAxisTicksSitOnTheSideEdges()
    {
        QQuickItem* preview = axisPreview(QStringLiteral("horizontal"), 200, 120);
        QVERIFY(preview);
        QQuickItem* start = tickOf(preview, "zonePreviewAxisTickStart");
        QQuickItem* end = tickOf(preview, "zonePreviewAxisTickEnd");
        QVERIFY(start && end);
        QVERIFY(start->isVisible());
        QVERIFY(end->isVisible());

        // Square, so `rotation` about the centre cannot move the on-screen
        // box. Measured against the arm rather than width against height,
        // which is an identity — see the vertical case for why.
        const qreal arm = preview->property("stripAxisHintArm").toReal();
        QVERIFY(arm > 0);
        QVERIFY(qAbs(start->width() - arm * kSqrt2) < kEdgeEpsilon);
        QVERIFY(qAbs(start->height() - arm * kSqrt2) < kEdgeEpsilon);

        // Inset from each side edge by the edge gap, and the pair is
        // symmetric about the box's vertical centre line.
        QVERIFY(qAbs(start->x() - kFixtureEdgeGap) < kEdgeEpsilon);
        QVERIFY(qAbs((preview->width() - end->x() - end->width()) - kFixtureEdgeGap) < kEdgeEpsilon);
        QVERIFY(qAbs(start->y() + start->height() / 2 - preview->height() / 2) < kEdgeEpsilon);
        QVERIFY(qAbs(end->y() + end->height() / 2 - preview->height() / 2) < kEdgeEpsilon);

        // Pointing outward, away from each other: 0 is left, 180 is right.
        QCOMPARE(start->rotation(), 0.0);
        QCOMPARE(end->rotation(), 180.0);
    }

    /// The vertical strip's ticks transpose to the TOP and BOTTOM edges.
    /// Drawing the horizontal pair on a vertical strip would name a direction
    /// that screen never takes, which is the one thing the old sketch got
    /// right and this must not lose.
    void testVerticalAxisTicksSitOnTheTopAndBottomEdges()
    {
        QQuickItem* preview = axisPreview(QStringLiteral("vertical"), 120, 200);
        QVERIFY(preview);
        QQuickItem* start = tickOf(preview, "zonePreviewAxisTickStart");
        QQuickItem* end = tickOf(preview, "zonePreviewAxisTickEnd");
        QVERIFY(start && end);
        QVERIFY(start->isVisible());
        QVERIFY(end->isVisible());

        // Asserted on BOTH ticks and before anything else, because it is the
        // precondition that makes every coordinate below mean what it says.
        // These legs are rotated 90 and 270 degrees about their own centres,
        // so on a non-square box the on-screen extent swaps and x/y stop
        // describing where the chevron actually lands.
        //
        // Compared against the ARM, not width against height: AxisChevron
        // binds its height to its own width, so w == h is an identity that
        // cannot fail. Tying it to the arm makes the square-box relationship
        // itself the thing under test — a snug arm*cos45 box changes the width
        // while leaving the arm alone, and only this form catches that.
        const qreal arm = preview->property("stripAxisHintArm").toReal();
        QVERIFY(arm > 0);
        QVERIFY(qAbs(start->width() - arm * kSqrt2) < kEdgeEpsilon);
        QVERIFY(qAbs(start->height() - arm * kSqrt2) < kEdgeEpsilon);
        QVERIFY(qAbs(end->width() - arm * kSqrt2) < kEdgeEpsilon);
        QVERIFY(qAbs(end->height() - arm * kSqrt2) < kEdgeEpsilon);

        QVERIFY(qAbs(start->y() - kFixtureEdgeGap) < kEdgeEpsilon);
        QVERIFY(qAbs((preview->height() - end->y() - end->height()) - kFixtureEdgeGap) < kEdgeEpsilon);
        QVERIFY(qAbs(start->x() + start->width() / 2 - preview->width() / 2) < kEdgeEpsilon);
        QVERIFY(qAbs(end->x() + end->width() / 2 - preview->width() / 2) < kEdgeEpsilon);

        QCOMPARE(start->rotation(), 90.0);
        QCOMPARE(end->rotation(), 270.0);
    }

    /// Each chevron is two strokes meeting at a shared tip. They pivot on
    /// Item.Left at the SAME origin — splaying them from their own centres
    /// instead opens the shape into a Z, which still looks like a mark and so
    /// would not fail any of the placement assertions above.
    void testAxisTickStrokesShareOneTip()
    {
        QQuickItem* preview = axisPreview(QStringLiteral("horizontal"), 200, 120);
        QVERIFY(preview);
        QQuickItem* start = tickOf(preview, "zonePreviewAxisTickStart");
        QVERIFY(start);

        // Told apart from the chevron's own Repeater by the `radius` only a
        // Rectangle carries, the same discriminator pillsOf uses.
        QList<QQuickItem*> strokes;
        for (QQuickItem* kid : start->childItems()) {
            if (kid->property("radius").isValid()) {
                strokes.append(kid);
            }
        }
        QCOMPARE(strokes.size(), 2);
        // THE assertion this case exists for. The shared tip is produced by
        // `transformOrigin: Item.Left`, and nothing else here observes it:
        // both strokes are instances of one Repeater delegate whose x and y
        // bindings never mention modelData, so the coordinate comparisons
        // below hold by construction and stay green with the pivot deleted
        // and the chevron opened into a Z.
        QCOMPARE(strokes.at(0)->property("transformOrigin").toInt(), static_cast<int>(QQuickItem::Left));
        QCOMPARE(strokes.at(1)->property("transformOrigin").toInt(), static_cast<int>(QQuickItem::Left));
        QCOMPARE(strokes.at(0)->x(), strokes.at(1)->x());
        QCOMPARE(strokes.at(0)->y(), strokes.at(1)->y());
        // Splayed either side of the axis, not stacked.
        QCOMPARE(strokes.at(0)->rotation(), -45.0);
        QCOMPARE(strokes.at(1)->rotation(), 45.0);
    }

    /// The arm clamp's lower and middle branches. Every other axis case runs
    /// at 200x120 or 120x200, whose short side of 120 puts the arm past the
    /// upper bound, so the clamp is pinned at 9 throughout and neither the
    /// proportional branch nor the floor is ever exercised — including at the
    /// small sizes the floor was written for.
    void testAxisTickArmScalesDownOnASmallWell_data()
    {
        QTest::addColumn<qreal>("width");
        QTest::addColumn<qreal>("height");
        QTest::addColumn<qreal>("expectedArm");
        // min side 54 -> 54*0.085 = 4.59, the proportional middle branch.
        QTest::newRow("combo thumbnail") << 90.0 << 54.0 << 4.59;
        // min side 20 -> 1.7, below the floor of 3.
        QTest::newRow("tiny well") << 40.0 << 20.0 << 3.0;
        // min side 120 -> 10.2, above the cap of 9.
        QTest::newRow("large well") << 200.0 << 120.0 << 9.0;
    }

    void testAxisTickArmScalesDownOnASmallWell()
    {
        QFETCH(qreal, width);
        QFETCH(qreal, height);
        QFETCH(qreal, expectedArm);

        QQuickItem* preview = axisPreview(QStringLiteral("horizontal"), width, height);
        QVERIFY(preview);
        QCOMPARE(preview->property("stripAxisHintArm").toReal(), expectedArm);

        QQuickItem* start = tickOf(preview, "zonePreviewAxisTickStart");
        QVERIFY(start);
        // Still drawn, still square, at every size: the floor exists so the
        // smallest host keeps a visible mark rather than a sub-pixel smudge.
        // Against the arm, not width-against-height, which is an identity.
        QVERIFY(start->isVisible());
        QVERIFY(qAbs(start->width() - expectedArm * kSqrt2) < kEdgeEpsilon);
        QVERIFY(qAbs(start->height() - expectedArm * kSqrt2) < kEdgeEpsilon);
    }

    /// Tick geometry on a POPULATED strip, and the z-order that keeps it
    /// readable there. Every other axis case uses an empty preview, but the
    /// tick's `z: 1` exists precisely because a live strip's edge column lands
    /// under it, and a tick beneath a zone fill is a smudge.
    void testAxisTicksSitAboveTheZonesOnAPopulatedStrip()
    {
        QQmlComponent component(&m_engine);
        component.setData(
            "import QtQuick\n"
            "import org.plasmazones.common as QFZCommon\n"
            "QFZCommon.ZonePreview { }\n",
            QUrl(QStringLiteral("qrc:/test_zone_preview_axis_populated.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));

        QVariantMap initial;
        initial[QStringLiteral("zones")] = fourZones();
        initial[QStringLiteral("stripAxisHint")] = QStringLiteral("horizontal");
        initial[QStringLiteral("width")] = 200.0;
        initial[QStringLiteral("height")] = 120.0;
        initial[QStringLiteral("edgeGap")] = kFixtureEdgeGap;
        auto* preview = qobject_cast<QQuickItem*>(component.createWithInitialProperties(initial));
        QVERIFY2(preview, qPrintable(component.errorString()));
        preview->setParent(&m_engine);

        QQuickItem* start = tickOf(preview, "zonePreviewAxisTickStart");
        QQuickItem* end = tickOf(preview, "zonePreviewAxisTickEnd");
        QVERIFY(start && end);
        QVERIFY(start->isVisible());
        QVERIFY(end->isVisible());

        // Above every zone delegate, not merely later in declaration order.
        const QList<QQuickItem*> zoneItems = zonesOf(preview);
        QVERIFY(!zoneItems.isEmpty());
        for (QQuickItem* zone : zoneItems) {
            QVERIFY(start->z() > zone->z());
            QVERIFY(end->z() > zone->z());
        }

        // Placement is unchanged by the presence of zones.
        QVERIFY(qAbs(start->x() - kFixtureEdgeGap) < kEdgeEpsilon);
        QVERIFY(qAbs((preview->width() - end->x() - end->width()) - kFixtureEdgeGap) < kEdgeEpsilon);
    }

    /// The ticks must survive the hop through LayoutCard, which is how every
    /// real host reaches ZonePreview (the settings thumbnail, the picker and
    /// the selector all go through it; only the OSD instantiates the preview
    /// directly).
    ///
    /// This exists because the analogous hop was MISSED on the shell side:
    /// LayoutOsdContent grew the property and the C++ pushed it, but the
    /// osdSlot in between never declared it, so setProperty quietly created a
    /// dead dynamic property and no tick ever drew. A property that is only
    /// tested at its two ends passes while the middle is unwired.
    void testLayoutCardForwardsTheAxisHintToItsPreview()
    {
        QQmlComponent component(&m_engine);
        component.setData(
            "import QtQuick\n"
            "import org.plasmazones.common as QFZCommon\n"
            "QFZCommon.LayoutCard { previewWidth: 200; previewHeight: 120 }\n",
            QUrl(QStringLiteral("qrc:/test_layout_card_axis.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));

        QVariantMap initial;
        QVariantMap layoutData;
        layoutData[QStringLiteral("zones")] = fourZones();
        initial[QStringLiteral("layoutData")] = layoutData;
        initial[QStringLiteral("stripAxisHint")] = QStringLiteral("vertical");
        auto* card = qobject_cast<QQuickItem*>(component.createWithInitialProperties(initial));
        QVERIFY2(card, qPrintable(component.errorString()));
        card->setParent(&m_engine);

        // Identified by carrying BOTH `zones` and `stripAxisHint`, which is
        // ZonePreview's signature and nothing else in the card's tree has.
        // Not by the delegates' objectName: those are QObject-parented to
        // their Repeater, so walking up from one is a detour through an
        // ownership chain this test has no reason to depend on.
        QQuickItem* preview = nullptr;
        for (QQuickItem* kid : card->findChildren<QQuickItem*>()) {
            if (kid->property("zones").isValid() && kid->property("stripAxisHint").isValid()) {
                preview = kid;
                break;
            }
        }
        QVERIFY(preview);
        QCOMPARE(preview->property("stripAxisHint").toString(), QStringLiteral("vertical"));
        QQuickItem* start = tickOf(preview, "zonePreviewAxisTickStart");
        QVERIFY(start);
        QVERIFY(start->isVisible());
        // The vertical hint must reach the tick's PLACEMENT, not just the
        // preview's property: a forward that arrived too late to re-evaluate
        // the geometry would satisfy the property compare above while leaving
        // the ticks on the side edges.
        QCOMPARE(start->rotation(), 90.0);
        QVERIFY(qAbs(start->x() + start->width() / 2 - preview->width() / 2) < kEdgeEpsilon);
    }

    /// An empty-strip caption swaps the well's contents: the zone preview goes
    /// away (its own ticks would double up with the arrow) and the empty state
    /// takes over. Same missed-hop risk as above.
    void testLayoutCardEmptyCaptionReplacesThePreview_data()
    {
        QTest::addColumn<QString>("axisHint");
        QTest::addColumn<bool>("expectedVertical");
        QTest::newRow("horizontal") << QStringLiteral("horizontal") << false;
        // The vertical row is not symmetry for its own sake: it is the only
        // thing that makes the verticalAxis assertion below able to fail. An
        // absent property reads back as false, so a horizontal-only test
        // passes against a LayoutCard that stopped forwarding the axis
        // entirely, or a StripEmptyState that lost the property.
        QTest::newRow("vertical") << QStringLiteral("vertical") << true;
    }

    void testLayoutCardEmptyCaptionReplacesThePreview()
    {
        QFETCH(QString, axisHint);
        QFETCH(bool, expectedVertical);

        QQmlComponent component(&m_engine);
        component.setData(
            "import QtQuick\n"
            "import org.plasmazones.common as QFZCommon\n"
            "QFZCommon.LayoutCard { previewWidth: 200; previewHeight: 120 }\n",
            QUrl(QStringLiteral("qrc:/test_layout_card_empty.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));

        QVariantMap initial;
        QVariantMap layoutData;
        layoutData[QStringLiteral("zones")] = QVariantList();
        initial[QStringLiteral("layoutData")] = layoutData;
        initial[QStringLiteral("stripAxisHint")] = axisHint;
        initial[QStringLiteral("stripEmptyCaption")] = QStringLiteral("No windows on the strip yet");
        auto* card = qobject_cast<QQuickItem*>(component.createWithInitialProperties(initial));
        QVERIFY2(card, qPrintable(component.errorString()));
        card->setParent(&m_engine);

        // The empty state carries the caption; the preview is hidden.
        bool sawCaption = false;
        bool sawPreview = false;
        for (QQuickItem* kid : card->findChildren<QQuickItem*>()) {
            const QVariant caption = kid->property("caption");
            if (caption.isValid() && caption.toString() == QLatin1String("No windows on the strip yet")) {
                sawCaption = true;
                QVERIFY(kid->isVisible());
                // isValid() first: without it an absent property compares
                // equal to false and the horizontal row asserts nothing.
                const QVariant vertical = kid->property("verticalAxis");
                QVERIFY(vertical.isValid());
                QCOMPARE(vertical.toBool(), expectedVertical);
            }
            const QVariant hint = kid->property("stripAxisHint");
            if (hint.isValid() && kid->property("zones").isValid()) {
                sawPreview = true;
                QVERIFY(!kid->isVisible());
            }
        }
        QVERIFY(sawCaption);
        // Guarded like the caption half. Without this a predicate that stopped
        // matching the preview (a renamed property, a restructured tree) would
        // silently skip the hidden-preview assertion and the case would still
        // pass on sawCaption alone — half the test vanishing without a failure.
        QVERIFY(sawPreview);
    }

    /// The settings host reaches LayoutCard through LayoutThumbnail, which
    /// needs its OWN declarations and its own forwarding pair. That middle hop
    /// is the same shape as the osdSlot one that broke: tested at the page and
    /// at the card, it would pass with the thumbnail unwired.
    void testLayoutThumbnailForwardsTheStripPropertiesToItsCard()
    {
        // Loaded from the source tree, not by import: the settings QML module
        // is attached to the settings EXECUTABLE target and cannot be linked
        // into a test binary. LayoutThumbnail's own imports (QtQuick, Kirigami,
        // org.plasmazones.common) all resolve here.
        QQmlComponent component(
            &m_engine,
            QUrl::fromLocalFile(QStringLiteral(P_SOURCE_DIR "/src/settings/qml/pages/layouts/LayoutThumbnail.qml")));
        QTRY_VERIFY_WITH_TIMEOUT(component.status() != QQmlComponent::Loading, 5000);
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));

        QVariantMap initial;
        QVariantMap layout;
        layout[QStringLiteral("zones")] = QVariantList();
        // Named, so the thumbnail does not take its i18n("Unnamed") branch.
        // `i18n` is injected by KLocalizedContext and this is a bare
        // QQmlEngine, so that branch throws a ReferenceError — harmless to
        // this assertion but exactly the console noise that hides a real
        // error later.
        layout[QStringLiteral("displayName")] = QStringLiteral("Fixture");
        initial[QStringLiteral("layout")] = layout;
        initial[QStringLiteral("stripAxisHint")] = QStringLiteral("vertical");
        initial[QStringLiteral("stripEmptyCaption")] = QStringLiteral("No windows on the strip yet");
        auto* thumb = qobject_cast<QQuickItem*>(component.createWithInitialProperties(initial));
        QVERIFY2(thumb, qPrintable(component.errorString()));
        thumb->setParent(&m_engine);

        bool sawCaption = false;
        for (QQuickItem* kid : thumb->findChildren<QQuickItem*>()) {
            const QVariant caption = kid->property("caption");
            if (caption.isValid() && caption.toString() == QLatin1String("No windows on the strip yet")) {
                sawCaption = true;
                QCOMPARE(kid->property("verticalAxis").toBool(), true);
            }
        }
        QVERIFY(sawCaption);
    }

    /// StripEmptyState's own arrow geometry. Its arrowheads are the shared
    /// AxisChevron, the same component the ticks use, so the rotated legs get
    /// the same treatment here that the ticks get above — including the
    /// vertical case, which is where the unrotated-extent bug lived.
    void testStripEmptyStateArrowFollowsTheAxis_data()
    {
        QTest::addColumn<bool>("verticalAxis");
        QTest::addColumn<qreal>("width");
        QTest::addColumn<qreal>("height");
        QTest::addColumn<qreal>("firstRotation");
        QTest::addColumn<qreal>("secondRotation");
        QTest::newRow("horizontal") << false << 200.0 << 120.0 << 0.0 << 180.0;
        QTest::newRow("vertical") << true << 120.0 << 200.0 << 90.0 << 270.0;
    }

    void testStripEmptyStateArrowFollowsTheAxis()
    {
        QFETCH(bool, verticalAxis);
        QFETCH(qreal, width);
        QFETCH(qreal, height);
        QFETCH(qreal, firstRotation);
        QFETCH(qreal, secondRotation);

        QQmlComponent component(&m_engine);
        component.setData(
            "import QtQuick\n"
            "import org.plasmazones.common as QFZCommon\n"
            "QFZCommon.StripEmptyState { }\n",
            QUrl(QStringLiteral("qrc:/test_strip_empty_state_axis.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));

        QVariantMap initial;
        initial[QStringLiteral("verticalAxis")] = verticalAxis;
        initial[QStringLiteral("caption")] = QStringLiteral("No windows on the strip yet");
        initial[QStringLiteral("width")] = width;
        initial[QStringLiteral("height")] = height;
        auto* state = qobject_cast<QQuickItem*>(component.createWithInitialProperties(initial));
        QVERIFY2(state, qPrintable(component.errorString()));
        state->setParent(&m_engine);

        // The two arrowheads, told apart from the shaft by carrying the
        // chevron's `direction` property.
        QList<QQuickItem*> heads;
        for (QQuickItem* kid : state->findChildren<QQuickItem*>()) {
            if (kid->property("direction").isValid() && kid->property("arm").isValid()) {
                heads.append(kid);
            }
        }
        QCOMPARE(heads.size(), 2);
        // Keyed by `direction` rather than by list position, so the case does
        // not rest on QObject child order.
        QQuickItem* first = nullptr;
        QQuickItem* second = nullptr;
        for (QQuickItem* head : heads) {
            const int dir = head->property("direction").toInt();
            if (dir == (verticalAxis ? 2 : 0)) {
                first = head;
            } else if (dir == (verticalAxis ? 3 : 1)) {
                second = head;
            }
        }
        QVERIFY(first && second);
        QCOMPARE(first->rotation(), firstRotation);
        QCOMPARE(second->rotation(), secondRotation);

        // The heads must also be ANCHORED to opposite ends along the strip
        // axis. Direction alone is not enough: a component that kept the
        // rotation switch but lost the anchor swap draws both heads stacked at
        // one end, which still satisfies every rotation assertion above.
        const QPointF firstPos(first->x(), first->y());
        const QPointF secondPos(second->x(), second->y());
        if (verticalAxis) {
            QVERIFY(firstPos.y() < secondPos.y());
        } else {
            QVERIFY(firstPos.x() < secondPos.x());
        }

        // Both wells here are tall enough to seat the arrow, so the fit guard
        // must say so and the arrow must actually be painted. Without this the
        // whole suite passes with `_arrowFits` stuck false and the arrow gone
        // from every host.
        QVERIFY(state->property("_arrowFits").toBool());
        QVERIFY(first->isVisible());
        QVERIFY(second->isVisible());

        // Square, for the same reason the ticks are: these legs rotate about
        // their own centres, so a non-square box swaps the on-screen extent on
        // the vertical row only, silently and on one axis alone. Measured
        // against the ARM, because width-against-height is an identity —
        // AxisChevron binds height to width — and the regression that matters
        // (a snug arm*cos45 box) changes the width without touching the arm.
        for (QQuickItem* head : heads) {
            const qreal arm = head->property("arm").toReal();
            QVERIFY(arm > 0);
            QVERIFY(qAbs(head->width() - arm * kSqrt2) < kEdgeEpsilon);
            QVERIFY(qAbs(head->height() - arm * kSqrt2) < kEdgeEpsilon);
        }
    }

    /// A well too short for the arrow drops it rather than clipping the
    /// caption. The hosts clip this component, so without the drop the
    /// sentence that carries the whole message loses its bottom line.
    /// A StripEmptyState's live metrics at @p width: the caption's height plus
    /// the Column's spacing, and the constant arrowhead extent. MEASURED, not
    /// assumed — both are font, DPI and gridUnit products, so a fixture that
    /// hardcodes them only discriminates on the machine it was written on.
    /// Returns false if the component could not be built.
    bool stripEmptyStateMetrics(qreal width, const QString& caption, qreal* capPlusSpacing, qreal* headExtent)
    {
        QQmlComponent component(&m_engine);
        component.setData(
            "import QtQuick\n"
            "import org.plasmazones.common as QFZCommon\n"
            "QFZCommon.StripEmptyState { }\n",
            QUrl(QStringLiteral("qrc:/test_strip_empty_state_probe.qml")));
        if (!component.isReady()) {
            return false;
        }
        QVariantMap initial;
        initial[QStringLiteral("caption")] = caption;
        // Horizontal on purpose: there _arrowStackExtent is the CONSTANT
        // arrowhead box, so the rest of _stackHeight is exactly the caption
        // plus the Column's spacing.
        initial[QStringLiteral("verticalAxis")] = false;
        initial[QStringLiteral("width")] = width;
        initial[QStringLiteral("height")] = 400.0;
        std::unique_ptr<QQuickItem> probe(qobject_cast<QQuickItem*>(component.createWithInitialProperties(initial)));
        if (!probe) {
            return false;
        }
        *headExtent = probe->property("_arrowStackExtent").toReal();
        *capPlusSpacing = probe->property("_stackHeight").toReal() - *headExtent;
        return *headExtent > 0 && *capPlusSpacing > 0;
    }

    void testStripEmptyStateDropsTheArrowBeforeTheCaption_data()
    {
        QTest::addColumn<bool>("verticalAxis");
        QTest::addColumn<qreal>("width");
        QTest::addColumn<qreal>("height");
        // BOTH axes, because the arrow's extent along the stacking axis is not
        // the same quantity on each: horizontally it is the arrowhead box, and
        // vertically it is the arrow's own span, which grows with the well.
        // A fit test written against the horizontal quantity looks right and
        // silently never fires on the vertical axis — which is precisely the
        // bug the first version of this guard shipped with.
        //
        // The heights are DERIVED from the live component rather than
        // hardcoded. Every term is a font, DPI and gridUnit product, so a
        // literal that discriminates on one machine is a hard failure or a
        // silent pass on the next.
        const QString caption = QStringLiteral("This screen could not be measured");
        qreal capPlusSpacing = 0;
        qreal headExtent = 0;

        // Horizontal: anything below head + spacing + caption drops the arrow,
        // with no upper bound, so just inside it is stable across themes.
        if (stripEmptyStateMetrics(190.0, caption, &capPlusSpacing, &headExtent)) {
            QTest::newRow("horizontal") << false << 190.0 << (std::floor(headExtent + capPlusSpacing) - 1.0);
        }

        // Vertical is the delicate one. The height has to sit where the
        // CORRECT formula (arrow measured as its 0.45h span) says the arrow
        // does not fit while the OLD BUGGY one (arrow measured as the constant
        // arrowhead box) says it does; outside that band the row passes
        // against either formula and pins nothing. Solving
        //     0.45h + capPlusSpacing > h   and   headExtent + capPlusSpacing <= h
        // gives  headExtent + capPlusSpacing <= h < capPlusSpacing / 0.55.
        // If the running theme leaves that band empty the row is skipped
        // rather than failing, because there is then no height at which this
        // component can distinguish the two.
        if (stripEmptyStateMetrics(90.0, caption, &capPlusSpacing, &headExtent)) {
            const qreal lo = headExtent + capPlusSpacing;
            const qreal hi = capPlusSpacing / 0.55;
            const qreal h = std::floor(lo) + 1.0;
            if (lo < hi && h < hi) {
                QTest::newRow("vertical") << true << 90.0 << h;
            }
        }
    }

    /// The zero floors on the shaft and the caption width, on a well narrower
    /// than either was written for. Its own case rather than a row of the drop
    /// test above, because at this size the caption collapses to zero width
    /// and the fit guard legitimately reports that the arrow fits — a
    /// different property than the one that test pins.
    void testStripEmptyStateSurvivesAHairlineWell()
    {
        QQmlComponent component(&m_engine);
        component.setData(
            "import QtQuick\n"
            "import org.plasmazones.common as QFZCommon\n"
            "QFZCommon.StripEmptyState { }\n",
            QUrl(QStringLiteral("qrc:/test_strip_empty_state_hairline.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));

        QVariantMap initial;
        initial[QStringLiteral("caption")] = QStringLiteral("This screen could not be measured");
        initial[QStringLiteral("width")] = 10.0;
        initial[QStringLiteral("height")] = 10.0;
        auto* state = qobject_cast<QQuickItem*>(component.createWithInitialProperties(initial));
        QVERIFY2(state, qPrintable(component.errorString()));
        state->setParent(&m_engine);

        // Nothing in the tree may be handed a negative size. Without the
        // Math.max(0, ...) floors the shaft's width is the well minus a head
        // arm, and the caption's is the well minus a gridUnit — both negative
        // here.
        for (QQuickItem* kid : state->findChildren<QQuickItem*>()) {
            QVERIFY2(
                kid->width() >= 0,
                qPrintable(
                    QStringLiteral("negative width on %1").arg(QString::fromLatin1(kid->metaObject()->className()))));
            QVERIFY(kid->height() >= 0);
        }
    }

    void testStripEmptyStateDropsTheArrowBeforeTheCaption()
    {
        QFETCH(bool, verticalAxis);
        QFETCH(qreal, width);
        QFETCH(qreal, height);

        QQmlComponent component(&m_engine);
        component.setData(
            "import QtQuick\n"
            "import org.plasmazones.common as QFZCommon\n"
            "QFZCommon.StripEmptyState { }\n",
            QUrl(QStringLiteral("qrc:/test_strip_empty_state_short.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));

        QVariantMap initial;
        initial[QStringLiteral("caption")] = QStringLiteral("This screen could not be measured");
        initial[QStringLiteral("verticalAxis")] = verticalAxis;
        initial[QStringLiteral("width")] = width;
        // Well below arrow + spacing + a wrapped two-line caption on this axis.
        initial[QStringLiteral("height")] = height;
        auto* state = qobject_cast<QQuickItem*>(component.createWithInitialProperties(initial));
        QVERIFY2(state, qPrintable(component.errorString()));
        state->setParent(&m_engine);

        QVERIFY(!state->property("_arrowFits").toBool());

        // The arrow is not merely marked unfit, it is not painted. Found by
        // carrying the chevron's `direction`, the same discriminator the axis
        // case uses.
        for (QQuickItem* kid : state->findChildren<QQuickItem*>()) {
            if (kid->property("direction").isValid() && kid->property("arm").isValid()) {
                QVERIFY(!kid->isVisible());
            }
        }

        bool sawCaptionLabel = false;
        for (QQuickItem* kid : state->findChildren<QQuickItem*>()) {
            const QVariant text = kid->property("text");
            if (text.isValid() && text.toString() == QLatin1String("This screen could not be measured")) {
                sawCaptionLabel = true;
                QVERIFY(kid->isVisible());
                // The point of dropping the arrow: the caption now fits the
                // well, so the host's clip has nothing to cut. (The zero
                // floors are pinned separately, on a hairline well.)
                QVERIFY(kid->height() <= state->height());
            }
        }
        QVERIFY(sawCaptionLabel);
    }

    /// The zone selector's active layout card. Before the fix `isActive` lit
    /// every zone, so the hit-tested zone was indistinguishable and picking a
    /// specific zone to snap into had no visible feedback.
    void testActiveCardHighlightsOnlySelectedZone()
    {
        QVariantMap props;
        props[QStringLiteral("isActive")] = true;
        props[QStringLiteral("selectedZoneIndex")] = 2;

        const QList<bool> states = highlightStates(props);
        QCOMPARE(states, QList<bool>({false, false, true, false}));
    }

    /// The card under the cursor: LayoutCard maps its `isSelected` onto
    /// ZonePreview's `isHovered`, so the whole card reads as hovered.
    void testHoveredCardHighlightsOnlySelectedZone()
    {
        QVariantMap props;
        props[QStringLiteral("isHovered")] = true;
        props[QStringLiteral("selectedZoneIndex")] = 1;

        const QList<bool> states = highlightStates(props);
        QCOMPARE(states, QList<bool>({false, true, false, false}));
    }

    /// Consumers with no per-zone selection keep the whole-card highlight.
    void testCardStateLightsEveryZoneWithoutSelection()
    {
        QVariantMap active;
        active[QStringLiteral("isActive")] = true;
        active[QStringLiteral("selectedZoneIndex")] = -1;
        QCOMPARE(highlightStates(active), QList<bool>({true, true, true, true}));

        QVariantMap hovered;
        hovered[QStringLiteral("isHovered")] = true;
        hovered[QStringLiteral("selectedZoneIndex")] = -1;
        QCOMPARE(highlightStates(hovered), QList<bool>({true, true, true, true}));
    }

    /// The ID-based half of `hasZoneSelection`: a highlightedZoneIds list
    /// singles out the matching zone and suppresses the card-level
    /// `isActive` lighting of the other zones.
    void testHighlightedZoneIdsLightOnlyMatchedZone()
    {
        QVariantMap props;
        props[QStringLiteral("isActive")] = true;
        props[QStringLiteral("highlightedZoneIds")] = QVariantList{QStringLiteral("zone-3")};

        const QList<bool> states = highlightStates(props);
        QCOMPARE(states, QList<bool>({false, false, true, false}));
    }

    /// Nothing selected and no card-level state: every zone stays dim.
    void testNeutralCardHighlightsNothing()
    {
        const QList<bool> states = highlightStates(QVariantMap());
        QCOMPARE(states, QList<bool>({false, false, false, false}));
    }

    /// Carrying tab data is the whole gate. A LAYOUT zone has no tab keys, so
    /// the layout picker, the OSD and the algorithm previews must draw no band
    /// at all — not an inert one — or they sprout indicators for windows that
    /// do not exist and pay for an overlay item per zone to do it.
    void testTabIndicatorDrawsOnlyOnZonesCarryingTabData()
    {
        QQuickItem* preview = tabPreview(2, 1, Bottom, 0.5);
        QVERIFY(preview);
        const QList<QQuickItem*> bands = bandsOf(preview);
        QCOMPARE(bands.size(), 1);
        // The other three zones are still drawn; it is only the band that is
        // absent, so this is not passing because the whole preview is empty.
        QCOMPARE(zonesOf(preview).size(), 4);
    }

    /// One pill per tab, hidden tabs included. The strip walk emits only a
    /// tabbed column's SHOWN tab, so the pill count is the whole of what tells
    /// the viewer how many windows the tile stands for.
    void testTabIndicatorDrawsOnePillPerTab()
    {
        QQuickItem* preview = tabPreview(5, 1, Bottom, 0.5);
        QVERIFY(preview);
        const QList<QQuickItem*> bands = bandsOf(preview);
        QCOMPARE(bands.size(), 1);
        // Counted by the property the pills carry: the band's Repeater is a
        // child item of its own, and a raw child count would fold it in.
        QCOMPARE(pillsOf(bands.at(0)).size(), 5);
    }

    /// The band runs along the edge the engine resolved, at the length it
    /// resolved, for every one of the four positions. Expected geometry is
    /// read off the tabbed ZONE delegate, so a band that ignored the position
    /// key cannot land inside the tolerance of another edge.
    void testTabIndicatorFollowsTheResolvedEdge_data()
    {
        QTest::addColumn<int>("position");
        QTest::addColumn<bool>("vertical");

        QTest::newRow("left") << static_cast<int>(Left) << true;
        QTest::newRow("right") << static_cast<int>(Right) << true;
        QTest::newRow("top") << static_cast<int>(Top) << false;
        QTest::newRow("bottom") << static_cast<int>(Bottom) << false;
    }

    void testTabIndicatorFollowsTheResolvedEdge()
    {
        QFETCH(int, position);
        QFETCH(bool, vertical);

        constexpr qreal lengthProportion = 0.5;

        QQuickItem* preview = tabPreview(2, 1, position, lengthProportion);
        QVERIFY(preview);
        const QList<QQuickItem*> bands = bandsOf(preview);
        QCOMPARE(bands.size(), 1);
        QQuickItem* band = bands.at(0);
        QQuickItem* tile = zonesOf(preview).at(1);

        // Long axis: the resolved proportion of the tile's extent, centred,
        // exactly as indicatorRectFor centres the real bar on its column.
        const qreal tileLength = vertical ? tile->height() : tile->width();
        const qreal bandLength = vertical ? band->height() : band->width();
        const qreal bandThickness = vertical ? band->width() : band->height();
        QVERIFY(bandThickness < bandLength);
        QVERIFY(qAbs(bandLength - tileLength * lengthProportion) < kEdgeEpsilon);

        const qreal crossOrigin = vertical ? band->y() : band->x();
        const qreal tileCrossOrigin = vertical ? tile->y() : tile->x();
        QVERIFY(qAbs(crossOrigin - (tileCrossOrigin + (tileLength - bandLength) / 2)) < kEdgeEpsilon);

        // Short axis: flush INSIDE the named edge. Outside it would land on
        // the neighbouring tile at this scale and read as that tile's bar.
        const qreal nearEdge = vertical ? tile->x() : tile->y();
        const qreal farEdge = nearEdge + (vertical ? tile->width() : tile->height());
        const qreal bandNear = vertical ? band->x() : band->y();
        if (position == Left || position == Top) {
            QVERIFY(qAbs(bandNear - nearEdge) < kEdgeEpsilon);
        } else {
            QVERIFY(qAbs((bandNear + bandThickness) - farEdge) < kEdgeEpsilon);
        }
    }

    /// Exactly the tab on show is lit. Without this the band reports how many
    /// windows the column holds but not which one the viewer is looking at,
    /// and a preview that lit all of them or none would look plausible.
    void testTabIndicatorLightsOnlyTheShownTab()
    {
        constexpr int shownTab = 2;
        QQuickItem* preview = tabPreview(4, 1, Bottom, 0.8, shownTab);
        QVERIFY(preview);
        const QList<QQuickItem*> bands = bandsOf(preview);
        QCOMPARE(bands.size(), 1);
        const QList<QQuickItem*> pills = pillsOf(bands.at(0));
        QCOMPARE(pills.size(), 4);

        const QColor lit = pills.at(shownTab)->property("color").value<QColor>();
        int litCount = 0;
        for (const QQuickItem* pill : pills) {
            if (pill->property("color").value<QColor>() == lit) {
                ++litCount;
            }
        }
        QCOMPARE(litCount, 1);
    }

    /// A column with more tabs than its band has pixels. Both pill extents are
    /// floored, so the row laid out is longer than the band — the pills must
    /// stay inside the tile rather than running onto its neighbour, which is
    /// the whole reason the band draws inside the edge in the first place.
    void testTabIndicatorPillsStayInsideTheBandWhenCrowded()
    {
        QQuickItem* preview = tabPreview(40, 1, Bottom, 0.2);
        QVERIFY(preview);
        const QList<QQuickItem*> bands = bandsOf(preview);
        QCOMPARE(bands.size(), 1);
        QQuickItem* band = bands.at(0);
        const QList<QQuickItem*> pills = pillsOf(band);
        QCOMPARE(pills.size(), 40);

        // The laid-out row really does overrun, so the clip is load-bearing
        // rather than decorative — assert the premise before the guard.
        const QQuickItem* last = pills.constLast();
        QVERIFY(last->x() + last->width() > band->width());
        QVERIFY(band->clip());

        // The band itself is within the tile. Pill containment rides on the
        // clip asserted above rather than being measured here: the pills
        // deliberately DO overrun, so their own rects prove nothing.
        QQuickItem* tile = zonesOf(preview).at(1);
        QVERIFY(band->x() >= tile->x() - kEdgeEpsilon);
        QVERIFY(band->x() + band->width() <= tile->x() + tile->width() + kEdgeEpsilon);
    }
};

QTEST_MAIN(TestZonePreviewHighlight)
#include "test_zone_preview_highlight.moc"
