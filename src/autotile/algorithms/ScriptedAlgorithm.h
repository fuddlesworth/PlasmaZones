// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../SplitTree.h"
#include "../TilingAlgorithm.h"
#include "ScriptedAlgorithmHelpers.h"
#include <QJSValue>

#include <atomic>
#include <functional>
#include <memory>
#include <type_traits>

class QJSEngine;

namespace PlasmaZones {

struct WatchdogContext; // defined in ScriptedAlgorithm.cpp (internal implementation detail)

// Note: ScriptedAlgorithm is NOT thread-safe despite the base class const contract.
// All calls must occur on the main thread. QJSEngine is inherently single-threaded.

namespace detail {

/**
 * @brief Type-checker for QJSValue results — selects isBool() for bool, isNumber() for numeric types
 */
template<typename T>
bool jsValueHasType(const QJSValue& v)
{
    if constexpr (std::is_same_v<T, bool>)
        return v.isBool();
    else
        return v.isNumber();
}

/**
 * @brief Type-converter for QJSValue results — selects toBool() for bool, toInt() for int, toNumber() for qreal
 */
template<typename T>
T jsValueTo(const QJSValue& v)
{
    if constexpr (std::is_same_v<T, bool>)
        return v.toBool();
    else if constexpr (std::is_same_v<T, int>)
        return v.toInt();
    else
        return static_cast<T>(v.toNumber());
}

} // namespace detail

/**
 * @brief Scripted tiling algorithm backed by a user-provided JavaScript file
 *
 * Wraps a QJSEngine to execute tiling algorithms written in JavaScript.
 * Scripts must define a global `calculateZones(params)` function that returns
 * an array of `{x, y, width, height}` objects.
 *
 * Script metadata is parsed from leading comment lines using `// @key value`
 * syntax. Supported metadata keys:
 * - @name, @description
 * - @supportsMasterCount, @supportsSplitRatio, @producesOverlappingZones (bool)
 * - @centerLayout (bool)
 * - @zoneNumberDisplay (string: "all", "last", "first", "firstAndLast", "none")
 * - @defaultSplitRatio (qreal)
 * - @defaultMaxWindows, @minimumWindows, @masterZoneIndex (int)
 *
 * Scripts may also export optional JS functions (masterZoneIndex,
 * supportsMasterCount, supportsSplitRatio) for dynamic overrides.
 *
 * Script execution is guarded by a 100ms timeout to prevent runaway loops.
 *
 * @note Registration is handled externally by ScriptedAlgorithmLoader,
 *       not by self-registration macros.
 */
class PLASMAZONES_EXPORT ScriptedAlgorithm : public TilingAlgorithm
{
    Q_OBJECT

public:
    /**
     * @brief Construct a ScriptedAlgorithm from a JavaScript file
     * @param filePath Absolute path to the .js script file
     * @param parent Parent QObject
     */
    explicit ScriptedAlgorithm(const QString& filePath, QObject* parent = nullptr);
    ~ScriptedAlgorithm() override;

    /**
     * @brief Whether the script loaded successfully and has a calculateZones function
     */
    bool isValid() const;

    /**
     * @brief Absolute path to the source script file
     */
    QString filePath() const;

    /**
     * @brief Identifier derived from the filename without extension
     *
     * For example, "my-layout.js" yields scriptId "my-layout".
     */
    QString scriptId() const;

    /**
     * @brief Optional built-in algorithm ID from @builtinId metadata
     *
     * When non-empty, the loader uses this ID instead of "script:filename"
     * for algorithm registration.
     */
    QString builtinId() const;

    /**
     * @brief Mark whether this script was loaded from a user directory
     * @param isUser true if from ~/.local/share/plasmazones/algorithms/
     */
    void setUserScript(bool isUser);

    // TilingAlgorithm interface
    // Note: name(), description(), and the JS-override accessors below lack noexcept
    // because they can theoretically call into QJSEngine (which may throw or fail).
    // Only metadata-only accessors (supportsMemory, centerLayout, etc.) are noexcept.
    QString name() const override;
    QString description() const override;
    QVector<QRect> calculateZones(const TilingParams& params) const override;
    int masterZoneIndex() const override;
    bool supportsMasterCount() const override;
    bool supportsSplitRatio() const override;
    qreal defaultSplitRatio() const override;
    int minimumWindows() const override;
    int defaultMaxWindows() const override;
    bool producesOverlappingZones() const override;
    bool supportsMinSizes() const noexcept override;
    bool supportsMemory() const noexcept override;
    QString zoneNumberDisplay() const noexcept override;
    bool centerLayout() const override;
    bool isScripted() const noexcept override;
    bool isUserScript() const noexcept override;
    void prepareTilingState(TilingState* state) const override;

