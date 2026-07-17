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

    void withSettings_stillZero()
    {
        // The border shader recolours the window's OWN outermost band INSIDE the
        // frame (window border/title-bar appearance is now resolved through the
        // window rules, not a per-mode ConfigDefaults), so a snapped window fills
        // its zone exactly and needs no inset — snapBorderInset() is 0 regardless.
        StubSettings settings;
        DaemonGeometryResolver resolver(&settings);
        QCOMPARE(resolver.snapBorderInset(), 0);
    }
};

QTEST_MAIN(TestDaemonGeometryResolverInset)
#include "test_daemongeometryresolver_inset.moc"
