// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorOverlay/ShellHost.h>

#include <PhosphorAnimation/SurfaceAnimator.h>
#include <PhosphorLayer/Surface.h>

#include <QChar>
#include <QQuickItem>
#include <QQuickWindow>

namespace PhosphorOverlay {

PhosphorLayer::Role makePerInstanceRole(const PhosphorLayer::Role& base, QStringView screenId, quint64 generation)
{
    QString prefix;
    prefix.reserve(base.scopePrefix.size() + 1 + screenId.size() + 1 + 20);
    prefix.append(base.scopePrefix);
    prefix.append(QLatin1Char('-'));
    prefix.append(screenId);
    prefix.append(QLatin1Char('-'));
    prefix.append(QString::number(generation));
    return base.withScopePrefix(std::move(prefix));
}

ShellHost::ShellHost(QObject* parent)
    : QObject(parent)
{
}

ShellHost::~ShellHost()
{
    // Surfaces still mapped must have deleteLater scheduled (so the Qt
    // event loop unmaps them cleanly) and consumers must get a chance
    // to drop their borrowed pointers. Iterate keys via destroyShell
    // first; then qDeleteAll wipes the now-zeroed heap objects.
    //
    // destroyShell gates PreDestroyCallback on `shellSurface != nullptr`,
    // so entries previously drained by the consumer (shellSurface already
    // null) skip the callback in this pass — no re-entry into consumer
    // state that may already be partially destroyed.
    const QStringList keys = m_states.keys();
    for (const QString& key : keys) {
        destroyShell(key);
    }
    qDeleteAll(m_states);
    m_states.clear();
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

void ShellHost::setSurfaceAnimator(PhosphorAnimationLayer::SurfaceAnimator* animator)
{
    m_surfaceAnimator = animator;
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
    if (it != m_states.end() && it.value()->m_shellSurface) {
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
    state->m_shellSurface = surface;
    state->m_shellWindow = surface->window();
    state->m_physScreen = physScreen;

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

    auto* state = it.value();
    // Gate the PreDestroyCallback on shellSurface non-null: re-calls on
    // already-drained entries (e.g. ~ShellHost's loop after a consumer
    // drain) skip the callback so consumer state that may have already
    // started destruction isn't touched.
    if (state->m_shellSurface && m_preDestroyCallback) {
        m_preDestroyCallback(screenId);
    }

    if (state->m_shellSurface) {
        state->m_shellSurface->deleteLater();
    }
    state->m_shellSurface = nullptr;
    state->m_shellWindow = nullptr;
    state->m_physScreen = nullptr;
    state->slots.clear();
}

void ShellHost::syncSurfaceState(const QString& screenId, bool anyVisible, bool anyInputGrabbing)
{
    auto it = m_states.find(screenId);
    if (it == m_states.end() || !it.value()->m_shellSurface || !it.value()->m_shellWindow) {
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
    if (anyVisible && !s.m_shellSurface->isLogicallyShown()) {
        s.m_shellSurface->show();
    }

    // Drive the Qt input flag based purely on whether a modal slot is
    // up. A non-modal slot (OSD / main overlay / zone selector) being
    // visible keeps the surface mapped for rendering but leaves the
    // shell click-through, so background windows stay interactable
    // for the non-modal slot's lifetime instead of eating every click
    // on every screen for several seconds.
    const bool wantTransparent = !anyInputGrabbing;
    if (s.m_shellWindow->flags().testFlag(Qt::WindowTransparentForInput) != wantTransparent) {
        s.m_shellWindow->setFlag(Qt::WindowTransparentForInput, wantTransparent);
    }
}

bool ShellHost::rekey(const QString& oldKey, const QString& newKey)
{
    // Same key is idempotent success: the entry is at newKey after this
    // call (it was already there). Callers that distinguish "moved" from
    // "already there" should compare keys themselves before calling.
    if (oldKey == newKey) {
        return m_states.contains(oldKey) && m_states.value(oldKey)->m_shellSurface != nullptr;
    }
    auto donor = m_states.find(oldKey);
    if (donor == m_states.end() || !donor.value()->m_shellSurface) {
        return false;
    }

    // Drop a stale (non-live) entry under newKey before the move lands.
    // Refuse to clobber a live one — the caller should not have selected
    // this donor when the target slot is occupied.
    auto existing = m_states.find(newKey);
    if (existing != m_states.end()) {
        if (existing.value()->m_shellSurface) {
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

void ShellHost::registerConfigForRole(const PhosphorLayer::Role& role,
                                      PhosphorAnimationLayer::SurfaceAnimator::Config config)
{
    if (!m_surfaceAnimator) {
        return;
    }
    m_surfaceAnimator->registerConfigForRole(role, std::move(config));
}

void ShellHost::hideSlot(const QString& screenId, const QString& slotKey, std::function<void()> completion)
{
    // Programmer-setup errors (no animator wired, empty ids) — drop
    // completion silently because the caller has no recovery path.
    if (!m_surfaceAnimator || screenId.isEmpty() || slotKey.isEmpty()) {
        return;
    }
    // Benign no-ops (no shell, no slot, slot Item gone, slot already
    // hidden) — fire completion synchronously so consumer cleanup that
    // relies on "post-hide" semantics (clear loader mode, clear
    // sentinels, restore sibling slot) still runs. Without this, a
    // dismiss called on an already-hidden slot leaves consumer parallel
    // state stuck "live" forever.
    auto runCompletion = [&completion]() {
        if (completion) {
            auto cb = std::move(completion);
            cb();
        }
    };
    auto it = m_states.find(screenId);
    if (it == m_states.end()) {
        runCompletion();
        return;
    }
    auto& state = *it.value();
    if (!state.m_shellSurface) {
        runCompletion();
        return;
    }
    auto slotIt = state.slots.constFind(slotKey);
    if (slotIt == state.slots.cend()) {
        runCompletion();
        return;
    }
    auto* item = slotIt.value().item.data();
    if (!item || !item->isVisible()) {
        runCompletion();
        return;
    }
    m_surfaceAnimator->beginHide(state.m_shellSurface, item, slotIt.value().role, std::move(completion));
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
