// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// PanelWindow::visibleBand — the rect a panel surface's input region is
// masked to.
//
// A panel's wl_surface is `thickness + shadowSize` deep, but only
// `thickness` of it is painted; the rest exists so a drop shadow has
// somewhere to render. It is transparent and, without an input region,
// still clickable. Because the exclusiveZone advertised to the compositor
// is `thickness`, other windows are placed exactly where that strip
// overlaps them, so every click near the edge of the window under the
// panel is swallowed.
//
// Applying the region needs a live Wayland surface, but choosing the rect
// is pure arithmetic, and it is the half that can be silently wrong: an
// inverted Bottom/Right offset produces a region that is the same SIZE as
// the correct one and masks precisely the wrong half.

#include <PhosphorShell/PanelWindow.h>

#include <QRect>
#include <QSize>
#include <QtTest/QtTest>

using PhosphorShell::PanelWindow;

class TestPanelWindowInputRegion : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    /// A top panel's painted band sits flush against the top of the
    /// surface, and the shadow strip below it is excluded.
    void topBandExcludesTheShadowStripBelowIt()
    {
        // 52px visible + 16px shadow on a 1920-wide output.
        const QRect band = PanelWindow::visibleBand(PanelWindow::Top, 52, QSize(1920, 68));
        QCOMPARE(band, QRect(0, 0, 1920, 52));
        // The excluded strip is the bottom 16px — the part that overlaps
        // the window beneath.
        QCOMPARE(band.bottom() + 1, 52);
    }

    /// A bottom panel is the mirror image: the band is flush against the
    /// BOTTOM and the shadow strip is above it. Getting this backwards
    /// yields a correctly-sized rect covering exactly the wrong pixels,
    /// which is why it is asserted separately rather than by symmetry.
    void bottomBandIsAnchoredToTheBottomEdge()
    {
        const QRect band = PanelWindow::visibleBand(PanelWindow::Bottom, 52, QSize(1920, 68));
        QCOMPARE(band, QRect(0, 16, 1920, 52));
        QCOMPARE(band.top(), 16);
        QCOMPARE(band.bottom() + 1, 68);
    }

    /// Vertical panels swap which axis the thickness applies to.
    void leftBandIsAnchoredToTheLeftEdge()
    {
        const QRect band = PanelWindow::visibleBand(PanelWindow::Left, 52, QSize(68, 1080));
        QCOMPARE(band, QRect(0, 0, 52, 1080));
    }

    void rightBandIsAnchoredToTheRightEdge()
    {
        const QRect band = PanelWindow::visibleBand(PanelWindow::Right, 52, QSize(68, 1080));
        QCOMPARE(band, QRect(16, 0, 52, 1080));
        QCOMPARE(band.right() + 1, 68);
    }

    /// With no shadow the band is the whole surface, which is what "no
    /// mask" already means. The region must still not overhang.
    void aPanelWithNoShadowCoversItsWholeSurface()
    {
        QCOMPARE(PanelWindow::visibleBand(PanelWindow::Top, 52, QSize(1920, 52)), QRect(0, 0, 1920, 52));
        QCOMPARE(PanelWindow::visibleBand(PanelWindow::Bottom, 52, QSize(1920, 52)), QRect(0, 0, 1920, 52));
    }

    /// A thickness larger than the surface must clamp rather than emit a
    /// region the compositor has to clip. This is reachable transiently:
    /// the window is configured by the compositor and can be momentarily
    /// smaller than the requested thickness.
    void anOversizedThicknessClampsToTheSurface()
    {
        const QRect top = PanelWindow::visibleBand(PanelWindow::Top, 200, QSize(1920, 68));
        QCOMPARE(top, QRect(0, 0, 1920, 68));

        const QRect bottom = PanelWindow::visibleBand(PanelWindow::Bottom, 200, QSize(1920, 68));
        QCOMPARE(bottom, QRect(0, 0, 1920, 68));

        const QRect right = PanelWindow::visibleBand(PanelWindow::Right, 200, QSize(68, 1080));
        QCOMPARE(right, QRect(0, 0, 68, 1080));
    }

    /// Degenerate inputs yield a null rect, which the caller reads as
    /// "apply nothing". Returning a zero-area rect instead would mask the
    /// surface to nothing and make the panel completely unclickable —
    /// strictly worse than the bug being fixed.
    void degenerateInputsProduceNoRegion()
    {
        QVERIFY(PanelWindow::visibleBand(PanelWindow::Top, 0, QSize(1920, 68)).isNull());
        QVERIFY(PanelWindow::visibleBand(PanelWindow::Top, -5, QSize(1920, 68)).isNull());
        QVERIFY(PanelWindow::visibleBand(PanelWindow::Top, 52, QSize(0, 68)).isNull());
        QVERIFY(PanelWindow::visibleBand(PanelWindow::Top, 52, QSize(1920, 0)).isNull());
        QVERIFY(PanelWindow::visibleBand(PanelWindow::Top, 52, QSize()).isNull());
    }

    /// The band always lies within the surface, for every edge and a
    /// spread of thicknesses. This is the invariant the individual cases
    /// above are specific instances of.
    void theBandNeverEscapesTheSurface()
    {
        const QSize horizontal(1920, 68);
        const QSize vertical(68, 1080);
        const QRect hBounds(QPoint(0, 0), horizontal);
        const QRect vBounds(QPoint(0, 0), vertical);

        for (int thickness : {1, 17, 52, 67, 68, 69, 500}) {
            for (auto edge : {PanelWindow::Top, PanelWindow::Bottom}) {
                const QRect band = PanelWindow::visibleBand(edge, thickness, horizontal);
                QVERIFY2(hBounds.contains(band),
                         qPrintable(QStringLiteral("edge %1 thickness %2 escaped the surface")
                                        .arg(static_cast<int>(edge))
                                        .arg(thickness)));
            }
            for (auto edge : {PanelWindow::Left, PanelWindow::Right}) {
                const QRect band = PanelWindow::visibleBand(edge, thickness, vertical);
                QVERIFY2(vBounds.contains(band),
                         qPrintable(QStringLiteral("edge %1 thickness %2 escaped the surface")
                                        .arg(static_cast<int>(edge))
                                        .arg(thickness)));
            }
        }
    }
};

QTEST_MAIN(TestPanelWindowInputRegion)

#include "test_panelwindow_inputregion.moc"
