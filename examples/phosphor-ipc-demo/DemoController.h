// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <PhosphorIpc/IpcRouter.h>

#include <QObject>
#include <QString>
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
public:
    explicit DemoController(QObject* parent = nullptr);
    ~DemoController() override;
    Q_DISABLE_COPY_MOVE(DemoController)

    // Start the router on the given socket path. Empty path falls
    // back to PhosphorIpc::IpcRouter's XDG default. Returns true
    // on success — the demo's main() shows the resolved path in
    // the window title regardless.
    bool start(const QString& socketPath);

    [[nodiscard]] QString socketPath() const;
    [[nodiscard]] QString status() const;

    // Exposed for IpcEngine::install in main().
    [[nodiscard]] PhosphorIpc::IpcRouter* router() const;

Q_SIGNALS:
    void socketPathChanged();
    void statusChanged();

private:
    void onTargetRegistered(const QString& name);
    void onTargetUnregistered(const QString& name);

    std::unique_ptr<PhosphorIpc::IpcRouter> m_router;
    QString m_status;
};

} // namespace PhosphorIpcDemo
