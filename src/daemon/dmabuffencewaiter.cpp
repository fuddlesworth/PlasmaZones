// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "dmabuffencewaiter.h"
#include "../core/logging.h"

#include <QSocketNotifier>
#include <QTimer>

#include <unistd.h>

namespace PlasmaZones {

DmabufFenceWaiter::DmabufFenceWaiter(int fenceFd, int timeoutMs, QObject* parent)
    : QObject(parent)
    , m_fenceFd(fenceFd)
{
    // A signaled sync_file is level-triggered readable; we disable the notifier
    // in finish() and self-delete, so activated() fires at most once.
    // QSocketNotifier::Read also fires on an error condition on the fd, which it
    // can't distinguish from "signaled" — acceptable here: the buffer is already
    // posted, so the worst case is revealing the thumbnail a frame early, never
    // reading a torn buffer (the caller only reaches this path with a valid dup).
    m_notifier = new QSocketNotifier(fenceFd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, [this]() {
        finish(/*signaled=*/true);
    });
    QTimer::singleShot(timeoutMs, this, [this]() {
        finish(/*signaled=*/false);
    });
}

DmabufFenceWaiter::~DmabufFenceWaiter()
{
    if (m_fenceFd >= 0) {
        ::close(m_fenceFd);
    }
}

void DmabufFenceWaiter::finish(bool signaled)
{
    if (m_done) {
        return; // notifier and timeout race — first wins
    }
    m_done = true;
    // m_notifier is created in the ctor and never nulled, so it is always live here.
    m_notifier->setEnabled(false);
    if (signaled) {
        Q_EMIT ready();
    } else {
        qCWarning(lcOverlay) << "DmabufFenceWaiter: fence did not signal within timeout — dropping thumbnail reveal";
    }
    deleteLater();
}

} // namespace PlasmaZones
