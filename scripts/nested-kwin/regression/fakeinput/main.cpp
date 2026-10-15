// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// fakeinput: drive pointer and keyboard input in the nested KWin session
// through org_kde_kwin_fake_input, for live checks of interactive gestures.
// The compositor must run with KWIN_WAYLAND_NO_PERMISSION_CHECKS=1.
//
// Usage: WAYLAND_DISPLAY=<nested socket> fakeinput CMD...
//   abs X Y            pointer to an absolute compositor position
//   rel DX DY          pointer by a delta
//   path X1 Y1 X2 Y2 N MS   N absolute steps from (X1,Y1) to (X2,Y2), MS apart
//   down left|right|middle   button press
//   up left|right|middle     button release
//   keydown CODE       key press (linux/input-event-codes.h value)
//   keyup CODE         key release
//   wait MS            pause
// Commands run in order; the tool exits when the list is done.

#include <KWayland/Client/connection_thread.h>
#include <KWayland/Client/fakeinput.h>
#include <KWayland/Client/registry.h>

#include <QGuiApplication>
#include <QPointF>
#include <QSizeF>
#include <QStringList>
#include <QTimer>

#include <cstdio>
#include <functional>

using namespace KWayland::Client;

namespace {

quint32 buttonCode(const QString& name)
{
    if (name == QLatin1String("left")) {
        return 0x110; // BTN_LEFT
    }
    if (name == QLatin1String("right")) {
        return 0x111; // BTN_RIGHT
    }
    if (name == QLatin1String("middle")) {
        return 0x112; // BTN_MIDDLE
    }
    return name.toUInt();
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "wayland");
    QGuiApplication app(argc, argv);
    QStringList args = app.arguments();
    args.removeFirst();
    if (args.isEmpty()) {
        std::fprintf(stderr, "fakeinput: no commands\n");
        return 2;
    }

    ConnectionThread* connection = ConnectionThread::fromApplication(&app);
    if (!connection) {
        std::fprintf(stderr, "fakeinput: no wayland connection\n");
        return 1;
    }
    Registry registry;
    FakeInput* fake = nullptr;
    int exitCode = 0;

    // The command list, consumed front to back; each step schedules the next.
    std::function<void()> step;
    step = [&]() {
        connection->flush();
        if (args.isEmpty()) {
            QTimer::singleShot(100, &app, [&app]() {
                app.quit();
            });
            return;
        }
        const QString cmd = args.takeFirst();
        const auto num = [&args]() {
            return args.isEmpty() ? 0.0 : args.takeFirst().toDouble();
        };
        int delay = 15;
        if (cmd == QLatin1String("abs")) {
            const double x = num();
            const double y = num();
            fake->requestPointerMoveAbsolute(QPointF(x, y));
        } else if (cmd == QLatin1String("rel")) {
            const double dx = num();
            const double dy = num();
            fake->requestPointerMove(QSizeF(dx, dy));
        } else if (cmd == QLatin1String("path")) {
            const double x1 = num();
            const double y1 = num();
            const double x2 = num();
            const double y2 = num();
            const int n = qMax(1, int(num()));
            const int ms = int(num());
            QStringList expanded;
            for (int i = 1; i <= n; ++i) {
                const double t = double(i) / n;
                expanded << QStringLiteral("abs") << QString::number(x1 + (x2 - x1) * t)
                         << QString::number(y1 + (y2 - y1) * t) << QStringLiteral("wait") << QString::number(ms);
            }
            args = expanded + args;
            fake->requestPointerMoveAbsolute(QPointF(x1, y1));
        } else if (cmd == QLatin1String("down")) {
            fake->requestPointerButtonPress(buttonCode(args.isEmpty() ? QString() : args.takeFirst()));
        } else if (cmd == QLatin1String("up")) {
            fake->requestPointerButtonRelease(buttonCode(args.isEmpty() ? QString() : args.takeFirst()));
        } else if (cmd == QLatin1String("keydown")) {
            fake->requestKeyboardKeyPress(quint32(num()));
        } else if (cmd == QLatin1String("keyup")) {
            fake->requestKeyboardKeyRelease(quint32(num()));
        } else if (cmd == QLatin1String("wait")) {
            delay = int(num());
        } else {
            std::fprintf(stderr, "fakeinput: unknown command %s\n", qPrintable(cmd));
            exitCode = 2;
            app.quit();
            return;
        }
        connection->flush();
        QTimer::singleShot(delay, &app, step);
    };

    QObject::connect(&registry, &Registry::fakeInputAnnounced, &app, [&](quint32 name, quint32 version) {
        fake = registry.createFakeInput(name, version, &app);
        fake->authenticate(QStringLiteral("pz-fakeinput"), QStringLiteral("PlasmaZones live checks"));
        QTimer::singleShot(50, &app, step);
    });
    QObject::connect(&registry, &Registry::interfacesAnnounced, &app, [&]() {
        if (!fake) {
            std::fprintf(stderr, "fakeinput: org_kde_kwin_fake_input not offered (permission checks on?)\n");
            exitCode = 1;
            app.quit();
        }
    });
    registry.create(connection);
    registry.setup();
    connection->flush();
    QTimer::singleShot(20000, &app, [&]() {
        std::fprintf(stderr, "fakeinput: timed out\n");
        exitCode = 1;
        app.quit();
    });
    const int rc = app.exec();
    return exitCode ? exitCode : rc;
}
