// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Contract tests for the Toplevels singleton facade, focused on
// `activeToplevel`.
//
// These run under the offscreen QPA with no Wayland display, so
// `zwlr_foreign_toplevel_manager_v1` is never advertised and the shared
// ForeignToplevelManager is inert by construction (it warns and returns
// early). That is not a degenerate case to work around — it is exactly
// the state the shell's own test environment and any non-wlroots
// compositor present, and the bar's FocusedApp widget binds straight to
// `activeToplevel`. What must hold is that the accessor reads null and
// stays quiet rather than crashing or churning bindings.

#include <PhosphorShell/Toplevels.h>

#include <QAbstractListModel>
#include <QSignalSpy>
#include <QtTest/QtTest>

using PhosphorShell::Toplevels;

class TestToplevels : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    /// With no compositor support the accessor must read null. A widget
    /// binding `Toplevels.activeToplevel.title` would otherwise
    /// dereference whatever the scan left behind.
    void activeToplevelIsNullWithoutCompositorSupport()
    {
        Toplevels toplevels;
        QCOMPARE(toplevels.isSupported(), false);
        QCOMPARE(toplevels.toplevels().size(), 0);
        QCOMPARE(toplevels.activeToplevel(), nullptr);
    }

    /// Construction is silent when there is no compositor to report.
    ///
    /// This does NOT pin the change-gate inside recomputeActive, and the
    /// distinction matters: with no manager, watchToplevel is never called
    /// and the toplevelRemoved wire is never connected, so recomputeActive
    /// is unreachable and the gate is not executed at all. What is pinned
    /// is narrower and still worth having — a shell on a compositor without
    /// the protocol must not emit a property change during startup, which
    /// would wake every binding on `activeToplevel` for no reason.
    /// Exercising the gate itself needs an injectable toplevel source,
    /// which ForeignToplevelManager does not offer.
    void constructionIsSilentWithoutCompositorSupport()
    {
        Toplevels toplevels;
        QSignalSpy activeSpy(&toplevels, &Toplevels::activeToplevelChanged);
        QVERIFY(activeSpy.isValid());

        // Drain anything the constructor might have posted.
        QTest::qWait(50);

        QCOMPARE(activeSpy.count(), 0);
        QCOMPARE(toplevels.activeToplevel(), nullptr);
    }

    /// The wrapper is per-engine while the ForeignToplevelManager is
    /// process-wide, so a hot reload builds a fresh Toplevels against the
    /// same manager. Each generation subscribes to the manager's signals
    /// and to every existing toplevel; a subscription that outlived its
    /// wrapper would fire into a destroyed object on the next event.
    void repeatedConstructionIsSafe()
    {
        for (int generation = 0; generation < 3; ++generation) {
            Toplevels toplevels;
            QCOMPARE(toplevels.activeToplevel(), nullptr);
            QVERIFY(toplevels.model() != nullptr);
        }
        // Give any connection that survived its wrapper a chance to fire
        // into freed memory before the test ends.
        QTest::qWait(50);
    }

    /// The model is always constructed, even with no manager, so QML can
    /// bind `Repeater { model: Toplevels.model }` unconditionally.
    void modelExistsAndIsEmpty()
    {
        Toplevels toplevels;
        auto* model = toplevels.model();
        QVERIFY(model);
        QCOMPARE(model->rowCount(), 0);
    }
};

QTEST_MAIN(TestToplevels)

#include "test_toplevels.moc"
