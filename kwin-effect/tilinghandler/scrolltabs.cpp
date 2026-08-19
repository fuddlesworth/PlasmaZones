// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Compositor-drawn scrolling tab indicators — the MODEL half.
//
// The scroll engine resolves where each tabbed column's indicator sits and
// which tabs it holds; that structural payload arrives here over D-Bus
// (scrollTabStripsChanged). Everything else a pill needs — its title, whether
// it is urgent, which colours a window rule gave it — the effect either
// already knows (captions, demandsAttention) or queries once and caches
// (scrollTabColors). This file turns the two into ScrollTabIndicator models
// and hands them to the effect's ScrollTabIndicatorPainter, which rasterises
// and blits them inside the paint pass, under the same view offset as the
// windows they label.
//
// Why the model is rebuilt here rather than pushed enriched from the daemon:
// the daemon used to enrich and render these into a layer surface, and a
// surface the compositor slides can never sit on the frame the windows moved
// — its content is always a render behind. Resolving from the effect's own
// window state has no such hop, and the payload itself changes only on a
// relayout, which is exactly when the strip moves.
//
// Screen ids and outputs. The model is keyed by the daemon's EFFECTIVE screen
// id and the painter by KWin::LogicalOutput*, joined through
// PlasmaZonesEffect::outputForScreenId, which matches on the PHYSICAL id and
// so collapses every virtual-screen spelling of one monitor onto its output.
// That is safe only because scrolling is assigned per physical screen (the
// engine never emits a `vs:` spelling in a strip payload — see the scrolling
// assignment rule in tiling.cpp), so exactly one payload per output exists
// and setIndicators' replace-the-whole-model semantics cannot clobber a
// sibling's pills.

#include "tilinghandler.h"

#include "plasmazoneseffect/plasmazoneseffect.h"
#include "compositor/scrolltabindicatorpainter.h"
#include "compositor/stripviewanimator.h"

#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorProtocol/ServiceConstants.h>

#include <KColorScheme>

#include <core/rect.h>
#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <window.h>

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QEvent>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLoggingCategory>
#include <QPalette>

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

