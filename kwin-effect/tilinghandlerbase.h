// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QHash>
#include <QObject>
#include <QPointF>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace KWin {
class EffectWindow;
}

namespace PlasmaZones {

class PlasmaZonesEffect;

/**
 * @brief Common state and primitives for tiling-mode lifecycle handlers.
 *
 * @c ScrollHandler and @c AutotileHandler each route window-lifecycle events
 * to their respective daemon-side engine over D-Bus. The two share an
 * irreducible set of bookkeeping:
 *
 *   - @c m_notifiedWindows: windows currently reported open to the engine
 *   - @c m_notifiedWindowScreens: each tracked window's screen at report time
 *   - @c m_pendingCloses: windows that closed before their windowOpened ack
 *     resolved (D-Bus ordering race) — the late ack is suppressed
 *   - @c m_focusFollowsMouse: FFM master toggle
 *
 * Every other piece of state is mode-specific:
 *   - Autotile owns per-screen borderless buckets, monocle-maximized tracking,
 *     pre-autotile geometry caches, stagger generations, and a re-entrancy
 *     guard for handleWindowOutputChanged.
 *   - Scroll owns a flat borderless set, slot/applied geometry caches, a
 *     re-assert debounce timer with re-entrancy guard, drag-to-reorder bookkeeping,
 *     and a daemon-epoch counter for rolling back failed D-Bus calls safely
 *     across reconnects.
 *
 * The base deliberately does NOT abstract the signal-wiring / settings-load
 * surface: each subclass listens to a distinct set of D-Bus signals
 * (autotile has five mode-specific ones, scroll has one), and the post-load
 * batch they trigger has different shape (autotile filters by screen, scroll
 * always sends the whole stacking order). Forcing a template method there
 * would just smear the differences without saving lines.
 *
 * The pure-virtual @c interfaceName() / @c screensProperty() hooks document
 * the per-mode constants without unifying the call sites — they are kept
 * available for any future helper that needs them but are not consumed by
 * the base today (the subclasses still inline @c Service::Interface::Scroll
 * / @c Autotile at their D-Bus call sites for clarity).
 */
class TilingHandlerBase : public QObject
{
    Q_OBJECT

public:
    explicit TilingHandlerBase(PlasmaZonesEffect* effect, QObject* parent = nullptr);
    ~TilingHandlerBase() override = default;

    // ── Focus follows mouse ─────────────────────────────────────────────
    /// Enable/disable focus-follows-mouse for this handler's screens.
    void setFocusFollowsMouse(bool enabled);

protected:
    // ── Mode-specific D-Bus identifiers ─────────────────────────────────
    /// Fully-qualified D-Bus interface name (e.g. @c org.plasmazones.Scroll).
    /// Available for derived helpers that want to factor an asyncCall site.
    virtual QString interfaceName() const = 0;
    /// Property name on @c interfaceName() that publishes this mode's screen
    /// set (e.g. @c scrollScreens, @c autotileScreens). Available for derived
    /// helpers that share the @c org.freedesktop.DBus.Properties.Get pattern.
    virtual QString screensProperty() const = 0;

    PlasmaZonesEffect* m_effect; ///< Non-owning. The effect outlives this handler.

    /// Windows currently reported open to this mode's engine.
    QSet<QString> m_notifiedWindows;
    /// windowId → screen ID at the time the window was reported. The
    /// authoritative answer to "where does the engine think this window
    /// lives?" — does not change implicitly when KWin moves the window
    /// across outputs; subclasses update it via their handleWindowOutputChanged
    /// path so the per-mode logic can detect and act on the move.
    QHash<QString, QString> m_notifiedWindowScreens;
    /// Windows closed before their @c windowOpened D-Bus call resolved; the
    /// matching open is suppressed when it arrives (D-Bus ordering race).
    QSet<QString> m_pendingCloses;

    /// Focus-follows-mouse enabled for this handler's screens. The
    /// per-handler @c handleCursorMoved compares the under-cursor window
    /// against @c KWin::effects->activeWindow() rather than caching a last-
    /// auto-focused id, because that cache went stale every time another
    /// path changed compositor focus and silently suppressed legitimate
    /// re-focuses (see fix #503 / discussion #461 item 13).
    bool m_focusFollowsMouse = false;

    /// Incremented on every daemon (re)connect via @c onDaemonReady (or its
    /// per-handler equivalent). A D-Bus reply captures the epoch at call time
    /// and skips its rollback if the daemon reconnected meanwhile —
    /// onDaemonReady has already rebuilt the tracking sets, and removing the
    /// windowId here would corrupt that fresh state.
    ///
    /// Lives on the base because both AutotileHandler and ScrollHandler need
    /// the same rollback-skip pattern: a stale @c windowOpened ack landing
    /// after a daemon restart must not mutate @c m_notifiedWindows, which the
    /// daemon-ready handler has already re-seeded from a fresh batch.
    ///
    /// @c quint64 because the increment is a generation counter and unsigned
    /// wrap is well-defined; signed overflow would be UB. The wider type
    /// pushes any theoretical wrap so far out it never matters in practice.
    quint64 m_daemonEpoch = 0;
};

} // namespace PlasmaZones
