// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"

#include <PhosphorProtocol/ServiceConstants.h>

#include <QDBusAbstractAdaptor>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <functional>

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
 * effect uses as its Mode-stamp discriminator, the strip-preview snapshot
 * (with the preset vocabulary beside it), the wheel-driven focusColumn and
 * scrollView verbs, the four absolute width/height setters for external
 * scripting, the
 * clearWindowedFullscreen reconciliation call (inbound, effect to daemon,
 * when a client leaves fullscreen on its own), the reapplyWindowGeometry
 * repair call (inbound too, for a fullscreen exit whose strip rects never
 * moved), the blueprintProgressJson template-seed report, and the
 * stripChanged wake-up that tells a preview its strip is worth re-reading.
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
    Q_PROPERTY(QVariantMap scrollEffectBehaviour READ scrollEffectBehaviour NOTIFY scrollEffectBehaviourChanged)
    Q_PROPERTY(QStringList scrollFocusScrollBlockedWindows READ scrollFocusScrollBlockedWindows NOTIFY
                   scrollFocusScrollBlockedWindowsChanged)

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

    /// The three scrolling facts the COMPOSITOR owns, as already-resolved
    /// screen-id lists: `{"focusFollowsMouse": [...], "cropStraddlers": [...],
    /// "verticalAxis": [...]}`.
    ///
    /// The focus-follows-mouse scroll cap's blocked-window list is NOT on
    /// this map; it has its own property (see @ref
    /// scrollFocusScrollBlockedWindows). These three change when settings or
    /// rules change, while that one changes on every relayout, and carrying
    /// both on one property made a strip that merely scrolled re-publish,
    /// re-parse and re-compare three screen lists that had not moved.
    ///
    /// The first two are per-context rule slots
    /// (SetScrollFocusFollowsMouse / SetScrollCropStraddlers) whose consumer
    /// lives in the KWin effect, not the engine — so unlike their four
    /// siblings they cannot ride the engine's per-screen override map. The
    /// third is the strip axis as membership (see @ref verticalAxisKey). The
    /// daemon resolves `rule ?? config` per scrolling screen and publishes the
    /// RESOLVED membership, which keeps the effect free of any config
    /// knowledge: it answers each question with a set lookup and needs no
    /// fallback of its own. A screen absent from a list has that behaviour
    /// OFF (for verticalAxis, a horizontal strip); a screen absent from the
    /// daemon's scrolling set appears in none of the three screen lists.
    ///
    /// One map rather than three list properties so the compositor pays a
    /// single bring-up query and a single change subscription for what is
    /// really one push.
    QVariantMap scrollEffectBehaviour() const;

    /// Replace the published behaviour map and broadcast on a real change.
    /// Driven by the daemon's per-screen scrolling pass, which resolves all
    /// three while it is already walking the scrolling screens. Emit-on-change:
    /// the compositor's copy is a plain cache it can re-query, so a redundant
    /// broadcast would only cost a rule-cache invalidation on the other side.
    ///
    /// All three lists are SORTED and de-duplicated here rather than taken on
    /// trust. The published contract says sorted, and the change gate is a
    /// list compare, so canonicalizing at the one write site is what makes
    /// both true whatever order the producer walked its screens in.
    /// @p verticalAxisScreens are the scrolling screens whose strip runs
    /// VERTICALLY. Absent from the list, like an absent key, means horizontal —
    /// the historical layout — so a compositor that predates the axis reads
    /// the whole desktop as horizontal, which is what it would have drawn
    /// anyway.
    void setScrollEffectBehaviour(const QStringList& focusFollowsMouseScreens, const QStringList& cropStraddlerScreens,
                                  const QStringList& verticalAxisScreens);

    /// The windows focus-follows-mouse must REFUSE to focus, because
    /// activating one would scroll the strip further than the per-context
    /// cap allows (niri's `max-scroll-amount`). WINDOW ids, not screen ids.
    ///
    /// Its own property rather than a fourth key on @ref
    /// scrollEffectBehaviour because it updates on a different clock: which
    /// windows are past the cap depends on where each strip's view sits, so
    /// the daemon re-derives it on every relayout, while the behaviour map
    /// answers settings and rules. Sharing one property made every scroll
    /// re-publish the three screen lists too, and the effect re-parse them
    /// to discover they had not moved.
    ///
    /// Empty means nothing is refused, which is what the default cap of 100
    /// percent resolves to, and it is also what an absent property reads as
    /// on the other side — so both ends of a missed update fail OPEN, with
    /// focus following the pointer as it did before the setting existed.
    QStringList scrollFocusScrollBlockedWindows() const;

    /// Replace the blocked-window list and broadcast on a real change.
    /// Sorted and de-duplicated here for the same two reasons the behaviour
    /// map's lists are: the published contract says sorted, and the change
    /// gate below is an order-sensitive list compare, so an unsorted
    /// producer would re-broadcast the same membership.
    void setScrollFocusScrollBlockedWindows(const QStringList& windowIds);

    /// Wire keys of the @ref scrollEffectBehaviour map. The spellings live in
    /// PhosphorProtocol::Service::ScrollBehaviourKey, shared with the KWin
    /// effect's reader (kwin-effect/tilinghandler/state.cpp) so a rename is a
    /// compile error on both sides; these accessors keep the daemon-side
    /// callers (this adaptor and its tests) on QString. The XML DocString
    /// spells them as prose only.
    static QString focusFollowsMouseKey()
    {
        return PhosphorProtocol::Service::ScrollBehaviourKey::FocusFollowsMouse;
    }
    static QString cropStraddlersKey()
    {
        return PhosphorProtocol::Service::ScrollBehaviourKey::CropStraddlers;
    }
    /// Membership, not a per-screen value: a screen IN this list runs its
    /// strip vertically, and absence means horizontal. Membership keeps the
    /// map's one shape rather than introducing a second, and it makes an
    /// absent key and an empty list mean the same safe thing.
    static QString verticalAxisKey()
    {
        return PhosphorProtocol::Service::ScrollBehaviourKey::VerticalAxis;
    }

    /// Clear the engine pointer during shutdown (same late-D-Bus-call
    /// contract as the sibling adaptors' clearEngine).
    void clearEngine();

    /// Install the reader scrollView uses for its step, as a PERCENT of the
    /// work area along the strip. A provider rather than a Settings pointer
    /// so this library never names the app's settings type: the daemon binds
    /// it to ShortcutManager's step getter, where the other step percents
    /// live. Unset, scrollView is a silent no-op.
    void setViewScrollStepProvider(std::function<int()> provider);

    /// Install the per-context disable gate every geometry verb on this
    /// surface consults: true when @p screenId's CURRENT scrolling context
    /// is disabled. The keyboard verbs already pass through this gate in the
    /// daemon (Daemon::isFocusedContextGatedForMode); the wheel and the
    /// absolute setters arrive on the bus instead and would otherwise pan
    /// and resize a context the user turned off. A provider rather than the
    /// resolver type so this library never names the daemon's context
    /// machinery. FAIL-CLOSED: with no provider installed every gated verb
    /// refuses, the same way the daemon's gate refuses with no resolver, so
    /// a daemon that forgot the install cannot ship an ungated wheel. The
    /// daemon installs it in the same pass that creates the adaptor.
    void setContextGateProvider(std::function<bool(const QString& screenId)> provider);

