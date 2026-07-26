// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorScrollEngine/ScrollEngine.h>

#include <PhosphorEngine/WindowRegistry.h>
#include <PhosphorEngine/IWindowTrackingService.h>
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorScrollEngine/IScrollSettings.h>
#include <PhosphorScreens/Manager.h>

#include "scrollenginelogging.h"

#include <QMetaObject>

namespace PhosphorScrollEngine {

ScrollEngine::ScrollEngine(PhosphorEngine::IWindowTrackingService* windowTracker,
                           PhosphorScreens::ScreenManager* screenManager, QObject* parent)
    : PhosphorEngine::PlacementEngineBase(parent)
    , m_windowTracker(windowTracker)
    , m_screenManager(screenManager)
{
}

ScrollEngine::~ScrollEngine() = default;

void ScrollEngine::setWindowRegistry(QObject* registry)
{
    m_windowRegistry = qobject_cast<PhosphorEngine::WindowRegistry*>(registry);
}

QString ScrollEngine::canonicalizeForLookup(const QString& rawWindowId) const
{
    if (rawWindowId.isEmpty()) {
        return rawWindowId;
    }
    if (m_windowRegistry) {
        return m_windowRegistry->canonicalizeForLookup(rawWindowId);
    }
    return rawWindowId;
}

// ── Screen ownership ────────────────────────────────────────────────────────

bool ScrollEngine::isActiveOnScreen(const QString& screenId) const
{
    return m_scrollingScreens.contains(screenId);
}

bool ScrollEngine::isEnabled() const noexcept
{
    return !m_scrollingScreens.isEmpty();
}

void ScrollEngine::setActiveScreens(const QSet<QString>& screens)
{
    if (screens == m_scrollingScreens) {
        // Identical-set re-emit contract (mirrors setAutotileScreens): a
        // desktop/activity switch that lands on the same set still wakes the
        // compositor effect's catch-scan; an empty identical set has nothing
        // to catch.
        if (!screens.isEmpty()) {
            QStringList sortedSame(screens.cbegin(), screens.cend());
            sortedSame.sort();
            Q_EMIT scrollingScreensChanged(sortedSame, true);
            for (const QString& screenId : screens) {
                scheduleRetileForScreen(screenId);
            }
        }
        return;
    }

    const bool wasEnabled = isEnabled();
    const QSet<QString> removed = m_scrollingScreens - screens;
    const QSet<QString> added = screens - m_scrollingScreens;
    m_scrollingScreens = screens;

    QStringList releasedWindows;
    QSet<QString> releasedScreens;
    for (const QString& screenId : removed) {
        // Tear down every context state for the leaving screen; the windows
        // are released to whichever engine now owns the screen.
        m_states.removeStatesIf(
            [&screenId](const PhosphorEngine::PlacementStateKey& key, ScrollState*) {
                return key.screenId == screenId;
            },
            [this, &releasedWindows](const PhosphorEngine::PlacementStateKey&, ScrollState* state) {
                releaseScreenState(state, releasedWindows);
            });
        releasedScreens.insert(screenId);
        m_context.removeScreen(screenId);
    }
    if (!releasedWindows.isEmpty()) {
        m_states.removeWindowsIf([&releasedWindows](const QString& windowId, const PhosphorEngine::PlacementStateKey&) {
            return releasedWindows.contains(windowId);
        });
        Q_EMIT windowsReleased(releasedWindows, releasedScreens);
    }

    for (const QString& screenId : added) {
        scheduleRetileForScreen(screenId);
    }

    // Sorted: QSet iteration order is unspecified across runs, and a wire
    // consumer comparing successive payloads must not see phantom changes.
    QStringList sorted(screens.cbegin(), screens.cend());
    sorted.sort();
    Q_EMIT scrollingScreensChanged(sorted, false);
    if (wasEnabled != isEnabled()) {
        Q_EMIT enabledChanged(isEnabled());
    }
}

void ScrollEngine::setActiveScreenHint(const QString& screenId)
{
    if (!screenId.isEmpty() && m_scrollingScreens.contains(screenId)) {
        m_activeScreen = screenId;
    }
}

void ScrollEngine::releaseScreenState(ScrollState* state, QStringList& releasedWindows)
{
    const QString screenId = state->screenId();
    dropWindowBookkeeping(state);
    releasedWindows.append(state->managedWindows());
    // Per-screen bookkeeping dies with the state: a stale seed must not
    // replay on re-entry, and the tab-strip overlay must be told to clear —
    // no relayout will ever run for a departed screen to do it.
    m_pendingInitialOrder.remove(screenId);
    clearTabStripsForScreen(screenId);
    state->deleteLater();
}

void ScrollEngine::clearTabStripsForScreen(const QString& screenId)
{
    // Latch-guarded single clear: only screens that actually showed a strip
    // get the "[]" broadcast, so plain relayouts never spam the overlay.
    m_lastTabStripPayload.remove(screenId);
    if (m_screensWithTabStrips.remove(screenId)) {
        Q_EMIT tabStripsChanged(screenId, QStringLiteral("[]"));
    }
}

// ── State resolution ────────────────────────────────────────────────────────

ScrollState* ScrollEngine::stateForKey(const PhosphorEngine::PlacementStateKey& key, bool createIfMissing)
{
    if (!createIfMissing) {
        return m_states.stateForKey(key);
    }
    return m_states.forKey(key, [this, &key]() -> ScrollState* {
        if (!m_scrollingScreens.contains(key.screenId)) {
            return nullptr;
        }
        return new ScrollState(key.screenId, this);
    });
}

ScrollState* ScrollEngine::stateForWindow(const QString& canonicalId, PhosphorEngine::PlacementStateKey* outKey) const
{
    return m_states.forWindow(canonicalId, outKey);
}

PhosphorEngine::IPlacementState* ScrollEngine::stateForScreen(const QString& screenId)
{
    return stateForKey(currentKeyForScreen(screenId), false);
}

const PhosphorEngine::IPlacementState* ScrollEngine::stateForScreen(const QString& screenId) const
{
    return m_states.stateForKey(m_context.currentKeyForScreen(screenId));
}

QString ScrollEngine::resolveOperationScreen(const QString& screenId) const
{
    if (!screenId.isEmpty() && m_scrollingScreens.contains(screenId)) {
        return screenId;
    }
    if (!m_activeScreen.isEmpty() && m_scrollingScreens.contains(m_activeScreen)) {
        return m_activeScreen;
    }
    if (m_scrollingScreens.isEmpty()) {
        return {};
    }
    // QSet iteration order is unspecified; pick the lexicographic minimum so
    // repeated shortcut presses with no active screen land deterministically.
    QString fallback = *m_scrollingScreens.cbegin();
    for (const QString& candidate : m_scrollingScreens) {
        if (candidate < fallback) {
            fallback = candidate;
        }
    }
    return fallback;
}

// ── Tracking predicates ─────────────────────────────────────────────────────

bool ScrollEngine::isWindowTracked(const QString& windowId) const
{
    return m_states.hasWindow(canonicalizeForLookup(windowId));
}

bool ScrollEngine::isWindowTiled(const QString& windowId) const
{
    const QString id = canonicalizeForLookup(windowId);
    const ScrollState* state = stateForWindow(id);
    return state && state->strip().containsWindow(id);
}

bool ScrollEngine::isWindowManaged(const QString& windowId) const
{
    return isWindowTiled(windowId);
}

QString ScrollEngine::screenForTrackedWindow(const QString& windowId) const
{
    return m_states.keyForWindow(canonicalizeForLookup(windowId)).screenId;
}

QRect ScrollEngine::lastManagedRect(const QString& rawWindowId) const
{
    return m_lastAppliedRect.value(canonicalizeForLookup(rawWindowId));
}

bool ScrollEngine::isWindowFloatingInScroll(const QString& windowId) const
{
    const QString id = canonicalizeForLookup(windowId);
    const ScrollState* state = stateForWindow(id);
    return state && state->isFloating(id);
}

QStringList ScrollEngine::allFloatingWindows() const
{
    QStringList all;
    const auto& states = m_states.states();
    for (auto it = states.cbegin(); it != states.cend(); ++it) {
        all += it.value()->floatingWindows();
    }
    return all;
}

bool ScrollEngine::isModeSpecificFloated(const QString& windowId) const
{
    return m_scrollFloatedWindows.contains(canonicalizeForLookup(windowId));
}

void ScrollEngine::markModeSpecificFloated(const QString& windowId)
{
    m_scrollFloatedWindows.insert(canonicalizeForLookup(windowId));
}

void ScrollEngine::clearModeSpecificFloatMarker(const QString& windowId)
{
    m_scrollFloatedWindows.remove(canonicalizeForLookup(windowId));
}

// ── Ordering (mode-transition seams) ────────────────────────────────────────

QStringList ScrollEngine::managedWindowOrder(const QString& screenId) const
{
    const ScrollState* state = m_states.stateForKey(m_context.currentKeyForScreen(screenId));
    return state ? state->strip().windowsInOrder() : QStringList();
}

void ScrollEngine::setInitialWindowOrder(const QString& screenId, const QStringList& windowIds)
{
    if (windowIds.isEmpty()) {
        m_pendingInitialOrder.remove(screenId);
    } else {
        m_pendingInitialOrder.insert(screenId, windowIds);
    }
}

int ScrollEngine::pruneStaleWindows(const QSet<QString>& aliveWindowIds)
{
    int pruned = PlacementEngineBase::pruneStaleWindows(aliveWindowIds);
    QStringList dead;
    const auto& windowKeys = m_states.windowKeys();
    for (auto it = windowKeys.cbegin(); it != windowKeys.cend(); ++it) {
        if (!aliveWindowIds.contains(it.key())) {
            dead.append(it.key());
        }
    }
    QSet<QString> affectedScreens;
    // Per-screen params cache: layoutParamsForScreen costs a ScreenManager
    // query plus a context-gap provider invocation, and a batch prune of N
    // dead windows on one screen needs it once, not N times.
    QHash<QString, ScrollLayoutParams> paramsByScreen;
    for (const QString& windowId : dead) {
        PhosphorEngine::PlacementStateKey key;
        ScrollState* state = stateForWindow(windowId, &key);
        if (state) {
            auto paramsIt = paramsByScreen.find(key.screenId);
            if (paramsIt == paramsByScreen.end()) {
                paramsIt = paramsByScreen.insert(key.screenId, layoutParamsForScreen(key.screenId));
            }
            state->strip().removeWindow(windowId, *paramsIt);
            state->removeFloating(windowId);
            affectedScreens.insert(key.screenId);
        }
        m_states.removeWindow(windowId);
        m_lastAppliedRect.remove(windowId);
        m_floatRestore.remove(windowId);
        m_scrollFloatedWindows.remove(windowId);
        ++pruned;
    }
    for (const QString& screenId : affectedScreens) {
        scheduleRetileForScreen(screenId);
    }
    return pruned;
}

// ── Desktop / activity context ──────────────────────────────────────────────

void ScrollEngine::setCurrentDesktop(int desktop)
{
    m_context.setCurrentDesktop(desktop);
}

void ScrollEngine::setCurrentDesktopForScreen(const QString& screenId, int desktop)
{
    m_context.setCurrentDesktopForScreen(screenId, desktop);
}

void ScrollEngine::clearCurrentDesktopForScreen(const QString& screenId)
{
    m_context.clearCurrentDesktopForScreen(screenId);
}

void ScrollEngine::setCurrentActivity(const QString& activity)
{
    m_context.setCurrentActivity(activity);
}

QSet<int> ScrollEngine::desktopsWithActiveState() const
{
    QSet<int> desktops;
    const auto& states = m_states.states();
    for (auto it = states.cbegin(); it != states.cend(); ++it) {
        desktops.insert(it.key().desktop);
    }
    return desktops;
}

void ScrollEngine::dropWindowBookkeeping(const ScrollState* state)
{
    // Shared sweep for every state-destruction path: the per-window side
    // maps must die with the state or they grow unbounded and
    // lastManagedRect keeps answering for windows whose context is gone —
    // the float-back poison-guard input (mirrors the autotile prunes'
    // in-callback drops).
    const QStringList windows = state->managedWindows();
    for (const QString& windowId : windows) {
        m_lastAppliedRect.remove(windowId);
        m_floatRestore.remove(windowId);
        m_scrollFloatedWindows.remove(windowId);
    }
}

void ScrollEngine::pruneStatesForDesktop(int removedDesktop)
{
    m_states.removeStatesIf(
        [removedDesktop](const PhosphorEngine::PlacementStateKey& key, ScrollState*) {
            return key.desktop == removedDesktop;
        },
        [this](const PhosphorEngine::PlacementStateKey&, ScrollState* state) {
            dropWindowBookkeeping(state);
            state->deleteLater();
        });
    m_states.removeWindowsIf([removedDesktop](const QString&, const PhosphorEngine::PlacementStateKey& key) {
        return key.desktop == removedDesktop;
    });
    m_context.pruneDesktop(removedDesktop);
}

void ScrollEngine::pruneStatesForActivities(const QStringList& validActivities)
{
    const auto stale = [&validActivities](const QString& activity) {
        return !activity.isEmpty() && !validActivities.contains(activity);
    };
    m_states.removeStatesIf(
        [&stale](const PhosphorEngine::PlacementStateKey& key, ScrollState*) {
            return stale(key.activity);
        },
        [this](const PhosphorEngine::PlacementStateKey&, ScrollState* state) {
            dropWindowBookkeeping(state);
            state->deleteLater();
        });
    m_states.removeWindowsIf([&stale](const QString&, const PhosphorEngine::PlacementStateKey& key) {
        return stale(key.activity);
    });
}

void ScrollEngine::pruneStatesForRemovedScreen(const QString& physicalScreenId)
{
    const auto matches = [&physicalScreenId](const QString& screenId) {
        // Match the physical id and every virtual sub-screen of it.
        return screenId == physicalScreenId
            || (screenId.size() > physicalScreenId.size() && screenId.startsWith(physicalScreenId)
                && screenId.at(physicalScreenId.size()) == QLatin1Char('#'));
    };
    m_states.removeStatesIf(
        [&matches](const PhosphorEngine::PlacementStateKey& key, ScrollState*) {
            return matches(key.screenId);
        },
        [this](const PhosphorEngine::PlacementStateKey&, ScrollState* state) {
            // Removed screen: per-screen bookkeeping goes with the state —
            // stale seeds must not replay if the connector id ever returns,
            // and the tab-strip latch/overrides must not linger.
            dropWindowBookkeeping(state);
            m_pendingInitialOrder.remove(state->screenId());
            // Through clearTabStripsForScreen so a still-listening overlay
            // gets the "[]" broadcast (mirrors releaseScreenState).
            clearTabStripsForScreen(state->screenId());
            m_perScreenOverrides.remove(state->screenId());
            state->deleteLater();
        });
    m_states.removeWindowsIf([&matches](const QString&, const PhosphorEngine::PlacementStateKey& key) {
        return matches(key.screenId);
    });
    m_context.removeScreensIf(matches);
}

// ── Persistence + settings ──────────────────────────────────────────────────

void ScrollEngine::saveState()
{
    if (m_persistSaveFn) {
        m_persistSaveFn();
    }
}

void ScrollEngine::loadState()
{
    if (m_persistLoadFn) {
        m_persistLoadFn();
    }
}

void ScrollEngine::refreshConfigFromSettings()
{
    auto* settings = qobject_cast<PhosphorEngine::IScrollSettings*>(engineSettings());
    if (!settings) {
        return;
    }
    const auto parsePresets = [](const QStringList& raw, const QList<qreal>& fallback) {
        QList<qreal> out;
        for (const QString& entry : raw) {
            bool ok = false;
            const qreal v = entry.trimmed().toDouble(&ok);
            if (ok && v > 0.0 && v <= 1.0) {
                out.append(v);
            }
        }
        return out.isEmpty() ? fallback : out;
    };
    const QList<qreal> defaults{1.0 / 3.0, 0.5, 2.0 / 3.0};
    m_presetColumnWidths = parsePresets(settings->scrollingPresetColumnWidths(), defaults);
    m_presetWindowHeights = parsePresets(settings->scrollingPresetWindowHeights(), defaults);

    const int center = settings->scrollingCenterFocusedColumn();
    m_centerFocusedColumn =
        (center >= 0 && center <= 2) ? static_cast<CenterFocusedColumn>(center) : CenterFocusedColumn::Never;
    m_alwaysCenterSingleColumn = settings->scrollingAlwaysCenterSingleColumn();

    const auto widthKind = static_cast<DefaultWidthKind>(settings->scrollingDefaultColumnWidthKind());
    const qreal widthValue = settings->scrollingDefaultColumnWidthValue();
    m_defaultWidthClientDecides = (widthKind == DefaultWidthKind::ClientDecides);
    if (widthKind == DefaultWidthKind::Fixed) {
        m_defaultColumnWidth = ColumnWidth::makeFixed(qMax(1, qRound(widthValue)));
    } else {
        // KEEP IN SYNC: the 0.05 proportion floor mirrors
        // ConfigDefaults::scrollingDefaultColumnWidthValueMin and the
        // rules-side kMinColumnWidthRatio (both app-side; this LGPL lib
        // cannot include them).
        m_defaultColumnWidth = ColumnWidth::makeProportion(qBound<qreal>(0.05, widthValue, 1.0));
    }
    const int display = settings->scrollingDefaultColumnDisplay();
    m_defaultColumnDisplay = (display == 1) ? ColumnDisplay::Tabbed : ColumnDisplay::Normal;

    // Re-resolve every active strip against the new parameters.
    for (const QString& screenId : std::as_const(m_scrollingScreens)) {
        scheduleRetileForScreen(screenId);
    }
}

// ── Per-context rule overrides ──────────────────────────────────────────────

void ScrollEngine::applyPerScreenConfig(const QString& screenId, const QVariantMap& overrides)
{
    if (m_perScreenOverrides.value(screenId) == overrides) {
        return;
    }
    m_perScreenOverrides.insert(screenId, overrides);
    scheduleRetileForScreen(screenId);
}

void ScrollEngine::clearPerScreenConfig(const QString& screenId)
{
    if (m_perScreenOverrides.remove(screenId) > 0) {
        scheduleRetileForScreen(screenId);
    }
}

CenterFocusedColumn ScrollEngine::effectiveCenterFocusedColumn(const QString& screenId) const
{
    const QVariantMap overrides = m_perScreenOverrides.value(screenId);
    const auto it = overrides.constFind(ScrollPerScreenKeys::centerFocusedColumn());
    if (it != overrides.constEnd()) {
        const int mode = it->toInt();
        if (mode >= 0 && mode <= 2) {
            return static_cast<CenterFocusedColumn>(mode);
        }
    }
    return m_centerFocusedColumn;
}

ColumnWidth ScrollEngine::effectiveDefaultColumnWidth(const QString& screenId) const
{
    const QVariantMap overrides = m_perScreenOverrides.value(screenId);
    const auto it = overrides.constFind(ScrollPerScreenKeys::defaultColumnWidth());
    if (it != overrides.constEnd()) {
        const qreal fraction = it->toDouble();
        if (fraction >= 0.05 && fraction <= 1.0) {
            return ColumnWidth::makeProportion(fraction);
        }
    }
    return m_defaultColumnWidth;
}

ColumnDisplay ScrollEngine::effectiveDefaultColumnDisplay(const QString& screenId) const
{
    const QVariantMap overrides = m_perScreenOverrides.value(screenId);
    const auto it = overrides.constFind(ScrollPerScreenKeys::defaultColumnDisplay());
    if (it != overrides.constEnd()) {
        return it->toInt() == 1 ? ColumnDisplay::Tabbed : ColumnDisplay::Normal;
    }
    return m_defaultColumnDisplay;
}

void ScrollEngine::retile(const QString& screenId)
{
    if (screenId.isEmpty()) {
        for (const QString& sid : std::as_const(m_scrollingScreens)) {
            applyLayout(sid);
        }
        return;
    }
    applyLayout(screenId);
}

void ScrollEngine::scheduleRetileForScreen(const QString& screenId)
{
    if (screenId.isEmpty() || !m_scrollingScreens.contains(screenId)) {
        return;
    }
    if (m_pendingRetiles.contains(screenId)) {
        return;
    }
    m_pendingRetiles.insert(screenId);
    QMetaObject::invokeMethod(
        this,
        [this, screenId]() {
            if (m_pendingRetiles.remove(screenId) && m_scrollingScreens.contains(screenId)) {
                applyLayout(screenId);
            }
        },
        Qt::QueuedConnection);
}

} // namespace PhosphorScrollEngine
