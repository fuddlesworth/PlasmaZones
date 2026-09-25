// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// phosphor-widgets-kitchen-sink entry point.
//
// QGuiApplication + QQmlApplicationEngine. The demo is pure QML (the
// atoms and the Theme singleton carry all the state), so there is no
// controller to expose; main just loads Main.qml from the demo module.

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QString>

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Phosphor"));
    QCoreApplication::setApplicationName(QStringLiteral("phosphor-widgets-kitchen-sink"));

    // Same Basic-style choice the other Phosphor demos use: it keeps the
    // QtQuick.Controls chrome (ToolBar, ScrollView, Label) out of any
    // platform style's retint path so only our token-driven atoms drive
    // the colours.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QQmlApplicationEngine engine;
    engine.loadFromModule(QStringLiteral("Phosphor.WidgetsKitchenSink"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }
    return app.exec();
}
