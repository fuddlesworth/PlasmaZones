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

#include "tilinghandler.h"

#include "plasmazoneseffect/plasmazoneseffect.h"
#include "compositor/scrolltabindicatorpainter.h"
#include "compositor/stripviewanimator.h"

#include <PhosphorIdentity/WindowId.h>
#include <PhosphorProtocol/ServiceConstants.h>

#include <KColorScheme>

#include <core/rect.h>
#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <window.h>

#include <QDBusConnection>
#include <QEvent>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QPalette>

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

namespace PlasmaZones {

namespace {

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

/// Keys of the per-window colour map the daemon's scrollTabColors answers
/// with, and of the per-screen context-override map scrollTabPaintOverrides
/// carries (WindowColorKeys / WindowPaintKeys on the daemon side — the
/// spellings are the contract). A missing or unparsable value reads as "not
/// overridden", which the painter resolves to the next tier.
constexpr QLatin1String kColorActive("activeColor");
constexpr QLatin1String kColorInactive("inactiveColor");
constexpr QLatin1String kColorUrgent("urgentColor");
constexpr QLatin1String kPaintTabStyle("tabStyle");
constexpr QLatin1String kPaintGapsBetweenTabs("gapsBetweenTabs");
constexpr QLatin1String kPaintCornerRadius("cornerRadius");

QColor colorFromMap(const QVariantMap& map, QLatin1String key)
{
    const QString text = map.value(key).toString();
    return text.isEmpty() ? QColor() : QColor(text);
}

/// Theme palette and units for the pills, resolved to match what the QML
/// rendering saw through Kirigami. The effect has no QML engine, so
/// Kirigami.Theme itself is out of reach; instead the values come from the
/// same sources Kirigami's desktop platform plugin reads them from. The five
/// colours are KColorScheme's View set (Kirigami's Theme.View is the
/// default colour set for an overlay, and highlight / highlightedText /
/// text / background / negativeText are its Selection and View roles); the
/// spacing units derive exactly as Kirigami::Platform::Units does for the
/// desktop style: gridUnit is the general font's line height,
/// smallSpacing = max(2, gridUnit / 4), largeSpacing = smallSpacing * 3.
/// KColorScheme reads the active scheme through KSharedConfig, which tracks
/// kdeglobals, so a scheme change re-resolves here on the next rebuild —
/// which the palette hook (eventFilter) triggers at once.
void fillThemePalette(ScrollTabIndicatorStyle& style)
{
    const KColorScheme view(QPalette::Active, KColorScheme::View);
    const KColorScheme selection(QPalette::Active, KColorScheme::Selection);
    style.themeHighlight = selection.background(KColorScheme::NormalBackground).color();
    style.themeHighlightedText = selection.foreground(KColorScheme::NormalText).color();
    style.themeText = view.foreground(KColorScheme::NormalText).color();
    style.themeBackground = view.background(KColorScheme::NormalBackground).color();
    style.themeNegativeText = view.foreground(KColorScheme::NegativeText).color();
    const int gridUnit = qMax(1, QFontMetrics(QFontDatabase::systemFont(QFontDatabase::GeneralFont)).height());
    style.smallSpacing = qMax(2, gridUnit / 4);
    style.largeSpacing = style.smallSpacing * 3;
}

} // namespace

// ── Payload intake ───────────────────────────────────────────────────────────

void TilingHandler::slotScrollTabStripsChanged(const QString& screenId, const QString& stripsJson)
{
    if (screenId.isEmpty()) {
        return;
    }
    // Reverse index: drop this screen from every window it used to name, so
    // a window that left a tabbed column stops triggering rebuilds here.
    for (auto it = m_scrollTabScreensByWindow.begin(); it != m_scrollTabScreensByWindow.end();) {
        it->remove(screenId);
        if (it->isEmpty()) {
            it = m_scrollTabScreensByWindow.erase(it);
        } else {
            ++it;
        }
    }
    if (stripsJson.isEmpty() || stripsJson == kEmptyPayload) {
        m_scrollTabPayloadByScreen.remove(screenId);
        if (KWin::LogicalOutput* out = m_effect->outputForScreenId(screenId)) {
            const QRect bounds = m_effect->m_scrollTabPainter->boundsFor(out);
            m_effect->m_scrollTabPainter->clearOutput(out);
            if (bounds.isValid() && KWin::effects) {
                KWin::effects->addRepaint(KWin::Rect(bounds));
            }
        }
        if (m_scrollTabHoverScreen == screenId) {
            m_scrollTabHoverScreen.clear();
            // The pill under the parked pointer just vanished; the override
            // must not outlive it.
            setScrollTabHoverCursor(false);
        }
        return;
    }
    m_scrollTabPayloadByScreen.insert(screenId, stripsJson);
    // Re-index from the new payload. Parsed once here and once in the rebuild;
    // the payload is a handful of small objects, and keeping a parsed copy
    // would duplicate the painter's own model for the sake of one parse.
    const QJsonArray strips = QJsonDocument::fromJson(stripsJson.toUtf8()).array();
    for (const QJsonValue& v : strips) {
        const QJsonArray tabs = v.toObject().value(kKeyTabs).toArray();
        for (const QJsonValue& t : tabs) {
            m_scrollTabScreensByWindow[t.toString()].insert(screenId);
        }
    }
    rebuildScrollTabIndicators(screenId);
}

void TilingHandler::slotScrollTabColorsChanged(const QString& windowId, const QVariantMap& colors)
{
    if (windowId.isEmpty()) {
        // Broadcast: every verdict may have moved. Drop the cache so the next
        // rebuild re-queries, then rebuild every screen (the queries are async
        // and land as further rebuilds).
        m_scrollTabColorCache.clear();
        rebuildAllScrollTabIndicators();
        return;
    }
    m_scrollTabColorCache.insert(windowId, colors);
    noteScrollTabWindowChanged(windowId);
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

void TilingHandler::rebuildAllScrollTabIndicators()
{
    const QStringList screens = m_scrollTabPayloadByScreen.keys();
    for (const QString& screenId : screens) {
        rebuildScrollTabIndicators(screenId);
    }
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
        if (bounds.isValid() && KWin::effects) {
            KWin::effects->addRepaint(KWin::Rect(bounds));
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
    style.font = m_effect->scrollTabIndicatorFont();
    fillThemePalette(style);
    // Context-rule overrides for THIS screen layer over the global settings,
    // per key: a rule that only sets the active colour leaves the style, gaps
    // and radius at their global values. Same precedence the daemon applied
    // when it pushed these to QML (override wins over setting wins over
    // theme), now resolved where the pills are drawn.
    if (const auto ovIt = m_scrollTabPaintOverrides.constFind(screenId); ovIt != m_scrollTabPaintOverrides.constEnd()) {
        const QVariantMap& ov = *ovIt;
        if (const auto v = ov.constFind(kPaintTabStyle); v != ov.constEnd()) {
            style.style = v->toInt();
        }
        if (const auto v = ov.constFind(kPaintGapsBetweenTabs); v != ov.constEnd()) {
            style.gapsBetweenTabs = v->toInt();
        }
        if (const auto v = ov.constFind(kPaintCornerRadius); v != ov.constEnd()) {
            style.cornerRadius = v->toInt();
        }
        if (const QColor c = colorFromMap(ov, kColorActive); c.isValid()) {
            style.activeColor = c;
        }
        if (const QColor c = colorFromMap(ov, kColorInactive); c.isValid()) {
            style.inactiveColor = c;
        }
        if (const QColor c = colorFromMap(ov, kColorUrgent); c.isValid()) {
            style.urgentColor = c;
        }
    }
    m_scrollTabStyleGenerationSeen = m_effect->m_tabIndicatorStyleGeneration;

    QVector<ScrollTabIndicator> indicators;
    QStringList colorsToQuery;
    const QJsonArray strips = QJsonDocument::fromJson(payloadIt->toUtf8()).array();
    indicators.reserve(strips.size());
    for (const QJsonValue& v : strips) {
        const QJsonObject o = v.toObject();
        ScrollTabIndicator indicator;
        indicator.rect = QRect(o.value(kKeyX).toInt(), o.value(kKeyY).toInt(), o.value(kKeyWidth).toInt(),
                               o.value(kKeyHeight).toInt());
        if (!indicator.rect.isValid()) {
            continue;
        }
        indicator.position = o.value(kKeyPosition).toInt();
        // -1 default so a payload missing the key lights no tab, matching the
        // daemon parser's reading of the same absence.
        const int activeIndex = o.value(kKeyActiveIndex).toInt(-1);
        const QJsonArray tabs = o.value(kKeyTabs).toArray();
        indicator.tabs.reserve(tabs.size());
        for (int i = 0; i < tabs.size(); ++i) {
            ScrollTabPill pill;
            pill.windowId = tabs.at(i).toString();
            pill.active = (i == activeIndex);
            if (KWin::EffectWindow* w = m_effect->findWindowById(pill.windowId)) {
                pill.title = w->caption();
                if (KWin::Window* kw = w->window()) {
                    pill.urgent = kw->isDemandingAttention();
                }
            }
            if (pill.title.isEmpty()) {
                // Same fallback the daemon applied: the app id is always
                // known even when the client has not set a title yet.
                pill.title = PhosphorIdentity::WindowId::extractAppId(pill.windowId);
            }
            const auto colorIt = m_scrollTabColorCache.constFind(pill.windowId);
            if (colorIt == m_scrollTabColorCache.constEnd()) {
                colorsToQuery.append(pill.windowId);
            } else {
                pill.activeColor = colorFromMap(*colorIt, kColorActive);
                pill.inactiveColor = colorFromMap(*colorIt, kColorInactive);
                pill.urgentColor = colorFromMap(*colorIt, kColorUrgent);
            }
            indicator.tabs.append(pill);
        }
        indicators.append(indicator);
    }

    const QRect before = painter->boundsFor(out);
    painter->setIndicators(out, indicators, style);
    if (KWin::effects) {
        const QRect after = painter->boundsFor(out);
        // Damage the union: the old rect must be repainted away and the new
        // one painted in, and the painter's own blit damages nothing.
        QRect damage = before.isValid() ? before : after;
        if (after.isValid()) {
            damage = damage.isValid() ? damage.united(after) : after;
        }
        if (damage.isValid()) {
            KWin::effects->addRepaint(KWin::Rect(damage));
        }
    }

    // Ask the daemon for the verdicts this rebuild could not answer. One
    // round trip per never-seen window; the reply lands as a cache write plus
    // a targeted rebuild. A window with NO rule is cached as an empty map so
    // it is never asked again.
    for (const QString& windowId : colorsToQuery) {
        // Mark as in flight with an empty map so a second rebuild during the
        // round trip does not dispatch a duplicate query; the reply overwrites.
        m_scrollTabColorCache.insert(windowId, QVariantMap());
        QDBusMessage msg = QDBusMessage::createMethodCall(
            PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
            PhosphorProtocol::Service::Interface::Tiling, QStringLiteral("scrollTabColors"));
        msg << windowId;
        QDBusPendingCall call =
            QDBusConnection::sessionBus().asyncCall(msg, PhosphorProtocol::Service::SyncCallTimeoutMs);
        auto* watcher = new QDBusPendingCallWatcher(call, this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, windowId](QDBusPendingCallWatcher* w) {
            w->deleteLater();
            QDBusPendingReply<QVariantMap> reply = *w;
            if (reply.isError()) {
                // Leave the empty placeholder: the next broadcast clears it.
                qCDebug(lcEffect) << "scrollTabColors failed for" << windowId << reply.error().message();
                return;
            }
            const QVariantMap colors = reply.value();
            if (colors.isEmpty()) {
                return; // the placeholder already says "no rule"
            }
            m_scrollTabColorCache.insert(windowId, colors);
            noteScrollTabWindowChanged(windowId);
        });
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
    if (!painter->hasIndicators(out)) {
        return QString();
    }
    // The same offset the blit applies: a pill mid-leg is where it is DRAWN,
    // not where the engine resolved it.
    return painter->pillAt(out, pos, m_effect->m_stripViewAnimator->offsetFor(out));
}

bool TilingHandler::updateScrollTabHover(const QPointF& pos)
{
    if (!KWin::effects) {
        return false;
    }
    if (m_scrollTabPayloadByScreen.isEmpty()) {
        // No pills anywhere. The override can still be held from a strip
        // that just vanished under a parked cursor; give it back.
        setScrollTabHoverCursor(false);
        return false;
    }
    ScrollTabIndicatorPainter* painter = m_effect->m_scrollTabPainter.get();
    KWin::LogicalOutput* out = KWin::effects->screenAt(pos.toPoint());
    const QString screenId = out ? m_effect->outputScreenId(out) : QString();
    bool changed = false;
    // A pointer that crossed to another output must clear the hover it left
    // behind; the painter tracks hover per output and cannot see the cross.
    if (!m_scrollTabHoverScreen.isEmpty() && m_scrollTabHoverScreen != screenId) {
        if (KWin::LogicalOutput* prev = m_effect->outputForScreenId(m_scrollTabHoverScreen)) {
            if (painter->setHover(prev, QPointF(-1.0e9, -1.0e9))) {
                KWin::effects->addRepaint(KWin::Rect(painter->boundsFor(prev)));
                changed = true;
            }
        }
        m_scrollTabHoverScreen.clear();
    }
    bool overPill = false;
    if (out && painter->hasIndicators(out)) {
        const QPointF viewOffset = m_effect->m_stripViewAnimator->offsetFor(out);
        if (painter->setHover(out, pos, viewOffset)) {
            KWin::effects->addRepaint(KWin::Rect(painter->boundsFor(out)));
            changed = true;
        }
        overPill = !painter->pillAt(out, pos, viewOffset).isEmpty();
    }
    m_scrollTabHoverScreen = overPill ? screenId : QString();
    // The pills are painted over the column's own edge, so KWin would show
    // whatever THAT surface asks for there — a resize arrow at the column
    // border, the client's cursor elsewhere. Hold the effects override cursor
    // for exactly the span the pointer is over a pill: it outranks the
    // window's shape while held and restores it the moment it is released.
    setScrollTabHoverCursor(overPill);
    return changed;
}

bool TilingHandler::eventFilter(QObject* watched, QEvent* event)
{
    // Palette and font changes arrive as application events on qGuiApp; the
    // two are what the pills' theme and units derive from. Rebuilding all
    // screens is a no-op compare in the painter when nothing visible moved.
    if (event
        && (event->type() == QEvent::ApplicationPaletteChange || event->type() == QEvent::ApplicationFontChange)) {
        rebuildAllScrollTabIndicators();
    }
    return QObject::eventFilter(watched, event);
}

void TilingHandler::clearScrollTabState()
{
    setScrollTabHoverCursor(false);
    m_scrollTabHoverScreen.clear();
    m_scrollTabPayloadByScreen.clear();
    m_scrollTabScreensByWindow.clear();
    m_scrollTabColorCache.clear();
    m_scrollTabPaintOverrides.clear();
    m_effect->m_scrollTabPainter->clearAll();
}

void TilingHandler::noteScrollTabOutputRemoved(KWin::LogicalOutput* output)
{
    if (!output) {
        return;
    }
    if (!m_scrollTabHoverScreen.isEmpty() && m_effect->outputForScreenId(m_scrollTabHoverScreen) == output) {
        m_scrollTabHoverScreen.clear();
        setScrollTabHoverCursor(false);
    }
    m_effect->m_scrollTabPainter->clearOutput(output);
}

void TilingHandler::setScrollTabHoverCursor(bool overPill)
{
    if (overPill == m_scrollTabCursorOverridden || !KWin::effects) {
        return;
    }
    // Mouse INTERCEPTION, not the bare effects override cursor: KWin's cursor
    // image only consults the effects cursor while an effect holds the
    // interception (CursorImage::reevaluteSource orders it by that flag), so
    // the override alone is silently outranked by the focused surface's own
    // shape — the resize arrow at the column edge, the client's cursor
    // elsewhere. While the pointer is over a pill the interception is held
    // with the pointing hand; every pointer event then lands on
    // PlasmaZonesEffect::pointerMotion / pointerButton, which route back here
    // (hover keeps tracking, a left press activates the tab) and the
    // interception ends the moment the pointer leaves the pill. Held for the
    // pill's span only, so no column ever loses input it should have had.
    if (overPill) {
        KWin::effects->startMouseInterception(m_effect, Qt::PointingHandCursor);
    } else {
        KWin::effects->stopMouseInterception(m_effect);
    }
    m_scrollTabCursorOverridden = overPill;
}

bool TilingHandler::activateScrollTabAt(const QPointF& pos)
{
    const QString windowId = scrollTabPillAt(pos);
    if (windowId.isEmpty()) {
        return false;
    }
    // The same activation the daemon performed for a pill click: focus the
    // tab's window, and the strip learns which tab is showing through the
    // ordinary windowFocused report. One owner of "which tab is active".
    slotFocusWindowRequested(windowId);
    return true;
}

} // namespace PlasmaZones
