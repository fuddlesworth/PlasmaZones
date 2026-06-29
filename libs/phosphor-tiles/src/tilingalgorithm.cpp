// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorTiles/TilingAlgorithm.h>
#include <PhosphorTiles/AutotileConstants.h>
#include <PhosphorTiles/TilingState.h>
#include <algorithm>

namespace PhosphorTiles {

using namespace AutotileDefaults;

QVector<WindowInfo> buildWindowInfos(const TilingState* state, int windowCount,
                                     const std::function<QString(const QString&)>& appIdResolver, int& focusedIndex)
{
    focusedIndex = -1;
    if (!state) {
        return {};
    }
    QVector<WindowInfo> infos;
    const QStringList windows = state->tiledWindows();
    const QString focusedWin = state->focusedWindow();
    infos.reserve(windowCount);
    for (int i = 0; i < windowCount && i < windows.size(); ++i) {
        WindowInfo info;
        // Live class lookup via the caller-supplied resolver so a LuauTileAlgorithm
        // user script sees the current appId, not a stale first-seen parse. Callers
        // that don't care about the class (pure geometry algorithms) can pass
        // a no-op lambda; the resolver is cheap either way.
        info.appId = appIdResolver ? appIdResolver(windows[i]) : QString();
        info.focused = (windows[i] == focusedWin);
        info.windowId = windows[i];
        if (info.focused) {
            focusedIndex = i;
        }
        infos.append(info);
    }
    return infos;
}

TilingAlgorithm::TilingAlgorithm(QObject* parent)
    : QObject(parent)
{
}

int TilingAlgorithm::masterZoneIndex() const
{
    return -1; // Default: no master concept (subclasses override if they have one)
}

bool TilingAlgorithm::supportsMasterCount() const
{
    return false;
}

bool TilingAlgorithm::supportsSplitRatio() const
{
    return false;
}

qreal TilingAlgorithm::defaultSplitRatio() const
{
    return AutotileDefaults::DefaultSplitRatio;
}

int TilingAlgorithm::minimumWindows() const
{
    return 1;
}

int TilingAlgorithm::defaultMaxWindows() const
{
    return AutotileDefaults::DefaultMaxWindows;
}

bool TilingAlgorithm::producesOverlappingZones() const
{
    return false;
}

QString TilingAlgorithm::zoneNumberDisplay() const noexcept
{
    return QStringLiteral("all");
}

bool TilingAlgorithm::centerLayout() const
{
    return false;
}

bool TilingAlgorithm::supportsSingleWindow() const noexcept
{
    return false;
}

bool TilingAlgorithm::isScripted() const noexcept
{
    return false;
}

bool TilingAlgorithm::isUserScript() const noexcept
{
    return false;
}

bool TilingAlgorithm::supportsMinSizes() const noexcept
{
    return true;
}

bool TilingAlgorithm::supportsMemory() const noexcept
{
    return false;
}

void TilingAlgorithm::prepareTilingState(TilingState* /*state*/) const
{
    // Default no-op. Memory-based algorithms override to ensure their SplitTree exists.
}

bool TilingAlgorithm::supportsLifecycleHooks() const noexcept
{
    return false;
}

void TilingAlgorithm::onWindowAdded(TilingState* /*state*/, int /*windowIndex*/)
{
    // Default no-op. Algorithms with lifecycle hooks override.
}

void TilingAlgorithm::onWindowRemoved(TilingState* /*state*/, int /*windowIndex*/)
{
    // Default no-op. Algorithms with lifecycle hooks override.
}

bool TilingAlgorithm::supportsResizeHook() const noexcept
{
    return false;
}

void TilingAlgorithm::onWindowResized(TilingState* /*state*/, const ResizeEvent& /*resize*/)
{
    // Default no-op. Non-memory algorithms that react to resize override.
}

bool TilingAlgorithm::supportsScriptState() const noexcept
{
    return false;
}

bool TilingAlgorithm::supportsCustomParams() const noexcept
{
    return false;
}

QVariantList TilingAlgorithm::customParamDefList() const
{
    return {};
}

bool TilingAlgorithm::hasCustomParam(const QString& /*name*/) const
{
    return false;
}

QRect TilingAlgorithm::innerRect(const QRect& screenGeometry, int outerGap)
{
    outerGap = std::max(0, outerGap);
    const int w = std::max(1, screenGeometry.width() - 2 * outerGap);
    const int h = std::max(1, screenGeometry.height() - 2 * outerGap);
    // When outerGap exceeds half the screen dimension, center the result
    // to avoid placing the rect off-screen
    const int x = screenGeometry.left() + (screenGeometry.width() - w) / 2;
    const int y = screenGeometry.top() + (screenGeometry.height() - h) / 2;
    return QRect(x, y, w, h);
}

QRect TilingAlgorithm::innerRect(const QRect& screenGeometry, const EdgeGaps& gaps)
{
    const int l = std::max(0, gaps.left);
    const int r = std::max(0, gaps.right);
    const int t = std::max(0, gaps.top);
    const int b = std::max(0, gaps.bottom);
    const int w = std::max(1, screenGeometry.width() - l - r);
    const int h = std::max(1, screenGeometry.height() - t - b);
    // When gaps exceed screen dimension, center the result to avoid placing
    // the rect off-screen (same behavior as the uniform overload)
    const int x = (l + r >= screenGeometry.width()) ? screenGeometry.left() + (screenGeometry.width() - w) / 2
                                                    : screenGeometry.left() + l;
    const int y = (t + b >= screenGeometry.height()) ? screenGeometry.top() + (screenGeometry.height() - h) / 2
                                                     : screenGeometry.top() + t;
    return QRect(x, y, w, h);
}

} // namespace PhosphorTiles