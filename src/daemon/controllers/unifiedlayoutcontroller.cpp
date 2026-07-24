// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "unifiedlayoutcontroller.h"

#include "config/settings.h"
#include "core/platform/logging.h"
#include "core/types/constants.h"
#include "core/utils/utils.h"

#include <PhosphorEngine/PlacementEngineBase.h>
#include <PhosphorLayoutApi/ILayoutSource.h>
// Full definition required: the header stores a QPointer<ScreenManager> (only
// forward-declared there), and QPointer construction/deref needs the complete
// QObject-derived type. Previously satisfied transitively via the Unity build;
// included directly so it doesn't depend on jumbo-grouping order.
#include <PhosphorScreens/Manager.h>
#include <PhosphorTiles/ITileAlgorithmRegistry.h>
#include <PhosphorZones/LayoutRegistry.h>

namespace PlasmaZones {

UnifiedLayoutController::UnifiedLayoutController(PhosphorZones::LayoutRegistry* layoutManager, Settings* settings,
                                                 PhosphorScreens::ScreenManager* screenManager,
                                                 PhosphorTiles::ITileAlgorithmRegistry* algorithmRegistry,
                                                 PhosphorEngine::PlacementEngineBase* autotileEngine, QObject* parent)
    : QObject(parent)
    , m_layoutManager(layoutManager)
    , m_settings(settings)
    , m_screenManager(screenManager)
    , m_algorithmRegistry(algorithmRegistry)
    , m_autotileEngine(autotileEngine)
{
    if (m_layoutManager) {
        connect(m_layoutManager, &PhosphorZones::LayoutRegistry::layoutsChanged, this, [this]() {
            m_cacheValid = false;
        });

        connect(m_layoutManager, &PhosphorZones::LayoutRegistry::activeLayoutChanged, this,
                [this](PhosphorZones::Layout* layout) {
                    // Only while there is no per-screen context. m_currentLayoutId
                    // is a PER-SCREEN value everywhere else that writes it
                    // (setCurrentScreenName, applyEntry, and syncFromExternalState
                    // with an override), so mirroring the GLOBAL active layout into
                    // it once a screen is known lets any global writer — a
                    // drop-to-layout, a D-Bus assignment, a layout deletion
                    // re-pointing active at the default — overwrite the focused
                    // screen's assignment id and misaim the next cycle().
                    //
                    // Kept for the pre-screen window: the constructor calls
                    // syncFromExternalState() before any screen name exists, and
                    // that is the state this mirror is genuinely for.
                    //
                    // Clears on null so the property cannot latch an observer on an
                    // id whose layout no longer exists.
                    if (m_currentScreenName.isEmpty()) {
                        setCurrentLayoutId(layout ? layout->id().toString() : QString());
                    }
                });
    }

    if (m_settings) {
        connect(m_settings, &Settings::settingsChanged, this, [this]() {
            m_cacheValid = false;
        });
    }

    // Autotile entries enter the unified list via the tile-algorithm registry,
    // not LayoutManager. Cache invalidation comes through the long-lived
    // autotile source wired in setAutotileLayoutSource — it self-bridges the
    // registry's contentsChanged into its own, so subscribing there covers
    // algorithm-set mutations and preview-params changes without
    // double-firing (the PR #343 senior review flagged this double-subscribe
    // as redundant).
    //
    // Seed the fallback subscription to the registry now: composition roots
    // that never call setAutotileLayoutSource still get cache invalidation,
    // and callers that do inject a bundle source will swap this connection
    // to the source inside setAutotileLayoutSource().
    if (m_algorithmRegistry) {
        m_autotileSourceConnection =
            connect(m_algorithmRegistry, &PhosphorTiles::ITileAlgorithmRegistry::contentsChanged, this, [this]() {
                m_cacheValid = false;
            });
    }

    syncFromExternalState();
}

UnifiedLayoutController::~UnifiedLayoutController() = default;

void UnifiedLayoutController::setAutotileLayoutSource(PhosphorLayout::ILayoutSource* source)
{
    if (m_autotileLayoutSource == source) {
        return;
    }
    // Disconnect any prior subscription before swapping. Idempotent on a
    // default-constructed QMetaObject::Connection (returns false cleanly).
    QObject::disconnect(m_autotileSourceConnection);
    m_autotileLayoutSource = source;
    if (m_autotileLayoutSource) {
        // Subscribe to the source's contentsChanged so cache invalidation
        // routes through the single notifier the source already bridges
        // from the registry. Previously we subscribed to the registry
        // directly AND the source self-bridged registry → source
        // contentsChanged, firing our invalidation twice per mutation.
        m_autotileSourceConnection =
            connect(m_autotileLayoutSource, &PhosphorLayout::ILayoutSource::contentsChanged, this, [this]() {
                m_cacheValid = false;
            });
    } else if (m_algorithmRegistry) {
        // Fallback path: no long-lived source, subscribe to the registry
        // directly so hot-loaded user scripts and preview-params changes
        // still invalidate the cache.
        m_autotileSourceConnection =
            connect(m_algorithmRegistry, &PhosphorTiles::ITileAlgorithmRegistry::contentsChanged, this, [this]() {
                m_cacheValid = false;
            });
    }
    m_cacheValid = false;
}

QVector<PhosphorLayout::LayoutPreview> UnifiedLayoutController::layouts() const
{
    if (!m_cacheValid) {
        // Use filtered overload to respect visibility settings (hiddenFromSelector, allowed lists)
        // and mode-based filtering (manual-only vs autotile-only).
        // Registry comes directly from our injected pointer rather than the
        // Law-of-Demeter reach-through engine->algorithmRegistry().
        // Per-output virtual desktops (#648): resolve the current screen's own
        // desktop, not the global active one.
        // buildUnifiedLayoutList returns an empty list outright when the layout
        // manager is null, so the fallback arm's value is never observable.
        const int desktop = m_layoutManager ? m_layoutManager->currentVirtualDesktopForScreen(m_currentScreenName) : 0;
        m_cachedLayouts = PhosphorZones::LayoutUtils::buildUnifiedLayoutList(
            m_layoutManager, m_algorithmRegistry, m_currentScreenName, desktop, m_currentActivity,
            m_includeManualLayouts, m_includeAutotileLayouts,
            Utils::screenAspectRatio(m_screenManager, m_currentScreenName),
            m_settings && m_settings->filterLayoutsByAspectRatio(),
            PhosphorZones::LayoutUtils::buildCustomOrder(m_settings, m_includeManualLayouts, m_includeAutotileLayouts),
            m_autotileLayoutSource);

        m_cachedScreenDesktop = desktop;
        m_cacheValid = true;
    }
    return m_cachedLayouts;
}

bool UnifiedLayoutController::applyLayoutById(const QString& layoutId)
{
    const auto list = layouts();
    const PhosphorLayout::LayoutPreview* preview = PhosphorZones::LayoutUtils::findLayout(list, layoutId);
    if (!preview) {
        qCWarning(lcDaemon) << "applyLayoutById: layout not found:" << layoutId;
        return false;
    }
    return applyEntry(*preview);
}

bool UnifiedLayoutController::applyLayoutByIndex(int index)
{
    const auto list = layouts();
    if (list.isEmpty()) {
        qCWarning(lcDaemon) << "applyLayoutByIndex: no layouts available";
        return false;
    }
    if (index < 0 || index >= list.size()) {
        qCWarning(lcDaemon) << "applyLayoutByIndex: invalid index" << index << "(valid: 0 to" << (list.size() - 1)
                            << ")";
        return false;
    }
    return applyEntry(list[index]);
}

void UnifiedLayoutController::cycleNext()
{
    cycle(true);
}

void UnifiedLayoutController::cyclePrevious()
{
    cycle(false);
}

void UnifiedLayoutController::cycle(bool forward)
{
    const auto list = layouts();
    if (list.isEmpty()) {
        qCWarning(lcDaemon) << "cycle: layout list is empty (manual=" << m_includeManualLayouts
                            << "autotile=" << m_includeAutotileLayouts << ")";
        return;
    }

    int currentIndex = findCurrentIndex();
    if (currentIndex < 0) {
        currentIndex = 0;
    }

    // Calculate next index with wraparound
    int nextIndex;
    if (forward) {
        nextIndex = (currentIndex + 1) % list.size();
    } else {
        nextIndex = (currentIndex - 1 + list.size()) % list.size();
    }

    qCInfo(lcDaemon) << "cycle: listSize=" << list.size() << "currentIdx=" << currentIndex << "nextIdx=" << nextIndex
                     << "from=" << (currentIndex < list.size() ? list[currentIndex].displayName : QStringLiteral("?"))
                     << "to=" << (nextIndex < list.size() ? list[nextIndex].displayName : QStringLiteral("?"));

    applyLayoutByIndex(nextIndex);
}

void UnifiedLayoutController::syncFromExternalState(std::optional<QString> overrideId)
{
    if (overrideId.has_value()) {
        setCurrentLayoutId(*overrideId);
    } else if (m_layoutManager && m_layoutManager->activeLayout()) {
        setCurrentLayoutId(m_layoutManager->activeLayout()->id().toString());
    } else {
        setCurrentLayoutId(QString());
    }
}

void UnifiedLayoutController::setCurrentScreenName(const QString& screenId)
{
    if (m_currentScreenName != screenId) {
        m_currentScreenName = screenId;
        m_cacheValid = false;

        // Sync the current layout ID to what is actually ASSIGNED to this
        // screen, not the global active layout, which may belong to another.
        //
        // assignmentIdForScreen, not layoutForScreen. layoutForScreen falls
        // back to defaultLayout() on a cascade miss and consults only the snap
        // provider, so it answers a different question: it never returns null
        // for an unassigned screen (it hands back the default layout's UUID as
        // if it were the assignment), and on a screen assigned an
        // "autotile:<algo>" id it returns a manual Layout* instead. Either way
        // m_currentLayoutId ends up holding something that is not this screen's
        // assignment, findCurrentIndex() then misses under the autotile filter,
        // and the next cycle() jumps to the wrong entry.
        //
        // assignmentIdForScreen is mode-aware and returns the stored id verbatim,
        // including the "autotile:<algo>" form, which is the property that
        // matters: findCurrentIndex() compares it against the preview ids, and an
        // autotile-assigned screen has to yield an autotile id. It is NOT
        // "empty on a miss": it falls through to the level-1 global defaults,
        // consulting the snap provider and then the autotile provider, so an
        // unassigned screen with a configured default still gets that default's
        // id. (layoutForScreen consults only the snap provider, which is why it
        // is the wrong call here.) Empty means both providers missed.
        //
        // Written unconditionally so an empty screen name, or a null registry,
        // clears rather than leaving the previous screen's id latched.
        // currentVirtualDesktopForScreen already falls back to the registry's
        // global desktop for an empty screen id, so no branch is needed for it.
        setCurrentLayoutId(
            m_layoutManager && !screenId.isEmpty()
                ? m_layoutManager->assignmentIdForScreen(
                      screenId, m_layoutManager->currentVirtualDesktopForScreen(screenId), m_currentActivity)
                : QString());
    }
}

void UnifiedLayoutController::setCurrentVirtualDesktop(int desktop)
{
    // Invalidate on a change of EITHER the global desktop (this member) or the
    // current screen's own desktop, which is what layouts() actually keys the
    // cache on. Only the global currentDesktopChanged handler calls this, and
    // the per-output screenDesktopChanged path reaches the controller through
    // setters that early-return when nothing they own changed, so guarding on
    // the global value alone would leave a stale list on a per-output switch.
    const int screenDesktop =
        m_layoutManager ? m_layoutManager->currentVirtualDesktopForScreen(m_currentScreenName) : 0;
    if (m_currentVirtualDesktop != desktop || m_cachedScreenDesktop != screenDesktop) {
        m_currentVirtualDesktop = desktop;
        m_cachedScreenDesktop = screenDesktop;
        m_cacheValid = false;
    }
}

void UnifiedLayoutController::setCurrentActivity(const QString& activity)
{
    if (m_currentActivity != activity) {
        m_currentActivity = activity;
        m_cacheValid = false;
    }
}

void UnifiedLayoutController::setLayoutFilter(bool includeManual, bool includeAutotile)
{
    if (m_includeManualLayouts == includeManual && m_includeAutotileLayouts == includeAutotile) {
        return;
    }
    m_includeManualLayouts = includeManual;
    m_includeAutotileLayouts = includeAutotile;
    m_cacheValid = false;
}

bool UnifiedLayoutController::applyEntry(const PhosphorLayout::LayoutPreview& preview)
{
    // Handle autotile entries: assign autotile ID to the current screen.
    // The daemon's layoutAssigned handler calls updateAutotileScreens() which
    // derives per-screen autotile state from assignments automatically.
    if (preview.isAutotile()) {
        if (m_autotileEngine && m_layoutManager) {
            QString algoId = PhosphorLayout::LayoutId::extractAlgorithmId(preview.id);
            // Assign layout FIRST so that layoutAssigned → updateAutotileScreens()
            // updates per-screen overrides before setAlgorithm's deferred retile.
            // Without this ordering, setAlgorithm's retile uses stale per-screen
            // overrides (old algorithm), producing wrong zone geometries.
            if (!m_currentScreenName.isEmpty()) {
                // Write to the current context (screen, desktop, activity).
                // Most specific context wins — an activity-keyed entry takes
                // priority over a desktop-only entry in the cascade, and a
                // different activity falls through to the broader entry.
                m_layoutManager->assignLayoutById(m_currentScreenName,
                                                  m_layoutManager->currentVirtualDesktopForScreen(m_currentScreenName),
                                                  m_currentActivity, preview.id);
            }
            m_autotileEngine->setAlgorithm(algoId);
            setCurrentLayoutId(preview.id);
            qCInfo(lcDaemon) << "Applied autotile algorithm=" << preview.displayName;
            Q_EMIT autotileApplied(preview.displayName, 0);
            return true;
        }
        qCWarning(lcDaemon) << "applyEntry: cannot apply autotile entry" << preview.id << "- autotile engine is"
                            << (m_autotileEngine ? "present" : "null") << "and layout manager is"
                            << (m_layoutManager ? "present" : "null");
        return false;
    }

    // Manual layout: assign the UUID to the current screen.
    // If the previous assignment was autotile, it gets replaced and
    // updateAutotileScreens() will remove the screen from autotile set.
    auto uuidOpt = Utils::parseUuid(preview.id);
    if (uuidOpt && m_layoutManager) {
        PhosphorZones::Layout* layout = m_layoutManager->layoutById(*uuidOpt);
        if (layout) {
            if (!m_currentScreenName.isEmpty()) {
                // Write to the current context (screen, desktop, activity).
                // Only the per-context assignment is stored — the global
                // activeLayout pointer is NOT updated so that other screens
                // and contexts keep their existing fallback layout.
                m_layoutManager->assignLayout(m_currentScreenName,
                                              m_layoutManager->currentVirtualDesktopForScreen(m_currentScreenName),
                                              m_currentActivity, layout);
            }
            setCurrentLayoutId(preview.id);
            qCInfo(lcDaemon) << "Applied unified layout=" << preview.displayName << "screen=" << m_currentScreenName;
            Q_EMIT layoutApplied(layout);
            return true;
        }
        qCWarning(lcDaemon) << "applyEntry: no layout found for id" << preview.id;
        return false;
    }
    qCWarning(lcDaemon) << "applyEntry: cannot apply manual entry" << preview.id << "-"
                        << (uuidOpt ? "layout manager is null" : "id is not a valid UUID");
    return false;
}

void UnifiedLayoutController::setCurrentLayoutId(const QString& layoutId)
{
    if (m_currentLayoutId == layoutId) {
        return;
    }

    m_currentLayoutId = layoutId;
    Q_EMIT currentLayoutIdChanged();
}

int UnifiedLayoutController::findCurrentIndex() const
{
    const auto list = layouts();
    return PhosphorZones::LayoutUtils::findLayoutIndex(list, m_currentLayoutId);
}

} // namespace PlasmaZones