namespace PlasmaZones {

namespace {

namespace ScrollTabKey = PhosphorProtocol::Service::ScrollTabKey;

/// The payload the engine emits for "this screen has no indicators". Matched
/// literally rather than parsed: every clear path emits exactly this string
/// (ScrollEngine::clearTabStripsForScreen), and parsing it would cost a
/// QJsonDocument for the commonest message on the channel.
constexpr QLatin1String kEmptyPayload("[]");

/// Wire keys of one strip object, as the scroll engine emits them
/// (ScrollEngine::applyLayout's tab-strip block is the sole producer).
constexpr QLatin1String kKeyX("x");
constexpr QLatin1String kKeyY("y");
constexpr QLatin1String kKeyWidth("width");
constexpr QLatin1String kKeyHeight("height");
constexpr QLatin1String kKeyPosition("position");
constexpr QLatin1String kKeyActiveIndex("activeIndex");
constexpr QLatin1String kKeyTabs("tabs");

/// The engine's TabIndicatorPosition values (ScrollTypes.h): 0 Left, 1
/// Right, 2 Top, 3 Bottom. Top is the fallback for a payload without the key
/// — a producer/consumer version split — because a HORIZONTAL reading is the
/// least surprising for a rect whose orientation cannot be confirmed (the
/// bare int default of 0 would silently flip it to a vertical Left bar).
constexpr int kPositionTop = 2;
constexpr int kPositionMax = 3;

/// Closed set and bounds of the per-screen override ints, the SAME ones the
/// global loaders in daemon_settings.cpp apply (which mirror
/// ConfigDefaults::scrollingTabIndicator{Style,GapsBetweenTabs,CornerRadius}
/// {Min,Max}). A context override is the same value on a different channel,
/// so it gets the same D-Bus-boundary guard: a wrong-typed or out-of-range
/// override leaves the global value standing instead of collapsing the
/// style to chips or the radius to 0 via toInt()'s silent zero.
constexpr int kStyleMin = 0; // title chips
constexpr int kStyleMax = 1; // segment bar
constexpr int kGapsMin = 0;
constexpr int kGapsMax = 64;
constexpr int kCornerRadiusMin = -1; // the "fully rounded" pill sentinel
constexpr int kCornerRadiusMax = 64;

QColor colorFromMap(const QVariantMap& map, QLatin1String key)
{
    const QString text = map.value(key).toString();
    return text.isEmpty() ? QColor() : QColor(text);
}

/// Reads an override int with the same type guard the global loaders use:
/// present, integral, inside [@p min, @p max]. Returns false (leave the
/// global value) otherwise.
bool overrideInt(const QVariantMap& map, QLatin1String key, int min, int max, int& out)
{
    const auto it = map.constFind(key);
    if (it == map.constEnd()) {
        return false;
    }
    bool ok = false;
    const int value = it->toInt(&ok);
    if (!ok || value < min || value > max) {
        return false;
    }
    out = value;
    return true;
}

} // namespace

// ── Theme and font cache ─────────────────────────────────────────────────────

void TilingHandler::fillScrollTabTheme(ScrollTabIndicatorStyle& style)
{
    // Theme palette and units for the pills, resolved to match what the QML
    // rendering saw through Kirigami. The effect has no QML engine, so
    // Kirigami.Theme itself is out of reach; instead the values come from the
    // same sources Kirigami's desktop platform plugin reads them from. The five
    // colours are KColorScheme's View set (Kirigami's Theme.View is the
    // default colour set for an overlay, and highlight / highlightedText /
    // text / background / negativeText are its Selection and View roles); the
    // spacing units derive exactly as Kirigami::Platform::Units does for the
    // desktop style: gridUnit is the general font's line height,
    // smallSpacing = max(2, gridUnit / 4), largeSpacing = smallSpacing * 2.
    //
    // Cached: two KColorScheme constructions (each a KSharedConfig lookup),
    // two QFontDatabase lookups and a QFontMetrics per rebuild were
    // measurable on the caption-tick path. The cache is dropped by the
    // palette / font event filter below and by every tab paint-setting edge
    // (the label font is built from cached settings), which are the only
    // things that can move these values.
    if (!m_scrollTabThemeCached) {
        const KColorScheme view(QPalette::Active, KColorScheme::View);
        const KColorScheme selection(QPalette::Active, KColorScheme::Selection);
        m_scrollTabTheme.themeHighlight = selection.background(KColorScheme::NormalBackground).color();
        m_scrollTabTheme.themeHighlightedText = selection.foreground(KColorScheme::NormalText).color();
        m_scrollTabTheme.themeText = view.foreground(KColorScheme::NormalText).color();
        m_scrollTabTheme.themeBackground = view.background(KColorScheme::NormalBackground).color();
        m_scrollTabTheme.themeNegativeText = view.foreground(KColorScheme::NegativeText).color();
        const int gridUnit = qMax(1, QFontMetrics(QFontDatabase::systemFont(QFontDatabase::GeneralFont)).height());
        m_scrollTabTheme.smallSpacing = qMax(2, gridUnit / 4);
        m_scrollTabTheme.largeSpacing = m_scrollTabTheme.smallSpacing * 2;
        m_scrollTabTheme.font = m_effect->scrollTabIndicatorFont();
        m_scrollTabThemeCached = true;
    }
    style.themeHighlight = m_scrollTabTheme.themeHighlight;
    style.themeHighlightedText = m_scrollTabTheme.themeHighlightedText;
    style.themeText = m_scrollTabTheme.themeText;
    style.themeBackground = m_scrollTabTheme.themeBackground;
    style.themeNegativeText = m_scrollTabTheme.themeNegativeText;
    style.smallSpacing = m_scrollTabTheme.smallSpacing;
    style.largeSpacing = m_scrollTabTheme.largeSpacing;
    style.font = m_scrollTabTheme.font;
}

void TilingHandler::invalidateScrollTabTheme()
{
    m_scrollTabThemeCached = false;
}

// ── Payload intake ───────────────────────────────────────────────────────────

void TilingHandler::slotScrollTabStripsChanged(const QString& screenId, const QString& stripsJson)
{
    if (screenId.isEmpty()) {
        return;
    }
    if (stripsJson.isEmpty() || stripsJson == kEmptyPayload) {
        dropScrollTabScreen(screenId);
        return;
    }
    // Parse ONCE, here, and keep the parsed array: the rebuild and the
    // reverse index both read it, and a parse error must not be mistaken for
    // an empty strip. A malformed payload means we know nothing about the
    // screen's strips, which is not the same as "there are none": clearing
    // on it would wipe live pills on a producer bug. Warn and keep whatever
    // was painted, exactly as the daemon's parser did before the move.
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(stripsJson.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isArray()) {
        qCWarning(lcEffect) << "scrollTabStripsChanged: unparsable payload for" << screenId << error.errorString()
                            << "- keeping the previous model";
        return;
    }
    const QJsonArray strips = doc.array();
    // Reverse index: drop this screen from every window it used to name, so
    // a window that left a tabbed column stops triggering rebuilds here.
    const QList<QString> indexedBefore = m_scrollTabScreensByWindow.keys();
    unindexScrollTabScreen(screenId);
    m_scrollTabPayloadByScreen.insert(screenId, strips);
    for (const QJsonValue& v : strips) {
        const QJsonArray tabs = v.toObject().value(kKeyTabs).toArray();
        for (const QJsonValue& t : tabs) {
            const QString windowId = t.toString();
            if (!windowId.isEmpty()) {
                m_scrollTabScreensByWindow[windowId].insert(screenId);
            }
        }
    }
    dropScrollTabColorsForUnindexed(indexedBefore);
    rebuildScrollTabIndicators(screenId);
}

void TilingHandler::dropScrollTabColorsForUnindexed(const QList<QString>& indexedBefore)
{
    // A window that stopped being a tab anywhere (untabbed, hidden by
    // hideWhenSingleTab, moved to a plain column) loses its cached colour
    // verdict, so that when it is tabbed again the rebuild re-queries. The
    // daemon's targeted title relay is gated on the window being a tab in its
    // cached strips, so a retitle in the un-tabbed interval reaches us as
    // nothing; without this eviction a `Title contains …` rule would keep the
    // pre-interval verdict painted on re-tab. Windows still indexed somewhere
    // keep theirs.
    for (const QString& windowId : indexedBefore) {
        if (!m_scrollTabScreensByWindow.contains(windowId)) {
            dropScrollTabColorsForWindow(windowId);
        }
    }
}

void TilingHandler::unindexScrollTabScreen(const QString& screenId)
{
    for (auto it = m_scrollTabScreensByWindow.begin(); it != m_scrollTabScreensByWindow.end();) {
        it->remove(screenId);
        if (it->isEmpty()) {
            it = m_scrollTabScreensByWindow.erase(it);
        } else {
            ++it;
        }
    }
}

void TilingHandler::dropScrollTabScreen(const QString& screenId)
{
    const QList<QString> indexedBefore = m_scrollTabScreensByWindow.keys();
    unindexScrollTabScreen(screenId);
    dropScrollTabColorsForUnindexed(indexedBefore);
    m_scrollTabPayloadByScreen.remove(screenId);
    if (KWin::LogicalOutput* out = m_effect->outputForScreenId(screenId)) {
        const QRect bounds = m_effect->m_scrollTabPainter->boundsFor(out);
        m_effect->m_scrollTabPainter->clearOutput(out);
        drainRetiredScrollTabTextures();
        if (bounds.isValid() && KWin::effects) {
            KWin::effects->addRepaint(KWin::Rect(bounds));
        }
    }
    // Compare by OUTPUT, not by raw id: the hover screen is recorded in the
    // effect's own physical spelling while the daemon may name the screen in
    // its effective spelling; both resolve to one output.
    if (!m_scrollTabHoverScreen.isEmpty()
        && m_effect->outputForScreenId(m_scrollTabHoverScreen) == m_effect->outputForScreenId(screenId)) {
        m_scrollTabHoverScreen.clear();
        // The pill under the parked pointer just vanished; the override must
        // not outlive it (unless a press is still being held — see
        // setScrollTabHoverCursor).
        setScrollTabHoverCursor(false);
    }
}

void TilingHandler::slotScrollTabColorsChanged(const QString& windowId, const QVariantMap& colors)
{
    if (windowId.isEmpty()) {
        // Broadcast: every verdict may have moved. Re-query every window we
        // hold a verdict for (or are already asking about) WITHOUT dropping
        // the cached answers first: a dropped cache repaints every
        // rule-coloured pill in its theme colour for the length of a D-Bus
        // round trip, once per rules save, which was a visible flash. The
        // stale-but-right-looking colour stays until its fresh reply lands.
        // The generation bump makes any reply still in flight from BEFORE
        // this broadcast inert, so an old verdict cannot land after the new
        // one and latch.
        ++m_scrollTabColorGeneration;
        QStringList windows = m_scrollTabColorCache.keys();
        for (auto it = m_scrollTabColorsInFlight.constBegin(); it != m_scrollTabColorsInFlight.constEnd(); ++it) {
            if (!windows.contains(it.key())) {
                windows.append(it.key());
            }
        }
        m_scrollTabColorsInFlight.clear();
        for (const QString& id : std::as_const(windows)) {
            queryScrollTabColors(id);
        }
        return;
    }
    // Targeted form (the daemon's title re-drive): one window's verdict.
    // Gated like every other setter — an unchanged map is a no-op, so a
    // re-push costs nothing downstream.
    const auto it = m_scrollTabColorCache.constFind(windowId);
    if (it != m_scrollTabColorCache.constEnd() && *it == colors) {
        return;
    }
    m_scrollTabColorCache.insert(windowId, colors);
    m_scrollTabColorsInFlight.remove(windowId);
    noteScrollTabWindowChanged(windowId);
}

void TilingHandler::queryScrollTabColors(const QString& windowId)
{
    if (windowId.isEmpty() || m_scrollTabColorsInFlight.contains(windowId)) {
        return;
    }
    // Without a daemon the query cannot succeed; leave the window un-asked so
    // the next rebuild after the daemon returns retries, rather than burning
    // a round trip per rebuild into a dead service.
    if (!m_effect->m_daemonGate.serviceRegistered) {
        return;
    }
    // Per-DISPATCH token, not a per-window flag: an evict (untab, close)
    // followed by a re-query must let only the re-query's reply land. Two
    // replies for the same window arrive in order, and the first would
    // otherwise consume a per-window marker and latch the stale verdict.
    const quint64 serial = ++m_scrollTabColorsSerial;
    m_scrollTabColorsInFlight.insert(windowId, serial);
    const quint64 generation = m_scrollTabColorGeneration;
    QDBusMessage msg =
        QDBusMessage::createMethodCall(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                                       PhosphorProtocol::Service::Interface::Tiling, QStringLiteral("scrollTabColors"));
    msg << windowId;
    QDBusPendingCall call = QDBusConnection::sessionBus().asyncCall(msg, PhosphorProtocol::Service::SyncCallTimeoutMs);
    auto* watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, windowId, generation, serial](QDBusPendingCallWatcher* w) {
                w->deleteLater();
                if (generation != m_scrollTabColorGeneration) {
                    // A broadcast superseded this query and re-dispatched it;
                    // the fresh reply is the one that may land.
                    return;
                }
                // The in-flight token is the liveness test: it is dropped by
                // the window's close or untab (dropScrollTabColorsForWindow)
                // and by a fresher targeted verdict (the daemon's title
                // re-drive), and replaced by a re-dispatch. A reply that
                // finds it gone or newer must not resurrect a cache row for
                // a dead window or overwrite the fresher answer.
                const auto inFlight = m_scrollTabColorsInFlight.constFind(windowId);
                if (inFlight == m_scrollTabColorsInFlight.constEnd() || *inFlight != serial) {
                    return;
                }
                m_scrollTabColorsInFlight.erase(inFlight);
                QDBusPendingReply<QVariantMap> reply = *w;
                if (reply.isError()) {
                    // Evict whatever is cached so the next rebuild re-asks:
                    // on the broadcast path the OLD verdict is still cached
                    // (re-queried in place, by design) and would otherwise be
                    // held until the next broadcast. The painter's model is
                    // deliberately NOT rebuilt here: the pushed colours stay
                    // painted until the next rebuild re-asks, because a
                    // rebuild from this arm would re-dispatch, error again
                    // and loop. Bounded by the serviceRegistered gate above:
                    // a dead daemon stops the dispatch, not the retry.
                    m_scrollTabColorCache.remove(windowId);
                    qCDebug(lcEffect) << "scrollTabColors failed for" << windowId << reply.error().message();
                    return;
                }
                // An EMPTY reply is an answer ("no rule"), cached as such so
                // the common no-rule window is one probe per rebuild rather
                // than a round trip — and so a rule that was REMOVED stops
                // painting: the previous verdict is overwritten, not kept.
                const QVariantMap colors = reply.value();
                const auto it = m_scrollTabColorCache.constFind(windowId);
                const bool changed = it == m_scrollTabColorCache.constEnd() || *it != colors;
                m_scrollTabColorCache.insert(windowId, colors);
                if (changed) {
                    noteScrollTabWindowChanged(windowId);
                }
            });
}

