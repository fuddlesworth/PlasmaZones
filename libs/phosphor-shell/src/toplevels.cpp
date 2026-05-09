// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShell/Toplevels.h>

#include <QQmlEngine>

namespace PhosphorShell {

using PhosphorWayland::ForeignToplevel;
using PhosphorWayland::ForeignToplevelManager;

Toplevels::Toplevels(QObject* parent)
    : QObject(parent)
    , m_manager(std::make_unique<ForeignToplevelManager>())
{
    connect(m_manager.get(), &ForeignToplevelManager::toplevelAdded, this, &Toplevels::toplevelsChanged);
    connect(m_manager.get(), &ForeignToplevelManager::toplevelRemoved, this, &Toplevels::toplevelsChanged);
}

Toplevels::~Toplevels() = default;

Toplevels* Toplevels::create(QQmlEngine* engine, QJSEngine* scriptEngine)
{
    Q_UNUSED(engine)
    Q_UNUSED(scriptEngine)
    // Per-engine instance so multi-engine setups (rare in shells, but
    // possible) get their own manager. The QML registry deletes the
    // returned pointer on engine teardown.
    return new Toplevels();
}

QList<ForeignToplevel*> Toplevels::toplevels() const
{
    return m_manager->toplevels();
}

bool Toplevels::isSupported() const
{
    return ForeignToplevelManager::isSupported();
}

} // namespace PhosphorShell
