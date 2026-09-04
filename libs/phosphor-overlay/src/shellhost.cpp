// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorOverlay/ShellHost.h>

#include <PhosphorAnimation/SurfaceAnimator.h>
#include <PhosphorLayer/ILayerShellTransport.h>
#include <PhosphorLayer/Surface.h>

#include <QChar>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegion>

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
    // null) skip the callback in this pass - no re-entry into consumer
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
        // Refresh m_physScreen on the cached entry - a fast hot-plug
        // cycle (monitor removed and re-added under the same key) can
        // leave a stale QScreen* on the state object while the lib
        // surface stays alive. Callers reading physScreen() rely on
        // it tracking the most recent ensureShell call. Refresh
        // m_shellWindow alongside since Surface::window() is the
        // single source of truth; the 1:1 Surface↔Window invariant
        // holds today, but re-reading is cheap insurance against
        // future Surface internals that could swap the window across
        // a transport reattach.
        it.value()->m_physScreen = physScreen;
        it.value()->m_shellWindow = it.value()->m_shellSurface->window();
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
    // Contract: this function tears down the SHELL members of a ShellState
    // (shellSurface, shellWindow, physScreen, slot map) but does NOT delete
    // the ShellState* itself — that's the destructor's job (see ~ShellHost,
    // which calls destroyShell then qDeleteAll). A future refactor that adds
    // `delete state;` here will double-free when ~ShellHost runs its drain
    // loop and then the qDeleteAll over the same map.
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

void ShellHost::syncSurfaceState(const QString& screenId, bool anyVisible, bool anyInputGrabbing,
                                 bool anyKeyboardGrabbing)
{
    auto it = m_states.find(screenId);
    if (it == m_states.end() || !it.value()->m_shellSurface || !it.value()->m_shellWindow) {
        return;
    }
    auto& s = *it.value();

    // Show/hide are driven through the Surface state machine on every
    // anyVisible transition. The behavior in each direction depends on
    // the SurfaceConfig the consumer registered:
    //
    //   show()  - maps the wl_surface, warms the RHI, fires the
    //             animator's attach callbacks. Always runs on
    //             false→true.
    //
    //   hide()  - under keepMappedOnHide=true the wl_surface stays
    //             mapped (the animator drives root opacity to 0 and
    //             the window goes click-through); under
    //             keepMappedOnHide=false the wl_surface is unmapped
    //             synchronously. Consumers that need the shell to
    //             stop being composited every frame when idle opt
    //             into the latter at creation time.
    //
    // The shell surface's role typically has no registered animator
    // Config, so the animator's cancel / beginHide calls inside
    // Surface::hide() collapse to no-ops on this surface. Per-slot
    // tracking lives on slot animator targets (different keys), so
    // hiding the shell surface does not interfere with slot state.
    //
    // The Qt::WindowTransparentForInput toggle below is independent
    // of show/hide and flips directly on every input-grab transition
    // (modal slot in / out) - this keeps non-modal slot dismissals
    // from re-entering the surface state machine for a pure
    // click-through change.
    if (anyVisible && !s.m_shellSurface->isLogicallyShown()) {
        s.m_shellSurface->show();
    } else if (!anyVisible && s.m_shellSurface->isLogicallyShown()) {
        s.m_shellSurface->hide();
    }

    // Drive the Qt input flag based purely on whether a modal slot is
    // up. A non-modal slot (OSD / main overlay / zone selector) being
    // visible keeps the surface mapped for rendering but leaves the
    // shell click-through, so background windows stay interactable
    // for the non-modal slot's lifetime instead of eating every click
    // on every screen for several seconds.
    //
    // Two states:
    //
    //   visible modal up -> whole surface takes input (no mask).
    //   anything else    -> click-through.
    //
    // The grab term is spelled `anyVisible && anyInputGrabbing` rather than
    // bare `anyInputGrabbing` so the derivation does not rest on callers
    // guaranteeing that a grabbing slot is also a visible one. Nothing in this
    // library enforces that, and if it were ever false the bare form would hand
    // an invisible surface the whole screen's clicks.
    const bool wantTransparent = !(anyVisible && anyInputGrabbing);
    if (s.m_shellWindow->flags().testFlag(Qt::WindowTransparentForInput) != wantTransparent) {
        s.m_shellWindow->setFlag(Qt::WindowTransparentForInput, wantTransparent);
    }
    // QWindow::setMask reaches wl_surface.set_input_region through the Qt
    // Wayland platform window (our layer shell is a SHELL INTEGRATION, so the
    // window underneath is a normal QWaylandWindow and the standard path
    // applies). Nothing in the tree installs a mask any more (the partial
    // input region went with the daemon-drawn tab strip), so the empty set is
    // always the answer: the whole surface, with the flag above deciding
    // whether it takes input at all. Kept as an explicit re-assert rather
    // than dropped because a re-created platform window starts from scratch,
    // and stating the region here keeps the surface's input shape in one
    // place with the flag. Every caller is on a structural edge (slot show,
    // hide completion, screen add/remove), never a paint path.
    s.m_shellWindow->setMask(QRegion());

    // Keyboard interactivity is a separate axis from the pointer flag above:
    // the picker and snap assist are pointer-modal but deliberately leave the
    // keyboard with the focused toplevel, because every key they answer to is
    // a global shortcut the compositor routes before any surface sees it. Only
    // a slot that has to TYPE asks for this, and while it holds it the user's
    // focused window receives nothing.
    //
    // Runtime set_keyboard_interactivity is legal on wlr-layer-shell after the
    // initial configure (unlike the scope, which is immutable), so this rides
    // the same structural edges as the input flag rather than needing a
    // surface rebuild. The transport handle is null until the surface has
    // attached; a pre-attach call would be dropped, and the role's own
    // KeyboardInteractivity::None is the correct value to attach with anyway.
    if (auto* handle = s.m_shellSurface->transport()) {
        const auto wantKeyboard = (anyVisible && anyKeyboardGrabbing) ? PhosphorLayer::KeyboardInteractivity::Exclusive
                                                                      : PhosphorLayer::KeyboardInteractivity::None;
        handle->setKeyboardInteractivity(wantKeyboard);
    }
}

