// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"

#include <QDBusAbstractAdaptor>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace PhosphorScrollEngine {
class ScrollEngine;
}

namespace PlasmaZones {

/**
 * @brief D-Bus adaptor for the scrolling placement engine
 *
 * Provides D-Bus interface: org.plasmazones.Scrolling
 *
 * The scroll-SPECIFIC wire surface: the scrolling screen set the KWin
 * effect uses as its Mode-stamp discriminator, the strip-preview snapshot,
 * the wheel-driven focusColumn verb, the clearWindowedFullscreen
 * reconciliation call (inbound, effect to daemon, when a client leaves
 * fullscreen on its own), and the reapplyWindowGeometry repair call
 * (inbound too, for a fullscreen exit whose strip rects never moved).
 * Window lifecycle and tile-request traffic for
 * scrolling screens deliberately stays on org.plasmazones.Tiling — the
 * effect keeps ONE engine-managed screen set and one geometry pipeline
 * for both tiling-family engines, and TilingAdaptor routes per screen.
 */
class PLASMAZONES_EXPORT ScrollingAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.plasmazones.Scrolling")

    Q_PROPERTY(QStringList scrollingScreens READ scrollingScreens NOTIFY scrollingScreensChanged)

public:
    explicit ScrollingAdaptor(PhosphorScrollEngine::ScrollEngine* engine, QObject* parent = nullptr);
    ~ScrollingAdaptor() override = default;

    /// The scrolling screen set, sorted so a property read and a signal
    /// payload for the same set compare equal.
    ///
    /// This is the RAW set. The KWin effect does not answer its Mode
    /// discriminator from it directly: it intersects this with the
    /// engine-managed union published on org.plasmazones.Tiling at read
    /// time, because the two arrive as independent signals with no
    /// ordering guarantee and this set can transiently name a screen the
    /// union has already dropped.
    QStringList scrollingScreens() const;

    /// Clear the engine pointer during shutdown (same late-D-Bus-call
    /// contract as the sibling adaptors' clearEngine).
    void clearEngine();

    /// Record (or drop, for @p surfaceId 0) the tab-indicator surface for
    /// @p screenId and broadcast the change. Driven by the overlay service; no
    /// engine involvement, since the strip's model knows nothing about which
    /// wl_surface happens to be drawing its indicators.
    void setScrollTabSurface(const QString& screenId, quint32 surfaceId);

public Q_SLOTS:
    /**
     * @brief Focus the adjacent column on a scrolling screen
     *
     * The KWin effect's Meta+wheel axis shortcut calls this with the
     * cursor's screen. Gated on the engine actually owning @p screenId —
     * the engine's own screen fallback would otherwise redirect a wheel
     * event from a non-scrolling monitor onto the active scrolling one.
     *
     * Every rejection is a SILENT no-op, not an error reply: an empty
     * @p screenId, a screen the engine does not own, and any @p delta
     * other than -1 or +1 all return without acting, so a caller cannot
     * distinguish a refusal from a call that landed on an empty strip.
     *
     * @param screenId Screen whose strip should move (the cursor's screen);
     *                 an empty string is ignored
     * @param delta -1 focuses the column to the left, +1 to the right; any
     *              other value is ignored
     */
    void focusColumn(const QString& screenId, int delta);

    /**
     * @brief Absolute width/height intents for the focused column and window
     *
     * The D-Bus home of niri's absolute set-column-width and
     * set-window-height: a global shortcut carries no value argument, so the
     * absolute setters live only on this surface. All four share focusColumn's
     * silent ownership gate, and each refuses out-of-range values silently —
     * proportions outside the settings UI's proportion range, pixels outside
     * its fixed range (the width and height fixed ranges happen to agree
     * today; each is validated against its own accessor). A value equal to
     * the current intent answers with a no-target OSD, like the step verbs.
     *
     * Width proportions are exact (ColumnWidth has a Proportion kind).
     * Height proportions are NOT: the strip model stores them as a fraction
     * anchor that snaps to the nearest effective height preset at relayout
     * (WindowHeight::Preset's value-anchored contract), so an exact height
     * needs the pixel form.
     *
     * NOTE: nothing in this tree calls these four — they exist FOR external
     * scripting, like presetVocabularyJson below. Do not re-justify them by
     * naming an in-tree caller; there is none beyond the contract tests.
     */
    void setColumnWidthProportion(const QString& screenId, double proportion);
    void setColumnWidthPixels(const QString& screenId, int px);
    void setWindowHeightProportion(const QString& screenId, double proportion);
    void setWindowHeightPixels(const QString& screenId, int px);

    /**
     * @brief Drop a window's windowed-fullscreen flag (compositor reconciliation)
     *
     * The KWin effect calls this when a windowed-fullscreen client leaves
     * fullscreen on its own (the app's in-app toggle), so the strip's flag
     * follows reality. Silent no-op for an unknown window or one whose
     * flag is not set, same wire-boundary policy as focusColumn.
     *
     * @param windowId Window whose flag to clear; an empty string is ignored
     */
    void clearWindowedFullscreen(const QString& windowId);

