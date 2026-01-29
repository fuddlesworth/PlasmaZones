// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "AlgorithmRegistry.h"
#include "TilingAlgorithm.h"
#include "core/constants.h"

#include <QDebug>
#include <algorithm>

namespace PlasmaZones {

using namespace DBus::AutotileAlgorithm;

// Global pending registrations list - shared by all AlgorithmRegistrar instantiations
QList<PendingAlgorithmRegistration> &pendingAlgorithmRegistrations()
{
    static QList<PendingAlgorithmRegistration> s_pending;
    return s_pending;
}

AlgorithmRegistry::AlgorithmRegistry(QObject *parent)
    : QObject(parent)
{
    registerBuiltInAlgorithms();
}

AlgorithmRegistry::~AlgorithmRegistry()
{
    // Clear the hash first. The TilingAlgorithm objects are QObject children
    // of this registry, so they will be deleted by ~QObject() after this
    // destructor completes. We clear the hash to prevent any accidental
    // access to stale pointers during destruction.
    m_algorithms.clear();
    m_registrationOrder.clear();
}

AlgorithmRegistry *AlgorithmRegistry::instance()
{
    // Meyer's singleton: C++11 guarantees thread-safe initialization
    // of static local variables (§6.7 [stmt.dcl] p4)
    static AlgorithmRegistry s_instance;
    return &s_instance;
}

void AlgorithmRegistry::registerAlgorithm(const QString &id, TilingAlgorithm *algorithm)
{
    // Validate inputs - take ownership and delete on failure to prevent leaks
    if (id.isEmpty()) {
        delete algorithm;
        return;
    }
    if (!algorithm) {
        return;
    }

    // Check if this algorithm pointer is already registered under a different ID
    // to prevent double-free issues. Don't delete - it's still owned under the original ID.
    const QString existingId = findAlgorithmId(algorithm);
    if (!existingId.isEmpty() && existingId != id) {
        qWarning() << "AlgorithmRegistry: algorithm" << algorithm->name()
                   << "is already registered as" << existingId
                   << "- cannot register as" << id;
        // Note: NOT deleting because it's still registered under existingId
        return;
    }

    // Remove existing algorithm with same ID (replacement case)
    auto *old = removeAlgorithmInternal(id);
    if (old && old != algorithm) {
        delete old;
    }

    // Take ownership
    algorithm->setParent(this);
    m_algorithms.insert(id, algorithm);
    m_registrationOrder.append(id);

    Q_EMIT algorithmRegistered(id);
}

QString AlgorithmRegistry::findAlgorithmId(TilingAlgorithm *algorithm) const
{
    for (auto it = m_algorithms.constBegin(); it != m_algorithms.constEnd(); ++it) {
        if (it.value() == algorithm) {
            return it.key();
        }
    }
    return QString();
}

TilingAlgorithm *AlgorithmRegistry::removeAlgorithmInternal(const QString &id)
{
    if (!m_algorithms.contains(id)) {
        return nullptr;
    }
    auto *algorithm = m_algorithms.take(id);
    m_registrationOrder.removeOne(id);
    return algorithm;
}

bool AlgorithmRegistry::unregisterAlgorithm(const QString &id)
{
    auto *algorithm = removeAlgorithmInternal(id);
    if (!algorithm) {
        return false;
    }

    delete algorithm;
    Q_EMIT algorithmUnregistered(id);
    return true;
}

TilingAlgorithm *AlgorithmRegistry::algorithm(const QString &id) const
{
    return m_algorithms.value(id, nullptr);
}

QStringList AlgorithmRegistry::availableAlgorithms() const noexcept
{
    return m_registrationOrder;
}

QList<TilingAlgorithm *> AlgorithmRegistry::allAlgorithms() const
{
    QList<TilingAlgorithm *> result;
    result.reserve(m_registrationOrder.size());

    for (const QString &id : m_registrationOrder) {
        result.append(m_algorithms.value(id));
    }

    return result;
}

bool AlgorithmRegistry::hasAlgorithm(const QString &id) const noexcept
{
    return m_algorithms.contains(id);
}

QString AlgorithmRegistry::defaultAlgorithmId() noexcept
{
    return MasterStack;
}

TilingAlgorithm *AlgorithmRegistry::defaultAlgorithm() const
{
    return algorithm(defaultAlgorithmId());
}

void AlgorithmRegistry::registerBuiltInAlgorithms()
{
    // Process all pending registrations from AlgorithmRegistrar instances
    // Each algorithm registers itself via static initialization in its .cpp file
    auto &pending = pendingAlgorithmRegistrations();

    // Sort by priority (lower = first) for deterministic registration order
    std::sort(pending.begin(), pending.end(),
              [](const auto &a, const auto &b) { return a.priority < b.priority; });

    for (const auto &reg : pending) {
        registerAlgorithm(reg.id, reg.factory());
    }

    // Clear pending list (registrations are now complete)
    pending.clear();
}

} // namespace PlasmaZones