void TilingHandler::slotScrollTabPaintOverridesChanged(const QString& screenId, const QVariantMap& overrides)
{
    if (screenId.isEmpty()) {
        return;
    }
    if (overrides.isEmpty()) {
        if (m_scrollTabPaintOverrides.remove(screenId) == 0) {
            return;
        }
    } else {
        auto it = m_scrollTabPaintOverrides.find(screenId);
        if (it != m_scrollTabPaintOverrides.end() && *it == overrides) {
            return;
        }
        m_scrollTabPaintOverrides.insert(screenId, overrides);
    }
    rebuildScrollTabIndicators(screenId);
}

void TilingHandler::noteScrollTabWindowChanged(const QString& windowId)
{
    const auto it = m_scrollTabScreensByWindow.constFind(windowId);
    if (it == m_scrollTabScreensByWindow.constEnd()) {
        return;
    }
    // Copy: rebuild does not mutate the index, but the loop must not hold an
    // iterator into a hash a nested slot could touch.
    const QSet<QString> screens = *it;
    for (const QString& screenId : screens) {
        rebuildScrollTabIndicators(screenId);
    }
}

void TilingHandler::noteScrollTabTitleChanged(const QString& windowId)
{
    const auto it = m_scrollTabScreensByWindow.constFind(windowId);
    if (it == m_scrollTabScreensByWindow.constEnd()) {
        return;
    }
    // A title is only DRAWN by the chips style. In the shipped default (the
    // segment bar) a caption tick — a terminal, a progress-bar title — would
    // re-parse the payload and re-push a model whose only change the raster
    // never reads, so those screens are skipped. "Effective" style is the
    // per-screen override when a rule set one, else the global setting.
    const QSet<QString> screens = *it;
    for (const QString& screenId : screens) {
        int style = m_effect->m_cachedTabIndicatorStyle;
        if (const auto ovIt = m_scrollTabPaintOverrides.constFind(screenId);
            ovIt != m_scrollTabPaintOverrides.constEnd()) {
            overrideInt(*ovIt, ScrollTabKey::TabStyle, kStyleMin, kStyleMax, style);
        }
        if (style == 1) {
            continue;
        }
        rebuildScrollTabIndicators(screenId);
    }
}