public Q_SLOTS:
    /**
     * @brief Focus the adjacent column on a scrolling screen
     *
     * The KWin effect's Meta+wheel axis shortcut calls this with the
     * cursor's screen. Gated on the engine actually owning @p screenId —
     * the engine's own screen fallback would otherwise redirect a wheel
     * event from a non-scrolling monitor onto the active scrolling one.
     *
     * @p delta is STRIP-RELATIVE, not screen-relative: -1 is the previous
     * column along the strip and +1 the next one, which reads as left/right
     * on a horizontal strip and up/down on a vertical one.
     *
     * A press at the strip's END CROSSES onto the adjacent output when
     * one exists (a different-mode neighbour defers to the daemon, which
     * activates that engine's entry-edge window), and a press on an EMPTY
     * scrolling screen crosses the same way instead of dead-ending — so a
     * wheel notch can legitimately move focus to another monitor.
     *
     * Every rejection is a SILENT no-op, not an error reply: an empty
     * @p screenId, a screen the engine does not own, a screen whose current
     * scrolling context is disabled (setContextGateProvider), and any
     * @p delta other than -1 or +1 all return without acting, so a caller
     * cannot distinguish a refusal from a call that landed on an empty strip.
     *
     * @param screenId Screen whose strip should move (the cursor's screen);
     *                 an empty string is ignored
     * @param delta -1 focuses the previous column along the strip, +1 the
     *              next one; any other value is ignored
     */
    void focusColumn(const QString& screenId, int delta);

    /**
     * @brief Scroll the view one step along the strip without moving focus
     *
     * The pointer-gesture twin of focusColumn: the KWin effect's
     * Meta+Shift+wheel axis shortcut calls this once per notch with the
     * cursor's screen. One step is the view scroll step setting, read
     * through the provider the daemon installs with setViewScrollStepProvider.
     * The keyboard's pan is the PAGE (a whole viewport, on its own shortcut
     * pair); the step has no keyboard row, for the same reason focusColumn
     * has none. Same silent wire-boundary policy as focusColumn (ownership
     * and per-context gates). A notch at the strip's end never crosses to
     * another output: it answers with the no-target navigation OSD like the
     * sibling verbs, and a step that rounds to zero pixels answers under its
     * own no_movement reason.
     *
     * @param screenId Screen whose strip should move (the cursor's screen);
     *                 an empty string is ignored
     * @param delta -1 scrolls toward the strip's start, +1 toward its end;
     *              any other value is ignored
     */
    void scrollView(const QString& screenId, int delta);

    /**
     * @brief Absolute width/height intents for the focused column and window
     *
     * The D-Bus home of niri's absolute set-column-width and
     * set-window-height: a global shortcut carries no value argument, so the
     * absolute setters live only on this surface. All four share focusColumn's
     * silent ownership and per-context gates, and each refuses out-of-range
     * values silently —
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
     * A tile whose column draws a tab indicator carries four more keys —
     * tabCount, activeTab, tabPosition and tabLength (PhosphorProtocol
     * StripPreviewKey) — describing the bar that column shows. A column
     * drawing none carries no tab key at all, so an absent key and a daemon
     * from before this payload read the same. Those keys are the only thing
     * distinguishing a stack of tabs from the single window the walk emits
     * for it, since the walk lists a tabbed column's SHOWN tab only.
     *
     * Excluded and unnumbered: hidden tabs of a tabbed column, minimized
     * tiles, and tiles whose intersection with the work area is EMPTY (a
     * stack whose minimum heights overflow the work area resolves its tail
     * below the bottom edge). Partially visible columns are clipped rather
     * than dropped, with no minimum-visibility threshold, so an arbitrarily
     * thin sliver still carries its number — and a barely-visible column
     * parked below its peek floor keeps its number too, which is what makes
     * a digit press for it work (see ScrollEngine.h).
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
     * screen. Same ownership gate as focusColumn (reads are not
     * context-gated): an empty object when
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
     * @brief How far the screen has worked through its template blueprint
     *
     * Returns a JSON object {"total": n, "used": n}. @c total is how many
     * starting columns the context's resolved scrolling template declares,
     * and @c used is how many of them the screen's strip has already taken.
     * An entry is spent once a column has taken it, so a closed column does
     * not return its entry to the pool, and @c used == @c total means further
     * columns open at the template's ordinary width and display instead.
     * The count restarts when the strip empties or the screen is given a
     * different template.
     *
     * Reports the CURRENT (desktop, activity) context, matching
     * visibleStripJson. Same ownership gate as focusColumn (reads are not
     * context-gated): an empty
     * object when the screen is not scrolling.
     *
     * @param screenId Screen whose progress to describe
     * @return JSON object string
     */
    QString blueprintProgressJson(const QString& screenId) const;

