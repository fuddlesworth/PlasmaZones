// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorOverlay/ShellHost.h>

#include <PhosphorLayer/Surface.h>

namespace PhosphorOverlay {

ShellHost::ShellHost(QObject* parent)
    : QObject(parent)
{
}

ShellHost::~ShellHost() = default;

void ShellHost::setSurfaceFactory(SurfaceFactory factory)
{
    m_surfaceFactory = std::move(factory);
}

void ShellHost::setPostCreateCallback(PostCreateCallback callback)
{
    m_postCreateCallback = std::move(callback);
}

void ShellHost::setPreDestroyCallback(PreDestroyCallback callback)
{
    m_preDestroyCallback = std::move(callback);
}

ShellState* ShellHost::ensureShell(const QString& screenId, QScreen* physScreen)
{
    auto it = m_states.find(screenId);
    if (it != m_states.end() && it->shellSurface) {
        return &it.value();
    }

    // Sticky-failure short-circuit: matches legacy
    // OverlayService::ensurePassiveShellFor semantics where a screen
    // whose shell-create failed is not retried until the failure flag
    // is cleared (typically on hot-plug). Return the existing zeroed
    // state when present so callers that previously held a pointer
    // don't see it disappear.
    if (m_creationFailed.contains(screenId)) {
        return (it == m_states.end()) ? nullptr : &it.value();
    }

    if (!m_surfaceFactory) {
        return nullptr;
    }

    PhosphorLayer::Surface* surface = m_surfaceFactory(screenId, physScreen);
    if (!surface) {
        m_creationFailed.insert(screenId);
        return nullptr;
    }

    auto& state = m_states[screenId];
    state.shellSurface = surface;
    state.shellWindow = surface->window();
    state.physScreen = physScreen;

    if (m_postCreateCallback) {
        m_postCreateCallback(screenId, state);
    }

    return &state;
}

void ShellHost::destroyShell(const QString& screenId)
{
    auto it = m_states.find(screenId);
    if (it == m_states.end()) {
        return;
    }

    if (m_preDestroyCallback) {
        m_preDestroyCallback(screenId);
    }

    if (it->shellSurface) {
        it->shellSurface->deleteLater();
    }
    it->shellSurface = nullptr;
    it->shellWindow = nullptr;
    it->physScreen = nullptr;
    it->slots.clear();
}

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