void TilingHandler::rebuildAllScrollTabIndicators()
{
    const QStringList screens = m_scrollTabPayloadByScreen.keys();
    for (const QString& screenId : screens) {
        rebuildScrollTabIndicators(screenId);
    }
}

void TilingHandler::dropScrollTabColorsForWindow(const QString& windowId)
{
    m_scrollTabColorCache.remove(windowId);
    m_scrollTabColorsInFlight.remove(windowId);
}

// ── Model resolution ─────────────────────────────────────────────────────────

void TilingHandler::rebuildScrollTabIndicators(const QString& screenId)
{
    KWin::LogicalOutput* out = m_effect->outputForScreenId(screenId);
    if (!out) {
        return;
    }
    ScrollTabIndicatorPainter* painter = m_effect->m_scrollTabPainter.get();
    const auto payloadIt = m_scrollTabPayloadByScreen.constFind(screenId);
    if (payloadIt == m_scrollTabPayloadByScreen.constEnd() || !m_effect->m_cachedTabIndicatorEnabled) {
        // No payload, or the master switch is off: nothing to paint. The
        // engine still reserves the indicator's space when the switch is on
        // engine-side; the effect-side switch only governs the drawing, and
        // the two are the same setting, so they agree.
        const QRect bounds = painter->boundsFor(out);
        painter->clearOutput(out);
        drainRetiredScrollTabTextures();
        if (bounds.isValid() && KWin::effects) {
            KWin::effects->addRepaint(KWin::Rect(bounds));
        }
        // The pills this screen showed are gone; a pointer parked over one
        // must not keep the hand or eat the next click.
        if (KWin::effects) {
            updateScrollTabHover(KWin::effects->cursorPos());
        }
        return;
    }

    ScrollTabIndicatorStyle style;
    style.style = m_effect->m_cachedTabIndicatorStyle;
    style.gapsBetweenTabs = m_effect->m_cachedTabIndicatorGapsBetweenTabs;
    style.cornerRadius = m_effect->m_cachedTabIndicatorCornerRadius;
    style.activeColor = m_effect->m_cachedTabIndicatorActiveColor;
    style.inactiveColor = m_effect->m_cachedTabIndicatorInactiveColor;
    style.urgentColor = m_effect->m_cachedTabIndicatorUrgentColor;
    fillScrollTabTheme(style);
    // Context-rule overrides for THIS screen layer over the global settings,
    // per key: a rule that only sets the active colour leaves the style, gaps
    // and radius at their global values. Same precedence the daemon applied
    // when it pushed these to QML (override wins over setting wins over
    // theme), now resolved where the pills are drawn. The ints are guarded
    // exactly like the global loaders (see overrideInt).
    if (const auto ovIt = m_scrollTabPaintOverrides.constFind(screenId); ovIt != m_scrollTabPaintOverrides.constEnd()) {
        const QVariantMap& ov = *ovIt;
        overrideInt(ov, ScrollTabKey::TabStyle, kStyleMin, kStyleMax, style.style);
        overrideInt(ov, ScrollTabKey::GapsBetweenTabs, kGapsMin, kGapsMax, style.gapsBetweenTabs);
        overrideInt(ov, ScrollTabKey::CornerRadius, kCornerRadiusMin, kCornerRadiusMax, style.cornerRadius);
        if (const QColor c = colorFromMap(ov, ScrollTabKey::ActiveColor); c.isValid()) {
            style.activeColor = c;
        }
        if (const QColor c = colorFromMap(ov, ScrollTabKey::InactiveColor); c.isValid()) {
            style.inactiveColor = c;
        }
        if (const QColor c = colorFromMap(ov, ScrollTabKey::UrgentColor); c.isValid()) {
            style.urgentColor = c;
        }
    }

    QVector<ScrollTabIndicator> indicators;
    QStringList colorsToQuery;
    const QJsonArray& strips = *payloadIt;
    indicators.reserve(strips.size());
    for (const QJsonValue& v : strips) {
        const QJsonObject o = v.toObject();
        ScrollTabIndicator indicator;
        // qRound(toDouble()), not toInt(): QJsonValue::toInt() yields 0 for
        // any non-integral number, so a producer that ever wrote a fractional
        // coordinate would silently park the whole strip at the origin.
        indicator.rect = QRect(qRound(o.value(kKeyX).toDouble()), qRound(o.value(kKeyY).toDouble()),
                               qRound(o.value(kKeyWidth).toDouble()), qRound(o.value(kKeyHeight).toDouble()));
        if (!indicator.rect.isValid()) {
            continue;
        }
        const int position = o.value(kKeyPosition).toInt(kPositionTop);
        // The painter treats 0/1 as vertical and everything else as
        // horizontal, so an out-of-range value would silently render as a
        // top bar; clamp it to the fallback explicitly instead.
        indicator.position = (position < 0 || position > kPositionMax) ? kPositionTop : position;
        // -1 default so a payload missing the key lights no tab, matching the
        // daemon parser's reading of the same absence.
        const int activeIndex = o.value(kKeyActiveIndex).toInt(-1);
        const QJsonArray tabs = o.value(kKeyTabs).toArray();
        indicator.tabs.reserve(tabs.size());
        for (int i = 0; i < tabs.size(); ++i) {
            ScrollTabPill pill;
            pill.windowId = tabs.at(i).toString();
            if (pill.windowId.isEmpty()) {
                continue; // a non-string entry names no window: not a tab
            }
            pill.active = (i == activeIndex);
            // EXACT lookup: the title and urgency are paint output, and the
            // fuzzy same-app fallback findWindowById carries would draw a
            // sibling's title on a stale id (the same rule every other
            // paint-affecting resolve in tiling.cpp follows). An exact miss
            // degrades to the untitled fallback below, which is the intended
            // reading of "the window is not here".
            if (KWin::EffectWindow* w = m_effect->findWindowByIdExact(pill.windowId)) {
                pill.title = w->caption();
                if (KWin::Window* kw = w->window()) {
                    pill.urgent = kw->isDemandingAttention();
                }
                // No separate live-id index entry: findWindowByIdExact is a
                // lookup in the reverse index keyed by getWindowId(), so an
                // exact hit's live id IS pill.windowId. A wire id that
                // diverged from the live one (a cross-session restore) misses
                // here on purpose, draws the untitled fallback and refuses
                // the click; the fuzzy re-key tiling.cpp does is for tile
                // placement, which is not paint output.
            }
            if (pill.title.isEmpty()) {
                // The QML drew a translated placeholder for a titleless tab;
                // the painter deliberately carries no i18n, so the caller
                // resolves it. Translated through the catalog the effect
                // loads at construction (see lifecycle.cpp).
                pill.title = QCoreApplication::translate("plasmazones", "Untitled window");
            }
            const auto colorIt = m_scrollTabColorCache.constFind(pill.windowId);
            if (colorIt == m_scrollTabColorCache.constEnd()) {
                colorsToQuery.append(pill.windowId);
            } else {
                pill.activeColor = colorFromMap(*colorIt, ScrollTabKey::ActiveColor);
                pill.inactiveColor = colorFromMap(*colorIt, ScrollTabKey::InactiveColor);
                pill.urgentColor = colorFromMap(*colorIt, ScrollTabKey::UrgentColor);
            }
            indicator.tabs.append(pill);
        }
        indicators.append(indicator);
    }

    const QRect before = painter->boundsFor(out);
    const bool changed = painter->setIndicators(out, indicators, style);
    if (changed && KWin::effects) {
        const QRect after = painter->boundsFor(out);
        // Damage the union: the old rect must be repainted away and the new
        // one painted in, and the painter's own blit damages nothing. Only on
        // a real change: most rebuilds (a colour reply, a caption tick on
        // another screen's tab, a palette event) verify as no-ops in the
        // painter and must not cost a recomposite.
        QRect damage = before.isValid() ? before : after;
        if (after.isValid()) {
            damage = damage.isValid() ? damage.united(after) : after;
        }
        if (damage.isValid()) {
            KWin::effects->addRepaint(KWin::Rect(damage));
        }
        // The pill under a parked pointer may have moved or vanished: a
        // stale hover would keep the hand (and the interception) over
        // whatever is there now until the next motion. Re-evaluate at the
        // live pointer.
        updateScrollTabHover(KWin::effects->cursorPos());
    }

    // Ask the daemon for the verdicts this rebuild could not answer. One
    // round trip per never-seen window; the reply lands as a cache write plus
    // a targeted rebuild. A window with NO rule is cached as an empty map so
    // it is never asked again (until the next broadcast).
    for (const QString& windowId : std::as_const(colorsToQuery)) {
        queryScrollTabColors(windowId);
    }
}