Q_SIGNALS:
    /**
     * @brief Emitted when the set of screens using the scrolling engine changes
     * @param screenIds List of screen IDs currently in scrolling mode
     */
    void scrollingScreensChanged(const QStringList& screenIds);

    /// The resolved per-screen effect-owned behaviour changed. Payload is the
    /// whole map (see @ref scrollEffectBehaviour), so a receiver never has to
    /// merge a delta against a copy it might have missed an update to.
    void scrollEffectBehaviourChanged(const QVariantMap& behaviour);

    /// The focus-follows-mouse scroll cap's blocked-window list changed.
    /// Payload is the whole list (see @ref scrollFocusScrollBlockedWindows),
    /// so a receiver never merges a delta. Separate from
    /// scrollEffectBehaviourChanged because it fires on every relayout that
    /// moves the answer, while that one fires on a settings or rule change.
    void scrollFocusScrollBlockedWindowsChanged(const QStringList& windowIds);

    /// @p screenId's strip changed shape: a wake-up for anyone rendering it,
    /// not a payload. A receiver re-reads @ref visibleStripJson if it cares.
    ///
    /// Carries no payload and is NOT gated against the strip payload, which
    /// is the deliberate half. Payload-gating it would mean resolving the
    /// strip on every placement change — a full relayout, paid on the daemon
    /// whether or not anything is listening — to suppress wake-ups whose only
    /// cost on the other side is one D-Bus read that the reader already
    /// discards when it fingerprints identical. The gate that DOES apply is
    /// the engine's own: this relays placementChanged, which the engine emits
    /// on a placement or focus change rather than on every relayout.
    ///
    /// That gate runs ONE WAY only, and a renderer must not read it as "every
    /// strip change wakes me". A settings, rule or template push re-resolves
    /// the rects through applyLayout, which announces the batch separately and
    /// emits placementChanged only if the view anchor moved — so a relayout
    /// that changes column widths while the anchor stays put changes the strip
    /// without waking anything here. A periodic re-read is still the backstop;
    /// this signal only makes the common cases prompt.
    ///
    /// So a receiver must treat this as "re-read soon", never as "the strip
    /// differs": a placement change that moves a floating window, or one on a
    /// context this screen is not currently showing, fires it without
    /// changing a single visible tile. Receivers should also coalesce — a
    /// drag or a burst of opens emits per step.
    void stripChanged(const QString& screenId);

