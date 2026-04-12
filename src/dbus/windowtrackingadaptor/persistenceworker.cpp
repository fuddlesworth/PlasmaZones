// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "persistenceworker.h"

#include "../../config/configbackend_json.h"
#include "../../core/logging.h"

#include <QThread>

namespace PlasmaZones {

// ═══════════════════════════════════════════════════════════════════════════════
// PersistenceIO — lives on the I/O thread
// ═══════════════════════════════════════════════════════════════════════════════

void PersistenceIO::processWrite(const QString& filePath, const QJsonObject& root)
{
    // Reuse JsonConfigBackend's existing atomic-write helper instead of
    // duplicating the QSaveFile/QDir/QJsonDocument dance. Keeping one
    // implementation means format/permission changes only need to land
    // in one place.
    const bool ok = JsonConfigBackend::writeJsonAtomically(filePath, root);
    if (!ok) {
        qCWarning(lcDbusWindow) << "PersistenceIO: atomic write failed for" << filePath;
    }
    Q_EMIT writeCompleted(filePath, ok);
}

// ═══════════════════════════════════════════════════════════════════════════════
// PersistenceWorker — main-thread coordinator
// ═══════════════════════════════════════════════════════════════════════════════

PersistenceWorker::PersistenceWorker(QObject* parent)
    : QObject(parent)
{
    m_thread = new QThread(this);
    m_thread->setObjectName(QStringLiteral("PersistenceIO"));
    m_io = new PersistenceIO(); // no parent — will be moved to thread
    m_io->moveToThread(m_thread);

    connect(this, &PersistenceWorker::requestWrite, m_io, &PersistenceIO::processWrite);
    // Forward completion back to main thread so callers (e.g. saveState)
    // can react to success/failure without touching the I/O thread.
    connect(m_io, &PersistenceIO::writeCompleted, this, &PersistenceWorker::writeCompleted);
    connect(m_thread, &QThread::finished, m_io, &QObject::deleteLater);

    m_thread->start();
}

PersistenceWorker::~PersistenceWorker()
{
    m_thread->quit();
    m_thread->wait(5000);
}

void PersistenceWorker::enqueueWrite(const QString& filePath, const QJsonObject& root)
{
    Q_EMIT requestWrite(filePath, root);
}

bool PersistenceWorker::writeSync(const QString& filePath, const QJsonObject& root)
{
    return JsonConfigBackend::writeJsonAtomically(filePath, root);
}

} // namespace PlasmaZones
