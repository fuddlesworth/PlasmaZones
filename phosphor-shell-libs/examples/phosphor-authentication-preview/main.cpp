// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <PhosphorIpc/IpcEngine.h>
#include <PhosphorIpc/IpcRouter.h>

#include <QCommandLineParser>
#include <QDir>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Phosphor"));
    QCoreApplication::setApplicationName(QStringLiteral("phosphor-authentication-preview"));
    app.setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Preview-only authentication fixture. No Polkit or PAM access."));
    parser.addHelpOption();
    parser.addOption({QStringLiteral("socket"), QStringLiteral("Preview IPC socket."), QStringLiteral("path")});
    parser.addOption({QStringLiteral("example"), QStringLiteral("file, software, accounts, code, background, or long."),
                      QStringLiteral("name"), QStringLiteral("file")});
    parser.addOption({QStringLiteral("state"),
                      QStringLiteral("ready, error, checking, success, cancel, or unavailable."),
                      QStringLiteral("name"), QStringLiteral("ready")});
    parser.addOption({QStringLiteral("windowed"), QStringLiteral("Open a resizable window instead of fullscreen.")});
    parser.process(app);

    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QQuickWindow::setDefaultAlphaBuffer(true);
    if (QIcon::fallbackThemeName().isEmpty()) {
        QIcon::setFallbackThemeName(QStringLiteral("breeze"));
    }

    QString socketPath = parser.value(QStringLiteral("socket"));
    if (socketPath.isEmpty()) {
        socketPath = qEnvironmentVariable("PHOSPHOR_SOCKET");
    }
    if (socketPath.isEmpty()) {
        socketPath = QDir(QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation))
                         .filePath(QStringLiteral("phosphor-authentication-preview.sock"));
    }
    PhosphorIpc::IpcRouter router;
    if (!router.start(socketPath)) {
        qWarning("Authentication preview could not start its IPC socket.");
        return 1;
    }

    QQmlApplicationEngine engine;
    PhosphorIpc::IpcEngine::install(&engine, &router);
    engine.setInitialProperties({{QStringLiteral("initialExample"), parser.value(QStringLiteral("example"))},
                                 {QStringLiteral("initialState"), parser.value(QStringLiteral("state"))},
                                 {QStringLiteral("windowed"), parser.isSet(QStringLiteral("windowed"))}});
    engine.loadFromModule(QStringLiteral("Phosphor.AuthenticationPreview"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }
    return app.exec();
}
