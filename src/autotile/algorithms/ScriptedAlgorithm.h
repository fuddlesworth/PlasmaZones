// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../TilingAlgorithm.h"
#include <QJSValue>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

class QJSEngine;

namespace PlasmaZones {

struct SplitNode;

// Note: ScriptedAlgorithm is NOT thread-safe despite the base class const contract.
// All calls must occur on the main thread. QJSEngine is inherently single-threaded.

/**
 * @brief Consolidated watchdog thread state shared between the main thread and detached watchdog threads
 *
 * All members are atomic or mutex-protected so that the destructor can safely
 * signal "engine is gone" to any in-flight watchdog thread.
 */
struct WatchdogContext
{
    std::atomic<bool> alive{true}; ///< Set to false in destructor to prevent use-after-free
    std::atomic<bool> active{false}; ///< C1: True while a watchdog thread is sleeping; prevents unbounded spawning
    std::mutex mutex; ///< Guards engine pointer access between watchdog and destructor
    std::atomic<uint64_t> generation{0}; ///< Generation counter to prevent stale watchdog interrupts
    QJSEngine* engine = nullptr; ///< Stable engine pointer shared with watchdog threads
};

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
     * @brief Mark whether this script was loaded from a user directory
     * @param isUser true if from ~/.local/share/plasmazones/algorithms/
     */
    void setUserScript(bool isUser);

    // TilingAlgorithm interface
    QString name() const override;
    QString description() const override;
    QVector<QRect> calculateZones(const TilingParams& params) const override;
    int masterZoneIndex() const noexcept override;
    bool supportsMasterCount() const noexcept override;
    bool supportsSplitRatio() const noexcept override;
    qreal defaultSplitRatio() const noexcept override;
    int minimumWindows() const noexcept override;
    int defaultMaxWindows() const noexcept override;
    bool producesOverlappingZones() const noexcept override;
    QString zoneNumberDisplay() const noexcept override;
    bool isScripted() const noexcept override;
    bool isUserScript() const noexcept override;

private:
    /**
     * @brief Load and validate a JavaScript file
     * @param filePath Absolute path to the .js script file
     * @return true if the script loaded successfully
     */
    bool loadScript(const QString& filePath);

    /**
     * @brief Parse // @key value metadata comments from script source
     * @param source Script source code
     */
    void parseMetadata(const QString& source);

    /**
     * @brief Convert a JS array of {x, y, width, height} objects to QRects
     * @param result JS value (should be an array)
     * @return Vector of validated QRects
     */
    QVector<QRect> jsArrayToRects(const QJSValue& result) const;

    /**
     * @brief Convert a SplitNode tree to a QJSValue object for script consumption
     * @param node Root of the subtree to convert (may be nullptr)
     * @param depth Current recursion depth (guarded by MaxTreeConversionDepth)
     * @return Read-only JS object representing the tree
     */
    QJSValue splitNodeToJSValue(const SplitNode* node, int depth = 0) const;

    static constexpr int MaxTreeConversionDepth = 30;

    mutable QJSEngine* m_engine = nullptr;
    std::shared_ptr<WatchdogContext> m_watchdog; ///< D1: Consolidated watchdog shared state
    mutable QJSValue m_calculateZonesFn;
    QString m_filePath;
    QString m_scriptId;
    bool m_valid = false;
    bool m_isUserScript = false;

    // Parsed metadata (from // @key value comments)
    QString m_name;
    QString m_description;
    bool m_supportsMasterCount = false;
    bool m_supportsSplitRatio = false;
    bool m_producesOverlappingZones = false;
    QString m_zoneNumberDisplay;
    qreal m_defaultSplitRatio = 0.0; // 0 = use base class default
    int m_defaultMaxWindows = 0; // 0 = use base class default
    int m_minimumWindows = 0; // 0 = use base class default
    int m_masterZoneIndex = -1;

    // Optional JS function overrides (checked at call time)
    mutable QJSValue m_jsMasterZoneIndex;
    mutable QJSValue m_jsSupportsMasterCount;
    mutable QJSValue m_jsSupportsSplitRatio;
    mutable QJSValue m_jsDefaultSplitRatio;
    mutable QJSValue m_jsMinimumWindows;
    mutable QJSValue m_jsDefaultMaxWindows;
    mutable QJSValue m_jsProducesOverlappingZones;

    // H5: Cached JS virtual method overrides (loaded once at script load time)
    int m_cachedMinimumWindows = -1;
    int m_cachedDefaultMaxWindows = -1;
    int m_cachedMasterZoneIndex = -1;
    qreal m_cachedDefaultSplitRatio = 0.0;
    bool m_cachedProducesOverlappingZones = false;
    bool m_cachedSupportsMasterCount = false;
    bool m_cachedSupportsSplitRatio = false;
    bool m_cachedValuesLoaded = false;
};

} // namespace PlasmaZones
