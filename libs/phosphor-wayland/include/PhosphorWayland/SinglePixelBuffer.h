// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorwayland_export.h>

#include <QColor>
#include <QObject>

#include <memory>

QT_BEGIN_NAMESPACE
class QWindow;
QT_END_NAMESPACE

namespace PhosphorWayland {

class PHOSPHORWAYLAND_EXPORT SinglePixelBuffer : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)

public:
    explicit SinglePixelBuffer(const QColor& color, QObject* parent = nullptr);
    ~SinglePixelBuffer() override;

    QColor color() const;
    void setColor(const QColor& color);

    bool attachTo(QWindow* window);

    static bool isSupported();

Q_SIGNALS:
    void colorChanged();

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace PhosphorWayland
