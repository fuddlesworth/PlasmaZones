// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "AlgorithmRegistry.h"
#include "TilingAlgorithm.h"
#include "algorithms/MasterStackAlgorithm.h"
#include "algorithms/ColumnsAlgorithm.h"
#include "algorithms/BSPAlgorithm.h"
#include "core/constants.h"

#include <QDebug>

namespace PlasmaZones {

using namespace DBus::AutotileAlgorithm;

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
    if (id.isEmpty() || !algorithm) {
        return;
    }

    // Check if this algorithm pointer is already registered under a different ID
    // to prevent double-free issues
    const QString existingId = findAlgorithmId(algorithm);
    if (!existingId.isEmpty() && existingId != id) {
        qWarning() << "AlgorithmRegistry: algorithm" << algorithm->name()
                   << "is already registered as" << existingId
                   << "- cannot register as" << id;
        return;
    }

    // Remove existing algorithm with same ID (replacement case)
    if (m_algorithms.contains(id)) {
        auto *old = m_algorithms.take(id);
        m_registrationOrder.removeOne(id);
        if (old != algorithm) {
            delete old;
        }
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

bool AlgorithmRegistry::unregisterAlgorithm(const QString &id)
{
    if (!m_algorithms.contains(id)) {
        return false;
    }

    auto *algorithm = m_algorithms.take(id);
    m_registrationOrder.removeOne(id);
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
    // Register in order of typical user preference
    // Master-Stack is the classic tiling WM default (dwm, Bismuth, etc.)
    registerAlgorithm(MasterStack, new MasterStackAlgorithm());

    // Columns is the simplest layout
    registerAlgorithm(Columns, new ColumnsAlgorithm());

    // BSP provides balanced recursive splitting
    registerAlgorithm(BSP, new BSPAlgorithm());

    // Future algorithms will be added here:
    // registerAlgorithm(Rows, new RowsAlgorithm());
    // registerAlgorithm(Monocle, new MonocleAlgorithm());
    // registerAlgorithm(Fibonacci, new FibonacciAlgorithm());
    // registerAlgorithm(ThreeColumn, new ThreeColumnAlgorithm());
}

} // namespace PlasmaZones
