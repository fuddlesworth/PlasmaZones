// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "EditorController.h"
#include "services/VirtualScreenService.h"
#include "../core/constants.h"
#include "../core/logging.h"
#include "../core/translationloader.h"
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

#include "pz_i18n.h"
#include "pz_qml_i18n.h"
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
    PlasmaZones::loadTranslations(&app);

    app.setApplicationName(QStringLiteral("plasmazones-editor"));
    app.setApplicationVersion(PlasmaZones::VERSION_STRING);
    app.setOrganizationName(QStringLiteral("plasmazones"));
    app.setOrganizationDomain(QStringLiteral("org.plasmazones"));
    app.setDesktopFileName(QStringLiteral("org.plasmazones.editor"));

    // Command line options
    QCommandLineParser parser;
    parser.setApplicationDescription(PzI18n::tr("Visual layout editor for PlasmaZones"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption layoutIdOption(QStringList{QStringLiteral("l"), QStringLiteral("layout")},
                                      PzI18n::tr("Layout ID to edit"), QStringLiteral("uuid"));
    QCommandLineOption screenOption(QStringList{QStringLiteral("s"), QStringLiteral("screen")},
                                    PzI18n::tr("Target screen name"), QStringLiteral("name"));
    QCommandLineOption newLayoutOption(QStringList{QStringLiteral("n"), QStringLiteral("new")},
                                       PzI18n::tr("Create new layout"));
    QCommandLineOption previewOption(QStringLiteral("preview"), PzI18n::tr("Open in read-only preview mode"));

    parser.addOptions({layoutIdOption, screenOption, newLayoutOption, previewOption});
    parser.process(app);

    // Use platform style if available, fall back to Fusion for non-KDE environments
    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE")) {
        const QString desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP").toLower();
        if (desktop.contains(QLatin1String("kde")) || desktop.contains(QLatin1String("plasma"))) {
            QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));
        } else {
            QQuickStyle::setStyle(QStringLiteral("Fusion"));
        }
    }

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

    // Set up i18n for QML (makes i18n() available in QML)
    auto* localizedContext = new PzLocalizedContext(&engine);
    engine.rootContext()->setContextObject(localizedContext);

    // Expose controller to QML
    engine.rootContext()->setContextProperty(QStringLiteral("editorController"), &controller);

    // Expose virtual screen service to QML
    PlasmaZones::VirtualScreenService virtualScreenService;
    engine.rootContext()->setContextProperty(QStringLiteral("virtualScreenService"), &virtualScreenService);

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
