// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

// ShellHost - owns the per-screen layer-shell shell-state map and the
// lifecycle bookkeeping (create / destroy / rekey / sync) that any
// multi-slot overlay consumer needs.
//
// Phase 2 foundation: the type holds a `QHash<QString, ShellState>`
// keyed by effective screen id plus a sticky creation-failure set
// (spam-guard for compositors that refuse a shell on a given screen).
// Method moves from OverlayService land in subsequent Phase 2 commits;
// this commit pins the shape of the public type and its accessors so
// the daemon can construct a ShellHost as a member alongside its
// existing per-screen state.

#include <PhosphorOverlay/ShellState.h>
#include <PhosphorOverlay/phosphoroverlay_export.h>

#include <PhosphorAnimation/SurfaceAnimator.h>
#include <PhosphorLayer/Role.h>

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QStringView>

#include <functional>

class QScreen;

namespace PhosphorLayer {
class Surface;
} // namespace PhosphorLayer

namespace PhosphorOverlay {

/// Build a per-instance @c PhosphorLayer::Role by appending
/// `-{screenId}-{generation}` to @p base's scope prefix. Single-source
/// for the policy "per-instance scope prefix-matches the base role's
/// prefix" so the SurfaceAnimator's longest-prefix lookup always
/// resolves the base role's registered config.
///
/// Pre-existing failure modes this prevents:
///  - Build the per-instance literal from scratch (e.g. typo), or
///  - Pass a different base role and re-type the literal, then later
///    rename the family role.
/// Either case made the longest-prefix match silently miss and the
/// surface fell back to the library's empty default config.
///
/// @param base       Named base role (e.g. PassiveShell pattern in Phosphor).
/// @param screenId   Effective screen id (physical or virtual).
/// @param generation Monotonic per-process counter, e.g. from
///                   @c PhosphorSurfaces::SurfaceManager::nextScopeGeneration().
[[nodiscard]] PHOSPHOROVERLAY_EXPORT PhosphorLayer::Role makePerInstanceRole(const PhosphorLayer::Role& base,
                                                                             QStringView screenId, quint64 generation);

class PHOSPHOROVERLAY_EXPORT ShellHost : public QObject
{
    Q_OBJECT

public:
    /// What the consumer's live content wants from the shell surface on one
    /// screen. See @ref syncSurfaceState for what each field drives.
    struct SurfaceStateWants
    {
        bool visible = false;
        bool inputGrabbing = false;
        bool keyboardGrabbing = false;
    };

    /// Consumer-provided factory for the per-screen layer-shell surface.
    /// The library does not know which Role / qmlSource / SurfaceManager
    /// the consumer wires through - the factory encapsulates all of
    /// that, returns the resulting Surface*, or nullptr on failure.
    using SurfaceFactory = std::function<PhosphorLayer::Surface*(const QString& screenId, QScreen* physScreen)>;

    /// Hook fired after a fresh shell surface + window are recorded in
    /// the ShellState. Consumers use this to look up content slot Items
    /// by QML object name, wire up QML signal handlers, prime the
    /// rendering pipeline, and run any per-shell warm-up. Receives a
    /// mutable reference to the ShellState the library just populated.
    using PostCreateCallback = std::function<void(const QString& screenId, ShellState& state)>;

    /// Hook fired before the library deletes the shell surface. Consumers
    /// use this to drop their parallel per-screen state (e.g. content-mode
    /// sentinels, content geometry caches, signal-disconnection bookkeeping)
    /// so a stale signal handler firing during teardown doesn't dereference
    /// a dangling pointer.
    ///
    /// Only fires when a live shell surface is being torn down - re-calls
    /// of @ref destroyShell on an already-drained entry (shellSurface
    /// already nullptr) skip the callback. ~ShellHost's cleanup loop
    /// therefore won't re-enter consumer state that may have already
    /// started destruction.
    using PreDestroyCallback = std::function<void(const QString& screenId)>;

    explicit ShellHost(QObject* parent = nullptr);
    ~ShellHost() override;

    /// Inject the per-screen surface factory. Required before any
    /// @ref ensureShell call; the library has no built-in surface
    /// creation pipeline.
    void setSurfaceFactory(SurfaceFactory factory);

    /// Inject the post-create hook (optional but typically used).
    void setPostCreateCallback(PostCreateCallback callback);

    /// Inject the pre-destroy hook (optional).
    void setPreDestroyCallback(PreDestroyCallback callback);

