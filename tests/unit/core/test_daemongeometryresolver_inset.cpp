// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Pins DaemonGeometryResolver::snapBorderInset() — the settings-driven gate
// behind the per-window snap-border inset. This gate is the dependency the
// login startup-ordering bug exposed: a snapped window restored before the
// snap show-border state is observable resolves with inset 0 (full zone) and
// must be corrected once settings are loaded. The contract pinned here:
//
//   - null settings           → inset 0  (no settings → no border → no inset)
//   - show-border OFF          → inset 0  (border not drawn → nothing to inset)
//   - show-border ON           → inset == snappingBorderWidth()  (mirror the
//                                effect's per-mode border width exactly)
//   - negative width clamps to 0 (defensive; never a negative inset)

#include <QtTest/QtTest>

#include "../../../src/core/daemongeometryresolver.h"
#include "../helpers/StubSettings.h"

using namespace PlasmaZones;

class TestDaemonGeometryResolverInset : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void nullSettings_insetZero()
    {
        DaemonGeometryResolver resolver(nullptr);
        QCOMPARE(resolver.snapBorderInset(), 0);
    }

    void showBorderOff_insetZero()
    {
        StubSettings settings;
        settings.setSnappingShowBorder(false);
        settings.setSnappingBorderWidth(4);
        DaemonGeometryResolver resolver(&settings);
        // Width is non-zero, but the show-border gate is off → no inset.
        QCOMPARE(resolver.snapBorderInset(), 0);
    }

    void showBorderOn_insetEqualsBorderWidth()
    {
        StubSettings settings;
        settings.setSnappingShowBorder(true);
        settings.setSnappingBorderWidth(3);
        DaemonGeometryResolver resolver(&settings);
        QCOMPARE(resolver.snapBorderInset(), 3);
    }

    void showBorderOn_tracksLiveWidthChange()
    {
        // The resolver reads settings live (no snapshot) — the same property
        // that, once loaded on login, must flip a freshly restored window from
        // un-inset to inset without reconstructing the resolver.
        StubSettings settings;
        settings.setSnappingShowBorder(true);
        settings.setSnappingBorderWidth(2);
        DaemonGeometryResolver resolver(&settings);
        QCOMPARE(resolver.snapBorderInset(), 2);

        settings.setSnappingBorderWidth(5);
        QCOMPARE(resolver.snapBorderInset(), 5);

        // Flipping show-border off collapses the inset back to 0 even with a
        // non-zero width still configured — the exact transition the login
        // race produces in reverse.
        settings.setSnappingShowBorder(false);
        QCOMPARE(resolver.snapBorderInset(), 0);
    }

    void negativeWidth_clampsToZero()
    {
        StubSettings settings;
        settings.setSnappingShowBorder(true);
        settings.setSnappingBorderWidth(-7);
        DaemonGeometryResolver resolver(&settings);
        QCOMPARE(resolver.snapBorderInset(), 0);
    }
};

QTEST_MAIN(TestDaemonGeometryResolverInset)
#include "test_daemongeometryresolver_inset.moc"