// ── Input ────────────────────────────────────────────────────────────────────

QString TilingHandler::scrollTabPillAt(const QPointF& pos) const
{
    if (!KWin::effects || m_scrollTabPayloadByScreen.isEmpty()) {
        return QString();
    }
    KWin::LogicalOutput* out = KWin::effects->screenAt(pos.toPoint());
    if (!out) {
        return QString();
    }
    const ScrollTabIndicatorPainter* painter = m_effect->m_scrollTabPainter.get();
    // paintedLastPass: a model can exist with nothing blitted (every column
    // parked, a teardown race) and input must answer for the PIXELS, not the
    // model — a pill nothing drew must neither show the hand nor eat a click.
    if (!painter->hasIndicators(out) || !painter->paintedLastPass(out)) {
        return QString();
    }
    // The same offset the blit applies: a pill mid-leg is where it is DRAWN,
    // not where the engine resolved it.
    const QString hit = painter->pillAt(out, pos, m_effect->m_stripViewAnimator->offsetFor(out));
    if (hit.isEmpty()) {
        return QString();
    }
    // The blit lands above the columns and BELOW whatever the stacking puts
    // over the strip, so a pill under a dialog, a floating window or a panel
    // was drawn and then overdrawn. Answer for what is visible: walk the
    // stacking order from the top and find the first paintable window under
    // the pointer — a strip column means the pill over it is showing, any
    // other window means it is covered, nothing means it sits over the gap.
    return scrollTabPillOccludedAt(pos, out) ? QString() : hit;
}