    /**
     * @brief Re-emit a window's true strip rect (compositor repair)
     *
     * The KWin effect calls this when the compositor moved a window behind
     * the engine's back (KWin restores a fullscreen-exiting window to its
     * pre-fullscreen rect one round-trip after the batch). The engine
     * evicts the window's emit-gate memory and relayouts its screen, so
     * the next batch re-carries the rect the gate would otherwise keep
     * silent. Silent no-op for an unknown window, same wire-boundary
     * policy as focusColumn.
     *
     * @param windowId Window to re-emit; an empty string is ignored
     */
    void reapplyWindowGeometry(const QString& windowId);

    /**
     * @brief The strip as it currently looks on a screen, for previews
     *
     * Returns a JSON array with ONE OBJECT PER TILE carrying
     * {x, y, width, height} plus zoneNumber, the 1-based visible tile slot
     * the rect occupies in strip order. The rects are 0.0 to 1.0 per axis,
     * normalized against the screen's FULL geometry — the tiles themselves
     * are clipped to the gap-inset work area, so the fractions show the
     * panel gap, and a consumer maps back to pixels by scaling against the
     * screen rectangle. Same basis as the daemon's own OSD strip card.
     *
     * Excluded and unnumbered: hidden tabs of a tabbed column, minimized
     * tiles, parked columns, and tiles whose intersection with the work
     * area is EMPTY (a stack whose minimum heights overflow the work area
     * resolves its tail below the bottom edge). Partially visible columns
     * are clipped rather than dropped, with no minimum-visibility
     * threshold, so an arbitrarily thin sliver still carries its number.
     *
     * The settings app renders it where the other modes show a layout
     * thumbnail. Empty array when the screen has no strip or is not
     * scrolling.
     *
     * @param screenId Screen whose strip to describe
     * @return JSON array string
     */
    QString visibleStripJson(const QString& screenId) const;

    /**
     * @brief The screen's effective preset vocabulary, for inspection
     *
     * Returns a JSON object {"columnWidths": [...], "windowHeights": [...]}
     * of 0.0 to 1.0 fractions. Each list resolves independently: a list the
     * context's resolved scrolling template supplies is that template's own
     * preset list, and a list it does not supply falls back to the configured
     * preset list, so a template that defines widths but no heights yields
     * template widths beside the configured heights. This is the vocabulary
     * the cycle-preset-width and cycle-preset-height shortcuts walk on that
     * screen. Same silent ownership gate as focusColumn: an empty object when
     * the screen is not scrolling.
     *
     * NOTE: nothing in this tree calls it. It stays as part of the PUBLISHED
     * D-Bus read surface (declared in org.plasmazones.Scrolling.xml, covered
     * by tests/unit/dbus), for external clients that want to know which
     * fractions the cycle shortcuts will walk. Do not re-justify it by naming
     * an in-tree caller; there is none.
     *
     * @param screenId Screen whose vocabulary to describe
     * @return JSON object string
     */
    QString presetVocabularyJson(const QString& screenId) const;

    /**
     * @brief Every live tab-indicator surface, as screenId → wl_surface id.
     *
     * The replay half of @c scrollTabSurfaceChanged, for a compositor-side
     * consumer that starts (or restarts) after the surfaces already exist and
     * would otherwise wait for a scroll that never re-announces them.
     *
     * @return Map of effective screen id to wl_surface protocol object id
     */
    QVariantMap scrollTabSurfaces() const;

Q_SIGNALS:
    /**
     * @brief Emitted when the set of screens using the scrolling engine changes
     * @param screenIds List of screen IDs currently in scrolling mode
     */
    void scrollingScreensChanged(const QStringList& screenIds);

    /**
     * @brief The wl_surface drawing @p screenId's tab indicators changed.
     *
     * The compositor slides that surface with the scrolling strip so the
     * indicators travel with the columns they label, and it has no other way
     * to tell it apart from the daemon's other overlays: they share a window
     * class, and a layer surface's scope is not exposed per window.
     *
     * @param screenId  Effective screen id
     * @param surfaceId wl_surface protocol object id, or 0 when the surface is
     *                  gone. A zero always precedes the surface's destruction,
     *                  because Wayland reuses object ids and a stale
     *                  registration would come to name an unrelated surface.
     */
    void scrollTabSurfaceChanged(const QString& screenId, quint32 surfaceId);

private:
    PhosphorScrollEngine::ScrollEngine* m_engine = nullptr;
    /// Last set broadcast on the bus (the change gate's memory; the engine
    /// re-emits identical sets on desktop switches for the tiling channel).
    QStringList m_lastBroadcastScreens;
    /// Live tab-indicator surfaces, screenId → wl_surface id. Held here rather
    /// than read back from the overlay service so the replay getter answers
    /// from the same values the signal published.
    QHash<QString, quint32> m_scrollTabSurfaces;
    /// Terminal latch set by clearEngine(): the overlay-service connection
    /// feeding setScrollTabSurface survives the clear (its context object is
    /// this adaptor), and a late push must not repopulate the registry.
    bool m_engineCleared = false;
};

} // namespace PlasmaZones
