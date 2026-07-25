// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Contract tests for the Workspaces singleton and its list model.
//
// These talk to the real session bus, so on a KWin desktop the
// compositor-present branch runs for real and on a headless runner the
// no-compositor branch does. Both are asserted; whichever did not apply
// reports a QSKIP so a run that exercised neither is visible rather than
// looking like a pass.
//
// Nothing here ever performs a real workspace switch. These tests may run
// on a developer's live session, and yanking the desktop out from under
// someone mid-run is not an acceptable side effect — so only the
// rejected-input paths of switchTo() are exercised.

#include <PhosphorShell/Workspaces.h>

#include <QAbstractListModel>
#include <QSignalSpy>
#include <QtTest/QtTest>

using PhosphorShell::WorkspaceListModel;
using PhosphorShell::Workspaces;

class TestWorkspaces : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    /// QML binds `Repeater { model: Workspaces.model }` unconditionally, so
    /// the model must exist even with no compositor behind it. The role
    /// names are the delegate's entire contract — a rename would break
    /// every `required property` silently, since QML reports a missing
    /// role as undefined rather than an error.
    void modelExistsAndRoleNamesArePinned()
    {
        Workspaces workspaces;
        auto* model = workspaces.model();
        QVERIFY(model);

        const QHash<int, QByteArray> roles = model->roleNames();
        QCOMPARE(roles.value(WorkspaceListModel::IdRole), QByteArray("workspaceId"));
        QCOMPARE(roles.value(WorkspaceListModel::NameRole), QByteArray("name"));
        QCOMPARE(roles.value(WorkspaceListModel::IsActiveRole), QByteArray("isActive"));
        QCOMPARE(roles.size(), 3);
    }

    /// With no workspace source the pager must collapse rather than show a
    /// single permanent pill. `supported` is what the widget gates on.
    void withoutACompositorEverythingIsEmpty()
    {
        Workspaces workspaces;
        if (workspaces.isSupported()) {
            QSKIP("a workspace source is present; the no-compositor branch is not exercised");
        }
        QCOMPARE(workspaces.count(), 0);
        QVERIFY(workspaces.activeId().isEmpty());
        QCOMPARE(workspaces.model()->rowCount(), 0);
    }

    /// The real thing: the model must describe the compositor's actual
    /// workspaces, and exactly one of them must be active. A pager showing
    /// zero or two highlighted pills is the visible failure.
    void withKWinTheModelMatchesTheCompositor()
    {
        Workspaces workspaces;
        if (!workspaces.isSupported()) {
            QSKIP("no workspace source present; the compositor branch is not exercised");
        }

        // The manager fetches the desktop list ASYNCHRONOUSLY (that is what
        // start()-before-init() buys — no blocking D-Bus call on the GUI
        // thread at startup), so the reply has not landed the instant the
        // singleton is constructed.
        QTRY_VERIFY_WITH_TIMEOUT(workspaces.count() > 0, 3000);

        auto* model = workspaces.model();
        QVERIFY(model);
        QCOMPARE(model->rowCount(), workspaces.count());
        QVERIFY2(workspaces.count() > 0, "a supported workspace source reported no workspaces");

        int activeRows = 0;
        QStringList ids;
        for (int row = 0; row < model->rowCount(); ++row) {
            const QModelIndex idx = model->index(row, 0);
            const QString id = model->data(idx, WorkspaceListModel::IdRole).toString();
            QVERIFY2(!id.isEmpty(), "a workspace row has no id; switchTo could never address it");
            ids << id;
            if (model->data(idx, WorkspaceListModel::IsActiveRole).toBool()) {
                ++activeRows;
            }
        }

        // Ids are the addressing scheme, so duplicates would make switchTo
        // ambiguous.
        QCOMPARE(ids.size(), QSet<QString>(ids.cbegin(), ids.cend()).size());
        QCOMPARE(activeRows, 1);
        QVERIFY2(ids.contains(workspaces.activeId()), "activeId is not one of the model's rows");

        // activeIndex is 1-based and must agree with the flagged row.
        QVERIFY(workspaces.activeIndex() >= 1);
        QVERIFY(workspaces.activeIndex() <= workspaces.count());
        QCOMPARE(ids.at(workspaces.activeIndex() - 1), workspaces.activeId());
    }

    /// An unknown id must never reach the compositor. Workspaces::switchTo
    /// forwards unconditionally; the rejection lives one layer down in
    /// VirtualDesktopManager::setCurrentDesktopById, which validates against
    /// the known id list before writing. Writing an unrecognised UUID would
    /// either be rejected by KWin or switch to something this side has no
    /// record of, leaving activeId stale.
    ///
    /// This is also what makes the test safe to run on a live session: it
    /// cannot move a developer's actual desktop.
    void switchToAnUnknownIdIsIgnored()
    {
        Workspaces workspaces;
        // Let the async list fetch settle first: reading activeId before it
        // lands would capture an empty baseline that the reply then changes,
        // and this test would blame switchTo for it.
        if (workspaces.isSupported()) {
            QTRY_VERIFY_WITH_TIMEOUT(workspaces.count() > 0, 3000);
        }
        const QString before = workspaces.activeId();

        workspaces.switchTo(QString());
        workspaces.switchTo(QStringLiteral("not-a-real-workspace-uuid"));
        // A real switch would come back asynchronously over D-Bus, so give
        // it a window to land before concluding nothing happened.
        QTest::qWait(200);

        QCOMPARE(workspaces.activeId(), before);
    }

    /// The wrapper is per-engine over a process-wide manager, so a hot
    /// reload builds a fresh one against the same subscriptions.
    void repeatedConstructionIsSafe()
    {
        for (int generation = 0; generation < 3; ++generation) {
            Workspaces workspaces;
            QVERIFY(workspaces.model());
        }
        QTest::qWait(50);
    }
};

QTEST_MAIN(TestWorkspaces)

#include "test_workspaces.moc"
