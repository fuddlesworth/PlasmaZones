// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "daemon/daemon.h"
#include "daemon/overlayservice.h"
#include "daemon/controllers/unifiedlayoutcontroller.h"
#include "core/resolve/screenmoderouter.h"
#include <PhosphorContext/ContextResolver.h>
#include <PhosphorZones/AssignmentEntry.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>
#include <PhosphorWorkspaces/ActivityManager.h>
#include "core/platform/logging.h"
#include "core/utils/utils.h"
#include <PhosphorZones/ZoneDetector.h>
#include <PhosphorEngine/IPlacementEngine.h>
#include <PhosphorEngine/IPlacementState.h>
#include <PhosphorEngine/PlacementEngineBase.h>
#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"
#include "daemon/overlayservice/internal.h"
#include "helpers.h"
#include "stripzones.h"
#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorLayoutApi/LayoutId.h>
#include <PhosphorLayoutApi/LayoutPreview.h>
#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorZones/ScrollingTemplateSource.h>
#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/AutotilePreviewRender.h>
#include <PhosphorTiles/TilingAlgorithm.h>
#include "config/settings.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QRegularExpression>
#include <QScreen>
#include <QTimer>
#include "phosphor_i18n.h"
#include <PhosphorScreens/ScreenIdentity.h>

#include <functional>
#include <utility>

