// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "scrollingadaptor.h"

#include "config/configdefaults.h"
#include "core/platform/logging.h"

#include <algorithm>

#include <PhosphorEngine/NavigationContext.h>
#include <PhosphorEngine/PlacementEngineBase.h>
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorZones/ZoneJsonKeys.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRectF>
#include <QSet>
#include <QVector>

namespace PlasmaZones {

namespace {
/// The presetVocabularyJson payload keys, in one place so the two writes
/// cannot drift apart (the sibling payloads use ZoneJsonKeys:: /
/// ScrollOpenKeys:: the same way). The XML DocString spells them by hand,
/// per the same kept-in-sync-BY-HAND rule the bounds literals follow.
inline QLatin1String presetColumnWidthsKey()
{
    return QLatin1String("columnWidths");
}
inline QLatin1String presetWindowHeightsKey()
{
    return QLatin1String("windowHeights");
}
/// The blueprintProgressJson payload keys, same one-place rule as the pair
/// above.
inline QLatin1String blueprintTotalKey()
{
    return QLatin1String("total");
}
inline QLatin1String blueprintUsedKey()
{
    return QLatin1String("used");
}
} // namespace

ScrollingAdaptor::ScrollingAdaptor(PhosphorScrollEngine::ScrollEngine* engine, QObject* parent)
    : QDBusAbstractAdaptor(parent)
    , m_engine(engine)
{
    // PRECONDITION: the adaptor is constructed before the engine can push a
    // screen set, so the empty m_lastBroadcastScreens below is a true "nothing
    // broadcast yet" and not a gate seeded with a set we already missed. The
    // daemon guarantees it — initEnginesAndWiring news this adaptor in the same
    // pass that creates the engine, before updateEngineScreens runs. A
    // late-constructed adaptor would swallow a first broadcast of the empty
    // set, and there is no re-wire path that could reach that state (clearEngine
    // is terminal; the next cycle deletes and re-news the whole adaptor set).
    if (!m_engine) {
        qCWarning(lcDbusScrolling) << "ScrollingAdaptor created with null engine";
        return;
    }
    connect(m_engine, &PhosphorScrollEngine::ScrollEngine::scrollingScreensChanged, this,
            [this](const QStringList& screenIds, bool /*isDesktopSwitch*/) {
                // Change-gated: the engine's identical-set desktop-switch
                // re-emit exists for the TILING channel's catch-scan; this
                // interface is a pure Mode discriminator, so an unchanged
                // set must not hit the bus (emit-on-change rule). The
                // isDesktopSwitch flag is deliberately not carried on this
                // wire — the effect's handler has no per-screen transitions
                // to skip.
                if (screenIds == m_lastBroadcastScreens) {
                    return;
                }
                m_lastBroadcastScreens = screenIds;
                Q_EMIT scrollingScreensChanged(screenIds);
            });
    // Strip wake-ups for anyone rendering the strip (the settings app's
    // Monitors thumbnail today). Relayed straight through: placementChanged
    // IS the engine's change gate, and the reasons this adaptor does not add
    // a second, payload-level one are on the signal's declaration.
    //
    // Undamped, unlike the sibling relay of this same signal onto
    // Tiling.tilingChanged (init_engines.cpp), which skips the edge
    // auto-scroll's ~60 Hz tick. Deliberate rather than an oversight: that
    // one had no in-tree subscriber to damp it, while this signal's only
    // reader coalesces every wake-up onto a settle timer and re-reads once.
    // The cost here is a payload-free bus message per tick, not a relayout.
    connect(m_engine, &PhosphorEngine::PlacementEngineBase::placementChanged, this, [this](const QString& screenId) {
        // A placement change for a screen this engine no longer owns
        // describes a strip no reader can fetch: visibleStripJson
        // answers "[]" for it through the same gate. Waking a reader
        // to be told nothing is the one wake-up worth suppressing,
        // and unlike a payload gate this one is a set lookup.
        if (screenId.isEmpty() || !m_engine || !m_engine->isActiveOnScreen(screenId)) {
            return;
        }
        Q_EMIT stripChanged(screenId);
    });
}

QStringList ScrollingAdaptor::scrollingScreens() const
{
    if (!m_engine) {
        return {};
    }
    // Sorted like the scrollingScreensChanged signal payload, so property
    // reads and signal deltas compare equal for the same set.
    const QSet<QString> screens = m_engine->activeScreens();
    QStringList out(screens.cbegin(), screens.cend());
    std::sort(out.begin(), out.end());
    return out;
}

QVariantMap ScrollingAdaptor::scrollEffectBehaviour() const
{
    // No engine gate, unlike scrollingScreens above: this map is daemon-built
    // (the engine never sees the three effect-owned facts), so a cleared
    // engine pointer during shutdown does not invalidate it. The last
    // published value stands until the daemon pushes another.
    return m_scrollEffectBehaviour;
}

void ScrollingAdaptor::setScrollEffectBehaviour(const QStringList& focusFollowsMouseScreens,
                                                const QStringList& cropStraddlerScreens,
                                                const QStringList& verticalAxisScreens,
                                                const QStringList& focusScrollBlockedWindows)
{
    // Canonicalized HERE, not assumed: the published contract (the XML
    // DocString and the property doc) says all three lists are sorted, and the
    // change compare below is a LIST compare, so an unsorted producer would
    // both break the documented wire shape and make the emit-on-change gate
    // order-sensitive — the same membership arriving in a different order
    // would re-broadcast. The in-tree producer builds these lists from a
    // QSet, whose iteration order is a hash order, so this is the site that
    // makes the contract true rather than a belt over one that already held.
    // Duplicates go with the sort: two spellings of one membership must not
    // compare unequal either.
    const auto canonical = [](QStringList screens) {
        std::sort(screens.begin(), screens.end());
        screens.erase(std::unique(screens.begin(), screens.end()), screens.end());
        return screens;
    };
    QVariantMap next;
    next.insert(focusFollowsMouseKey(), canonical(focusFollowsMouseScreens));
    next.insert(cropStraddlersKey(), canonical(cropStraddlerScreens));
    // Same canonicalization as its two siblings, for the same reason: this is
    // the published-contract boundary, and the emit-on-change compare below is
    // an order-sensitive list compare.
    next.insert(verticalAxisKey(), canonical(verticalAxisScreens));
    // WINDOW ids rather than screen ids, but canonicalized on the same terms
    // and for the same two reasons.
    next.insert(focusScrollBlockedWindowsKey(), canonical(focusScrollBlockedWindows));
    if (next == m_scrollEffectBehaviour) {
        return;
    }
    m_scrollEffectBehaviour = next;
    Q_EMIT scrollEffectBehaviourChanged(m_scrollEffectBehaviour);
}

void ScrollingAdaptor::focusColumn(const QString& screenId, int delta)
{
    // Wire-boundary validation: only the two adjacent steps are meaningful,
    // and the screen gate keeps a wheel event from a non-scrolling monitor
    // from being redirected onto the active scrolling screen by
    // resolveOperationScreen's fallback. The isEmpty check is kept even though
    // isActiveOnScreen would reject "" anyway: rejecting a malformed argument
    // at the wire boundary should not depend on how a callee treats it.
    if (!m_engine || screenId.isEmpty() || (delta != -1 && delta != 1)) {
        return;
    }
    if (!m_engine->isActiveOnScreen(screenId) || refusesForContext(screenId)) {
        return;
    }
    // The delta is STRIP-RELATIVE (previous/next column) — the effect collapses
    // both physical wheel axes onto one +/-1 before it reaches here. Spelling
    // it as "left"/"right" would be correct only while every strip runs
    // horizontally: on a vertical one that token means the stack, so a wheel
    // notch would walk WITHIN the column and then try to cross to the
    // physically-left monitor at its end. The engine synthesizes the token
    // against the screen's own axis instead.
    m_engine->focusColumnByDelta(delta, screenId);
}

void ScrollingAdaptor::setViewScrollStepProvider(std::function<int()> provider)
{
    m_viewScrollStep = std::move(provider);
}

void ScrollingAdaptor::setContextGateProvider(std::function<bool(const QString&)> provider)
{
    m_contextGated = std::move(provider);
}

bool ScrollingAdaptor::refusesForContext(const QString& screenId) const
{
    return !m_contextGated || m_contextGated(screenId);
}

void ScrollingAdaptor::scrollView(const QString& screenId, int delta)
{
    // focusColumn's wire-boundary gate, plus the provider: without a step
    // there is no distance to move, and a silent no-op beats inventing one.
    if (!m_engine || !m_viewScrollStep || screenId.isEmpty() || (delta != -1 && delta != 1)) {
        return;
    }
    if (!m_engine->isActiveOnScreen(screenId) || refusesForContext(screenId)) {
        return;
    }
    // Signed percent: the engine resolves the direction against the screen's
    // own axis, the same reason focusColumn hands down a bare delta.
    m_engine->scrollViewByPercent(delta * m_viewScrollStep(), screenId);
}

void ScrollingAdaptor::setColumnWidthProportion(const QString& screenId, double proportion)
{
    // Wire-boundary validation in focusColumn's terms: the screen gate keeps
    // a stray call from being redirected by the engine's screen fallback, and
    // the range refusal is silent per the interface's documented convention.
    // The bounds are the same accessors the settings UI and the hand-written
    // width setter enforce.
    if (!m_engine || screenId.isEmpty() || !m_engine->isActiveOnScreen(screenId) || refusesForContext(screenId)) {
        return;
    }
    // Negated inclusive form rather than "< min || > max": both of those
    // comparisons are false for NaN, which D-Bus type 'd' can carry, so the
    // exclusion form would wave a NaN through into the stored intent. The
    // conjunction is false for NaN by construction.
    if (!(proportion >= ConfigDefaults::scrollingDefaultColumnWidthProportionMin()
          && proportion <= ConfigDefaults::scrollingDefaultColumnWidthProportionMax())) {
        return;
    }
    m_engine->setColumnWidth(PhosphorScrollEngine::ColumnWidth::makeProportion(proportion), screenId);
}

void ScrollingAdaptor::setColumnWidthPixels(const QString& screenId, int px)
{
    if (!m_engine || screenId.isEmpty() || !m_engine->isActiveOnScreen(screenId) || refusesForContext(screenId)) {
        return;
    }
    if (px < ConfigDefaults::scrollingDefaultColumnWidthFixedMin()
        || px > ConfigDefaults::scrollingDefaultColumnWidthFixedMax()) {
        return;
    }
    m_engine->setColumnWidth(PhosphorScrollEngine::ColumnWidth::makeFixed(px), screenId);
}

void ScrollingAdaptor::setWindowHeightProportion(const QString& screenId, double proportion)
{
    if (!m_engine || screenId.isEmpty() || !m_engine->isActiveOnScreen(screenId) || refusesForContext(screenId)) {
        return;
    }
    // Height-proportion accessors (they delegate to the width range today;
    // the separate names keep the two wire contracts independently tunable).
    // Same negated inclusive form as setColumnWidthProportion: NaN must not
    // reach the stored intent, and "< min || > max" is false for NaN.
    if (!(proportion >= ConfigDefaults::scrollingWindowHeightProportionMin()
          && proportion <= ConfigDefaults::scrollingWindowHeightProportionMax())) {
        return;
    }
    m_engine->setWindowHeight(PhosphorScrollEngine::WindowHeight::makePreset(proportion), screenId);
}

void ScrollingAdaptor::setWindowHeightPixels(const QString& screenId, int px)
{
    if (!m_engine || screenId.isEmpty() || !m_engine->isActiveOnScreen(screenId) || refusesForContext(screenId)) {
        return;
    }
    if (px < ConfigDefaults::scrollingDefaultWindowHeightMin()
        || px > ConfigDefaults::scrollingDefaultWindowHeightMax()) {
        return;
    }
    m_engine->setWindowHeight(PhosphorScrollEngine::WindowHeight::makeFixed(px), screenId);
}

void ScrollingAdaptor::clearWindowedFullscreen(const QString& windowId)
{
    // Same wire-boundary policy as focusColumn: malformed input is a silent
    // no-op, and the engine's own lookup rejects an untracked window.
    if (!m_engine || windowId.isEmpty()) {
        return;
    }
    m_engine->clearWindowedFullscreen(windowId);
}

void ScrollingAdaptor::reapplyWindowGeometry(const QString& windowId)
{
    // Same wire-boundary policy as clearWindowedFullscreen: malformed input
    // is a silent no-op, and the engine's own lookup rejects an untracked
    // window.
    if (!m_engine || windowId.isEmpty()) {
        return;
    }
    m_engine->reapplyWindowGeometry(windowId);
}

QString ScrollingAdaptor::visibleStripJson(const QString& screenId) const
{
    // isEmpty kept for the same wire-boundary reason as in focusColumn.
    if (!m_engine || screenId.isEmpty()) {
        return QStringLiteral("[]");
    }
    // Same screen gate as focusColumn, making the "empty for a screen that is
    // not scrolling" half of the XML contract explicit at this call site.
    // LOAD-BEARING, not belt and braces: setActiveScreens prunes only the
    // CURRENT context's key, so a strip built under a sibling context (another
    // virtual desktop or activity) survives the screen leaving the active set
    // and resolves again the moment that context comes back (engine_core.cpp).
    // Without this gate a screen that is no longer scrolling would answer with
    // that surviving strip.
    if (!m_engine->isActiveOnScreen(screenId)) {
        return QStringLiteral("[]");
    }
    // The number comes from the engine's own walk (VisibleTile::zoneNumber),
    // not from this loop's index. Every producer of a scroll zone number reads
    // that field, so the numbering has exactly one definition; re-deriving it
    // as i + 1 here would silently fork the moment the walk stops being a
    // dense 1..N over the returned order.
    //
    // ONE walk, not two. Reading visibleTiles and visibleTileRectsRelative
    // separately resolved the strip twice per call — two layoutParamsForScreen
    // resolves, each parsing both preset vocabularies and the per-screen
    // override map, and two relayouts — for a payload the settings app polls
    // on a live timer while its Monitors state view is open. The paired reads
    // also needed a count-mismatch guard between them, which a single walk
    // makes structurally impossible rather than merely unlikely.
    const QVector<PhosphorScrollEngine::ScrollEngine::VisibleTileWithRect> tiles =
        m_engine->visibleTilesWithRects(screenId);
    QJsonArray arr;
    for (const auto& entry : tiles) {
        const QRectF& r = entry.relativeRect;
        QJsonObject obj;
        obj[PhosphorZones::ZoneJsonKeys::X] = r.x();
        obj[PhosphorZones::ZoneJsonKeys::Y] = r.y();
        obj[PhosphorZones::ZoneJsonKeys::Width] = r.width();
        obj[PhosphorZones::ZoneJsonKeys::Height] = r.height();
        obj[PhosphorZones::ZoneJsonKeys::ZoneNumber] = entry.tile.zoneNumber;
        // Tab keys only for a tile whose column actually draws an indicator.
        // Emitting a zeroed triple for every ordinary tile would put three
        // dead keys on every element of a payload the settings app polls on a
        // live timer, and "no tabCount key" and "tabCount 0" have to mean the
        // same thing at the far end anyway (an older daemon sends neither).
        if (entry.tile.tabCount > 0) {
            obj[PhosphorProtocol::Service::StripPreviewKey::TabCount] = entry.tile.tabCount;
            obj[PhosphorProtocol::Service::StripPreviewKey::ActiveTab] = entry.tile.activeTabIndex;
            // The numeric wire values are TabIndicatorPosition's explicit
            // enumerators (ScrollTypes.h). Kept in sync BY HAND with the two
            // places that spell them out for a reader: this interface's XML
            // and the StripPreviewKey doc. Renumbering the enum silently
            // rotates every preview's indicator to the wrong edge.
            obj[PhosphorProtocol::Service::StripPreviewKey::TabPosition] =
                static_cast<int>(entry.tile.tabIndicatorPosition);
            obj[PhosphorProtocol::Service::StripPreviewKey::TabLength] = entry.tile.tabLengthProportion;
        }
        arr.append(obj);
    }
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

QString ScrollingAdaptor::presetVocabularyJson(const QString& screenId) const
{
    // Same wire-boundary and screen gates as visibleStripJson, for the same
    // reasons: a screen that is not scrolling must not answer with the
    // settings fallback its surviving overrides would resolve to.
    if (!m_engine || screenId.isEmpty() || !m_engine->isActiveOnScreen(screenId)) {
        return QStringLiteral("{}");
    }
    const auto toArray = [](const QList<qreal>& values) {
        QJsonArray arr;
        for (const qreal v : values) {
            arr.append(v);
        }
        return arr;
    };
    QJsonObject obj;
    obj[presetColumnWidthsKey()] = toArray(m_engine->effectivePresetColumnWidths(screenId));
    obj[presetWindowHeightsKey()] = toArray(m_engine->effectivePresetWindowHeights(screenId));
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

QString ScrollingAdaptor::blueprintProgressJson(const QString& screenId) const
{
    // Same wire-boundary and screen gates as its two siblings. The gate is
    // what makes a non-scrolling screen report {0, 0} rather than the progress
    // its surviving overrides and state would still resolve to.
    if (!m_engine || screenId.isEmpty() || !m_engine->isActiveOnScreen(screenId)) {
        return QStringLiteral("{}");
    }
    const PhosphorScrollEngine::ScrollBlueprintProgress progress = m_engine->blueprintProgressForScreen(screenId);
    QJsonObject obj;
    obj[blueprintTotalKey()] = progress.total;
    obj[blueprintUsedKey()] = progress.used;
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

void ScrollingAdaptor::clearEngine()
{
    if (m_engine) {
        disconnect(m_engine, &PhosphorScrollEngine::ScrollEngine::scrollingScreensChanged, this, nullptr);
        // The strip wake-up relay dies with it, for the same reason: a
        // detached adaptor must not keep putting a dead engine's screen ids
        // on the bus. Defence in depth rather than the load-bearing half —
        // the relay's own `!m_engine` conjunct already makes a surviving
        // connection inert rather than fatal, so no test can tell this
        // disconnect from its absence. The sibling above is the one a spy
        // discriminates, because its lambda has no such guard.
        disconnect(m_engine, &PhosphorEngine::PlacementEngineBase::placementChanged, this, nullptr);
        m_engine = nullptr;
    }
    // Object-state consistency, NOT gate correctness. clearEngine is terminal
    // (the next cycle deletes and re-news the whole adaptor set), the
    // connection is already severed, and scrollingScreens() now answers empty,
    // so nothing reads this member again. Leaving it holding the last live set
    // would just mean a detached adaptor whose "last broadcast" memory
    // contradicts every other slot it answers.
    m_lastBroadcastScreens.clear();
    // m_scrollEffectBehaviour is deliberately NOT cleared, and it is the one
    // member that differs: the set above is engine-derived, so leaving it
    // populated would contradict every other slot this adaptor answers. The
    // behaviour map is daemon-built and its getter has no engine gate
    // (scrollEffectBehaviour documents why), so the last published value stays
    // the honest answer for as long as this object exists — clearing it would
    // replace a true answer with an empty one that reads as "no screen has any
    // of the three".
}

} // namespace PlasmaZones
