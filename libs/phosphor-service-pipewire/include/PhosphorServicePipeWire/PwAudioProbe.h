// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <PhosphorServicePipeWire/phosphorservicepipewire_export.h>
#include <QObject>
#include <memory>
namespace PhosphorServicePipeWire {
/// On-demand audio test. Captured samples are reduced to a peak in memory;
/// they are never retained or played back. Destruction stops all streams.
class PHOSPHORSERVICEPIPEWIRE_EXPORT PwAudioProbe : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool listening READ listening NOTIFY activeChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY activeChanged)
    Q_PROPERTY(qreal level READ level NOTIFY levelChanged)
public:
    explicit PwAudioProbe(QObject* parent = nullptr);
    ~PwAudioProbe() override;
    [[nodiscard]] bool listening() const;
    [[nodiscard]] bool playing() const;
    [[nodiscard]] qreal level() const;
    Q_INVOKABLE void startInputTest(const QString& nodeName);
    Q_INVOKABLE void playTestSound(const QString& nodeName);
    Q_INVOKABLE void stop();
Q_SIGNALS:
    void activeChanged();
    void levelChanged();
    void error(const QString& message);

private:
    Q_DISABLE_COPY_MOVE(PwAudioProbe)
    void start(const QString& nodeName, bool capture);
    class Private;
    std::unique_ptr<Private> d;
};
}