    // Lifecycle hooks (v2)
    bool supportsLifecycleHooks() const noexcept override;
    void onWindowAdded(TilingState* state, int windowIndex) override;
    void onWindowRemoved(TilingState* state, int windowIndex) override;

    // Custom parameters (v2)
    bool supportsCustomParams() const noexcept override;
    QVariantList customParamDefList() const override;
    /**
     * @brief Get the custom parameter definitions declared by this script
     */
    const QVector<ScriptedHelpers::CustomParamDef>& customParamDefs() const;

private:
    /**
     * @brief Load and validate a JavaScript file
     * @param filePath Absolute path to the .js script file
     * @return true if the script loaded successfully
     */
    bool loadScript(const QString& filePath);

    /**
     * @brief Convert a SplitNode tree to a QJSValue object for script consumption
     * @param node Root of the subtree to convert (may be nullptr)
     * @param depth Current recursion depth (guarded by MaxTreeConversionDepth)
     * @return Read-only JS object representing the tree
     */
    QJSValue splitNodeToJSValue(const SplitNode* node, int depth = 0) const;

    /// Recursive helper that receives a cached Object.freeze function
    QJSValue splitNodeToJSValueImpl(const SplitNode* node, const QJSValue& freezeFn, int depth, int& nodeCount) const;

    /**
     * @brief Resolve a JS override value: return cached if loaded, else call JS, else metadata fallback
     */
    template<typename T>
    T resolveJsOverride(const QJSValue& jsFn, T cachedValue, T metadataFallback) const;

    /**
     * @brief Like resolveJsOverride, but clamps the result to [minVal, maxVal]
     */
    template<typename T>
    T resolveJsOverrideClamped(const QJSValue& jsFn, T cachedValue, T metadataFallback, T minVal, T maxVal) const;

    /// Build a JS state object from TilingState for lifecycle hook calls
    QJSValue buildJsState(const TilingState* state) const;

    /// Build a JS array of {appId, focused} objects
    QJSValue buildJsWindowArray(const QVector<WindowInfo>& infos, int cap) const;

    /// Arms watchdog, calls fn(), disarms, checks for timeout. Returns error on timeout.
    QJSValue guardedCall(const std::function<QJSValue()>& fn) const;

    /// Unified with AutotileDefaults::MaxRuntimeTreeDepth to prevent silent truncation
    static constexpr int MaxTreeConversionDepth = AutotileDefaults::MaxRuntimeTreeDepth;

    // Owned via QObject parent; mutable because calculateZones() is const but JS evaluation mutates engine state
    mutable QJSEngine* m_engine = nullptr;
    std::shared_ptr<WatchdogContext> m_watchdog; ///< Consolidated watchdog shared state
    mutable QJSValue m_calculateZonesFn;
    QString m_filePath;
    QString m_scriptId;
    bool m_valid = false;
    bool m_isUserScript = false;
    mutable std::atomic<bool> m_evaluating{false}; ///< Re-entrancy guard for calculateZones
    mutable bool m_lastCallTimedOut = false; ///< Set by guardedCall on timeout, checked by callers
    mutable uint32_t m_gcCounter = 0; ///< GC throttle counter for calculateZones
    static constexpr uint32_t GcInterval = 8; ///< Collect garbage every N calculateZones calls

    // Consolidated parsed metadata (from // @key value comments)
    ScriptedHelpers::ScriptMetadata m_metadata;

    // Optional JS function overrides (checked at call time)
    mutable QJSValue m_jsMasterZoneIndex;
    mutable QJSValue m_jsSupportsMasterCount;
    mutable QJSValue m_jsSupportsSplitRatio;
    mutable QJSValue m_jsDefaultSplitRatio;
    mutable QJSValue m_jsMinimumWindows;
    mutable QJSValue m_jsDefaultMaxWindows;
    mutable QJSValue m_jsProducesOverlappingZones;
    mutable QJSValue m_jsCenterLayout;

    // Optional lifecycle hook JS functions
    mutable QJSValue m_jsOnWindowAdded;
    mutable QJSValue m_jsOnWindowRemoved;
    bool m_hasLifecycleHooks = false; ///< True if any lifecycle hook is defined

    // Cached JS virtual method overrides (loaded once at script load time)
    int m_cachedMinimumWindows = 1;
    int m_cachedDefaultMaxWindows = 6;
    int m_cachedMasterZoneIndex = -1;
    qreal m_cachedDefaultSplitRatio = ConfigDefaults::autotileSplitRatio();
    bool m_cachedProducesOverlappingZones = false;
    bool m_cachedSupportsMasterCount = false;
    bool m_cachedSupportsSplitRatio = false;
    bool m_cachedCenterLayout = false;
    bool m_cachedValuesLoaded = false;
};

} // namespace PlasmaZones
