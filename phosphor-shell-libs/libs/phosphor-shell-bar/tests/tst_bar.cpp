// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// QtQuickTest runner for the Phosphor.Bar framework. Cases live in the
// tst_*.qml files alongside; the source dir is passed via -input from
// add_test, and the offscreen QPA platform keeps the run headless.
//
// The setup object registers the Phosphor.Shell types. BarHost is a
// PanelWindow, and those types are registered imperatively (Qt's registry is
// process-global, so they cannot come from a declarative QML module the way
// Phosphor.Bar's own types do). Without this the pane-state cases fail to
// compile with "PanelWindow is not a type". Nothing is shown: the pane
// properties under test are plain QML state.

#include <PhosphorShell/QmlRegistration.h>

#include <QObject>
#include <QQmlEngine>
#include <QtQuickTest/quicktest.h>

class Setup : public QObject
{
    Q_OBJECT

public Q_SLOTS:
    void qmlEngineAvailable(QQmlEngine*)
    {
        PhosphorShell::registerQmlTypes();
    }
};

QUICK_TEST_MAIN_WITH_SETUP(phosphor_shell_bar, Setup)

#include "tst_bar.moc"
