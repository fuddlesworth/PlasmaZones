// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <QObject>
#include <QRectF>
#include <QtQml/qqmlregistration.h>
namespace PhosphorShellDashboard {
class DesktopStage : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
public:
    explicit DesktopStage(QObject* parent = nullptr);
    ~DesktopStage() override;
    bool active() const
    {
        return m_active;
    }
    Q_INVOKABLE void show(const QString& screen, const QRectF& normalizedRect, bool animate);
    Q_INVOKABLE void hide();
Q_SIGNALS:
    void activeChanged();

private:
    void setActive(bool active);
    const QString m_token;
    bool m_active = false;
    bool m_requested = false;
    int m_generation = 0;
};
}
