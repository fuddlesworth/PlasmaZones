// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "cli_wait.h"

#include "cli_io.h"

#include <PhosphorServicePipeWire/PipeWireConnection.h>
#include <PhosphorServicePipeWire/PwNode.h>

#include <QElapsedTimer>
#include <QEventLoop>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTextStream>
#include <QTimer>

namespace PhosphorPipeWireCli {

bool waitForConnect(PhosphorServicePipeWire::PipeWireConnection& conn, int timeoutMs)
{
    if (conn.isConnected())
        return true;
    QEventLoop loop;
    QObject::connect(&conn, &PhosphorServicePipeWire::PipeWireConnection::connectedChanged, &loop, [&loop, &conn]() {
        if (conn.isConnected())
            loop.quit();
    });
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    loop.exec();
    return conn.isConnected();
}

void settleRegistry(int extraMs)
{
    QEventLoop loop;
    QTimer::singleShot(extraMs, &loop, &QEventLoop::quit);
    loop.exec();
}

bool waitForPropsEchoAndVerify(PhosphorServicePipeWire::PwNode* node, const std::function<bool()>& predicate,
                               int timeoutMs, const char* label)
{
    QPointer<PhosphorServicePipeWire::PwNode> guard(node);
    if (!guard) {
        err() << label << ": node removed before wait started\n";
        return false;
    }
    if (predicate())
        return true;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (!guard)
            break;
        QEventLoop loop;
        const int remaining = timeoutMs - static_cast<int>(timer.elapsed());
        if (remaining <= 0)
            break;
        // Receiver is &loop (not guard.data()) so the connection auto-disconnects
        // when the loop is destroyed at scope exit; Qt no-ops a disconnect on a
        // dead sender, so a daemon-side node removal mid-wait is also safe.
        auto qtConn = QObject::connect(guard.data(), &PhosphorServicePipeWire::PwNode::propsChanged, &loop, [&loop]() {
            loop.quit();
        });
        QTimer::singleShot(remaining, &loop, &QEventLoop::quit);
        loop.exec();
        QObject::disconnect(qtConn);
        if (!guard)
            break;
        if (predicate())
            return true;
        // Unrelated emission (or spurious wake): fall through and
        // re-arm the wait for any remaining budget.
    }
    // Final recheck: a propsChanged that arrived during the disconnect
    // / loop-exit window would otherwise be missed, producing a spurious
    // timeout even though the daemon committed the value.
    if (guard && predicate())
        return true;
    if (!guard)
        err() << label << ": node removed during wait\n";
    else
        err() << label << ": timed out waiting for propsChanged echo (" << timeoutMs << "ms)\n";
    return false;
}

bool waitForString(PhosphorServicePipeWire::PipeWireConnection& conn,
                   void (PhosphorServicePipeWire::PipeWireConnection::*signal)(),
                   const std::function<QString()>& accessor, const QString& expected, int timeoutMs, const char* label)
{
    if (accessor() == expected)
        return true;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QEventLoop loop;
        const int remaining = timeoutMs - static_cast<int>(timer.elapsed());
        if (remaining <= 0)
            break;
        auto qtConn = QObject::connect(&conn, signal, &loop, [&loop]() {
            loop.quit();
        });
        QTimer::singleShot(remaining, &loop, &QEventLoop::quit);
        loop.exec();
        QObject::disconnect(qtConn);
        if (accessor() == expected)
            return true;
        // Unrelated emission: fall through and re-arm for any remaining
        // budget.
    }
    // Final recheck mirroring waitForPropsEchoAndVerify: a change
    // signal that landed during the disconnect / loop-exit window
    // would otherwise miss this helper too.
    if (accessor() == expected)
        return true;
    // Helper-level timeout diagnostic so this helper self-reports
    // consistently with waitForPropsEchoAndVerify. cmdSetDefault adds
    // a richer caller-level diagnostic (no-WirePlumber vs. slow-echo);
    // this line provides per-helper visibility.
    err() << label << ": timed out waiting for value '" << expected << "' (" << timeoutMs << "ms)\n";
    return false;
}

} // namespace PhosphorPipeWireCli
