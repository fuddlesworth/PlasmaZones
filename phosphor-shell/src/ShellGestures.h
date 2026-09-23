// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>

namespace PhosphorShellApp {

// The compositor's touchpad gestures, relayed by the daemon (A3, the
// per-surface "Gesture" rows). A layer-shell client never sees a
// three-finger swipe; the KWin effect registers the shell's gestures with
// the compositor's recognizer and reports each completed one over
// CompositorBridge.reportGesture, which the daemon re-emits as
// gestureReported. This object subscribes to that signal and exposes it
// to QML as the `ShellGestures` context property; shell.qml maps a
// gesture to a surface (three-finger swipe up opens the launcher).
//
// `service` is the daemon's bus name, parameterised so a test can stand
// up a fake daemon on a private bus.
class ShellGestures : public QObject
{
    Q_OBJECT

public:
    explicit ShellGestures(QObject* parent = nullptr);
    ShellGestures(const QString& service, const QString& objectPath, QObject* parent);

    /// The D-Bus signal this object subscribes to.
    static QString interfaceName();
    static QString signalName();

Q_SIGNALS:
    /// A completed swipe: `direction` is up, down, left or right.
    void swiped(const QString& direction, uint fingerCount);
    /// A completed pinch: `direction` is expanding or contracting.
    void pinched(const QString& direction, uint fingerCount);

private Q_SLOTS:
    void onGestureReported(const QString& kind, const QString& direction, uint fingerCount);
};

} // namespace PhosphorShellApp
