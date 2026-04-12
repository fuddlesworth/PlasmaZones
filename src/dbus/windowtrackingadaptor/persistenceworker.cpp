// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "persistenceworker.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QThread>

namespace PlasmaZones {

static bool writeJsonAtomically(const QString& filePath, const QJsonObject& root)
{
    QDir dir = QFileInfo(filePath).absoluteDir();
    if (!dir.exists() && !dir.mkpath(QLatin1String("."))) {
        qWarning("PersistenceIO: failed to create directory %s", qPrintable(dir.absolutePath()));
        return false;
    }

    QSaveFile f(filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning("PersistenceIO: failed to open %s: %s", qPrintable(filePath), qPrintable(f.errorString()));
        return false;
    }

    QJsonDocument doc(root);
    f.write(doc.toJson(QJsonDocument::Indented));

    if (!f.commit()) {
        qWarning("PersistenceIO: commit failed %s: %s", qPrintable(filePath), qPrintable(f.errorString()));
        return false;
    }

    return true;
}

// ═══════════════════════════════════════════════════════════════════════════════
// PersistenceIO — lives on the I/O thread
// ═══════════════════════════════════════════════════════════════════════════════

void PersistenceIO::processWrite(const QString& filePath, const QJsonObject& root)
{
    bool ok = writeJsonAtomically(filePath, root);
    Q_EMIT writeCompleted(ok);
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
    return writeJsonAtomically(filePath, root);
}

} // namespace PlasmaZones
