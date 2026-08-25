// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// ═══════════════════════════════════════════════════════════════════════════════
// Daemon — scrolling-engine screen-set management
//
// The scrolling counterpart of updateEngineScreens' engine push: order
// seeding across mode transitions, per-context rule-param resolution, and
// the setActiveScreens handoff. Driven
// from updateEngineScreens (one cascade walk derives both engines' sets) so
// the two sets always flip in the same recompute.
// ═══════════════════════════════════════════════════════════════════════════════

#include "daemon/daemon.h"
#include "config/settings.h"
#include "core/platform/logging.h"
// Complete type needed for setScrollDropIndicatorOverrides — daemon.h forward
// declares OverlayService only.
#include "daemon/overlayservice.h"
// Complete type needed for setScrollEffectBehaviour — daemon.h forward
// declares ScrollingAdaptor only.
#include "dbus/scrollingadaptor/scrollingadaptor.h"
#include "dbus/tilingadaptor/tilingadaptor.h"
#include "seedorderfilter.h"

#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"
// WindowColorKeys — the one home for the five colour-override key spellings
// this file produces. The three tab colours are consumed by the KWin effect,
// which paints the pills; the two indicator colours by the overlay's
// drop-indicator slot.
#include "dbus/windowtrackingadaptor/internal.h"

#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorPlacement/WindowTrackingService.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorZones/LayoutRegistry.h>

#include <algorithm>

