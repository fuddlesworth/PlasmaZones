// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QTest>
#include <QVariantList>
#include <QVariantMap>

#include <QtPlugin>

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
 */
class TestZonePreviewHighlight : public QObject
{
    Q_OBJECT

private:
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
    /// @p tabbedIndex carrying strip tab data, and return the tab-indicator
    /// bands in zone order. The preview OWNS the returned items; it is parented
    /// to the QML engine so they outlive the call.
    QList<QQuickItem*> tabBands(int tabCount, int tabbedIndex, int position, qreal lengthProportion)
    {
        QQmlComponent component(&m_engine);
        component.setData(
            "import QtQuick\n"
            "import org.plasmazones.common as QFZCommon\n"
            "QFZCommon.ZonePreview { width: 180; height: 101 }\n",
            QUrl(QStringLiteral("qrc:/test_zone_preview_tabs.qml")));
        QList<QQuickItem*> bands;
        if (!component.isReady()) {
            qWarning() << "component not ready:" << component.errorString();
            return bands;
        }

        QVariantList zones = fourZones();
        QVariantMap tabbed = zones.at(tabbedIndex).toMap();
        tabbed[QLatin1String("tabCount")] = tabCount;
        tabbed[QLatin1String("activeTab")] = 0;
        tabbed[QLatin1String("tabPosition")] = position;
        tabbed[QLatin1String("tabLength")] = lengthProportion;
        zones[tabbedIndex] = tabbed;

        QVariantMap initial;
        initial[QStringLiteral("zones")] = zones;
        // Parented to the engine so the items survive the return; the whole
        // tree dies with the fixture.
        auto* preview =
            qobject_cast<QQuickItem*>(component.createWithInitialProperties(initial, m_engine.rootContext()));
        if (!preview) {
            qWarning() << "create failed:" << component.errorString();
            return bands;
        }
        preview->setParent(&m_engine);
        // Model order, and no zone delegate answers to this name.
        for (QQuickItem* kid : preview->childItems()) {
            if (kid->objectName() == QLatin1String("zonePreviewTabIndicator")) {
                bands.append(kid);
            }
        }
        return bands;
    }

    /// The pills of one tab band: its Rectangle children, told apart from the
    /// band's own Repeater by the `radius` only a Rectangle carries.
    static int pillCount(QQuickItem* band)
    {
        int pills = 0;
        for (QQuickItem* kid : band->childItems()) {
            if (kid->property("radius").isValid()) {
                ++pills;
            }
        }
        return pills;
    }

private Q_SLOTS:
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

    /// The scrolling strip previews' tab indicators. A strip walk emits only
    /// a tabbed column's SHOWN tab, so the pills are the whole of what tells
    /// the viewer the tile stands for a stack — and a zone carrying no tab
    /// data (every LAYOUT zone) must draw none, or the layout picker sprouts
    /// indicators for windows that do not exist.
    void testTabIndicatorDrawsOnePillPerTabOnTheResolvedEdge()
    {
        const QList<QQuickItem*> bands = tabBands(2, 1, 3, 0.5);
        // One band per zone, but only the tabbed zone's is visible.
        QCOMPARE(bands.size(), 4);
        QCOMPARE(bands.at(0)->isVisible(), false);
        QCOMPARE(bands.at(2)->isVisible(), false);

        QQuickItem* band = bands.at(1);
        QVERIFY(band->isVisible());
        // One pill per tab, hidden tabs included. Counted by the property the
        // pills carry: the band's Repeater is a child item of its own, and a
        // raw child count would fold it in.
        QCOMPARE(pillCount(band), 2);

        // Bottom (3) runs along the tile's width, so the band is horizontal,
        // sits at the tile's bottom edge, and covers half of it — the
        // resolved length proportion, centered.
        const qreal tileWidth = 180.0 * 0.25;
        QCOMPARE(band->height() < band->width(), true);
        QVERIFY(qAbs(band->width() - tileWidth * 0.5) < tileWidth * 0.15);
        QVERIFY(band->y() + band->height() <= 101.0);
        QVERIFY(band->y() > 101.0 / 2);

        // Left (0) transposes it: a vertical band on the tile's near edge,
        // covering half the tile's HEIGHT. Same zone, so a band that ignored
        // the position key would not move at all.
        QQuickItem* sideBand = tabBands(2, 1, 0, 0.5).at(1);
        QVERIFY(sideBand->isVisible());
        QCOMPARE(sideBand->width() < sideBand->height(), true);
        QVERIFY(qAbs(sideBand->height() - 101.0 * 0.5) < 101.0 * 0.15);
        QVERIFY(qAbs(sideBand->x() - 0.25 * 180.0) < tileWidth / 2);
    }
};

QTEST_MAIN(TestZonePreviewHighlight)
#include "test_zone_preview_highlight.moc"