    /// Inject the SurfaceAnimator that drives every slot's show/hide
    /// leg. Required before any @ref hideSlot call. Borrowed pointer -
    /// the lib does not own the animator; composition roots typically
    /// own a single instance and thread it through every consumer.
    void setSurfaceAnimator(PhosphorAnimationLayer::SurfaceAnimator* animator);

    /// All methods on this class must be called on the GUI thread. It writes
    /// QQuickWindow flags and masks and issues Wayland protocol requests
    /// through the surface's transport, none of which is safe off-thread.
    /// The constraint is an obligation on the consumer, not something the
    /// class enforces or detects.
    ///
    /// Idempotent: bring up (or return) the per-screen shell for
    /// @p screenId on @p physScreen. Returns a pointer to the ShellState
    /// on success. Returns nullptr when:
    ///   - the factory is unset, or
    ///   - the factory was called and returned nullptr (sticky-failure
    ///     flag set automatically), AND no state object had been
    ///     materialized for that screen before.
    ///
    /// When the sticky-failure flag is set and a state object already
    /// exists (e.g. from a prior @ref getOrCreateStateFor or a
    /// previously-live shell that was torn down), returns that existing state with
    /// @c shellSurface() == nullptr. Callers that need to retry must
    /// call @ref clearFailure first.
    ///
    /// On the cached-live path (state exists and @c shellSurface() is
    /// non-null), the state's @c physScreen() is refreshed to the
    /// argument so callers that read it always see the most recent
    /// @c ensureShell call's value, and @c shellWindow() is re-read from
    /// @c Surface::window() alongside it.
    ///
    /// The PostCreateCallback runs with the state's mechanism fields already
    /// populated, and its return value is the pointer this function hands
    /// back. It may call @ref destroyShell for the same screen — the daemon
    /// does exactly that to tear down a half-wired shell — in which case the
    /// returned state is non-null with @c shellSurface() == nullptr. It must
    /// NOT call @ref removeState or @ref rekey for that screen, since both
    /// delete the object the caller is about to receive.
    ShellState* ensureShell(const QString& screenId, QScreen* physScreen);

    /// Tear down the shell for @p screenId. Fires the pre-destroy
    /// callback before scheduling shellSurface for deletion via
    /// deleteLater. After return, the ShellState entry survives with
    /// every shell-mechanism field nulled - callers that want the entry
    /// removed entirely should follow up with @ref removeState.
    void destroyShell(const QString& screenId);

