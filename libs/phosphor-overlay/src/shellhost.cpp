// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorOverlay/ShellHost.h>

#include <PhosphorLayer/Surface.h>

#include <QQuickWindow>

namespace PhosphorOverlay {

ShellHost::ShellHost(QObject* parent)
    : QObject(parent)
{
}

ShellHost::~ShellHost()
{
    qDeleteAll(m_states);
}

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

namespace {

ShellState* ensureEntry(QHash<QString, ShellState*>& states, const QString& screenId)
{
    auto it = states.find(screenId);
    if (it == states.end()) {
        it = states.insert(screenId, new ShellState());
    }
    return it.value();
}

} // namespace

ShellState* ShellHost::ensureShell(const QString& screenId, QScreen* physScreen)
{
    auto it = m_states.find(screenId);
    if (it != m_states.end() && it.value()->shellSurface) {
        return it.value();
    }

    // Sticky-failure short-circuit: matches legacy
    // OverlayService::ensurePassiveShellFor semantics where a screen
    // whose shell-create failed is not retried until the failure flag
    // is cleared (typically on hot-plug). Return the existing zeroed
    // state when present so callers that previously held a pointer
    // don't see it disappear.
    if (m_creationFailed.contains(screenId)) {
        return (it == m_states.end()) ? nullptr : it.value();
    }

    if (!m_surfaceFactory) {
        return nullptr;
    }

    PhosphorLayer::Surface* surface = m_surfaceFactory(screenId, physScreen);
    if (!surface) {
        m_creationFailed.insert(screenId);
        return nullptr;
    }

    auto* state = ensureEntry(m_states, screenId);
    state->shellSurface = surface;
    state->shellWindow = surface->window();
    state->physScreen = physScreen;

    if (m_postCreateCallback) {
        m_postCreateCallback(screenId, *state);
    }

    return state;
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

    auto* state = it.value();
    if (state->shellSurface) {
        state->shellSurface->deleteLater();
    }
    state->shellSurface = nullptr;
    state->shellWindow = nullptr;
    state->physScreen = nullptr;
    state->slots.clear();
}

void ShellHost::syncSurfaceState(const QString& screenId, bool anyVisible, bool anyInputGrabbing)
{
    auto it = m_states.find(screenId);
    if (it == m_states.end() || !it.value()->shellSurface || !it.value()->shellWindow) {
        return;
    }
    auto& s = *it.value();

    // Bring the surface up on the first transition from never-shown →
    // any-slot-visible. The first show() call goes through the Surface
    // state machine (maps the wl_surface, warms the RHI, fires animator
    // attach callbacks); subsequent input-region toggles flip
    // Qt::WindowTransparentForInput directly without re-entering the
    // Surface::show()/hide() path — that path's `animator().cancel(...)`
    // would wipe per-slot tracking and `beginHide(animatorTarget())`
    // would animate the shell root opacity, both of which we want to
    // avoid for a pure click-through toggle.
    if (anyVisible && !s.shellSurface->isLogicallyShown()) {
        s.shellSurface->show();
    }

    // Drive the Qt input flag based purely on whether a modal slot is
    // up. A non-modal slot (OSD / main overlay / zone selector) being
    // visible keeps the surface mapped for rendering but leaves the
    // shell click-through, so background windows stay interactable
    // for the non-modal slot's lifetime instead of eating every click
    // on every screen for several seconds.
    const bool wantTransparent = !anyInputGrabbing;
    if (s.shellWindow->flags().testFlag(Qt::WindowTransparentForInput) != wantTransparent) {
        s.shellWindow->setFlag(Qt::WindowTransparentForInput, wantTransparent);
    }
}

bool ShellHost::rekey(const QString& oldKey, const QString& newKey)
{
    if (oldKey == newKey) {
        return false;
    }
    auto donor = m_states.find(oldKey);
    if (donor == m_states.end() || !donor.value()->shellSurface) {
        return false;
    }

    // Drop a stale (non-live) entry under newKey before the move lands.
    // Refuse to clobber a live one — the caller should not have selected
    // this donor when the target slot is occupied.
    auto existing = m_states.find(newKey);
    if (existing != m_states.end()) {
        if (existing.value()->shellSurface) {
            return false;
        }
        delete existing.value();
        m_states.erase(existing);
    }

    ShellState* state = donor.value();
    m_states.erase(donor);
    m_states.insert(newKey, state);
    return true;
}

ShellState& ShellHost::stateFor(const QString& screenId)
{
    return *ensureEntry(m_states, screenId);
}

const ShellState* ShellHost::stateFor(const QString& screenId) const
{
    auto it = m_states.constFind(screenId);
    return it == m_states.cend() ? nullptr : it.value();
}

bool ShellHost::hasState(const QString& screenId) const
{
    return m_states.contains(screenId);
}

void ShellHost::removeState(const QString& screenId)
{
    auto it = m_states.find(screenId);
    if (it == m_states.end()) {
        return;
    }
    delete it.value();
    m_states.erase(it);
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