private:
    PhosphorScrollEngine::ScrollEngine* m_engine = nullptr;
    /// scrollView's step reader (setViewScrollStepProvider).
    std::function<int()> m_viewScrollStep;
    /// The per-context disable gate (setContextGateProvider).
    std::function<bool(const QString&)> m_contextGated;
    /// Whether the verb must refuse for @p screenId's context: the gate
    /// says so, or no gate is installed (fail-closed, see the setter). Called
    /// AFTER the ownership gate, which is the cheaper test and the one the
    /// keyboard path also resolves first.
    bool refusesForContext(const QString& screenId) const;
    /// Last set broadcast on the bus (the change gate's memory; the engine
    /// re-emits identical sets on desktop switches for the tiling channel).
    QStringList m_lastBroadcastScreens;
    /// Published copy of @ref scrollEffectBehaviour. Held as the built map so
    /// the property read is a plain copy and the change compare is one
    /// QVariantMap compare rather than three list compares.
    QVariantMap m_scrollEffectBehaviour;
    /// Published copy of @ref scrollFocusScrollBlockedWindows (the change
    /// gate's memory, and what makes a re-publish of the same membership a
    /// local no-op on the per-relayout path).
    QStringList m_scrollFocusScrollBlockedWindows;
};

} // namespace PlasmaZones
