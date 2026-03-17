// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#define TRANSLATION_DOMAIN "plasmazones-shader-editor"

#include "shadereditorwindow.h"
#include "../core/constants.h"
#include "../daemon/rendering/zoneshaderitem.h"
#include "version.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QIcon>
#include <QQuickStyle>
#include <QtQml/qqml.h>

#include <KAboutData>
#include <KLocalizedString>

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

    QApplication app(argc, argv);

    KLocalizedString::setApplicationDomain("plasmazones-shader-editor");

    KAboutData aboutData(QStringLiteral("plasmazones-shader-editor"),
                         i18n("PlasmaZones Shader Editor"),
                         PlasmaZones::VERSION_STRING,
                         i18n("Shader effect editor for PlasmaZones"),
                         KAboutLicense::GPL_V3,
                         i18n("(c) 2026 fuddlesworth"));
    aboutData.addAuthor(i18n("fuddlesworth"));
    aboutData.setDesktopFileName(QStringLiteral("org.plasmazones.shader-editor"));
    KAboutData::setApplicationData(aboutData);

    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("plasmazones-shader-editor")));

    // Command line options
    QCommandLineParser parser;
    aboutData.setupCommandLine(&parser);

    QCommandLineOption shaderIdOption(QStringList{QStringLiteral("S"), QStringLiteral("shader")},
                                      i18n("Open existing shader by ID"),
                                      QStringLiteral("id"));
    QCommandLineOption pathOption(QStringLiteral("path"),
                                  i18n("Open shader package from directory path"),
                                  QStringLiteral("dir"));
    QCommandLineOption newOption(QStringList{QStringLiteral("n"), QStringLiteral("new")},
                                 i18n("Create new shader from template"));

    parser.addOptions({shaderIdOption, pathOption, newOption});
    parser.process(app);
    aboutData.processCommandLine(&parser);

    // Warn about mutually exclusive flags
    int modeCount = (parser.isSet(shaderIdOption) ? 1 : 0)
                  + (parser.isSet(pathOption) ? 1 : 0)
                  + (parser.isSet(newOption) ? 1 : 0);
    if (modeCount > 1) {
        qWarning() << "--shader, --path, and --new are mutually exclusive; --shader takes precedence, then --path";
    }

    // Set QML style for the embedded preview widget
    QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));

    // Register ZoneShaderItem QML type (used by the live preview pane)
    qmlRegisterType<PlasmaZones::ZoneShaderItem>("PlasmaZones", 1, 0, "ZoneShaderItem");

    ShaderEditorWindow window;
    if (!window.isValid()) {
        qCritical() << "Failed to initialize KTextEditor";
        return 1;
    }

    if (parser.isSet(shaderIdOption)) {
        window.openShaderById(parser.value(shaderIdOption));
    } else if (parser.isSet(pathOption)) {
        window.openShaderPackage(parser.value(pathOption));
    } else if (parser.isSet(newOption)) {
        window.newShaderPackage();
    }

    window.show();

    return app.exec();
}
