// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Split from scriptedalgorithm.cpp to keep that translation unit under the
// project-wide 800-line cap. Holds the non-hot-path virtual accessors +
// prepareTilingState + v2 lifecycle-hook + custom-parameter surface of
// ScriptedAlgorithm; function ordering matches the original file.

#include <PhosphorTiles/AutotileConstants.h>
#include <PhosphorTiles/ScriptedAlgorithm.h>
#include <PhosphorTiles/SplitTree.h>
#include <PhosphorTiles/TilingState.h>

#include <QJSEngine>
#include <QJSValue>

#include <algorithm>
#include <memory>

namespace PhosphorTiles {

using namespace AutotileDefaults;

// --- Virtual method overrides ---
// Each checks for a JS function override first, then falls back to parsed metadata,
// then to the base class default.

QString ScriptedAlgorithm::name() const
{
    if (!m_metadata.name.isEmpty()) {
        return m_metadata.name;
    }
    // Fall back to basename (strip "script:" prefix) with first letter capitalized
    if (!m_scriptId.isEmpty()) {
        QString fallback = m_scriptId;
        if (fallback.startsWith(QLatin1String("script:"))) {
            fallback = fallback.mid(7);
        }
        if (!fallback.isEmpty()) {
            fallback[0] = fallback[0].toUpper();
        }
        return fallback;
    }
    // Library-side defaults — translation responsibility lives with the
    // application that consumes the library (it can substitute its own
    // localised label via Q_PROPERTY / display-name override).
    return QStringLiteral("Scripted");
}

QString ScriptedAlgorithm::description() const
{
    if (!m_metadata.description.isEmpty()) {
        return m_metadata.description;
    }
    return QStringLiteral("User-provided scripted tiling algorithm");
}

int ScriptedAlgorithm::masterZoneIndex() const
{
    // Unified three-tier resolution via template helper
    return resolveJsOverride<int>(m_jsMasterZoneIndex, m_cachedMasterZoneIndex, m_metadata.masterZoneIndex);
}

bool ScriptedAlgorithm::supportsMasterCount() const
{
    return resolveJsOverride<bool>(m_jsSupportsMasterCount, m_cachedSupportsMasterCount,
                                   m_metadata.supportsMasterCount);
}

bool ScriptedAlgorithm::supportsSplitRatio() const
{
    return resolveJsOverride<bool>(m_jsSupportsSplitRatio, m_cachedSupportsSplitRatio, m_metadata.supportsSplitRatio);
}

qreal ScriptedAlgorithm::defaultSplitRatio() const
{
    // Use resolveJsOverrideClamped to unify clamped resolution
    const qreal fallback =
        (m_metadata.defaultSplitRatio > 0.0) ? m_metadata.defaultSplitRatio : TilingAlgorithm::defaultSplitRatio();
    return resolveJsOverrideClamped<qreal>(m_jsDefaultSplitRatio, m_cachedDefaultSplitRatio, fallback, MinSplitRatio,
                                           MaxSplitRatio);
}

int ScriptedAlgorithm::minimumWindows() const
{
    // Use resolveJsOverrideClamped to unify clamped resolution
    const int fallback =
        (m_metadata.minimumWindows > 0) ? m_metadata.minimumWindows : TilingAlgorithm::minimumWindows();
    return resolveJsOverrideClamped<int>(m_jsMinimumWindows, m_cachedMinimumWindows, fallback, MinMetadataWindows,
                                         MaxMetadataWindows);
}

int ScriptedAlgorithm::defaultMaxWindows() const
{
    // Use resolveJsOverrideClamped to unify clamped resolution
    const int fallback =
        (m_metadata.defaultMaxWindows > 0) ? m_metadata.defaultMaxWindows : TilingAlgorithm::defaultMaxWindows();
    return resolveJsOverrideClamped<int>(m_jsDefaultMaxWindows, m_cachedDefaultMaxWindows, fallback, MinMetadataWindows,
                                         MaxMetadataWindows);
}

bool ScriptedAlgorithm::producesOverlappingZones() const
{
    return resolveJsOverride<bool>(m_jsProducesOverlappingZones, m_cachedProducesOverlappingZones,
                                   m_metadata.producesOverlappingZones);
}

bool ScriptedAlgorithm::supportsMinSizes() const noexcept
{
    return m_metadata.supportsMinSizes;
}

bool ScriptedAlgorithm::supportsMemory() const noexcept
{
    return m_metadata.supportsMemory;
}

QString ScriptedAlgorithm::zoneNumberDisplay() const noexcept
{
    if (m_metadata.zoneNumberDisplay != PhosphorLayout::ZoneNumberDisplay::RendererDecides) {
        return PhosphorLayout::zoneNumberDisplayToString(m_metadata.zoneNumberDisplay);
    }
    return TilingAlgorithm::zoneNumberDisplay();
}

bool ScriptedAlgorithm::centerLayout() const
{
    return resolveJsOverride<bool>(m_jsCenterLayout, m_cachedCenterLayout, m_metadata.centerLayout);
}

bool ScriptedAlgorithm::isScripted() const noexcept
{
    return true;
}

bool ScriptedAlgorithm::isUserScript() const noexcept
{
    return m_isUserScript;
}

QString ScriptedAlgorithm::builtinId() const
{
    return m_metadata.builtinId;
}

void ScriptedAlgorithm::prepareTilingState(TilingState* state) const
{
    if (!m_metadata.supportsMemory) {
        return; // Only memory-aware scripts need tree preparation
    }

    if (!state || state->splitTree()) {
        return; // Already has a tree (or no state)
    }

    // Only reset the split ratio to our default (0.5) if it still holds a
    // value from a different algorithm (e.g., MasterStack's 0.6).
    const qreal currentRatio = state->splitRatio();
    const qreal defRatio = defaultSplitRatio();
    // Reset split ratio to our default when it still holds a value from a
    // different algorithm (e.g. MasterStack's 0.6). Small differences within
    // the hysteresis band are kept so user fine-tuning is not discarded.
    if (currentRatio > defRatio + AutotileDefaults::SplitRatioHysteresis
        || currentRatio < defRatio - AutotileDefaults::SplitRatioHysteresis) {
        state->setSplitRatio(defRatio);
    }

    const QStringList tiledWindows = state->tiledWindows();
    if (tiledWindows.size() <= 1) {
        return; // No tree needed for 0-1 windows
    }

    // Cap window count to prevent unbounded tree growth (MaxZones = 256)
    const int maxWindows = qMin(static_cast<int>(tiledWindows.size()), AutotileDefaults::MaxZones);

    const qreal ratio = state->splitRatio();
    auto newTree = std::make_unique<SplitTree>();
    for (int i = 0; i < maxWindows; ++i) {
        newTree->insertAtEnd(tiledWindows[i], ratio);
    }
    state->setSplitTree(std::move(newTree));
}

// ═══════════════════════════════════════════════════════════════════════════════
// Lifecycle Hooks (v2)
// ═══════════════════════════════════════════════════════════════════════════════

bool ScriptedAlgorithm::supportsLifecycleHooks() const noexcept
{
    return m_hasLifecycleHooks;
}

QJSValue ScriptedAlgorithm::buildJsWindowArray(const QVector<WindowInfo>& infos, int cap) const
{
    QJSValue jsWindows = m_engine->newArray(static_cast<uint>(cap));
    for (int i = 0; i < cap; ++i) {
        QJSValue entry = m_engine->newObject();
        entry.setProperty(QStringLiteral("appId"), infos[i].appId);
        entry.setProperty(QStringLiteral("focused"), infos[i].focused);
        jsWindows.setProperty(static_cast<quint32>(i), entry);
    }
    return jsWindows;
}

QJSValue ScriptedAlgorithm::buildJsState(const TilingState* state) const
{
    QJSValue jsState = m_engine->newObject();
    jsState.setProperty(QStringLiteral("windowCount"), state->tiledWindowCount());
    jsState.setProperty(QStringLiteral("masterCount"), state->masterCount());
    jsState.setProperty(QStringLiteral("splitRatio"), std::clamp(state->splitRatio(), MinSplitRatio, MaxSplitRatio));

    const int winCount = state->tiledWindowCount();
    int focusedIdx = -1;
    // Resolver injected by AutotileEngine::setWindowRegistry → every scripted
    // algorithm sees the CURRENT app class for each tiled window, not a
    // first-seen parse. User JS that switches on window.appId (e.g. "pin
    // firefox as master") now sees "media.emby.client.beta" after Emby's
    // mid-session rename instead of the stale "emby-beta".
    const QVector<WindowInfo> infos = buildWindowInfos(state, winCount, appIdResolver(), focusedIdx);

    jsState.setProperty(QStringLiteral("windows"), buildJsWindowArray(infos, infos.size()));
    jsState.setProperty(QStringLiteral("focusedIndex"), focusedIdx);

    return jsState;
}

void ScriptedAlgorithm::onWindowAdded(TilingState* state, int windowIndex)
{
    if (!m_jsOnWindowAdded.isCallable() || !state) {
        return;
    }
    QJSValue jsState = buildJsState(state);
    guardedCall([this, &jsState, windowIndex]() {
        return m_jsOnWindowAdded.call({jsState, QJSValue(windowIndex)});
    });
}

void ScriptedAlgorithm::onWindowRemoved(TilingState* state, int windowIndex)
{
    if (!m_jsOnWindowRemoved.isCallable() || !state) {
        return;
    }
    QJSValue jsState = buildJsState(state);
    // Expose countAfterRemoval so hook authors don't need to subtract 1 from windowCount
    jsState.setProperty(QStringLiteral("countAfterRemoval"), qMax(0, state->tiledWindowCount() - 1));
    guardedCall([this, &jsState, windowIndex]() {
        return m_jsOnWindowRemoved.call({jsState, QJSValue(windowIndex)});
    });
}

bool ScriptedAlgorithm::supportsCustomParams() const noexcept
{
    return !m_metadata.customParams.isEmpty();
}

QVariantList ScriptedAlgorithm::customParamDefList() const
{
    QVariantList result;
    for (const auto& def : m_metadata.customParams) {
        result.append(def.toVariantMap());
    }
    return result;
}

bool ScriptedAlgorithm::hasCustomParam(const QString& name) const
{
    return std::any_of(m_metadata.customParams.cbegin(), m_metadata.customParams.cend(),
                       [&name](const ScriptedHelpers::CustomParamDef& def) {
                           return def.name == name;
                       });
}

const QVector<ScriptedHelpers::CustomParamDef>& ScriptedAlgorithm::customParamDefs() const
{
    return m_metadata.customParams;
}

} // namespace PhosphorTiles
