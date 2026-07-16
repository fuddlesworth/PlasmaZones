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

    /// `highlightAllZones` is the explicit opt-in for lighting every zone even
    /// though one is selected.
    void testHighlightAllZonesOptIn()
    {
        QVariantMap props;
        props[QStringLiteral("highlightAllZones")] = true;
        props[QStringLiteral("selectedZoneIndex")] = 2;

        const QList<bool> states = highlightStates(props);
        QCOMPARE(states, QList<bool>({true, true, true, true}));
    }

    /// Nothing selected and no card-level state: every zone stays dim.
    void testNeutralCardHighlightsNothing()
    {
        const QList<bool> states = highlightStates(QVariantMap());
        QCOMPARE(states, QList<bool>({false, false, false, false}));
    }
};

QTEST_MAIN(TestZonePreviewHighlight)
#include "test_zone_preview_highlight.moc"
