// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#define TRANSLATION_DOMAIN "plasmazones-editor"

#include "EditorController.h"
#include "../core/logging.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QCommandLineParser>
#include <QQuickStyle>
#include <QScreen>
#include <QObject>

#include <KLocalizedString>
#include <KLocalizedContext>
#include <KAboutData>

using namespace PlasmaZones;

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);

    KLocalizedString::setApplicationDomain("plasmazones-editor");

    KAboutData aboutData(QStringLiteral("plasmazones-editor"), i18n("PlasmaZones Layout Editor"),
                         QStringLiteral("1.1.0"), i18n("Visual layout editor for PlasmaZones"), KAboutLicense::GPL_V3,
                         i18n("(c) 2026 fuddlesworth"));
    aboutData.addAuthor(i18n("fuddlesworth"));
    aboutData.setDesktopFileName(QStringLiteral("org.plasmazones.editor"));
    KAboutData::setApplicationData(aboutData);

    // Command line options
    QCommandLineParser parser;
    aboutData.setupCommandLine(&parser);

    QCommandLineOption layoutIdOption(QStringList{QStringLiteral("l"), QStringLiteral("layout")},
                                      i18n("Layout ID to edit"), QStringLiteral("uuid"));
    QCommandLineOption screenOption(QStringList{QStringLiteral("s"), QStringLiteral("screen")},
                                    i18n("Target screen name"), QStringLiteral("name"));
    QCommandLineOption newLayoutOption(QStringList{QStringLiteral("n"), QStringLiteral("new")},
                                       i18n("Create new layout"));

    parser.addOptions({layoutIdOption, screenOption, newLayoutOption});
    parser.process(app);
    aboutData.processCommandLine(&parser);

    // Use Fusion style for consistent look
    QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));

    // Create controller
    EditorController controller;

    // Handle command line arguments
    // Determine target screen first (but don't trigger layout loading yet)
    QString targetScreen;
    if (parser.isSet(screenOption)) {
        targetScreen = parser.value(screenOption);
    } else {
        // Default to primary screen if no screen specified
        QScreen* primaryScreen = QGuiApplication::primaryScreen();
        if (primaryScreen) {
            targetScreen = primaryScreen->name();
        }
    }

    // Handle layout loading based on mode
    if (parser.isSet(newLayoutOption)) {
        // Create new layout - set target screen first (without loading), then create new layout
        if (!targetScreen.isEmpty()) {
            controller.setTargetScreenDirect(targetScreen);
        }
        controller.createNewLayout();
    } else if (parser.isSet(layoutIdOption)) {
        // Edit specific layout - load it, then set target screen (without reloading)
        controller.loadLayout(parser.value(layoutIdOption));
        if (!targetScreen.isEmpty()) {
            controller.setTargetScreenDirect(targetScreen);
        }
    } else {
        // No layout specified - set target screen which will load the assigned layout
        // or create a new one if none assigned
        if (!targetScreen.isEmpty()) {
            controller.setTargetScreen(targetScreen);
        }
    }

    // Set up QML engine
    QQmlApplicationEngine engine;

    // Set up i18n for QML (this makes i18n() available in QML)
    // Note: KLocalizedContext is deprecated in KF6 6.8+ but works in earlier versions
    KLocalizedContext* localizedContext = new KLocalizedContext(&engine);
    engine.rootContext()->setContextObject(localizedContext);

    // Expose controller to QML
    engine.rootContext()->setContextProperty(QStringLiteral("editorController"), &controller);

    // Expose screen list to QML
    engine.rootContext()->setContextProperty(QStringLiteral("availableScreens"), QVariant::fromValue(app.screens()));

    // Load main QML
    engine.loadFromModule("org.plasmazones.editor", "EditorWindow");

    if (engine.rootObjects().isEmpty()) {
        qCCritical(PlasmaZones::lcEditor) << "Editor: Failed to load EditorWindow.qml";
        return -1;
    }

    return app.exec();
}
