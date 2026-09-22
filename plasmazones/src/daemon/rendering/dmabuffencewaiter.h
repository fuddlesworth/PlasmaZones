// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>

class QSocketNotifier;

namespace PlasmaZones {

/**
 * @brief Event-driven wait for a sync_file fence fd to signal.
 *
 * A sync_file fd becomes readable when its fence signals — i.e. when the
 * producer's GPU render into the associated buffer completes. This watches the
 * fd with a QSocketNotifier on the owning thread's event loop, so there is no
 * busy-poll and no blocked thread (in particular the scene-graph render thread
 * is never stalled). When the fence signals it emits @ref ready exactly once
 * and self-deletes; a timeout bounds the lifetime so a never-signaling fence
 * (e.g. a crashed producer) self-cleans without ever emitting @ref ready.
 *
 * Consumer-agnostic: any feature that receives a dma-buf + fence can use this
 * to defer revealing/sampling the buffer until it is safe.
 *
 * Takes ownership of @p fenceFd (closed in the destructor).
 */
class DmabufFenceWaiter : public QObject
{
    Q_OBJECT

public:
    /// @param fenceFd   sync_file fence fd; ownership transferred (closed on destroy).
    /// @param timeoutMs upper bound before self-cleaning without emitting ready().
    DmabufFenceWaiter(int fenceFd, int timeoutMs, QObject* parent = nullptr);
    ~DmabufFenceWaiter() override;

Q_SIGNALS:
    /// Emitted once when the fence signals (render complete). Not emitted on timeout.
    void ready();

private:
    void finish(bool signaled);

    int m_fenceFd = -1;
    QSocketNotifier* m_notifier = nullptr;
    bool m_done = false;
};

} // namespace PlasmaZones
