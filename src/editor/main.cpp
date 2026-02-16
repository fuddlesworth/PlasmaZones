// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#define TRANSLATION_DOMAIN "plasmazones-editor"

#include "EditorController.h"
#include "../core/constants.h"
#include "../core/logging.h"
#include "version.h"
#include "../daemon/rendering/zoneshaderitem.h"

#include <QFile>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QCommandLineParser>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QScreen>
#include <QCursor>
#include <QObject>

#include <KLocalizedString>
#include <KLocalizedContext>
#include <KAboutData>
#include <QtQml/qqml.h>

using namespace PlasmaZones;

int main(int argc, char* argv[])
{
    // Ensure D-Bus session bus is reachable when launched from CLI (e.g. IDE terminal)
    // where DBUS_SESSION_BUS_ADDRESS may be unset. Use systemd default path.
    if (qEnvironmentVariableIsEmpty("DBUS_SESSION_BUS_ADDRESS")) {
        const QString runtimeDir = qEnvironmentVariable("XDG_RUNTIME_DIR");
        if (!runtimeDir.isEmpty()) {
            const QString busPath = runtimeDir + QStringLiteral("/bus");
            if (QFile::exists(busPath)) {
                qputenv("DBUS_SESSION_BUS_ADDRESS", QByteArray("unix:path=" + busPath.toUtf8()));
            }
        }
    }

    QGuiApplication app(argc, argv);

    KLocalizedString::setApplicationDomain("plasmazones-editor");

    KAboutData aboutData(QStringLiteral("plasmazones-editor"), i18n("PlasmaZones Layout Editor"),
                         PlasmaZones::VERSION_STRING, i18n("Visual layout editor for PlasmaZones"), KAboutLicense::GPL_V3,
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
    QCommandLineOption previewOption(QStringLiteral("preview"),
                                     i18n("Open in read-only preview mode"));

    parser.addOptions({layoutIdOption, screenOption, newLayoutOption, previewOption});
    parser.process(app);
    aboutData.processCommandLine(&parser);

    // Use Fusion style for consistent look
    QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));

    // Register ZoneShaderItem for QML (shader preview in ShaderSettingsDialog)
    qmlRegisterType<PlasmaZones::ZoneShaderItem>("PlasmaZones", 1, 0, "ZoneShaderItem");

    // Create controller
    EditorController controller;

    // Handle command line arguments
    // Determine target screen first (but don't trigger layout loading yet)
    QString targetScreen;
    if (parser.isSet(screenOption)) {
        targetScreen = parser.value(screenOption);
    } else {
        // Default to the screen under the cursor — more intuitive than primaryScreen()
        // which can be unreliable on Wayland (Qt may not match KDE's configured primary)
        QScreen* cursorScreen = QGuiApplication::screenAt(QCursor::pos());
        if (!cursorScreen) {
            cursorScreen = QGuiApplication::primaryScreen();
        }
        if (cursorScreen) {
            targetScreen = cursorScreen->name();
        }
    }

    // Warn about mutually exclusive flags
    if (parser.isSet(previewOption) && parser.isSet(newLayoutOption)) {
        qWarning() << "--preview and --new are mutually exclusive; ignoring --preview";
    }

    // Handle layout loading based on mode
    if (parser.isSet(newLayoutOption)) {
        // Create new layout - set target screen first (without loading), then create new layout
        if (!targetScreen.isEmpty()) {
            controller.setTargetScreenDirect(targetScreen);
        }
        controller.createNewLayout();
    } else if (parser.isSet(layoutIdOption)) {
        QString layoutId = parser.value(layoutIdOption);
        // Auto-detect preview mode for autotile layouts, or explicit --preview flag
        if (parser.isSet(previewOption) || LayoutId::isAutotile(layoutId)) {
            controller.setPreviewMode(true);
        }
        // Edit/preview specific layout - load it, then set target screen (without reloading)
        controller.loadLayout(layoutId);
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

    // Load main QML (Window starts with visible:false — QML calls
    // editorController.showFullScreenOnTargetScreen() which sets screen from C++)
    engine.loadFromModule("org.plasmazones.editor", "EditorWindow");

    if (engine.rootObjects().isEmpty()) {
        qCCritical(PlasmaZones::lcEditor) << "Editor: Failed to load EditorWindow.qml";
        return -1;
    }

    return app.exec();
}