bool TilingHandler::scrollTabPillOccludedAt(const QPointF& pos, KWin::LogicalOutput* out) const
{
    // A DEPTH probe, not a hit test. The pill blit runs right after the
    // anchor paints, and the anchor is the TOPMOST non-parked strip member
    // of this output, so the question is only "is the topmost thing painted
    // at pos above or below strip depth". Two consequences shape this walk:
    //
    //  - The strip-depth test must NOT be gated on containing the pointer.
    //    The first non-parked managed column of this output marks strip
    //    depth wherever the pointer is; everything below it (a dialog over a
    //    LOWER column, say) painted before the pills and covers nothing, so
    //    the walk ends there. Testing contains() first read such a dialog as
    //    an occluder and killed input on a pill that is visibly drawn.
    //
    //  - Committed rects are deliberate, view offset and all. The only
    //    verdict a managed column produces is "strip depth reached", and any
    //    non-parked managed column is at or below the anchor by the anchor's
    //    own election rule, so WHICH column the committed lookup lands on
    //    cannot change the answer. Non-managed windows (the only rects the
    //    walk actually hit-tests) paint at their committed rect.
    const QList<KWin::EffectWindow*> stack = KWin::effects->stackingOrder();
    for (auto it = stack.crbegin(); it != stack.crend(); ++it) {
        KWin::EffectWindow* w = *it;
        if (!w || w->isDeleted() || w->isMinimized() || w->isHidden() || w->isHiddenByShowDesktop()
            || !w->isOnCurrentDesktop() || !w->isOnCurrentActivity()) {
            continue;
        }
        // The desktop (wallpaper) window is below every strip column, and
        // with the default outside-the-column placement the band sits over
        // the gap, where it is the topmost window under the pointer. It
        // covers nothing. Docks and override-redirect popups are NOT skipped:
        // a panel or a menu over the band really is drawn after the anchor.
        if (w->isDesktop()) {
            continue;
        }
        // Our own click-through overlays (zone overlay, OSD, drop indicator)
        // stack above everything and take no input; they cover nothing for
        // this purpose.
        if (PlasmaZonesEffect::isOwnPassthroughOverlayClass(w->windowClass())) {
            continue;
        }
        KWin::LogicalOutput* const managed = m_effect->scrollManagedOutputFor(w);
        if (managed == out) {
            // Strip depth, unless the column is parked off-screen (paintWindow
            // culls a parked column, so it marks no depth here).
            if (m_effect->scrollParkedOffscreen(w, m_effect->getWindowId(w))) {
                continue;
            }
            return false; // at or below the anchor: the pills paint above
        }
        if (managed) {
            // A foreign-managed strip column (crop-mode straddler): culled
            // on this output's pass, so it overdraws nothing here.
            continue;
        }
        if (!w->frameGeometry().contains(pos)) {
            continue;
        }
        return true; // an ordinary window, dialog, float or panel over the band
    }
    return false;
}

