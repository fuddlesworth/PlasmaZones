// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// QtQuickTest runner for the Phosphor.ControlCenter surface. Cases live
// in the tst_*.qml files alongside; the source dir is passed via -input
// from add_test, and the offscreen QPA platform keeps the run headless.

#include <PhosphorControl/LocalizedContext.h>
#include <QQmlContext>
#include <QQmlEngine>
#include <QtQuickTest/quicktest.h>

class Setup : public QObject
{
    Q_OBJECT
public Q_SLOTS:
    void qmlEngineAvailable(QQmlEngine* engine)
    {
        auto* context = new PhosphorControl::LocalizedContext(engine);
        context->setTranslationContext(QStringLiteral("phosphorshell"));
        engine->rootContext()->setContextObject(context);
    }
};
QUICK_TEST_MAIN_WITH_SETUP(phosphor_shell_control_center, Setup)
#include "tst_controlcenter.moc"
