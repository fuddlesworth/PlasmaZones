// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

// ShellHost — owns the per-screen layer-shell shell-state map and the
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

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include <functional>

class QScreen;

namespace PhosphorLayer {
class Surface;
} // namespace PhosphorLayer

namespace PhosphorOverlay {

class PHOSPHOROVERLAY_EXPORT ShellHost : public QObject
{
    Q_OBJECT

public:
    /// Consumer-provided factory for the per-screen layer-shell surface.
    /// The library does not know which Role / qmlSource / SurfaceManager
    /// the consumer wires through — the factory encapsulates all of
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

    /// Idempotent: bring up (or return) the per-screen shell for
    /// @p screenId on @p physScreen. Returns a pointer to the ShellState
    /// on success. Returns nullptr if no shell exists AND the factory
    /// failed to create one (sticky-failure flag set automatically).
    /// If a previous create attempt failed and the failure flag is still
    /// set, returns the existing state (possibly with shellSurface=null)
    /// or nullptr if no state was ever materialized.
    ShellState* ensureShell(const QString& screenId, QScreen* physScreen);

    /// Tear down the shell for @p screenId. Fires the pre-destroy
    /// callback before scheduling shellSurface for deletion via
    /// deleteLater. After return, the ShellState entry survives with
    /// every shell-mechanism field nulled — callers that want the entry
    /// removed entirely should follow up with @ref removeState.
    void destroyShell(const QString& screenId);

    /// Reconcile the shell's mapped state + pointer-input region with
    /// the consumer's view of what's live on this screen.
    ///
    /// @p anyVisible — true when at least one slot wants the surface
    /// mapped (driven by the consumer's slot-visibility check). Brings
    /// the surface up on the first transition from never-shown → live;
    /// subsequent visibility transitions toggle the input flag directly
    /// without re-entering Surface::show()/hide() (the keep-mapped hide
    /// path cancels per-surface animator tracking, which would wipe
    /// in-flight beginShow's on OTHER slots on the same shell).
    ///
    /// @p anyInputGrabbing — true when at least one modal slot
    /// (consumer-defined; PZ today: snap-assist + layout picker) wants
    /// pointer input. When false the shell's QQuickWindow is flagged
    /// Qt::WindowTransparentForInput so background windows stay
    /// interactable beneath non-modal slots (OSDs, main overlay, zone
    /// selector during drag).
    ///
    /// No-op when the shell surface or window is not yet up.
    void syncSurfaceState(const QString& screenId, bool anyVisible, bool anyInputGrabbing);

    /// Read-write accessor. Returns the existing ShellState for
    /// @p screenId, default-constructing one in place if absent so
    /// callers can populate fields without an explicit insert.
    ShellState& stateFor(const QString& screenId);

    /// Read-only accessor. Returns nullptr if no state exists for
    /// @p screenId — callers that need to peek without materializing
    /// an empty entry should use this overload.
    const ShellState* stateFor(const QString& screenId) const;

    /// True if a ShellState exists (live or zeroed) for @p screenId.
    bool hasState(const QString& screenId) const;

    /// Remove the ShellState for @p screenId entirely. The library does
    /// NOT delete the shellSurface pointer — the caller's destroy
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
    QHash<QString, ShellState> m_states;
    QSet<QString> m_creationFailed;
    SurfaceFactory m_surfaceFactory;
    PostCreateCallback m_postCreateCallback;
    PreDestroyCallback m_preDestroyCallback;
};

} // namespace PhosphorOverlay