void TilingHandler::updateScrollTabHover(const QPointF& pos)
{
    if (!KWin::effects) {
        return;
    }
    if (m_scrollTabPayloadByScreen.isEmpty() || PlasmaZonesEffect::isShowingDesktop()) {
        // No pills anywhere, or Peek at Desktop has flung the columns to the
        // edges while the model stays put: either way the hand cursor and
        // the interception must not be held (activateScrollTabAt already
        // refuses during the peek, so a held interception would only eat the
        // click). The override can also still be held from a strip that just
        // vanished under a parked cursor; give it back.
        m_scrollTabHoverScreen.clear();
        setScrollTabHoverCursor(false);
        return;
    }
    ScrollTabIndicatorPainter* painter = m_effect->m_scrollTabPainter.get();
    KWin::LogicalOutput* out = KWin::effects->screenAt(pos.toPoint());
    const QString screenId = out ? m_effect->outputScreenId(out) : QString();
    // A pointer that crossed to another output must clear the hover it left
    // behind; the painter tracks hover per output and cannot see the cross.
    if (!m_scrollTabHoverScreen.isEmpty() && m_scrollTabHoverScreen != screenId) {
        if (KWin::LogicalOutput* prev = m_effect->outputForScreenId(m_scrollTabHoverScreen)) {
            if (painter->setHover(prev, QPointF(-1.0e9, -1.0e9))) {
                const QRect bounds = painter->boundsFor(prev);
                if (bounds.isValid()) {
                    KWin::effects->addRepaint(KWin::Rect(bounds));
                }
            }
        }
        m_scrollTabHoverScreen.clear();
    }
    bool overPill = false;
    if (out && painter->hasIndicators(out) && painter->paintedLastPass(out)) {
        const QPointF viewOffset = m_effect->m_stripViewAnimator->offsetFor(out);
        // A pill under a window stacked over the strip is drawn and then
        // overdrawn (see scrollTabPillAt); hover must not light it or take
        // the pointer. The test runs once here and feeds both the hover and
        // the interception decision below.
        const QPointF hoverPos = scrollTabPillOccludedAt(pos, out) ? QPointF(-1.0e9, -1.0e9) : pos;
        // One hit scan answers both the hover update and "is the pointer over
        // a pill": setHover reports the pill it resolved.
        QString hit;
        if (painter->setHover(out, hoverPos, viewOffset, &hit)) {
            const QRect bounds = painter->boundsFor(out);
            if (bounds.isValid()) {
                KWin::effects->addRepaint(KWin::Rect(bounds));
            }
        }
        overPill = !hit.isEmpty();
    }
    m_scrollTabHoverScreen = overPill ? screenId : QString();
    // The pills are painted over the column's own edge, so KWin would show
    // whatever THAT surface asks for there — a resize arrow at the column
    // border, the client's cursor elsewhere. Hold the effects override cursor
    // for exactly the span the pointer is over a pill: it outranks the
    // window's shape while held and restores it the moment it is released.
    setScrollTabHoverCursor(overPill);
}

void TilingHandler::noteScrollTabPress()
{
    // A left press landed on a pill through the interception. The
    // interception must now outlive the hover: if the pointer drags off the
    // pill before the button comes up, releasing it early would deliver the
    // RELEASE to the window underneath for a press it never saw — the same
    // pairing invariant ScrollOverhangInputFilter keeps for the presses it
    // consumes.
    m_scrollTabPressHeld = true;
}

void TilingHandler::noteScrollTabRelease(const QPointF& pos)
{
    if (!m_scrollTabPressHeld) {
        return;
    }
    m_scrollTabPressHeld = false;
    // The pair is complete; now the hover decides whether the interception
    // stays (pointer still over a pill) or goes.
    updateScrollTabHover(pos);
}

bool TilingHandler::eventFilter(QObject* watched, QEvent* event)
{
    // Palette and font changes arrive as application events on qGuiApp; the
    // two are what the pills' theme and units derive from. Gated on the
    // receiver: a filter installed on the application object sees every
    // object's events, and under a widgets application a palette change is
    // re-sent to every widget — one rebuild per receiver instead of one.
    if (event && watched == qGuiApp
        && (event->type() == QEvent::ApplicationPaletteChange || event->type() == QEvent::ApplicationFontChange)) {
        invalidateScrollTabTheme();
        rebuildAllScrollTabIndicators();
    }
    return QObject::eventFilter(watched, event);
}

void TilingHandler::voidInFlightScrollTabFetches()
{
    // Bumped at DISPATCH by the fetches themselves, so this is only for the
    // daemon-loss edge, where no re-dispatch follows: a reply the dead daemon
    // sent just before dropping its name must not re-install its payload
    // after the teardown cleared it. NOT called from the per-session drain
    // on bring-up, which runs AFTER loadSettings dispatched the replay and
    // would void it.
    ++m_scrollTabStripsQueryGeneration;
    ++m_scrollTabOverridesQueryGeneration;
}

void TilingHandler::clearScrollTabState()
{
    // The press latch goes too: a daemon that died mid-press takes the pills
    // with it, and holding an interception for a strip that no longer exists
    // would swallow the release of a press nothing will answer.
    m_scrollTabPressHeld = false;
    setScrollTabHoverCursor(false);
    m_scrollTabHoverScreen.clear();
    m_scrollTabPayloadByScreen.clear();
    m_scrollTabScreensByWindow.clear();
    m_scrollTabColorCache.clear();
    m_scrollTabColorsInFlight.clear();
    ++m_scrollTabColorGeneration; // in-flight replies belong to the dead session
    m_scrollTabPaintOverrides.clear();
    m_effect->m_scrollTabPainter->clearAll();
    // Same pairing as every clearOutput site: after the clear the effect can
    // leave the paint chain, so the retired textures cannot wait for a paint
    // pass. Idempotent under the teardown caller's own make-current +
    // releaseGl (releaseGl drains an already-empty graveyard).
    drainRetiredScrollTabTextures();
}

