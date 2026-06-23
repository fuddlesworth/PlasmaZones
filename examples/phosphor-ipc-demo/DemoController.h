// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <PhosphorIpc/IpcRouter.h>

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QtCore/qtclasshelpermacros.h>

#include <memory>

namespace PhosphorIpcDemo {

// Demo glue. Owns the IpcRouter, surfaces its socket-path + a
// human-readable status string for the QML status panel. The
// router itself is owned via unique_ptr so we can control its
// teardown order relative to the QQmlEngine in main().
class DemoController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString socketPath READ socketPath NOTIFY socketPathChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    // Live event log, newest first. Each IpcTarget in Main.qml
    // calls recordEvent() alongside its emitEvent() so the demo
    // window shows events flowing as they're broadcast. Bounded to
    // the most recent 20 entries so a long phosphorctl call session
    // doesn't grow the property unbounded.
    Q_PROPERTY(QStringList eventLog READ eventLog NOTIFY eventLogChanged)
public:
    explicit DemoController(QObject* parent = nullptr);
    ~DemoController() override;
    Q_DISABLE_COPY_MOVE(DemoController)

    // Start the router on the given socket path. Empty path falls
    // back to PhosphorIpc::IpcRouter's XDG default. Returns true
    // on success, the demo's main() shows the resolved path in
    // the window title regardless.
    bool start(const QString& socketPath);

    [[nodiscard]] QString socketPath() const;
    [[nodiscard]] QString status() const;
    [[nodiscard]] QStringList eventLog() const;

    // Exposed for IpcEngine::install in main().
    [[nodiscard]] PhosphorIpc::IpcRouter* router() const;

    // Append "<target>.<signal>([args])" to the eventLog. Called
    // from QML alongside IpcTarget.emitEvent so the demo window
    // visualises the same event stream wire subscribers receive,
    // removing the need to open a separate `phosphorctl subscribe`
    // terminal to see what subscribe delivers.
    Q_INVOKABLE void recordEvent(const QString& targetName, const QString& signalName, const QVariantList& args);

Q_SIGNALS:
    void socketPathChanged();
    void statusChanged();
    void eventLogChanged();

private:
    void onTargetRegistered(const QString& name);
    void onTargetUnregistered(const QString& name);
    // Rebuild m_status from the router's live registry. Called from
    // start() and from the targetRegistered / targetUnregistered
    // slots; consolidates the format string so the steady-state
    // status never carries stale "registered/unregistered" suffixes.
    void refreshStatus();

    std::unique_ptr<PhosphorIpc::IpcRouter> m_router;
    QString m_status;
    QStringList m_eventLog;
};

} // namespace PhosphorIpcDemo
