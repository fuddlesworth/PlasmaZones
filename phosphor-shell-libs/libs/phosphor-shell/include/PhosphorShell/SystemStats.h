// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <PhosphorShell/phosphorshell_export.h>
#include <QObject>
#include <QElapsedTimer>
#include <QHash>
#include <QThread>
#include <QTimer>
#include <QVariantMap>
#include <memory>
namespace PhosphorShell {
class SystemStatsReader;
class PHOSPHORSHELL_EXPORT SystemStats : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap snapshot READ snapshot NOTIFY changed)
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged)
    Q_PROPERTY(int interval READ interval WRITE setInterval NOTIFY intervalChanged)
    Q_PROPERTY(int revision READ revision NOTIFY sampled)
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
public:
    explicit SystemStats(QObject* parent = nullptr);
    ~SystemStats() override;
    QVariantMap snapshot() const
    {
        return m_snapshot;
    }
    bool paused() const
    {
        return m_paused;
    }
    void setPaused(bool value);
    int interval() const
    {
        return m_interval;
    }
    void setInterval(int value);
    int revision() const
    {
        return m_revision;
    }
    bool active() const
    {
        return !m_consumers.isEmpty();
    }
    // Consumers share one sampler. Destruction releases interest automatically.
    Q_INVOKABLE void watch(QObject* consumer, bool enabled);
    // Returns normalized horizontal positions and raw values, including gaps.
    // GPU keys are "gpu:<device id>" so switching devices never mixes histories.
    Q_INVOKABLE QVariantList history(const QString& metric, int minutes = 1) const;
Q_SIGNALS:
    void changed();
    void pausedChanged();
    void intervalChanged();
    void sampled();
    void activeChanged();

private:
    void reconfigure();
    void requestSample();
    void acceptSample(const QVariantMap& value);
    struct Frame
    {
        qint64 time;
        QVariantMap values;
    };
    QVariantMap m_snapshot;
    QList<Frame> m_history;
    QHash<QObject*, QMetaObject::Connection> m_consumers;
    QElapsedTimer m_clock;
    QTimer m_timer;
    QThread m_thread;
    QObject* m_worker = nullptr;
    std::unique_ptr<SystemStatsReader> m_reader;
    bool m_paused = false;
    bool m_busy = false;
    bool m_resetRates = true;
    int m_interval = 2000;
    int m_revision = 0;
    quint64 m_generation = 0;
};
}