    /// Reconcile the shell's mapped state + pointer-input region with
    /// the consumer's view of what's live on this screen.
    ///
    /// @c wants.visible - true when at least one slot wants the surface
    /// mapped (driven by the consumer's slot-visibility check). Drives
    /// the Surface state machine in both directions: show() on
    /// false→true, hide() on true→false. The behavior of hide() is
    /// governed by the SurfaceConfig the consumer registered:
    /// keepMappedOnHide=true keeps the wl_surface mapped (animator
    /// drives root opacity to 0); keepMappedOnHide=false unmaps the
    /// wl_surface synchronously so the shell stops being composited
    /// when idle. The shell surface's role typically has no animator
    /// Config registered, so per-surface cancel/beginHide collapse
    /// to no-ops and do not interfere with per-slot animator state
    /// (which lives on different keys).
    ///
    /// @c wants.inputGrabbing - true when at least one modal slot
    /// (consumer-defined; Phosphor today: snap-assist, layout picker and the
    /// cheatsheet) wants pointer input. When false, OR when @c wants.visible is false, the shell's
    /// QQuickWindow is flagged Qt::WindowTransparentForInput so background
    /// windows stay interactable beneath non-modal slots (OSDs, main overlay,
    /// zone selector during drag). The @c wants.visible term means this does not
    /// rest on the caller guaranteeing that a grabbing slot is a visible one:
    /// a grab with nothing visible would otherwise hand an unseen surface
    /// every click on the screen.
    ///
    /// Input is all-or-nothing for the whole shell surface: a visible modal
    /// grab takes every click the surface covers, and anything else leaves it
    /// click-through. Every passive slot shares one screen-sized surface, so
    /// a slot wanting clicks only where it draws would need a sub-surface
    /// input region, which nothing asks for today.
    ///
    /// @c wants.keyboardGrabbing - true when at least one slot needs to TYPE
    /// (consumer-defined; Phosphor today: the cheatsheet's search field).
    /// Drives the layer surface's keyboard interactivity between Exclusive
    /// and None at runtime, which wlr-layer-shell permits after the initial
    /// configure. Separate from @c wants.inputGrabbing because the two are
    /// genuinely different asks: snap-assist and the layout picker are modal
    /// for the pointer but must NOT take the keyboard away from the focused
    /// toplevel, since they are driven entirely by global shortcuts the
    /// compositor routes before any surface sees them.
    ///
    /// Exclusive rather than OnDemand: a slot that wants the keyboard was
    /// opened by an explicit user gesture and expects to receive the next
    /// keystroke, not to be clicked into first. The cost is that the focused
    /// window stops receiving keys for the slot's lifetime, so consumers must
    /// drop the flag on the FIRST edge of dismissal rather than waiting for a
    /// hide animation to finish.
    ///
    /// Gated on @c wants.visible for the same reason the input flag is: an
    /// invisible surface must never hold the session's keyboard.
    ///
    /// Per-screen, with no cross-screen arbitration: this call reads and
    /// writes one screen's state and nothing else. When the keyboard-grabbing
    /// slot MOVES between screens, the consumer must call this for the screen
    /// being LEFT as well as the one being entered. Syncing the new screen
    /// does not release the old one, and two surfaces asking for an exclusive
    /// keyboard at once is resolved by the compositor, not here.
    ///
    /// No-op when the shell surface or window is not yet up, and the keyboard
    /// write is additionally skipped when the surface has not attached yet
    /// (null transport handle) — harmless, since the role attaches kbd-None.
    ///
    /// Taken as a struct rather than three bools in a row: the three are
    /// independent, same-typed, and a transposition would compile silently
    /// while producing exactly the failure this doc block warns about (a
    /// pointer-modal slot taking the keyboard from the focused window).
    /// Designated initialisers at the call site name each one.
    void syncSurfaceState(const QString& screenId, const SurfaceStateWants& wants);

    /// Move the ShellState entry from @p oldKey to @p newKey, preserving
    /// the underlying heap-allocated state object (the borrowed pointer
    /// stored on the consumer's parallel per-screen state stays valid).
    /// Returns true on success; returns false when:
    ///   - oldKey has no live shell (no entry or shellSurface is nullptr)
    ///   - newKey already has a LIVE entry (refuses to clobber); a stale
    ///     zeroed entry under newKey is dropped to make room.
    ///
    /// That drop DELETES the stale state object, so this is a third delete
    /// site alongside the destructor and @ref removeState, and any borrowed
    /// pointer to the newKey entry dies with it. No PreDestroyCallback fires
    /// for it — the dropped entry is non-live by construction, and the
    /// callback is gated on a live shell surface, so @ref removeState would
    /// not fire one for the same entry either.
    ///
    /// On success the sticky creation-failure flag at newKey is cleared, since
    /// a live shell now backs that key. oldKey's flag is deliberately left
    /// alone.
    ///
    /// Same-key (`oldKey == newKey`) is idempotent success: returns true
    /// iff a live entry exists under that key. The entry is at newKey
    /// after the call - the bool reports "the postcondition holds", not
    /// "a move happened".
    ///
    /// Surface re-anchoring is the consumer's responsibility: the layer
    /// surface's anchors / margins were baked in at attach time for
    /// oldKey, so a flavor-changing rekey (e.g. physical → virtual
    /// screen) requires the consumer to push corrected placement through
    /// the surface transport after a successful @ref rekey. Consumers
    /// that don't need re-anchoring (same-screen identifier drift) can
    /// ignore the post-rekey step.
    bool rekey(const QString& oldKey, const QString& newKey);

    /// Register an animator @p config under @p role. Pass-through to
    /// @c PhosphorAnimationLayer::SurfaceAnimator::registerConfigForRole
    /// so consumers route every animator-config write through the same
    /// host that owns the slot vocabulary. No-op when the SurfaceAnimator
    /// has not been injected.
    void registerConfigForRole(const PhosphorLayer::Role& role, PhosphorAnimationLayer::SurfaceAnimator::Config config);