bool ShellHost::rekey(const QString& oldKey, const QString& newKey)
{
    // Same key is idempotent success: the entry is at newKey after this
    // call (it was already there). Callers that distinguish "moved" from
    // "already there" should compare keys themselves before calling.
    if (oldKey == newKey) {
        auto it = m_states.find(oldKey);
        return it != m_states.end() && it.value()->m_shellSurface != nullptr;
    }
    auto donor = m_states.find(oldKey);
    if (donor == m_states.end() || !donor.value()->m_shellSurface) {
        return false;
    }

    // Drop a stale (non-live) entry under newKey before the move lands.
    // Refuse to clobber a live one - the caller should not have selected
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
    // Clear any sticky creation-failure flag at newKey: a live shell
    // now backs the key, so future ensureShell calls must not short-
    // circuit to the failure path. Symmetric with the donor side:
    // oldKey's flag stays as-is (it may legitimately be marked failed
    // separately by the consumer's id-grammar logic).
    m_creationFailed.remove(newKey);
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
    // Programmer-setup errors (no animator wired, empty ids) - drop
    // completion silently because the caller has no recovery path.
    if (!m_surfaceAnimator || screenId.isEmpty() || slotKey.isEmpty()) {
        return;
    }
    // Benign no-ops (no shell, no slot, slot Item gone, slot already
    // hidden) - fire completion synchronously so consumer cleanup that
    // relies on "post-hide" semantics (clear loader mode, clear
    // sentinels, restore sibling slot) still runs. Without this, a
    // dismiss called on an already-hidden slot leaves consumer parallel
    // state stuck "live" forever.
    //
    // Helper takes the callback by rvalue-ref so the move from the
    // caller's `completion` is explicit at each early-return site -
    // and the outer `completion` is left in a moved-from-but-still-
    // destructible state for any subsequent reference (the function
    // returns immediately after; the std::move(completion) on the
    // animator-dispatch path below only runs when no early-return
    // fired).
    auto runCompletion = [](std::function<void()>&& cb) {
        if (cb) {
            auto local = std::move(cb);
            local();
        }
    };
    auto it = m_states.find(screenId);
    if (it == m_states.end()) {
        runCompletion(std::move(completion));
        return;
    }
    auto& state = *it.value();
    if (!state.m_shellSurface) {
        runCompletion(std::move(completion));
        return;
    }
    auto slotIt = state.slots.constFind(slotKey);
    if (slotIt == state.slots.cend()) {
        runCompletion(std::move(completion));
        return;
    }
    auto* item = slotIt.value().item.data();
    if (!item || !item->isVisible()) {
        runCompletion(std::move(completion));
        return;
    }
    m_surfaceAnimator->beginHide(state.m_shellSurface, item, slotIt.value().role, std::move(completion));
}

ShellState& ShellHost::getOrCreateStateFor(const QString& screenId)
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
    // Drain the surface before deleting the state object. Callers that
    // forget to call destroyShell first would otherwise leak the
    // wl_surface (orphaned with no event-loop unmap) AND skip the
    // PreDestroyCallback consumers rely on for parallel-state cleanup.
    // destroyShell gates its callback on shellSurface non-null and
    // zeroes the field, so re-calling here is safe for already-drained
    // entries (the gate makes it a no-op).
    if (it.value()->m_shellSurface) {
        destroyShell(screenId);
        // destroyShell only zeroes fields; m_states keys are unchanged.
        // Re-find to refresh the iterator (defensive: QHash::find()
        // doesn't invalidate non-mutated entries, but explicit re-find
        // makes the invariant survive future refactors).
        it = m_states.find(screenId);
        if (it == m_states.end()) {
            return;
        }
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
