// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorOverlay/ShellHost.h>

namespace PhosphorOverlay {

ShellHost::ShellHost(QObject* parent)
    : QObject(parent)
{
}

ShellHost::~ShellHost() = default;

ShellState& ShellHost::stateFor(const QString& screenId)
{
    return m_states[screenId];
}

const ShellState* ShellHost::stateFor(const QString& screenId) const
{
    auto it = m_states.constFind(screenId);
    return it == m_states.cend() ? nullptr : &it.value();
}

bool ShellHost::hasState(const QString& screenId) const
{
    return m_states.contains(screenId);
}

void ShellHost::removeState(const QString& screenId)
{
    m_states.remove(screenId);
}

QStringList ShellHost::screenIds() const
{
    return m_states.keys();
}

void ShellHost::markFailure(const QString& screenId)
{
    m_creationFailed.insert(screenId);
}

void ShellHost::clearFailure(const QString& screenId)
{
    m_creationFailed.remove(screenId);
}

bool ShellHost::hasFailure(const QString& screenId) const
{
    return m_creationFailed.contains(screenId);
}

QStringList ShellHost::failureScreenIds() const
{
    return QStringList(m_creationFailed.cbegin(), m_creationFailed.cend());
}

} // namespace PhosphorOverlay