    /// Animator-driven slot hide for the slot keyed by @p slotKey on the
    /// shell for @p screenId.
    ///
    /// @p completion fires:
    ///   - asynchronously, on the hide-leg's settle, when the slot was
    ///     visible and the animator ran the transition;
    ///   - synchronously, before this call returns, when nothing needed
    ///     to animate (no shell, no slot under that key, slot Item gone,
    ///     slot Item not currently visible) - consumer post-hide cleanup
    ///     (clear loader mode, release content state, restore siblings)
    ///     runs in either case.
    ///
    /// Completion is dropped when the call is a programmer-setup error:
    /// animator not injected, empty @p screenId, or empty @p slotKey - none
    /// have a recovery path the consumer could take.
    ///
    /// It is ALSO dropped when the hide leg is cancelled before it settles,
    /// which the animator does per its own cancellation contract. Hiding the
    /// shell surface cancels every leg on that surface, and so does destroying
    /// it, so a @ref syncSurfaceState that unmaps the surface or a
    /// @ref destroyShell landing while a slot hide is in flight will strand
    /// whatever that completion was going to clean up. Consumers must not rely
    /// on this completion as the only path that clears parallel per-screen
    /// state across a teardown.
    void hideSlot(const QString& screenId, const QString& slotKey, std::function<void()> completion = {});

    /// Read-write accessor. Returns the existing ShellState for
    /// @p screenId, default-constructing one in place if absent so
    /// callers can populate fields without an explicit insert. Named
    /// `getOrCreateStateFor` (not `stateFor`) to make the materialisation
    /// contract explicit — a `stateFor(...)` call that silently inserted
    /// a fresh entry on miss could surprise readers expecting a pure
    /// accessor. Pair with @ref hasState if "peek without materialising"
    /// is the intent.
    ShellState& getOrCreateStateFor(const QString& screenId);

    /// Read-only accessor. Returns nullptr if no state exists for
    /// @p screenId - callers that need to peek without materializing
    /// an empty entry should use this overload.
    const ShellState* stateFor(const QString& screenId) const;

    /// True if a ShellState exists (live or zeroed) for @p screenId.
    bool hasState(const QString& screenId) const;

    /// Remove the ShellState for @p screenId entirely. The library does
    /// NOT delete the shellSurface pointer - the caller's destroy
    /// pathway is expected to schedule that via deleteLater first.
    void removeState(const QString& screenId);

    /// Enumerate every screen id with a ShellState entry (in arbitrary
    /// QHash iteration order).
    QStringList screenIds() const;

    /// Sticky per-screen creation-failure flag. Once set, subsequent
    /// create attempts on @p screenId short-circuit until cleared
    /// (typically on screen hot-plug). Mirrors the legacy
    /// OverlayService::m_notificationCreationFailed semantics.
    void markFailure(const QString& screenId);
    void clearFailure(const QString& screenId);
    bool hasFailure(const QString& screenId) const;

    /// Snapshot of every screen id currently flagged as failed. Used by
    /// hot-plug cleanup paths that need to clear sentinels by consumer-
    /// specific id schemes (e.g. clear every virtual-screen id rooted on
    /// a now-removed physical monitor) without burdening the library
    /// with the consumer's id grammar.
    QStringList failureScreenIds() const;

private:
    /// State entries are heap-allocated raw owning pointers so the
    /// @c ShellState* values returned by @ref getOrCreateStateFor / @ref ensureShell
    /// stay valid across QHash rehashes (QHash holds @c ShellState* by
    /// value, but the pointed-to objects are stable). Consumers cache
    /// these pointers on parallel per-screen state and need them stable.
    /// The host's destructor, @ref removeState, and @ref rekey (which drops a
    /// stale entry standing at its target key) delete the pointed-to objects.
    /// Those three are the whole list, and a consumer holding a borrowed
    /// pointer must drop it at each of them; the host publishes no destroyed
    /// signal, so the PreDestroyCallback and the consumer's own call site are
    /// the only notifications.
    ///
    /// @c std::unique_ptr cannot live in @c QHash (Qt 6's hash requires
    /// copy-constructible values), and @c std::shared_ptr would add
    /// reference-counting overhead the lib does not need - there is one
    /// owner (the host) and any number of borrowed observers.
    QHash<QString, ShellState*> m_states;
    QSet<QString> m_creationFailed;
    SurfaceFactory m_surfaceFactory;
    PostCreateCallback m_postCreateCallback;
    PreDestroyCallback m_preDestroyCallback;
    PhosphorAnimationLayer::SurfaceAnimator* m_surfaceAnimator = nullptr;
};

} // namespace PhosphorOverlay
