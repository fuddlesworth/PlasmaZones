// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "AlgorithmRegistry.h"
#include "TilingAlgorithm.h"
#include "algorithms/MasterStackAlgorithm.h"
#include "algorithms/ColumnsAlgorithm.h"
#include "algorithms/BSPAlgorithm.h"
#include "core/constants.h"

namespace PlasmaZones {

using namespace DBus::AutotileAlgorithm;

AlgorithmRegistry *AlgorithmRegistry::s_instance = nullptr;

AlgorithmRegistry::AlgorithmRegistry(QObject *parent)
    : QObject(parent)
{
    registerBuiltInAlgorithms();
}

AlgorithmRegistry::~AlgorithmRegistry()
{
    // Algorithms are QObjects parented to this, so they'll be deleted automatically
    // But clear the hash to be explicit
    m_algorithms.clear();
}

AlgorithmRegistry *AlgorithmRegistry::instance()
{
    if (!s_instance) {
        s_instance = new AlgorithmRegistry();
    }
    return s_instance;
}

void AlgorithmRegistry::registerAlgorithm(const QString &id, TilingAlgorithm *algorithm)
{
    if (id.isEmpty() || !algorithm) {
        return;
    }

    // Remove existing algorithm with same ID
    if (m_algorithms.contains(id)) {
        auto *old = m_algorithms.take(id);
        m_registrationOrder.removeOne(id);
        delete old;
    }

    // Take ownership
    algorithm->setParent(this);
    m_algorithms.insert(id, algorithm);
    m_registrationOrder.append(id);

    Q_EMIT algorithmRegistered(id);
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

QStringList AlgorithmRegistry::availableAlgorithms() const
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

bool AlgorithmRegistry::hasAlgorithm(const QString &id) const
{
    return m_algorithms.contains(id);
}

QString AlgorithmRegistry::defaultAlgorithmId()
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
