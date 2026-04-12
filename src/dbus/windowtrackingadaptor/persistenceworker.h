// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <QJsonObject>
#include <QObject>

class QThread;

namespace PlasmaZones {

/**
 * @brief Internal worker that lives on the I/O thread.
 * Receives write requests via QueuedConnection signal.
 */
class PersistenceIO : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;

public Q_SLOTS:
    void processWrite(const QString& filePath, const QJsonObject& root);

Q_SIGNALS:
    void writeCompleted(bool success);
};

/**
 * @brief Main-thread coordinator for async JSON config writes.
 *
 * Owns a dedicated QThread + PersistenceIO worker. Call enqueueWrite()
 * from the main thread; the actual QSaveFile I/O runs off-thread.
 * QJsonObject is implicitly shared (COW) so the copy is cheap.
 *
 * For shutdown, call writeSync() which runs inline on the calling thread.
 */
class PLASMAZONES_EXPORT PersistenceWorker : public QObject
{
    Q_OBJECT

public:
    explicit PersistenceWorker(QObject* parent = nullptr);
    ~PersistenceWorker() override;

    void enqueueWrite(const QString& filePath, const QJsonObject& root);

    static bool writeSync(const QString& filePath, const QJsonObject& root);

Q_SIGNALS:
    void requestWrite(const QString& filePath, const QJsonObject& root);

private:
    QThread* m_thread = nullptr;
    PersistenceIO* m_io = nullptr;
};

} // namespace PlasmaZones
