// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Pins DaemonGeometryResolver::snapBorderInset() == 0 in ALL cases.
//
// The KWin effect's border shader recolours the window's OWN outermost band
// (INSIDE the frame), for decorated and borderless windows alike, so the border
// never extends past the frame edge into the neighbour. A snapped window
// therefore fills its zone exactly and needs NO inset — any visible separation
// between tiles must come from a zone gap/padding setting, not a border-width
// inset (which previously assumed the border was drawn OUTSIDE the frame and
// added a spurious 2x-border-width gap between tiles).

#include <QtTest/QtTest>

#include "../../../src/core/daemongeometryresolver.h"
#include "../helpers/StubSettings.h"

using namespace PlasmaZones;

class TestDaemonGeometryResolverInset : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void nullSettings_zero()
    {
        DaemonGeometryResolver resolver(nullptr);
        QCOMPARE(resolver.snapBorderInset(), 0);
    }

    void showBorderOff_zero()
    {
        StubSettings settings;
        settings.setSnappingShowBorder(false);
        settings.setSnappingBorderWidth(4);
        DaemonGeometryResolver resolver(&settings);
        QCOMPARE(resolver.snapBorderInset(), 0);
    }

    void decoratedShowBorder_stillZero()
    {
        // Decorated, show-border on, non-zero width: the border is drawn INSIDE
        // the window by the shader, so the window still fills its zone — no inset.
        StubSettings settings;
        settings.setSnappingShowBorder(true);
        settings.setSnappingHideTitleBars(false);
        settings.setSnappingBorderWidth(8);
        DaemonGeometryResolver resolver(&settings);
        QCOMPARE(resolver.snapBorderInset(), 0);
    }

    void borderless_zero()
    {
        StubSettings settings;
        settings.setSnappingShowBorder(true);
        settings.setSnappingHideTitleBars(true);
        settings.setSnappingBorderWidth(4);
        DaemonGeometryResolver resolver(&settings);
        QCOMPARE(resolver.snapBorderInset(), 0);
    }
};

QTEST_MAIN(TestDaemonGeometryResolverInset)
#include "test_daemongeometryresolver_inset.moc"