void TilingHandler::noteScrollTabOutputRemoved(KWin::LogicalOutput* output, const QString& removedScreenId)
{
    if (!output) {
        return;
    }
    // Compare against the id the caller resolved BEFORE it cleared the
    // screen-id cache (onScreenRemoved's documented order): resolving the
    // hover screen's output here would walk effects->screens(), which still
    // lists the dying output, and re-insert the very cache entry the handler
    // exists to purge.
    if (!m_scrollTabHoverScreen.isEmpty() && !removedScreenId.isEmpty()
        && PhosphorIdentity::VirtualScreenId::extractPhysicalId(m_scrollTabHoverScreen)
            == PhosphorIdentity::VirtualScreenId::extractPhysicalId(removedScreenId)) {
        m_scrollTabHoverScreen.clear();
        setScrollTabHoverCursor(false);
    }
    // The model for the screen goes with the output: a re-plug re-announces
    // it through the daemon's replay (fetchScrollTabStrips on the next
    // bring-up) or the next relayout, and a payload kept here would only
    // keep the input gates armed for pills nothing paints.
    if (!removedScreenId.isEmpty()) {
        const QList<QString> indexedBefore = m_scrollTabScreensByWindow.keys();
        const QStringList screens = m_scrollTabPayloadByScreen.keys();
        for (const QString& screenId : screens) {
            if (PhosphorIdentity::VirtualScreenId::extractPhysicalId(screenId)
                == PhosphorIdentity::VirtualScreenId::extractPhysicalId(removedScreenId)) {
                unindexScrollTabScreen(screenId);
                m_scrollTabPayloadByScreen.remove(screenId);
                m_scrollTabPaintOverrides.remove(screenId);
            }
        }
        // Same pairing as the other two unindex sites: a window that stopped
        // being a tab anywhere because its output left loses its verdict.
        dropScrollTabColorsForUnindexed(indexedBefore);
    }
    m_effect->m_scrollTabPainter->clearOutput(output);
    drainRetiredScrollTabTextures();
}

void TilingHandler::drainRetiredScrollTabTextures()
{
    // A clear can be the last thing that happens while the effect is in the
    // paint chain (isActive drops with the last indicator), so the retired
    // texture cannot wait for a paint pass. Guarded: ensureGlContextCurrent
    // is false only during compositor teardown, where releaseGl() and the
    // destructor cover it, and an unguarded call would turn the GL-free
    // clear into an off-context delete.
    if (m_effect->ensureGlContextCurrent()) {
        m_effect->m_scrollTabPainter->drainRetiredTextures();
    }
}

void TilingHandler::setScrollTabHoverCursor(bool overPill)
{
    // While a pill press is held the interception stays regardless of hover
    // (see noteScrollTabPress); the release path re-evaluates.
    const bool hold = overPill || m_scrollTabPressHeld;
    if (!KWin::effects) {
        // No compositor to talk to (teardown): record the intent so the latch
        // does not claim an interception that was never taken.
        m_scrollTabCursorOverridden = false;
        return;
    }
    if (hold == m_scrollTabCursorOverridden) {
        return;
    }
    // Mouse INTERCEPTION, not the bare effects override cursor: KWin's cursor
    // image only consults the effects cursor while an effect holds the
    // interception (CursorImage::reevaluteSource orders it by that flag), so
    // the override alone is silently outranked by the focused surface's own
    // shape — the resize arrow at the column edge, the client's cursor
    // elsewhere. While the pointer is over a pill the interception is held
    // with the pointing hand; every pointer event then lands on
    // PlasmaZonesEffect::pointerMotion / pointerButton / pointerAxis, which
    // route back here (hover keeps tracking, a left press activates the tab)
    // and the interception ends the moment the pointer leaves the pill. Held
    // for the pill's span only, so no column ever loses input it should have
    // had. The one cost, accepted: while held, KWin consumes EVERY pointer
    // event for the effect, so a wheel tick, a right or a middle press over a
    // pill reach nothing (there is no effect-side way to forward them to the
    // window underneath). The pills are thin and the hand makes the span
    // visible, which is the same trade a toolkit tab bar makes.
    if (hold) {
        KWin::effects->startMouseInterception(m_effect, Qt::PointingHandCursor);
    } else {
        KWin::effects->stopMouseInterception(m_effect);
    }
    m_scrollTabCursorOverridden = hold;
}

bool TilingHandler::activateScrollTabAt(const QPointF& pos)
{
    const QString windowId = scrollTabPillAt(pos);
    if (windowId.isEmpty()) {
        return false;
    }
    // Resolve EXACTLY and only then claim the press: the caller consumes the
    // press on a true return, so a tab whose window closed between the
    // payload and the click (or a click during show-desktop, which the focus
    // path refuses) must fall through to whatever is underneath instead of
    // being swallowed — and the fuzzy same-app fallback the focus slot's own
    // lookup carries must not activate a sibling window.
    if (PlasmaZonesEffect::isShowingDesktop() || !m_effect->findWindowByIdExact(windowId)) {
        return false;
    }
    // The same activation the daemon performed for a pill click: focus the
    // tab's window, and the strip learns which tab is showing through the
    // ordinary windowFocused report. One owner of "which tab is active".
    slotFocusWindowRequested(windowId);
    return true;
}

} // namespace PlasmaZones
