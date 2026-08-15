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
// Complete type needed for setScrollTabIndicatorOverrides — daemon.h forward
// declares OverlayService only.
#include "daemon/overlayservice.h"
// Complete type needed for setScrollEffectBehaviour — daemon.h forward
// declares ScrollingAdaptor only.
#include "dbus/scrollingadaptor/scrollingadaptor.h"
#include "seedorderfilter.h"

#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"
// WindowColorKeys — the one home for the five colour-override key spellings
// this file produces and the overlay slots consume.
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
    // TEMPLATE vocabulary adds its two preset-list keys (disjoint from every
    // rule slot, so its position in the merge cannot collide), and the
    // engine's effective* readers keep the final two-way
    // `override ?? global-config` fallback.
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
    // The axis rides the same walk and the same single push. Computing it here
    // rather than in a second pass is what keeps it correctly ordered on BOTH
    // paths into this function — the settings path and the debounced
    // geometry/rotation path — because the push happens before any
    // scheduleRetileForScreen, which always coalesces onto a later turn.
    QStringList verticalAxisScreens;
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
        // The effect-owned pair, resolved to a verdict here rather than
        // forwarded as an override: `rule ?? config`, so the compositor gets
        // membership it can answer with a set lookup. cropStraddlers ALSO
        // rides the override map above (the engine's own straddler clamp
        // reads it), so it is the one behaviour with two consumers — both
        // resolve from this same slot, so they cannot disagree.
        // m_settings is unguarded here: the function's entry check already
        // returns on a null one, so a second test only read as though the
        // resolve were optional.
        if (params.focusFollowsMouse.value_or(m_settings->scrollingFocusFollowsMouse())) {
            ffmScreens.append(screenId);
        }
        if (params.cropStraddlers.value_or(m_settings->scrollingCropStraddlers())) {
            cropScreens.append(screenId);
        }
        // Taken from the ENGINE, never re-derived here. The engine owns the
        // work area that Auto resolves against, and a second aspect-ratio
        // derivation in the daemon could disagree with it on a near-square
        // monitor — a disagreement that is intermittent, geometry-dependent
        // and invisible in tests, which is the worst shape a bug can have.
        // The base pointer does not carry the accessor: the axis is a
        // scrolling concept and deliberately not on IPlacementEngine, so this
        // is a downcast rather than a widening of the shared contract.
        if (const auto* scroll = qobject_cast<const PhosphorScrollEngine::ScrollEngine*>(m_scrollEngine.get())) {
            if (scroll->stripAxisForScreen(screenId).axis() == PhosphorProtocol::ScrollAxis::Vertical) {
                verticalAxisScreens.append(screenId);
            }
        }
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
        // engine: the six paint fields alongside them in ContextScrollingParams
        // cannot change a resolved rect, so they are collected separately below
        // and handed to the overlay instead (see IScrollSettings for the split).
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
        if (overrides.isEmpty()) {
            m_scrollEngine->clearPerScreenConfig(screenId);
        } else {
            m_scrollEngine->applyPerScreenConfig(screenId, overrides);
        }
        // The tab indicator's PAINT overrides, keyed by the QML property names
        // the overlay reads so the layering there is one value() per property.
        // Handed straight to the overlay: the engine has no use for them and
        // routing them through its override map would put presentation state
        // in a library that deliberately knows nothing about presentation.
        if (m_overlayService) {
            QVariantMap paint;
            if (params.tabIndicatorStyle) {
                // "tabStyle" — the overlay SLOT's property name, see the push
                // block in overlayservice/scrolltabs.cpp.
                paint.insert(QStringLiteral("tabStyle"), *params.tabIndicatorStyle);
            }
            if (params.tabIndicatorGapsBetweenTabs) {
                paint.insert(QStringLiteral("gapsBetweenTabs"), *params.tabIndicatorGapsBetweenTabs);
            }
            if (params.tabIndicatorCornerRadius) {
                paint.insert(QStringLiteral("cornerRadius"), *params.tabIndicatorCornerRadius);
            }
            // The three colour slots go through WindowColorKeys: the same
            // spellings are the map keys the tab-strip enrichment layers on and
            // the QML property names the slot exposes, so producer, layering
            // step and property write all read them from one place.
            if (params.tabIndicatorActiveColor) {
                paint.insert(WindowColorKeys::activeColor(), *params.tabIndicatorActiveColor);
            }
            if (params.tabIndicatorInactiveColor) {
                paint.insert(WindowColorKeys::inactiveColor(), *params.tabIndicatorInactiveColor);
            }
            if (params.tabIndicatorUrgentColor) {
                paint.insert(WindowColorKeys::urgentColor(), *params.tabIndicatorUrgentColor);
            }
            m_overlayService->setScrollTabIndicatorOverrides(screenId, paint);

            // The drop indicator's overrides, same shape and same reason: keyed
            // by the QML property names its slot reads, handed to the overlay
            // rather than the engine. ALL of them are paint — the indicator's
            // rect is the engine's own layout answer and no rule can move it.
            QVariantMap drop;
            if (params.dropIndicatorEnabled) {
                drop.insert(QStringLiteral("indicatorEnabled"), *params.dropIndicatorEnabled);
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
                drop.insert(QStringLiteral("indicatorOpacity"), *params.dropIndicatorOpacity);
            }
            if (params.dropIndicatorBorderWidth) {
                drop.insert(QStringLiteral("indicatorBorderWidth"), *params.dropIndicatorBorderWidth);
            }
            if (params.dropIndicatorBorderRadius) {
                drop.insert(QStringLiteral("indicatorBorderRadius"), *params.dropIndicatorBorderRadius);
            }
            m_overlayService->setScrollDropIndicatorOverrides(screenId, drop);
        }
    }
    // Publish the effect-owned pair in ONE push, after the walk. SORTED here,
    // not by construction: the walk iterates a QSet, whose order is hash order
    // and is not stable across insertions, so the same membership could be
    // built in two different orders and the adaptor's emit-on-change compare —
    // an order-sensitive list compare — would report a change that isn't one.
    // The canonicalization itself lives in ScrollingAdaptor::setScrollEffectBehaviour,
    // which sorts and de-duplicates on entry. That is the published-contract
    // boundary and it covers every producer, not just this one, so sorting a
    // second time here would be dead work claiming the same ownership. Screens
    // that LEFT scrolling are absent by construction (the walk only visits the
    // current set), so the departing-screen cleanup below has nothing to undo.
    if (m_scrollingAdaptor) {
        m_scrollingAdaptor->setScrollEffectBehaviour(ffmScreens, cropScreens, verticalAxisScreens);
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
            // Tear the indicator down BEFORE dropping the overrides. The
            // engine's own clear for a departing screen is DEFERRED (the
            // direct path finds the latch already released by
            // removeStatesIf), so at this point the overlay still holds the
            // screen's live strip model. setScrollTabIndicatorOverrides
            // replays that cached model through updateScrollTabStrips, which
            // would run the full show choreography for a screen that just left
            // scrolling and then animate it away again when the queued "[]"
            // lands. Clearing the model first makes both the override drop and
            // the queued clear no-ops.
            m_overlayService->updateScrollTabStrips(screenId, {});
            m_overlayService->setScrollTabIndicatorOverrides(screenId, {});
            // No model to clear first for the drop indicator: it holds no
            // cached strip and does not replay, so dropping the map is enough.
            m_overlayService->setScrollDropIndicatorOverrides(screenId, {});
        }
        m_lastScrollTabStripsJson.remove(screenId);
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

} // namespace PlasmaZones