namespace PlasmaZones {

void Daemon::captureScrollingOrders(const QSet<QString>& scrollingScreens)
{
    // Capture window order for screens LEAVING scrolling before their strip
    // states are destroyed. Called from updateEngineScreens in the shared
    // capture phase (capture-all → seed-all → apply-all), BEFORE either
    // engine seeds, so a same-pass scrolling→autotile flip replays the
    // column order as tiles and the reverse replays tiles as columns.
    if (!m_scrollEngine) {
        return;
    }
    const QString activity = currentActivity();
    const QSet<QString> currentScrollScreens = m_scrollEngine->activeScreens();
    for (const QString& screenId : currentScrollScreens - scrollingScreens) {
        // The non-creating state gate the autotile capture carries, and for
        // its exact documented corruption: a per-output desktop switch runs
        // setCurrentDesktopForScreen BEFORE this capture, so the read below
        // resolves through the NEW desktop's key — usually empty — and the
        // store then filed that empty order under the new desktop, wiping
        // whatever it had saved from an earlier toggle. No state for the
        // screen's current context means there is nothing true to capture.
        // (Both stateForScreen overloads are non-creating; this is the
        // autotile twin's exact shape.)
        if (!m_scrollEngine->stateForScreen(screenId)) {
            continue;
        }
        const int desktop = currentDesktopForScreen(screenId);
        // Stored UNCONDITIONALLY, empty included. An empty order must
        // overwrite a stale non-empty entry from an earlier toggle, or
        // re-entry resurrects windows that have since closed or left the
        // screen as columns. (The autotile capture documents the same rule;
        // its mode-toggle caller additionally pre-clears the toggled screen's
        // key, which this path has no equivalent of.)
        m_lastEngineOrders[TilingStateKey{screenId, desktop, activity}] = m_scrollEngine->managedWindowOrder(screenId);
    }
}

void Daemon::updateScrollingScreens(const QSet<QString>& scrollingScreens)
{
    if (!m_scrollEngine || !m_layoutManager || !m_settings) {
        return;
    }
    const QString activity = currentActivity();

    // Seed order for screens ENTERING scrolling from a captured order — the
    // deterministic mode-transition contract shared with autotile via
    // m_lastEngineOrders. Leaving-screen capture already ran in
    // captureScrollingOrders (the shared capture phase in
    // updateEngineScreens).
    //
    // Loop-invariant, so resolved once: the seed's admission rule is the same
    // as the autotile twin's — float is per mode, so non-minimized entries
    // always seed (a snap-mode float must not make the window unmanageable as
    // a strip column); minimized entries stay as placeholders except
    // user-floated-then-minimized ones. See filterEngineSeedOrder's doc.
    const QSet<QString> currentScrollScreens = m_scrollEngine->activeScreens();
    const QSet<QString> enteringScreens = scrollingScreens - currentScrollScreens;
    PhosphorPlacement::WindowTrackingService* wts =
        m_windowTrackingAdaptor ? m_windowTrackingAdaptor->service() : nullptr;
    if (!enteringScreens.isEmpty()) {
        if (!wts) {
            // Fail CLOSED, like the autotile twin. filterEngineSeedOrder
            // early-returns without a WTS, so seeding here would stage the
            // saved order UNFILTERED and hand a user-floated-then-minimized
            // window to the strip as a column instead of restoring its float.
            // Only the SEED is skipped, not the whole function: the screen set
            // and the per-screen overrides below don't depend on the WTS, and
            // withholding them would leave scrolling screens unclaimed.
            qCWarning(lcDaemon) << "updateScrollingScreens: no WindowTrackingService — refusing unfiltered seed";
        } else if (!wts->windowRegistry()) {
            // Mirror the autotile twin's registry warning: without a registry
            // the filter cannot read minimized state, so every entry reads as
            // non-minimized and the user-floated-then-minimized drop silently
            // stops firing. Degrading is correct, doing it quietly is not.
            qCWarning(lcDaemon) << "updateScrollingScreens: no WindowRegistry —"
                                << "the minimized seed filter degrades to pass-through";
        }
    }
    // No WTS means no correct seed, so the loop is skipped rather than run
    // unfiltered; everything below it still runs (see the warning above).
    static const QSet<QString> kNoScreens;
    for (const QString& screenId : wts ? enteringScreens : kNoScreens) {
        // (Snap-float presave for screens entering scrolling from snapping
        // runs in updateEngineScreens' derive phase, BEFORE any engine set
        // is applied — by this point snap's capturePlacement would already
        // refuse the screen's new mode and capture nothing.)
        const int desktop = currentDesktopForScreen(screenId);
        const auto it = m_lastEngineOrders.constFind(TilingStateKey{screenId, desktop, activity});
        if (it == m_lastEngineOrders.constEnd()) {
            // No captured order means this context has never been in
            // scrolling, and unlike the autotile twin there is deliberately
            // no spatial fallback here. buildZoneOrderedWindowList orders by
            // ZONE, which on a screen that was never tiled says nothing about
            // strip position; the strip instead takes its first-entry order
            // from the sequence the engine adopts windows in, which is the
            // order the compositor announces them. Only RE-entry is
            // order-deterministic, and that is what the capture exists for.
            continue;
        }
        QStringList order = it.value();
        filterEngineSeedOrder(order, wts, wts->windowRegistry(), PhosphorEngine::WindowPlacement::scrollingEngineId());
        if (!order.isEmpty()) {
            m_scrollEngine->setInitialWindowOrder(screenId, order);
        }
    }

    // Four-tier precedence, collapsed daemon-side exactly like autotile's
    // updateEngineScreens: per-screen SETTINGS seed the map first,
    // per-context RULES overwrite where a slot is filled, the context's
    // TEMPLATE pushes its whole shape over the settings-channel slots (preset
    // vocabularies, seed blueprint and the beyond-blueprint width trio, which
    // are all keys the rule channel never writes, plus the one display key it
    // shares with a rule slot and therefore only writes when no rule filled
    // it), and the engine's effective* readers keep the final two-way
    // `override ?? global-config` fallback. So: rule > template > settings >
    // engine fallback, described in full at the template block below.
    // The settings map is engine-spelled (PerScreenScrollingKey ==
    // ScrollPerScreenKeys settings channel), so the seed is a plain copy;
    // rules write their own channel keys (bare-fraction width/height plus the
    // shared int keys), which the engine reads first.
    // The two EFFECT-owned behaviours are collected as this loop walks the
    // scrolling screens, then published in one push below. They cannot ride
    // the engine's override map like their four siblings: focus-follows-mouse
    // and the straddler PAINT clip both live in the compositor, so the daemon
    // resolves `rule ?? config` here and hands the compositor the resolved
    // membership.
    QStringList ffmScreens;
    QStringList cropScreens;
    // Rebuilt from scratch by this walk rather than updated in place: a screen
    // that left scrolling, or whose rule stopped capping, must lose its entry,
    // and clearing on the way in is what makes the empty hash mean what
    // publishScrollFocusScrollBlocks reads it as.
    m_scrollFfmMaxScrollPercent.clear();
    for (const QString& screenId : scrollingScreens) {
        QVariantMap overrides = m_settings->getPerScreenScrollingSettings(screenId);
        if (overrides.isEmpty() && PhosphorIdentity::VirtualScreenId::isVirtual(screenId)) {
            // Virtual→physical fallback, mirroring autotile: a virtual
            // screen without its own overrides inherits its physical
            // parent's.
            overrides = m_settings->getPerScreenScrollingSettings(
                PhosphorIdentity::VirtualScreenId::extractPhysicalId(screenId));
        }
        const int desktop = currentDesktopForScreen(screenId);
        const PhosphorZones::ContextScrollingParams params =
            m_layoutManager->resolveContextScrollingParams(screenId, desktop, activity);
        if (params.centerFocusedColumn) {
            overrides.insert(PhosphorScrollEngine::ScrollPerScreenKeys::centerFocusedColumn(),
                             *params.centerFocusedColumn);
        }
        if (params.defaultColumnWidth) {
            overrides.insert(PhosphorScrollEngine::ScrollPerScreenKeys::defaultColumnWidth(),
                             *params.defaultColumnWidth);
        }
        if (params.defaultColumnDisplay) {
            overrides.insert(PhosphorScrollEngine::ScrollPerScreenKeys::defaultColumnDisplay(),
                             *params.defaultColumnDisplay);
        }
        if (params.insertPosition) {
            overrides.insert(PhosphorScrollEngine::ScrollPerScreenKeys::insertPosition(), *params.insertPosition);
        }
        if (params.defaultWindowHeight) {
            overrides.insert(PhosphorScrollEngine::ScrollPerScreenKeys::defaultWindowHeight(),
                             *params.defaultWindowHeight);
        }
        // Behaviour toggles — rules-only keys (the per-screen settings store
        // writes none of them), so an absent key leaves the engine on the
        // global config value via its effective* readers.
        if (params.alwaysCenterSingleColumn) {
            overrides.insert(PhosphorScrollEngine::ScrollPerScreenKeys::alwaysCenterSingleColumn(),
                             *params.alwaysCenterSingleColumn);
        }
        if (params.respectMinimumSize) {
            overrides.insert(PhosphorScrollEngine::ScrollPerScreenKeys::respectMinimumSize(),
                             *params.respectMinimumSize);
        }
        if (params.cropStraddlers) {
            overrides.insert(PhosphorScrollEngine::ScrollPerScreenKeys::cropStraddlers(), *params.cropStraddlers);
        }
        if (params.focusNewWindows) {
            overrides.insert(PhosphorScrollEngine::ScrollPerScreenKeys::focusNewWindows(), *params.focusNewWindows);
        }
        if (params.smartGaps) {
            overrides.insert(PhosphorScrollEngine::ScrollPerScreenKeys::smartGaps(), *params.smartGaps);
        }
        if (params.stickyWindowHandling) {
            overrides.insert(PhosphorScrollEngine::ScrollPerScreenKeys::stickyWindowHandling(),
                             *params.stickyWindowHandling);
        }
        // The strip axis shares its key with the settings channel (the seed
        // above may already carry it from the per-screen store), so this
        // insert IS the precedence collapse: rule > per-screen setting >
        // global. The axis membership the second walk below publishes to the
        // effect resolves through the engine's merged map, so the rule's
        // verdict reaches the compositor with no channel of its own.
        if (params.stripAxis) {
            overrides.insert(PhosphorScrollEngine::ScrollPerScreenKeys::stripAxis(), *params.stripAxis);
        }
        // The effect-owned pair, resolved to a verdict here rather than
        // forwarded as an override: `rule ?? config`, so the compositor gets
        // membership it can answer with a set lookup. cropStraddlers ALSO
        // rides the override map above (the engine's own straddler clamp
        // reads it), so it is the one behaviour with two consumers — both
        // resolve from this same slot, so they cannot disagree.
        // m_settings is unguarded here: the function's entry check already
        // returns on a null one, so a second test only read as though the
        // resolve were optional.
        const bool focusFollowsMouse = params.focusFollowsMouse.value_or(m_settings->scrollingFocusFollowsMouse());
        if (focusFollowsMouse) {
            ffmScreens.append(screenId);
        }
        if (params.cropStraddlers.value_or(m_settings->scrollingCropStraddlers())) {
            cropScreens.append(screenId);
        }
        // The CAP on focus-follows-mouse, resolved the same `rule ?? config`
        // way but not publishable as membership: which windows it refuses
        // depends on the strip's layout and the view, so only the percent is
        // resolved here and publishScrollFocusScrollBlocks turns it into a
        // window list, again on every relayout. The rule slot carries a
        // fraction and the config key a percent, each the convention of its
        // own layer, so the rule arm is scaled here at the one place both are
        // in hand. Stored only when it genuinely caps, and only when there is
        // a focus-follows-mouse to cap: the maximum is the no-cap value, and
        // an absent entry is what the per-relayout fast path tests for. The
        // focusFollowsMouse gate matters because the two are independent
        // settings and a rule can set the cap without the toggle — an entry
        // for a screen with the toggle off would put every relayout on that
        // screen through the strip walk for a list the compositor never
        // consults, since its lookup sits behind the same toggle.
        const int maxScrollPercent = params.focusFollowsMouseMaxScroll
            ? qRound(*params.focusFollowsMouseMaxScroll * 100.0)
            : m_settings->scrollingFocusFollowsMouseMaxScroll();
        if (focusFollowsMouse && maxScrollPercent < ConfigDefaults::scrollingFocusFollowsMouseMaxScrollMax()) {
            m_scrollFfmMaxScrollPercent.insert(screenId, maxScrollPercent);
        }
        // Taken from the ENGINE, never re-derived here. The engine owns the
        // work area that Auto resolves against, and a second aspect-ratio
        // derivation in the daemon could disagree with it on a near-square
        // monitor — a disagreement that is intermittent, geometry-dependent
        // and invisible in tests, which is the worst shape a bug can have.
        // The collection itself runs in a second walk below, once every
        // screen's override map is installed.
        // TEMPLATE channel: the context's resolved native ScrollingTemplate
        // (cascade entry, else the default-template setting) pushes its
        // whole shape. Precedence is rule > template > settings > engine
        // fallback: the template overwrites the settings-channel slots the
        // seed above copied, while the RULE-channel keys the params block
        // wrote are a different key set the engine reads first — except the
        // shared display key, which is therefore only written when no rule
        // filled it. Fail-soft: no resolved template inserts nothing and
        // the settings/compiled defaults stand.
        // diffActiveAssignments resolves the same context again on a switch.
        // The duplicate resolve is accepted: it is bounded by the scrolling
        // screen count and only runs for screens already in scrolling mode.
        const PhosphorZones::ScrollingTemplate templ =
            m_layoutManager->scrollingTemplateForContext(screenId, desktop, activity);
        if (templ.isValid()) {
            namespace SPK = PhosphorScrollEngine::ScrollPerScreenKeys;
            const auto fractionList = [](const QList<qreal>& values) {
                QVariantList list;
                for (qreal v : values) {
                    list.append(v);
                }
                return list;
            };
            // Preset vocabularies replace the settings lists WHOLESALE (no
            // merge — indices and cycle order stay stable within one
            // template). Empty lists insert nothing so the engine keeps its
            // fallback vocabulary.
            if (!templ.presetColumnWidths.isEmpty()) {
                overrides.insert(SPK::presetColumnWidths(), fractionList(templ.presetColumnWidths));
            }
            if (!templ.presetWindowHeights.isEmpty()) {
                overrides.insert(SPK::presetWindowHeights(), fractionList(templ.presetWindowHeights));
                // The height axis has no template trio to overwrite the seeded
                // settings ones with (a ScrollingTemplate carries no default
                // window height), and a preset INDEX only means anything
                // against the vocabulary it was authored for. Drop ONLY the
                // index: with the kind left standing, a Preset kind and no
                // index resolves against this template's own list (the
                // engine's index fallback), while a Fixed kind keeps reading
                // its pixel value — which the vocabulary swap never touched.
                overrides.remove(SPK::defaultWindowHeightPresetIndex());
            }
            // Seed blueprint for columns materializing on the fresh-open
            // path (engine_lifecycle consumes it at column creation).
            if (!templ.columns.isEmpty()) {
                QVariantList blueprint;
                for (const PhosphorZones::ScrollingTemplateColumn& column : templ.columns) {
                    QVariantMap entry;
                    entry.insert(SPK::templateColumnWidth(), column.width);
                    entry.insert(SPK::templateColumnDisplay(), column.display);
                    blueprint.append(entry);
                }
                overrides.insert(SPK::templateColumns(), blueprint);
            }
            // Beyond-blueprint defaults ride the settings-channel trio
            // (template values mirror the wire enums by construction).
            // Preset's index points into THIS template's preset list, which
            // was not inserted above when empty — pushing the index then
            // would resolve it against the settings vocabulary and pick an
            // unrelated width. ScrollingTemplate::normalize() already demotes
            // that shape to Proportion, so this only catches a template that
            // reached us without normalizing.
            const bool presetWidthsUsable = !templ.presetColumnWidths.isEmpty();
            if (templ.defaultColumnWidthKind == static_cast<int>(PhosphorScrollEngine::DefaultWidthKind::Preset)
                && !presetWidthsUsable) {
                overrides.insert(SPK::defaultColumnWidthKind(),
                                 static_cast<int>(PhosphorScrollEngine::DefaultWidthKind::Proportion));
            } else {
                overrides.insert(SPK::defaultColumnWidthKind(), templ.defaultColumnWidthKind);
            }
            overrides.insert(SPK::defaultColumnWidthValue(), templ.defaultColumnWidthValue);
            if (presetWidthsUsable) {
                overrides.insert(SPK::defaultColumnWidthPresetIndex(), templ.defaultColumnWidthPresetIndex);
            }
            if (!params.defaultColumnDisplay) {
                overrides.insert(SPK::defaultColumnDisplay(), templ.defaultColumnDisplay);
            }
        }
        // The tab indicator's GEOMETRY overrides. Only these seven reach the
        // engine: the eleven paint fields alongside them in ContextScrollingParams
        // cannot change a resolved rect, so they are collected separately below
        // and handed to the KWin effect through the Tiling adaptor instead
        // (see IScrollSettings for the split).
        {
            namespace K = PhosphorScrollEngine::ScrollPerScreenKeys;
            if (params.tabIndicatorEnabled) {
                overrides.insert(K::tabIndicatorEnabled(), *params.tabIndicatorEnabled);
            }
            if (params.tabIndicatorHideWhenSingleTab) {
                overrides.insert(K::tabIndicatorHideWhenSingleTab(), *params.tabIndicatorHideWhenSingleTab);
            }
            if (params.tabIndicatorPlaceWithinColumn) {
                overrides.insert(K::tabIndicatorPlaceWithinColumn(), *params.tabIndicatorPlaceWithinColumn);
            }
            if (params.tabIndicatorGap) {
                overrides.insert(K::tabIndicatorGap(), *params.tabIndicatorGap);
            }
            if (params.tabIndicatorWidth) {
                overrides.insert(K::tabIndicatorWidth(), *params.tabIndicatorWidth);
            }
            if (params.tabIndicatorLength) {
                overrides.insert(K::tabIndicatorLengthProportion(), *params.tabIndicatorLength);
            }
            if (params.tabIndicatorPosition) {
                overrides.insert(K::tabIndicatorPosition(), *params.tabIndicatorPosition);
            }
        }
        // Pushed even when EMPTY, rather than routed to clearPerScreenConfig.
        // The engine keys these per context, and its clear is the
        // whole-SCREEN door — it drops every context's entry, which is right
        // for the departing-screen loop further down and wrong here: this arm
        // fires when THIS context resolved no overrides, and taking the
        // screen-wide door let a template-less desktop wipe the template its
        // sibling desktop is holding. An empty map stored for this context
        // reads identically to an absent one at every effective* reader, so
        // the fallback behaviour is unchanged.
        m_scrollEngine->applyPerScreenConfig(screenId, overrides);
        // The tab indicator's PAINT params (style, gaps, corner radius, the
        // three colours and the five label-font fields) go to the KWin effect,
        // which draws the pills: it layers them over its global
        // Scrolling.TabIndicator settings for this screen alone. Keyed by the
        // same WindowPaintKeys / WindowColorKeys spellings the effect reads,
        // so producer and consumer share one home for the names. Handed
        // straight to the Tiling adaptor: the engine has no use for
        // presentation state. Pushed even when EMPTY, like the engine map
        // above — empty is how a screen whose rules stopped overriding falls
        // back to the global look.
        if (m_tilingAdaptor) {
            QVariantMap paint;
            if (params.tabIndicatorStyle) {
                paint.insert(WindowPaintKeys::tabStyle(), *params.tabIndicatorStyle);
            }
            if (params.tabIndicatorGapsBetweenTabs) {
                paint.insert(WindowPaintKeys::gapsBetweenTabs(), *params.tabIndicatorGapsBetweenTabs);
            }
            if (params.tabIndicatorCornerRadius) {
                paint.insert(WindowPaintKeys::cornerRadius(), *params.tabIndicatorCornerRadius);
            }
            if (params.tabIndicatorActiveColor) {
                paint.insert(WindowColorKeys::activeColor(), *params.tabIndicatorActiveColor);
            }
            if (params.tabIndicatorInactiveColor) {
                paint.insert(WindowColorKeys::inactiveColor(), *params.tabIndicatorInactiveColor);
            }
            if (params.tabIndicatorUrgentColor) {
                paint.insert(WindowColorKeys::urgentColor(), *params.tabIndicatorUrgentColor);
            }
            // The label font, per key like everything else on this map so a
            // rule that only italicises leaves the family, weight and the
            // other two flags at their global values. An empty family is
            // inserted rather than skipped: it is the user asking for the
            // system font, which the effect cannot distinguish from "no
            // override" if the key is absent.
            if (params.tabIndicatorFontFamily) {
                paint.insert(WindowPaintKeys::tabFontFamily(), *params.tabIndicatorFontFamily);
            }
            if (params.tabIndicatorFontWeight) {
                paint.insert(WindowPaintKeys::tabFontWeight(), *params.tabIndicatorFontWeight);
            }
            if (params.tabIndicatorFontItalic) {
                paint.insert(WindowPaintKeys::tabFontItalic(), *params.tabIndicatorFontItalic);
            }
            if (params.tabIndicatorFontUnderline) {
                paint.insert(WindowPaintKeys::tabFontUnderline(), *params.tabIndicatorFontUnderline);
            }
            if (params.tabIndicatorFontStrikeout) {
                paint.insert(WindowPaintKeys::tabFontStrikeout(), *params.tabIndicatorFontStrikeout);
            }
            m_tilingAdaptor->setScrollTabPaintOverrides(screenId, paint);
        }
        // The drop indicator's overrides are keyed by the QML property names
        // its slot reads and handed to the overlay rather than the engine. ALL
        // of them are paint — the indicator's rect is the engine's own layout
        // answer and no rule can move it.
        if (m_overlayService) {
            QVariantMap drop;
            if (params.dropIndicatorEnabled) {
                drop.insert(WindowPaintKeys::indicatorEnabled(), *params.dropIndicatorEnabled);
            }
            // Its two colour slots, same shared spellings — the remaining drop
            // keys (enabled, opacity, borderWidth, borderRadius) are not colour
            // overrides and have no entry in WindowColorKeys.
            if (params.dropIndicatorColor) {
                drop.insert(WindowColorKeys::indicatorColor(), *params.dropIndicatorColor);
            }
            if (params.dropIndicatorBorderColor) {
                drop.insert(WindowColorKeys::indicatorBorderColor(), *params.dropIndicatorBorderColor);
            }
            if (params.dropIndicatorOpacity) {
                drop.insert(WindowPaintKeys::indicatorOpacity(), *params.dropIndicatorOpacity);
            }
            if (params.dropIndicatorBorderWidth) {
                drop.insert(WindowPaintKeys::indicatorBorderWidth(), *params.dropIndicatorBorderWidth);
            }
            if (params.dropIndicatorBorderRadius) {
                drop.insert(WindowPaintKeys::indicatorBorderRadius(), *params.dropIndicatorBorderRadius);
            }
            m_overlayService->setScrollDropIndicatorOverrides(screenId, drop);
        }
    }

    // The axis is collected in a SECOND walk, after the loop above has
    // installed every screen's override map. stripAxisForScreen resolves
    // through the engine's CURRENT per-screen map, and the settings-channel
    // StripAxis intent is read into `overrides` at the top of that loop but
    // only handed to the engine at its bottom — so reading the axis inside
    // the walk answers from the PREVIOUS pass's map. That staleness is
    // sticky: applyPerScreenConfig early-returns on an unchanged map, so
    // nothing republishes and the wire keeps the wrong membership until some
    // unrelated event re-runs this function.
    //
    // Hoisting the apply above the read instead would install an INCOMPLETE
    // map, because the template and tab-indicator channels write into
    // `overrides` further down the same iteration.
    //
    // Only the settings channel was late. The GEOMETRY term never was:
    // layoutParamsForScreen resolves the work area live from the
    // ScreenManager and caches nothing, so a rotation is visible on the first
    // read either way. Ordering is unchanged — this still runs before the
    // single push, which still precedes setActiveScreens (which itself runs
    // INLINE, sync signals included — the autotile latch exists for exactly
    // that) and every scheduleRetileForScreen, whose queued retiles coalesce
    // onto a later turn.
    //
    // Through stripIsVerticalForScreen, which owns the downcast the accessor
    // needs (the axis is a scrolling concept and deliberately not on
    // IPlacementEngine, so reaching it off the base pointer is a downcast
    // rather than a widening of the shared contract).
    QStringList verticalAxisScreens;
    for (const QString& screenId : scrollingScreens) {
        if (stripIsVerticalForScreen(screenId)) {
            verticalAxisScreens.append(screenId);
        }
    }

    // Publish the effect-owned lists in ONE push, after the walk. They
    // leave here UNSORTED and are canonicalized at the ADAPTOR boundary: the
    // walk iterates a QSet, whose order is hash order and is not stable across
    // insertions, so the same membership could be built in two different orders
    // and the adaptor's emit-on-change compare — an order-sensitive list
    // compare — would report a change that isn't one.
    // ScrollingAdaptor::setScrollEffectBehaviour sorts and de-duplicates on
    // entry. That is the published-contract
    // boundary and it covers every producer, not just this one, so sorting a
    // second time here would be dead work claiming the same ownership. Screens
    // that LEFT scrolling are absent by construction (the walk only visits the
    // current set), so the departing-screen cleanup below has nothing to undo.
    if (m_scrollingAdaptor) {
        m_scrollingAdaptor->setScrollEffectBehaviour(ffmScreens, cropScreens, verticalAxisScreens);
        // Pushed alongside, not folded in: the block list rides its own
        // property because it is re-derived per relayout while these three
        // answer settings and rules. This pass is the one place both change
        // together, and it must push the list even when it is EMPTY — that
        // is how raising a cap back to no-cap clears a membership the
        // relayout path would otherwise never revisit, since it returns
        // early once no screen caps.
        m_scrollingAdaptor->setScrollFocusScrollBlockedWindows(scrollFocusScrollBlockedWindows());
    }
    m_scrollEngine->setActiveScreens(scrollingScreens);

    // Screens LEAVING scrolling drop their override entries too — otherwise a
    // stale map is replayed on re-entry before any rule change re-resolves it.
    // AFTER setActiveScreens: clearPerScreenConfig schedules a retile for the
    // screen it clears, and a departing screen is no longer in the engine's
    // live set by now, so the schedule is refused instead of queueing a no-op.
    for (const QString& screenId : currentScrollScreens - scrollingScreens) {
        m_scrollEngine->clearPerScreenConfig(screenId);
        // The overlay's PAINT overrides need the same treatment for the same
        // reason — they are the other half of the same context resolve, and a
        // screen that left scrolling would otherwise keep them until it
        // re-entered and something re-resolved.
        if (m_overlayService) {
            // The drop indicator holds no cached strip and does not replay, so
            // dropping the map is enough.
            m_overlayService->setScrollDropIndicatorOverrides(screenId, {});
        }
        // And the effect's tab paint overrides for the departed screen: the
        // pills it painted there are gone with the strip, and a stale map
        // would colour the next strip this screen hosts.
        if (m_tilingAdaptor) {
            m_tilingAdaptor->setScrollTabPaintOverrides(screenId, {});
        }
    }

    // setActiveScreens retiles only ADDED screens on a changed set (the
    // identical-set branch retiles everything itself). Force a retile for
    // every already-active screen so a rule save that changes GAP rules on a
    // screen whose overrides map did not move still applies live — gaps
    // resolve through the context-gap provider at retile time, never through
    // the overrides diff. Mirrors the load-bearing autotile loop in
    // updateEngineScreens; scheduleRetileForScreen coalesces, so the
    // identical-set overlap costs nothing.
    // LOAD-BEARING dependency: this gate is only correct because
    // ScrollEngine::setActiveScreens' identical-set branch
    // (engine_core.cpp, screens == m_scrollingScreens) retiles every screen
    // itself. If that branch ever stops retiling, this gate must be
    // dropped (scheduleRetileForScreen coalesces, so dropping it is cheap).
    // The autotile twin has no identical-set retile to lean on, so it runs
    // on every pass regardless of whether the set changed, gated only on
    // skipping the screens setActiveScreens just added.
    if (scrollingScreens != currentScrollScreens) {
        for (const QString& screenId : (scrollingScreens & currentScrollScreens)) {
            m_scrollEngine->scheduleRetileForScreen(screenId);
        }
    }
}

QStringList Daemon::scrollFocusScrollBlockedWindows() const
{
    // The fast path, and the one that runs on every relayout with the shipped
    // defaults: no screen caps, so nothing is refused and the engine is never
    // asked.
    if (m_scrollFfmMaxScrollPercent.isEmpty() || !m_scrollEngine) {
        return {};
    }
    const auto* scroll = qobject_cast<const PhosphorScrollEngine::ScrollEngine*>(m_scrollEngine.get());
    if (!scroll) {
        return {};
    }
    QStringList blocked;
    for (auto it = m_scrollFfmMaxScrollPercent.cbegin(); it != m_scrollFfmMaxScrollPercent.cend(); ++it) {
        blocked += scroll->windowsBeyondFocusScrollLimit(it.key(), it.value());
    }
    return blocked;
}

void Daemon::publishScrollFocusScrollBlocks()
{
    // The emptiness test twice over: once here so a layout change on an
    // uncapped desktop costs nothing at all, and once inside the gatherer for
    // the callers that reach it another way. Without this the push below would
    // still be a no-op (the adaptor compares before emitting), but it would
    // build and canonicalize the list to discover that.
    if (!m_scrollingAdaptor || m_scrollFfmMaxScrollPercent.isEmpty()) {
        return;
    }
    // Only the block list is pushed. The three screen lists answer settings
    // and rules, which this path cannot have changed, so they are not touched
    // at all — that is the whole point of the block list having its own
    // property. Sharing one made a strip that merely scrolled re-publish all
    // four lists, and the effect re-parse and re-compare three screen sets
    // that had not moved, on a path that runs per relayout. The adaptor's
    // emit-on-change gate then makes a scroll that crosses no window's
    // threshold a local no-op rather than a bus broadcast.
    m_scrollingAdaptor->setScrollFocusScrollBlockedWindows(scrollFocusScrollBlockedWindows());
}

/// Deliberately NOT liveness-gated, unlike the three providers wired in
/// init_engines.cpp (the cards provider, the overlay axis provider and the
/// UnifiedLayoutController one), which all guard on isActiveOnScreen.
///
/// Those are PUBLIC entry points that any screen can be asked about, so a
/// screen the engine does not own must answer "no vertical strip" rather than
/// the axis the engine WOULD resolve from its shape. This one is private, and
/// no caller of it can be asked about a screen that is not already scrolling
/// or about to be:
///
///  - The OSD callers announce a card for a screen they are in the middle of
///    putting into scrolling mode. showScrollingTemplateOsd is the clear case:
///    it fires on apply, and the engine may not have adopted the screen yet, so
///    a liveness gate would answer horizontal for a vertical strip at exactly
///    the moment the card exists to depict it. The value also lays that card's
///    bands, so gating would desynchronise bands from ticks rather than merely
///    suppress them.
///  - updateScrollingScreens builds the KWin effect's verticalAxisScreens list
///    from a loop that already iterates only scrolling screens, so a gate there
///    would be redundant rather than protective.
bool Daemon::stripIsVerticalForScreen(const QString& screenId) const
{
    const auto* scroll = qobject_cast<const PhosphorScrollEngine::ScrollEngine*>(m_scrollEngine.get());
    return scroll && scroll->stripAxisForScreen(screenId).isVertical();
}

} // namespace PlasmaZones
