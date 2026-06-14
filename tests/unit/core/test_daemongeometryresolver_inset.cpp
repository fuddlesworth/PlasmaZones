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

    void showBorderOn_titleBarsShown_insetEqualsBorderWidth()
    {
        StubSettings settings;
        settings.setSnappingShowBorder(true);
        settings.setSnappingHideTitleBars(false); // decorated mode → inset
        settings.setSnappingBorderWidth(3);
        DaemonGeometryResolver resolver(&settings);
        QCOMPARE(resolver.snapBorderInset(), 3);
    }

    void hideTitleBarsOn_insetZero()
    {
        // Borderless mode: the window fills its zone and the border is recoloured
        // inside the content edge, so it must NOT be inset (insetting leaves an
        // empty gap = "too small"). Width is non-zero, border is shown, but the
        // title-bar gate collapses the inset.
        StubSettings settings;
        settings.setSnappingShowBorder(true);
        settings.setSnappingHideTitleBars(true);
        settings.setSnappingBorderWidth(4);
        DaemonGeometryResolver resolver(&settings);
        QCOMPARE(resolver.snapBorderInset(), 0);
    }

    void showBorderOn_tracksLiveWidthChange()
    {
        // The resolver reads settings live (no snapshot) — the same property
        // that, once loaded on login, must flip a freshly restored window from
        // un-inset to inset without reconstructing the resolver.
        StubSettings settings;
        settings.setSnappingShowBorder(true);
        settings.setSnappingHideTitleBars(false); // decorated mode → inset
        settings.setSnappingBorderWidth(2);
        DaemonGeometryResolver resolver(&settings);
        QCOMPARE(resolver.snapBorderInset(), 2);

        settings.setSnappingBorderWidth(5);
        QCOMPARE(resolver.snapBorderInset(), 5);

        // Flipping title bars off (borderless) collapses the inset back to 0 even
        // with a non-zero width still configured — the decorated/borderless gate.
        settings.setSnappingHideTitleBars(true);
        QCOMPARE(resolver.snapBorderInset(), 0);

        // And flipping show-border off also collapses it, independent of mode.
        settings.setSnappingHideTitleBars(false);
        settings.setSnappingShowBorder(false);
        QCOMPARE(resolver.snapBorderInset(), 0);
    }

    void negativeWidth_clampsToZero()
    {
        StubSettings settings;
        settings.setSnappingShowBorder(true);
        // Decorated mode so the show-border + hide-title-bars gates both pass and
        // the negative width reaches the qMax(0, ...) clamp under test — without
        // this the StubSettings default (hideTitleBars=true) would return 0 at the
        // borderless gate, passing the test for the wrong reason.
        settings.setSnappingHideTitleBars(false);
        settings.setSnappingBorderWidth(-7);
        DaemonGeometryResolver resolver(&settings);
        QCOMPARE(resolver.snapBorderInset(), 0);
    }
};

QTEST_MAIN(TestDaemonGeometryResolverInset)
#include "test_daemongeometryresolver_inset.moc"