namespace PlasmaZones {

namespace {

void showKdeTextOsd(const QString& icon, const QString& text)
{
    QDBusMessage msg =
        QDBusMessage::createMethodCall(QStringLiteral("org.kde.plasmashell"), QStringLiteral("/org/kde/osdService"),
                                       QStringLiteral("org.kde.osdService"), QStringLiteral("showText"));
    msg << icon << text;
    QDBusConnection::sessionBus().asyncCall(msg);
}

/// Object-name prefix of the per-screen scrolling-OSD settle timers. One
/// restartable timer per screen, parented to the Daemon and found back by
/// name.
const QLatin1String kScrollingSettlePrefix("scrollingOsdSettle:");

QString scrollingSettleTimerName(const QString& screenId)
{
    return kScrollingSettlePrefix + screenId;
}

/// The screen id a settle timer belongs to. Sliced by LENGTH, not by
/// splitting on ':' — a virtual screen id carries its own colon
/// ("DP-1/vs:0") and a split would hand back a truncated id.
QString screenIdFromSettleTimerName(const QString& objectName)
{
    return objectName.mid(kScrollingSettlePrefix.size());
}

/// @p screenId's visible strip tiles, or empty when the scroll engine does
/// not currently own the screen. The isActiveOnScreen gate matches the
/// navigation-OSD zone provider (init_engines.cpp): a screen the engine has
/// released has no strip to preview, whatever stale state its context holds.
QVector<StripZones::VisibleTile> visibleStripTiles(const PhosphorEngine::PlacementEngineBase* engine,
                                                   const QString& screenId)
{
    const auto* scroll = qobject_cast<const PhosphorScrollEngine::ScrollEngine*>(engine);
    if (!scroll || !scroll->isActiveOnScreen(screenId)) {
        return {};
    }
    return scroll->visibleTiles(screenId);
}

/// Push the scrolling strip preview card for @p screenId from an ALREADY
/// RESOLVED tile list, so the caller that had to probe the strip to decide
/// whether to render at all does not pay for a second relayout and cannot
/// render a different snapshot than the one it probed.
///
/// @p tiles are the engine's visible tiles in zone-number order, carrying
/// their own zone numbers and their absolute clipped rects. Empty means the
/// strip has no visible tile, and the card falls back to the representative
/// sketch — as does a screen whose geometry cannot be resolved, since the
/// rects cannot be placed in the card's full-screen-shaped box without it.
///
/// @p verticalAxis is a callable, not a bool: only the sketch arm consumes it,
/// and resolving it eagerly at the call site costs a second per-screen layout
/// param resolve (rule cascade included) on every LIVE card, which is the
/// common one.
void pushScrollingStripOsd(OverlayService* overlay, PhosphorScreens::ScreenManager* screens, const QString& screenId,
                           const QVector<StripZones::VisibleTile>& tiles, const std::function<bool()>& verticalAxis)
{
    if (!overlay) {
        return;
    }
    // Through the shared resolver, not a bare screenGeometry(): the OSD
    // renderer this feeds sizes its window via the same helper, whose
    // QScreen fallback covers early startup and unmatched screen ids. A bare
    // lookup here would bail to the sketch (with a spurious warning) in a
    // case the renderer handles fine.
    const QRect screenGeometry = resolveScreenGeometry(screens, screenId);
    QVariantList zones = StripZones::zoneMapsForTiles(screenId, tiles, screenGeometry);
    const bool isSketch = zones.isEmpty();
    if (isSketch) {
        if (!tiles.isEmpty()) {
            // Tiles but no screen geometry to place them in. The card falls
            // back to the sketch rather than drawing rects it cannot
            // normalize, and says so here so the wrong-looking preview is
            // traceable to the screen lookup rather than to the strip.
            qCWarning(lcDaemon) << "Strip preview OSD: no geometry for screen=" << screenId << "tiles=" << tiles.size()
                                << "— falling back to the sketch";
        }
        // The LIVE arm needs no axis: those rects are the engine's own tiles,
        // already laid the way the strip runs. Only the sketch is drawn from
        // nothing and has to be told.
        zones = StripZones::sketchZoneMaps(screenId, verticalAxis && verticalAxis());
    }
    // Autotile category: the renderer treats it as "generated, not
    // editable", which is exactly what a live strip snapshot is.
    overlay->showLayoutOsd(QString(PhosphorLayout::LayoutId::ScrollingId),
                           PhosphorI18n::tr("Scrolling", "tiling mode name"), zones,
                           static_cast<int>(PhosphorZones::LayoutCategory::Autotile), false, screenId, false, false,
                           isSketch ? QStringLiteral("none") : QStringLiteral("all"), 1);
    qCInfo(lcDaemon) << "Showing scrolling-mode strip preview OSD: screen=" << screenId << "tiles=" << tiles.size()
                     << "zones=" << zones.size() << "sketch=" << isSketch;
}

} // namespace

void Daemon::reapScrollingOsdSettleTimers(const QString& screenId)
{
    const auto settleTimers = findChildren<QTimer*>(
        QRegularExpression(QLatin1Char('^') + QRegularExpression::escape(QString(kScrollingSettlePrefix))),
        Qt::FindDirectChildrenOnly);
    for (QTimer* settle : settleTimers) {
        const QString timerScreenId = screenIdFromSettleTimerName(settle->objectName());
        // samePhysical, not equality: a removed output takes every virtual
        // sub-screen of it with it, and each of those has its own timer.
        if (!screenId.isEmpty() && !PhosphorIdentity::VirtualScreenId::samePhysical(timerScreenId, screenId)) {
            continue;
        }
        settle->stop();
        // Clear the name BEFORE deleteLater: a deleted-but-not-yet-drained
        // timer stays a findChild hit, so showScrollingModeOsd would adopt a
        // zombie and start() a timer that is about to be destroyed.
        settle->setObjectName(QString());
        settle->deleteLater();
    }
}

void Daemon::showOverlay()
{
    // The overlay shows manual snap-zone selection during a drag. Don't
    // show it when no screen is in snap mode — that covers both
    // "every screen is autotile" and "every screen is in scrolling
    // (passthrough) mode" (a regression on the prior shape, which only
    // guarded the autotile-only case and let an all-scrolling setup
    // surface an empty overlay).
    if (m_screenModeRouter && m_screenManager) {
        const QStringList effectiveIds = m_screenManager->effectiveScreenIds();
        const auto parts = m_screenModeRouter->partitionByMode(effectiveIds);
        if (parts.snap.isEmpty()) {
            return;
        }
    }
    // Per-screen autotile exclusion is handled by OverlayService::initializeOverlay()
    // via m_excludedScreens (set in updateEngineScreens)
    if (m_overlayService) {
        m_overlayService->show();
    }
}

void Daemon::hideOverlay()
{
    clearHighlight();
    if (m_overlayService) {
        m_overlayService->hide();
    }
}

bool Daemon::isOverlayVisible() const
{
    return m_overlayService && m_overlayService->isVisible();
}

void Daemon::clearHighlight()
{
    if (m_zoneDetector) {
        m_zoneDetector->clearHighlights();
    }
}

void Daemon::armResnapOsdSuppression(int count)
{
    if (count <= 0) {
        return;
    }
    // ADD, never clobber: overlapping async resnap streams each pre-arm before
    // emitting, and their feedbacks drain this counter one-by-one. Overwriting
    // would drop a concurrent stream's outstanding count (one OSD wrongly shown,
    // a later one wrongly suppressed). The watchdog floors a stuck count.
    m_suppressResnapOsd += count;
    m_suppressResnapOsdWatchdog.start();
}

bool Daemon::globalOsdSuppressed() const
{
    if (m_shuttingDown) {
        return true;
    }
    // See queryPlasmaWorkspaceState() for why this catches phantom sessions.
    if (!m_plasmaWorkspaceActive) {
        return true;
    }
    // Screen-removal cooldown — see m_screensSettlingUntil in daemon.h.
    return std::chrono::steady_clock::now() < m_screensSettlingUntil;
}

bool Daemon::shouldSuppressOsd(const QString& screenId) const
{
    if (globalOsdSuppressed()) {
        return true;
    }
    // Per-context SetOsdEnabled rule: an explicit false suppresses every OSD
    // for the screen's current context. A screenless caller skips the rule
    // (fail-open); the force-ON half is layered at the trigger gates.
    return !screenId.isEmpty() && contextOsdRuleVerdict(screenId) == std::optional<bool>(false);
}

std::optional<bool> Daemon::contextOsdRuleVerdict(const QString& screenId) const
{
    if (screenId.isEmpty() || !m_layoutManager) {
        return std::nullopt;
    }
    // Callers hand over whatever screen identifier is in scope — some carry
    // the compositor name — so normalize to the canonical id form the rule
    // store matches on, same as showLockedPreviewOsd does.
    const QString resolvedId = PhosphorScreens::ScreenIdentity::idForName(screenId);
    const QString id = resolvedId.isEmpty() ? screenId : resolvedId;
    return m_layoutManager->resolveContextOsdEnabled(id, currentDesktopForScreen(id), currentActivity());
}

bool Daemon::navigationOsdAllowed(const QString& screenId) const
{
    if (!m_overlayService) {
        return false;
    }
    if (globalOsdSuppressed()) {
        return false;
    }
    // OSD style None means no cards at all, navigation feedback included. The
    // settings page presents the style as "visual style of on-screen
    // notifications" and enables it whenever ANY of the three OSD toggles is
    // on — the navigation one among them — so None has to be honoured on this
    // family too. It was the only family that never consulted the style, which
    // a SetOsdEnabled rule's force-ON half made reachable without the user
    // having any OSD toggle on at all.
    if ((m_settings ? m_settings->osdStyle() : OsdStyle::Preview) == OsdStyle::None) {
        return false;
    }
    // ONE verdict resolve for both halves of the rule: a false verdict is the
    // suppress half (what shouldSuppressOsd would have applied) and a true one
    // is the force-ON half, and value_or() collapses them into the same read.
    // Asking shouldSuppressOsd separately re-resolved the same context rule a
    // second time on every snap.
    const std::optional<bool> rule = contextOsdRuleVerdict(screenId);
    return rule.value_or(m_settings && m_settings->showNavigationOsd());
}

void Daemon::showLayoutOsd(PhosphorZones::Layout* layout, const QString& screenId)
{
    if (!layout) {
        return;
    }
    if (shouldSuppressOsd(screenId)) {
        return;
    }

    const QString layoutName = layout->name();

    // Check OSD style setting
    OsdStyle style = m_settings ? m_settings->osdStyle() : OsdStyle::Preview;

    switch (style) {
    case OsdStyle::None:
        // No OSD
        qCInfo(lcDaemon) << "OSD disabled, skipping for layout=" << layoutName;
        return;

    case OsdStyle::Text: {
        QString displayText = PhosphorI18n::tr("Layout: %1").arg(layoutName);
        showKdeTextOsd(QStringLiteral("plasmazones"), displayText);
        qCInfo(lcDaemon) << "Showing text OSD for layout=" << layoutName;
    } break;

    case OsdStyle::Preview:
        // Use visual layout preview OSD
        if (m_overlayService) {
            m_overlayService->showLayoutOsd(layout, screenId);
            qCInfo(lcDaemon) << "Preview OSD: layout=" << layoutName << "screen=" << screenId;
        } else {
            qCWarning(lcDaemon) << "Overlay service not available for preview OSD";
        }
        break;
    }
}

void Daemon::showLockedOsd(const QString& screenId)
{
    if (shouldSuppressOsd(screenId)) {
        return;
    }
    OsdStyle style = m_settings ? m_settings->osdStyle() : OsdStyle::Preview;
    if (style == OsdStyle::None) {
        return;
    }

    showKdeTextOsd(QStringLiteral("object-locked"), PhosphorI18n::tr("Layout Locked"));
    qCInfo(lcDaemon) << "Showing locked text OSD for screen=" << screenId;
}

void Daemon::showLockedPreviewOsd(const QString& screenId)
{
    if (shouldSuppressOsd(screenId)) {
        return;
    }
    OsdStyle style = m_settings ? m_settings->osdStyle() : OsdStyle::Preview;
    if (style == OsdStyle::None) {
        return;
    }

    // Show the visual preview OSD with lock overlay showing the current
    // layout. A Templates (scrolling) context resolves its native TEMPLATE:
    // resolveLayoutForScreen is the snap-only chain and would preview an
    // unrelated fallback snap layout's zones under the lock badge. A
    // template-less scrolling context falls through to the text card.
    if (style == OsdStyle::Preview && m_overlayService && m_layoutManager) {
        const QString resolvedId = PhosphorScreens::ScreenIdentity::idForName(screenId);
        const QString id = resolvedId.isEmpty() ? screenId : resolvedId;
        if (currentModeFor(id) == PhosphorZones::AssignmentEntry::Scrolling) {
            const PhosphorZones::ScrollingTemplate templ =
                m_layoutManager->scrollingTemplateForContext(id, currentDesktopForScreen(id), currentActivity());
            if (templ.isValid()) {
                showScrollingTemplateOsd(templ, id, /*locked=*/true);
                return;
            }
        } else if (PhosphorZones::Layout* layout = m_layoutManager->resolveLayoutForScreen(id)) {
            m_overlayService->showLockedLayoutOsd(layout, id);
            return;
        }
    }

    // Fall back to text OSD
    showLockedOsd(screenId);
}

void Daemon::showLayoutsUnavailableOsd(const QString& screenId)
{
    // Logged ahead of the settings gate: with navigation OSDs off the
    // refusal would otherwise be indistinguishable from a broken keybinding.
    qCDebug(lcDaemon) << "Layout shortcut ignored — engine provides no layouts for screen" << screenId;
    // Same gate as the navigationFeedback relay in signals.cpp — this is a
    // navigation-style failure OSD, not a layout-switch OSD, so it follows
    // the showNavigationOsd toggle rather than osdStyle. The whole
    // navigation-OSD family also carries the shouldSuppressOsd gate now:
    // shutdown, phantom sessions and the screen-settling cooldown suppress
    // navigation feedback the same way they suppress layout cards.
    if (navigationOsdAllowed(screenId)) {
        m_overlayService->showNavigationOsd(false, QStringLiteral("layout"), QStringLiteral("not_supported"), QString(),
                                            QString(), screenId);
    }
}

void Daemon::showContextDisabledOsd(const QString& screenId, int desktop, const QString& activity,
                                    DisabledReason reason)
{
    if (shouldSuppressOsd(screenId)) {
        return;
    }
    OsdStyle style = m_settings ? m_settings->osdStyle() : OsdStyle::Preview;
    if (style == OsdStyle::None) {
        return;
    }

    if (reason == DisabledReason::NotDisabled) {
        qCWarning(lcDaemon) << "showContextDisabledOsd called but context is not disabled"
                            << "screen=" << screenId << "desktop=" << desktop;
        return;
    }

    QString reasonText;
    switch (reason) {
    case DisabledReason::MonitorDisabled:
        reasonText = PhosphorI18n::tr("Disabled on this monitor");
        break;
    case DisabledReason::DesktopDisabled: {
        QString desktopLabel;
        if (m_virtualDesktopManager) {
            const QStringList names = m_virtualDesktopManager->desktopNames();
            if (desktop > 0 && desktop <= names.size()) {
                desktopLabel = names[desktop - 1];
            }
        }
        if (desktopLabel.isEmpty()) {
            desktopLabel = PhosphorI18n::tr("Desktop %1").arg(desktop);
        }
        reasonText = PhosphorI18n::tr("Disabled on %1").arg(desktopLabel);
        break;
    }
    case DisabledReason::ActivityDisabled: {
        QString activityLabel;
        if (m_activityManager && !activity.isEmpty()) {
            activityLabel = m_activityManager->activityName(activity);
        }
        if (activityLabel.isEmpty()) {
            reasonText = PhosphorI18n::tr("Disabled on this activity");
        } else {
            reasonText = PhosphorI18n::tr("Disabled on %1").arg(activityLabel);
        }
        break;
    }
    case DisabledReason::NotDisabled:
        Q_UNREACHABLE();
    }

    if (style == OsdStyle::Preview && m_overlayService) {
        m_overlayService->showDisabledOsd(reasonText, screenId);
        qCInfo(lcDaemon) << "Showing disabled preview OSD:" << reasonText << "screen=" << screenId;
        return;
    }

    // Fall back to text OSD
    showKdeTextOsd(QStringLiteral("dialog-cancel"), reasonText);
    qCInfo(lcDaemon) << "Showing disabled text OSD:" << reasonText << "screen=" << screenId;
}

void Daemon::showNotAssignedOsd(const QString& screenId)
{
    if (shouldSuppressOsd(screenId)) {
        return;
    }
    const OsdStyle style = m_settings ? m_settings->osdStyle() : OsdStyle::Preview;
    if (style == OsdStyle::None) {
        return;
    }
    const QString text = PhosphorI18n::tr("No layout assigned");
    if (style == OsdStyle::Preview && m_overlayService) {
        m_overlayService->showDisabledOsd(text, screenId);
        qCInfo(lcDaemon) << "Showing not-assigned preview OSD: screen=" << screenId;
        return;
    }
    showKdeTextOsd(QStringLiteral("dialog-information"), text);
    qCInfo(lcDaemon) << "Showing not-assigned text OSD: screen=" << screenId;
}

bool Daemon::isOsdTriggerEnabled(OsdTrigger trigger, const QString& screenId) const
{
    // The SetOsdEnabled rule's force-ON half: an explicit true verdict for
    // the context overrides an off per-trigger toggle. (The false half is
    // handled by shouldSuppressOsd, not here, so a false verdict simply
    // falls through to the toggle — the show still dies at the suppress
    // gate.)
    if (!screenId.isEmpty() && contextOsdRuleVerdict(screenId) == std::optional<bool>(true)) {
        return true;
    }
    if (!m_settings) {
        return true;
    }
    switch (trigger) {
    case OsdTrigger::LayoutSwitch:
        return m_settings->showOsdOnLayoutSwitch();
    case OsdTrigger::DesktopSwitch:
        return m_settings->showOsdOnDesktopSwitch();
    }
    return true;
}

void Daemon::showScrollingModeOsd(const QString& screenId, OsdTrigger trigger, StripSettle settle)
{
    // The mode-switch announcement for a screen entering Scrolling. Preview
    // style renders the strip: the engine's visible tiles stand in for the
    // zone list a layout switch would show, and an EMPTY strip shows the
    // representative endless-strip sketch (a full column with clipped edge
    // columns) — the algorithm OSD likewise previews with zero windows, and
    // the Monitors page uses the same sketch.
    if (shouldSuppressOsd(screenId)) {
        return;
    }
    const OsdStyle style = m_settings ? m_settings->osdStyle() : OsdStyle::Preview;
    if (style == OsdStyle::None) {
        // OSDs are off now, so an armed settle timer must not fire the card
        // the user just disabled.
        stopScrollingOsdSettleTimer(screenId);
        return;
    }
    if (style == OsdStyle::Preview && m_overlayService) {
        // ONE strip resolve, threaded into the render: probing with a
        // different accessor than the renderer used meant two relayouts of
        // the same strip and two chances to disagree about whether it was
        // empty.
        const QVector<StripZones::VisibleTile> tiles = visibleStripTiles(m_scrollEngine.get(), screenId);
        if (!tiles.isEmpty() || settle == StripSettle::Immediate) {
            // A card is rendering NOW, so an earlier toggle's settle timer
            // would land a second, identical card a beat later.
            stopScrollingOsdSettleTimer(screenId);
            recordAnnouncedScrollingTemplate(screenId);
            pushScrollingStripOsd(m_overlayService.get(), m_screenManager.get(), screenId, tiles, [this, &screenId] {
                return stripIsVerticalForScreen(screenId);
            });
            return;
        }
        // A mode toggle announces BEFORE the effect's re-announce batch
        // lands, so at OSD time the strip is still empty. Defer one beat
        // so the adoption settles and the card shows the user's actual
        // columns; a strip that is still empty then shows the sketch.
        //
        // One restartable timer per screen rather than a fresh singleShot per
        // call: repeated toggles inside the settle window used to queue a
        // card each, so the user got a burst of identical OSDs. Restarting
        // coalesces them into the last request's beat.
        //
        // Everything the immediate path gated on is re-checked on dispatch,
        // because the settle window is long enough for the user to leave the
        // mode or change the OSD style in it — and this is the only OSD entry
        // point whose decision and its effect are separated in time. That
        // includes the CALLER's own toggle, carried in @p trigger: a layout
        // switch and a desktop switch are gated by different settings, and
        // re-reading neither meant a toggle switched off inside the settle
        // window still produced its card.
        const QString timerName = scrollingSettleTimerName(screenId);
        auto* settleTimer = findChild<QTimer*>(timerName, Qt::FindDirectChildrenOnly);
        if (!settleTimer) {
            settleTimer = new QTimer(this);
            settleTimer->setObjectName(timerName);
            settleTimer->setSingleShot(true);
        } else {
            // A restart re-arms for the LATEST caller, so drop the previous
            // one's trigger along with its pending fire.
            settleTimer->disconnect(this);
        }
        connect(settleTimer, &QTimer::timeout, this, [this, screenId, trigger]() {
            if (shouldSuppressOsd(screenId) || !m_overlayService) {
                return;
            }
            if (currentModeFor(screenId) != PhosphorZones::AssignmentEntry::Scrolling) {
                return;
            }
            if ((m_settings ? m_settings->osdStyle() : OsdStyle::Preview) != OsdStyle::Preview) {
                return;
            }
            if (!isOsdTriggerEnabled(trigger, screenId)) {
                return;
            }
            showScrollingStripPreviewOsd(screenId);
        });
        settleTimer->start(kScrollingOsdAdoptSettleMs);
        return;
    }
    stopScrollingOsdSettleTimer(screenId);
    recordAnnouncedScrollingTemplate(screenId);
    showKdeTextOsd(QStringLiteral("plasmazones"), PhosphorI18n::tr("Scrolling", "tiling mode name"));
    qCInfo(lcDaemon) << "Showing scrolling-mode text OSD: screen=" << screenId;
}

void Daemon::recordAnnouncedScrollingTemplate(const QString& screenId)
{
    if (!m_layoutManager) {
        return;
    }
    const PhosphorZones::ScrollingTemplate current =
        m_layoutManager->scrollingTemplateForContext(screenId, currentDesktopForScreen(screenId), currentActivity());
    m_lastAnnouncedTemplateByScreen.insert(screenId, current.isValid() ? current.id.toString() : QString());
}

void Daemon::showScrollingStripPreviewOsd(const QString& screenId)
{
    // The settle dispatch's render point, so the ledger advances here rather
    // than at request time: the settle lambda re-checks its gates and can
    // return without rendering, and the template in force at dispatch is the
    // one this card actually shows.
    recordAnnouncedScrollingTemplate(screenId);
    // The engine's axis for this screen, for the empty-strip sketch inside.
    // Passed lazily: only the sketch arm reads it, and an absent engine falls
    // back to the horizontal sketch.
    pushScrollingStripOsd(m_overlayService.get(), m_screenManager.get(), screenId,
                          visibleStripTiles(m_scrollEngine.get(), screenId), [this, &screenId] {
                              return stripIsVerticalForScreen(screenId);
                          });
}

void Daemon::stopScrollingOsdSettleTimer(const QString& screenId)
{
    if (auto* settle = findChild<QTimer*>(scrollingSettleTimerName(screenId), Qt::FindDirectChildrenOnly)) {
        settle->stop();
    }
}

void Daemon::showScrollingTemplateOsd(const PhosphorZones::ScrollingTemplate& templ, const QString& screenId,
                                      bool locked)
{
    if (shouldSuppressOsd(screenId) || !templ.isValid()) {
        return;
    }
    const OsdStyle style = m_settings ? m_settings->osdStyle() : OsdStyle::Preview;
    if (style == OsdStyle::None) {
        return;
    }
    // Template-card twin of the ledger advance in showScrollingModeOsd: every
    // producer of a template card routes through here.
    m_lastAnnouncedTemplateByScreen.insert(screenId, templ.id.toString());
    if (style == OsdStyle::Text) {
        // The text card is a bare D-Bus call to plasmashell's OSD service, so
        // it works without an overlay service. Only the preview arm below
        // needs one, which is why the null check sits there and not above.
        showKdeTextOsd(
            QStringLiteral("plasmazones"),
            PhosphorI18n::tr("Column template — %1", "OSD caption, %1 is the template name").arg(templ.name));
        return;
    }
    if (!m_overlayService) {
        return;
    }

    // ONE projection for every surface that draws a template:
    // previewFromScrollingTemplate lays the blueprint columns (or, for a
    // vocabulary-only template, its preset widths, or, for a defaults-only
    // template, a single column) as bands along the strip axis and truncates
    // the last band at the far edge. Re-deriving the walk here is how the OSD
    // and the picker card end up disagreeing about the same template, so this
    // only reshapes the result into the QML payload.
    //
    // The axis comes from the ENGINE's resolution for THIS screen, never from
    // a second aspect-ratio derivation here: the card is a picture of what the
    // strip will look like, so on a vertical strip the bands have to stack the
    // way the engine will actually lay the columns. stripIsVerticalForScreen
    // owns the downcast that reaching a scrolling-only accessor off the base
    // engine pointer needs; an absent engine answers horizontal, the historical
    // depiction. Resolved eagerly here, unlike the strip card's lazy form: this
    // card ALWAYS draws from the axis, there is no live-tiles arm to skip it.
    const bool verticalAxis = stripIsVerticalForScreen(screenId);
    const PhosphorLayout::LayoutPreview preview = PhosphorZones::previewFromScrollingTemplate(templ, verticalAxis);
    QVariantList zones;
    zones.reserve(preview.zones.size());
    for (int i = 0; i < preview.zones.size(); ++i) {
        const QRectF& rel = preview.zones.at(i);
        QVariantMap relGeo;
        relGeo[QLatin1String("x")] = rel.x();
        relGeo[QLatin1String("y")] = rel.y();
        relGeo[QLatin1String("width")] = rel.width();
        relGeo[QLatin1String("height")] = rel.height();
        QVariantMap zoneMap;
        zoneMap[QLatin1String("zoneNumber")] = (i < preview.zoneNumbers.size()) ? preview.zoneNumbers.at(i) : (i + 1);
        zoneMap[QLatin1String("relativeGeometry")] = relGeo;
        // Namespaced synthetic id, never a bare index (see the autotile
        // preview projection above for the rationale).
        zoneMap[QLatin1String("id")] = QStringLiteral("scrolling-template:%1:%2").arg(templ.id.toString()).arg(i);
        zoneMap[QLatin1String("name")] = QString();
        zoneMap[QLatin1String("useCustomColors")] = false;
        zones.append(zoneMap);
    }
    m_overlayService->showScrollingTemplateOsd(templ.id.toString(), templ.name, zones, screenId, locked);
}

void Daemon::showLayoutOsdForAlgorithm(const QString& algorithmId, const QString& displayName, const QString& screenId)
{
    if (shouldSuppressOsd(screenId)) {
        return;
    }
    auto* algo = m_algorithmRegistry ? m_algorithmRegistry->algorithm(algorithmId) : nullptr;
    if (!algo) {
        qCWarning(lcDaemon) << "OSD: algorithm not found, algorithmId=" << algorithmId;
        return;
    }

    OsdStyle style = m_settings ? m_settings->osdStyle() : OsdStyle::Preview;

    switch (style) {
    case OsdStyle::None:
        qCInfo(lcDaemon) << "OSD disabled, skipping for algorithm=" << displayName;
        return;

    case OsdStyle::Text: {
        QString displayText = PhosphorI18n::tr("Tiling: %1").arg(displayName);
        showKdeTextOsd(QStringLiteral("plasmazones"), displayText);
        qCInfo(lcDaemon) << "Showing text OSD for algorithm=" << displayName;
    } break;

    case OsdStyle::Preview:
        if (m_overlayService) {
            int windowCount = 0;
            int masterCount = 1;
            if (m_autotileEngine) {
                // const overload for const-correctness on a read-only build.
                // BOTH stateForScreen overloads are non-creating today (the
                // lazy-allocation the non-const one once performed is gone),
                // so callers elsewhere gate on the non-const form safely.
                const auto* state = std::as_const(*m_autotileEngine).stateForScreen(screenId);
                if (state) {
                    windowCount = state->tiledWindowCount();
                    masterCount = state->masterCount();
                }
            }
            // Build the OSD QVariantList from the canonical preview. The QML
            // overlay expects `{zoneNumber, relativeGeometry:{x,y,w,h},
            // id, name, useCustomColors}` — we project the preview's zones
            // directly without going through a second preview-generation path.
            //
            // canvasSize: pass the target screen's available geometry so
            // aspect-sensitive algorithms (BSP / fibonacci / spiral) split
            // along the same axis the live tiler uses. Without this, BSP on
            // a portrait VS shows a left/right split in the OSD while the
            // tiler actually places windows top/bottom — preview lies. Use
            // available geometry (panel-excluded) so it matches the rect
            // the tiler computes against.
            QSize previewCanvas;
            if (m_screenManager) {
                const QRect avail = m_screenManager->screenAvailableGeometry(screenId);
                if (avail.isValid() && avail.width() > 0 && avail.height() > 0) {
                    previewCanvas = avail.size();
                }
            }
            const PhosphorLayout::LayoutPreview preview = PhosphorTiles::previewFromAlgorithm(
                algorithmId, algo, windowCount > 0 ? windowCount : -1, m_algorithmRegistry.get(), previewCanvas);
            QVariantList zones;
            zones.reserve(preview.zones.size());
            for (int i = 0; i < preview.zones.size(); ++i) {
                const QRectF& rel = preview.zones.at(i);
                QVariantMap relGeo;
                relGeo[QLatin1String("x")] = rel.x();
                relGeo[QLatin1String("y")] = rel.y();
                relGeo[QLatin1String("width")] = rel.width();
                relGeo[QLatin1String("height")] = rel.height();

                QVariantMap zoneMap;
                zoneMap[QLatin1String("zoneNumber")] =
                    (i < preview.zoneNumbers.size()) ? preview.zoneNumbers.at(i) : (i + 1);
                zoneMap[QLatin1String("relativeGeometry")] = relGeo;
                // Namespaced, never a bare index, for the reason the strip
                // and settings-app twins are (StripZones::zoneMapsForTiles):
                // these are render-only synthetic zones with no persisted
                // identity, and a bare "0"/"1"/"2" is indistinguishable from
                // a real zone id to any consumer that keys on zone.id.
                // CLAUDE.md: zone IDs everywhere, never indices.
                zoneMap[QLatin1String("id")] = QStringLiteral("autotile:%1:%2").arg(screenId).arg(i);
                zoneMap[QLatin1String("name")] = QString();
                zoneMap[QLatin1String("useCustomColors")] = false;
                zones.append(zoneMap);
            }
            QString layoutId = PhosphorLayout::LayoutId::makeAutotileId(algorithmId);
            m_overlayService->showLayoutOsd(layoutId, displayName, zones,
                                            static_cast<int>(PhosphorZones::LayoutCategory::Autotile), false, screenId,
                                            algo->supportsMasterCount(), algo->producesOverlappingZones(),
                                            algo->zoneNumberDisplay(), masterCount);
            qCInfo(lcDaemon) << "Preview OSD: algorithm=" << displayName << "screen=" << screenId;
        } else {
            qCWarning(lcDaemon) << "Overlay service not available for preview OSD";
        }
        break;
    }
}

void Daemon::showLayoutOsdDeferred(const QUuid& layoutId, const QString& screenId)
{
    if (shouldSuppressOsd(screenId)) {
        return;
    }
    // Defer so first-time QML compilation doesn't block the event loop.
    // Inner showLayoutOsd re-checks shouldSuppressOsd() on dispatch.
    QTimer::singleShot(0, this, [this, layoutId, screenId]() {
        PhosphorZones::Layout* l = m_layoutManager ? m_layoutManager->layoutById(layoutId) : nullptr;
        if (l) {
            showLayoutOsd(l, screenId);
        }
    });
}

void Daemon::showAlgorithmOsdDeferred(const QString& algorithmId, const QString& algorithmName, const QString& screenId)
{
    if (shouldSuppressOsd(screenId)) {
        return;
    }
    QTimer::singleShot(0, this, [this, algorithmId, algorithmName, screenId]() {
        showLayoutOsdForAlgorithm(algorithmId, algorithmName, screenId);
    });
}

void Daemon::updateLayoutFilter()
{
    updateLayoutFilterForScreen(QString());
}

void Daemon::updateLayoutFilterForScreen(const QString& focusedScreenId)
{
    if (!m_settings) {
        return;
    }

    bool autotileActive = false;
    bool manualActive = false;
    bool scrollingActive = false;

    if (m_layoutManager && m_screenManager) {
        const QString activity = currentActivity();
        // Both engine arms resolve through the ROUTER, never through a master
        // switch. The switch gates whether autotile is OFFERED and DEFAULTED
        // (setDefaultAutotileAlgorithmProvider in init_services.cpp returns an
        // empty algorithm when off, and the mode cycle skips a disabled mode);
        // it does not un-claim a screen carrying an EXPLICIT autotile
        // assignment, which updateEngineScreens still admits and the engine
        // still tiles. Reading the switch here made this filter disagree with
        // the live engine: the picker and visibleLayoutCount both resolve the
        // autotile family through resolvePerScreenLayoutInclude, so a screen
        // with autotile off but an explicit assignment drew ALGORITHM cards
        // while the controller held a manual-only list — every visible card a
        // silent dead press, and the cycle gate counting rows the controller
        // never cycles. The quick-slot sibling (shortcuts_wiring.cpp) already
        // derives its filter from currentModeFor for exactly this reason.

        // Templates needs BOTH conjuncts, the same pair
        // resolvePerScreenLayoutInclude in overlayservice.cpp requires. Not
        // literally the same test: that one reads an injected resolver and
        // treats an unwired one as Templates, while this one asks the router
        // directly and has no such escape.
        // a scrolling assignment id AND a live engine still reporting
        // Templates. The live capability alone is not enough (an engine can
        // report Templates for a screen whose assignment is not scrolling),
        // and the assignment id alone is not enough (a scrolling assignment
        // the router downgraded — master switch off, Scrolling axis
        // context-disabled — answers Placement and must keep the manual
        // list). overlayservice.cpp owns the authoritative per-screen
        // decision; this controller-level filter only has to avoid
        // contradicting it.
        const auto classify = [&](const QString& screenId) {
            const QString assignmentId =
                m_layoutManager->assignmentIdForScreen(screenId, currentDesktopForScreen(screenId), activity);
            if (PhosphorLayout::LayoutId::isAutotile(assignmentId)) {
                autotileActive = true;
            } else if (PhosphorLayout::LayoutId::isScrolling(assignmentId)
                       && layoutSupportForScreen(screenId) == LayoutSupport::Templates) {
                scrollingActive = true;
            } else {
                manualActive = true;
            }
        };

        if (!focusedScreenId.isEmpty()) {
            // Per-screen filter: only check the focused screen's mode
            classify(focusedScreenId);
        } else {
            // Global filter: union of all effective screens (includes virtual
            // screens). Each screen resolves its OWN desktop (#648 per-output
            // virtual desktops) — a desktop hoisted from the empty focused id
            // is the global current desktop and misresolves per-output
            // screens showing a different one.
            const QStringList effectiveIds = m_screenManager->effectiveScreenIds();
            for (const QString& screenId : effectiveIds) {
                classify(screenId);
            }
            // Deliberately mixed: with even one Templates screen in the set,
            // the flags below turn manual and autotile off for everyone, so
            // the global value is templates-only. That is fine because it is
            // only a seed — every list consumer (picker, cycle, drag popup)
            // re-runs updateLayoutFilterForScreen with its own screen id
            // before reading a list, and OverlayService resolves the include
            // flags per screen anyway.
        }
    } else {
        manualActive = true;
    }
    const bool includeManual = !scrollingActive && (manualActive || !autotileActive);
    const bool includeAutotile = !scrollingActive && autotileActive;

    if (m_overlayService) {
        // No templates argument by design: this only seeds OverlayService's
        // own m_includeManualLayouts/m_includeAutotileLayouts, which survive
        // solely as the fall-through default in resolvePerScreenLayoutInclude
        // (no layout manager, or an empty screen id). Every screen an arm
        // claims — including the Templates arm — overwrites all three flags
        // there, so the false/false pair a scrolling screen writes here never
        // reaches a real picker list.
        m_overlayService->setLayoutFilter(includeManual, includeAutotile);
    }
    if (m_unifiedLayoutController) {
        m_unifiedLayoutController->setLayoutFilter(includeManual, includeAutotile, scrollingActive);
    }

    qCDebug(lcDaemon) << "Layout filter updated: manual=" << includeManual << "autotile=" << includeAutotile
                      << "templates=" << scrollingActive
                      << "screen=" << (focusedScreenId.isEmpty() ? QStringLiteral("all") : focusedScreenId);
}

void Daemon::syncModeFromAssignments()
{
    if (!m_layoutManager || !m_screenManager) {
        return;
    }

    const QString activity = currentActivity();

    // Sync UnifiedLayoutController's current layout ID to match this desktop.
    // Without this, layout cycling uses the old desktop's current index.
    if (m_unifiedLayoutController) {
        QString focusedScreenId = m_windowTrackingAdaptor
            ? resolveShortcutScreenId(m_screenManager.get(), m_windowTrackingAdaptor)
            : QString();
        if (focusedScreenId.isEmpty()) {
            const QStringList effectiveIds = m_screenManager->effectiveScreenIds();
            if (!effectiveIds.isEmpty()) {
                focusedScreenId = effectiveIds.first();
            } else {
                const auto trackedScreens = m_screenManager->screens();
                if (!trackedScreens.isEmpty()) {
                    focusedScreenId = trackedScreens.first().identifier;
                }
            }
        }
        if (!focusedScreenId.isEmpty()) {
            // Per-output virtual desktops (#648): each screen resolves its own
            // desktop. Inside the guard — a lookup for an empty id is dead work.
            const int desktop = currentDesktopForScreen(focusedScreenId);
            const QString focusedAssignmentId =
                m_layoutManager->assignmentIdForScreen(focusedScreenId, desktop, activity);
            m_unifiedLayoutController->setCurrentScreenName(focusedScreenId);
            // Pass the per-desktop assignment as override — syncFromExternalState()
            // without override only reads the global active layout, which doesn't
            // reflect per-desktop autotile assignments. Routed through
            // displayIdForAssignment: the raw id on a Templates screen is the
            // bare "scrolling:" sentinel, and writing it here would undo the
            // template-UUID substitution setCurrentScreenName just performed,
            // breaking the picker highlight and cycle reference after every
            // desktop or activity switch.
            m_unifiedLayoutController->syncFromExternalState(
                m_unifiedLayoutController->displayIdForAssignment(focusedScreenId, focusedAssignmentId));

            // Update the global active layout to match this desktop's per-screen
            // assignment. Without this, PhosphorZones::LayoutRegistry::activeLayout() returns the
            // previous desktop's layout, causing zone detection, overlay, and
            // onLayoutChanged to operate on the wrong zones.
            // Block activeLayoutChanged to prevent resnap buffer corruption.
            // Desktop switches and KCM saves both route through here — neither
            // should trigger resnap via the global active layout signal. KCM saves
            // use populateResnapBufferForAllScreens() + resnapToNewLayout()
            // (per-screen, independent of global active layout) instead.
            // Scrolling short-circuits the same way autotile does: the
            // "scrolling:" sentinel is not a manual layout, so
            // layoutForScreen() would hand back the context's fallback snap
            // layout and this would install it as the GLOBAL active layout —
            // clobbering it for every screen on a desktop switch.
            if (!PhosphorLayout::LayoutId::isAutotile(focusedAssignmentId)
                && !PhosphorLayout::LayoutId::isScrolling(focusedAssignmentId)) {
                PhosphorZones::Layout* desktopLayout =
                    m_layoutManager->layoutForScreen(focusedScreenId, desktop, activity);
                if (desktopLayout && desktopLayout != m_layoutManager->activeLayout()) {
                    QSignalBlocker blocker(m_layoutManager.get());
                    m_layoutManager->setActiveLayout(desktopLayout);
                }
            }
        }
    }

    updateLayoutFilter();
    // Context switches (per-screen desktop, activity) and KCM applies all
    // funnel through here without emitting layoutApplied/autotileApplied, so
    // this is the one site that keeps an OPEN cheatsheet's mode filter and
    // layouts capability current across them. Self-guarding: the refresh
    // returns immediately while the sheet is hidden.
    refreshCheatsheetIfVisible();
}

void Daemon::showDesktopSwitchOsd(const QString& activity)
{
    // Skip during startup — the initial activity/desktop detection fires
    // before start() completes and should not produce an OSD flash.
    if (!m_running) {
        return;
    }
    if (shouldSuppressOsd()) {
        return;
    }
    // Through isOsdTriggerEnabled like every other trigger gate rather than a
    // raw setting read: this batch is screenless, so it resolves to the plain
    // toggle today, but routing it through the one gate keeps the rule layering
    // in a single place instead of leaving a second, rule-blind reader behind.
    if (!m_settings || !isOsdTriggerEnabled(OsdTrigger::DesktopSwitch) || !m_overlayService || !m_layoutManager
        || !m_screenManager) {
        return;
    }
    showOsdForAllScreens(activity);
}

void Daemon::showDesktopSwitchOsdForScreen(const QString& screenId, const QString& activity)
{
    // Per-output virtual desktops (#648): only the screen that actually switched
    // shows the OSD, not every monitor. Same gating as the all-screens variant.
    if (!m_running) {
        return;
    }
    if (!m_settings || !m_overlayService || !m_layoutManager || !m_screenManager) {
        return;
    }
    // The per-output desktop report names the PHYSICAL output, but every
    // context this card reports against is keyed by EFFECTIVE id, and a
    // subdivided monitor has no context of its own — only its vs:N children,
    // which is all effectiveScreenIds() ever yields for it. Gating on the
    // physical id therefore asked about a context nothing else uses and left
    // the children that DID switch without a card. virtualScreenIdsFor()
    // returns the plain id for an unsubdivided output, so the common case is
    // unchanged.
    // Screen-independent, so hoisted out of the per-screen walk.
    if (globalOsdSuppressed()) {
        return;
    }
    const bool toggle = m_settings->showOsdOnDesktopSwitch();
    QStringList targets;
    const QStringList effectiveIds = m_screenManager->virtualScreenIdsFor(screenId);
    for (const QString& effectiveId : effectiveIds) {
        // ONE verdict resolve per screen for both halves of the SetOsdEnabled
        // rule, the same collapse navigationOsdAllowed makes: false is the
        // suppress half, true is the force-ON half over an off toggle, and
        // value_or() is both. Asking shouldSuppressOsd and isOsdTriggerEnabled
        // in turn resolved the identical context rule twice per screen.
        if (!contextOsdRuleVerdict(effectiveId).value_or(toggle)) {
            continue;
        }
        targets.append(effectiveId);
    }
    if (targets.isEmpty()) {
        return;
    }
    showOsdForScreens(targets, activity);
}

void Daemon::showOsdForAllScreens(const QString& activity)
{
    if (!m_screenManager) {
        return;
    }
    showOsdForScreens(m_screenManager->effectiveScreenIds(), activity);
}

void Daemon::showOsdForScreens(const QStringList& screenIds, const QString& activity)
{
    if (!m_layoutManager || !m_screenManager) {
        return;
    }
    if (shouldSuppressOsd()) {
        return;
    }
    // Batch all per-screen OSD shows into one deferred call so every
    // screen's surface->show() fires in the same event loop pass and the
    // compositor renders them simultaneously.
    QTimer::singleShot(0, this, [this, screenIds, activity]() {
        if (!m_layoutManager || !m_screenManager) {
            return;
        }
        if (shouldSuppressOsd()) {
            return;
        }
        for (const QString& screenId : screenIds) {
            // No per-screen SetOsdEnabled gate here: every card this loop can
            // reach (disabled, not-assigned, scrolling-mode, algorithm, layout)
            // opens with its own shouldSuppressOsd(screenId), so a rule-off
            // screen is already skipped. Re-asking here only bought a second
            // context-rule resolve per screen. The force-ON half deliberately
            // does not apply on this all-screens path either — it would need
            // the batch's toggle gate to open per screen, and the per-screen
            // desktop-switch variant already carries it.
            //
            // Each screen reports against its OWN current virtual desktop
            // (Plasma 6.7 per-output virtual desktops, #648) — via the shared
            // helper so the null-manager fallback cannot drift from every
            // other resolution site (the open-coded form fell back to the
            // global current desktop while the helper reports 0/unknown).
            const int desktop = currentDesktopForScreen(screenId);
            // Route the disabled-context probe through the resolver so this
            // OSD pass uses the same single snapshot façade as every other
            // call site — the prior hand-stitched (modeFor → settings →
            // contextDisabledReason) cascade was the exact 3-step rebuild
            // PhosphorContext::IContextResolver was introduced to collapse.
            // `handleForPersisted` is the right axis here because the
            // caller already pinned the (desktop, activity) tuple the OSD
            // reports against, while the screen's mode stays live.
            DisabledReason why = DisabledReason::NotDisabled;
            if (m_contextResolver) {
                why = toDaemonDisabledReason(m_contextResolver->disabledReason(
                    m_contextResolver->handleForPersisted(screenId, desktop, activity)));
            }
            if (why != DisabledReason::NotDisabled) {
                showContextDisabledOsd(screenId, desktop, activity, why);
                continue;
            }
            // No active layout for this context because the default assignment is
            // suppressed (global setting or per-context rule) — show a "not
            // assigned" OSD instead of the global default layout / algorithm the
            // fallback would otherwise surface for an unassigned screen.
            if (m_layoutManager->isContextActiveLayoutSuppressed(screenId, desktop, activity)) {
                showNotAssignedOsd(screenId);
                continue;
            }
            const QString assignmentId = m_layoutManager->assignmentIdForScreen(screenId, desktop, activity);
            if (PhosphorLayout::LayoutId::isScrolling(assignmentId)) {
                // Scrolling screens announce the mode, never a fallback snap
                // layout (the "scrolling:" sentinel is not a manual layout).
                // LIVE-mode gated: a context-disabled Scrolling assignment is
                // excluded from the engine set and the router downgrades the
                // screen, so announcing "Scrolling" there would claim a mode
                // that is inert (the disabled probe above checks the
                // DOWNGRADED mode's list and misses).
                if (currentModeFor(screenId) == PhosphorZones::AssignmentEntry::Scrolling) {
                    // StripSettle::Immediate: this batch exists so every
                    // screen's card appears in the same event-loop pass, and
                    // an empty-strip scrolling screen deferring by the settle
                    // beat would land ~300ms after its neighbours.
                    showScrollingModeOsd(screenId, OsdTrigger::DesktopSwitch, StripSettle::Immediate);
                    continue;
                }
                // Downgraded. Re-probe the SCROLLING disable lists directly
                // (the resolver probe above asked the downgraded mode) so a
                // context that turned scrolling off gets the same "why"
                // card the mode toggle shows, not a misleading "no layout
                // assigned".
                const DisabledReason scrollingWhy = contextDisabledReason(
                    m_settings.get(), PhosphorZones::AssignmentEntry::Scrolling, screenId, desktop, activity);
                if (scrollingWhy != DisabledReason::NotDisabled) {
                    showContextDisabledOsd(screenId, desktop, activity, scrollingWhy);
                } else {
                    showNotAssignedOsd(screenId);
                }
                continue;
            }
            if (PhosphorLayout::LayoutId::isAutotile(assignmentId)) {
                const QString algoId = PhosphorLayout::LayoutId::extractAlgorithmId(assignmentId);
                // Bare autotile (mode set, no concrete algorithm) draws its
                // algorithm from the suppressed global default, so it won't tile
                // (see updateEngineScreens) — show "not assigned" rather than
                // announcing the default algorithm. A concrete assigned algorithm
                // always shows.
                if (algoId.isEmpty()
                    && m_layoutManager->isDefaultAssignmentSuppressedForContext(screenId, desktop, activity)) {
                    showNotAssignedOsd(screenId);
                    continue;
                }
                // Explicit per-context opt-out ("autotile:none"): nothing
                // tiles here by the user's own choice, so no card — the
                // None-pick silent posture. Not the "not assigned" card:
                // that one prompts for an assignment this context refuses,
                // and the registry lookup below would print the raw
                // reserved word as the display name.
                if (algoId == PhosphorZones::NoTilingAlgorithm) {
                    continue;
                }
                auto* algo = m_algorithmRegistry ? m_algorithmRegistry->algorithm(algoId) : nullptr;
                const QString displayName = algo ? algo->name() : algoId;
                showLayoutOsdForAlgorithm(algoId, displayName, screenId);
            } else {
                PhosphorZones::Layout* layout = m_layoutManager->layoutForScreen(screenId, desktop, activity);
                if (layout) {
                    showLayoutOsd(layout, screenId);
                }
            }
        }
    });
}

int Daemon::currentDesktop() const
{
    return m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktop() : 0;
}

int Daemon::currentDesktopForScreen(const QString& screenId) const
{
    // Per-output virtual desktops (#648): resolve THIS screen's current desktop,
    // falling back to the global current when no per-output value is on record.
    return m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktopForScreen(screenId) : 0;
}

QString Daemon::currentActivity() const
{
    return PhosphorWorkspaces::ActivityManager::currentActivityOrEmpty(m_activityManager.get());
}

bool Daemon::isCurrentContextLockedForMode(const QString& screenId, PhosphorZones::AssignmentEntry::Mode mode) const
{
    if (!m_contextResolver) {
        return false;
    }
    return m_contextResolver->isLocked(m_contextResolver->handleForMode(screenId, mode));
}

// The shortcut cheatsheet overlay lives in daemon/cheatsheet.cpp.

} // namespace PlasmaZones
