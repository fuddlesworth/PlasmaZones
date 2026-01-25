# PlasmaZones Autotiling Design Document

**Date:** 2026-01-24
**Author:** Architecture Agent
**Status:** Design Phase
**Version:** 1.0

---

## Executive Summary

This document specifies the design for adding autotiling capabilities to PlasmaZones, filling the gap left by archived Bismuth/Krohnkite projects in the KDE Plasma 6 ecosystem. The design enables automatic window tiling while preserving PlasmaZones' core zone-based features, giving users the best of both worlds.

### Design Goals

1. **Coexistence**: Autotiling works alongside manual zone placement
2. **Algorithms**: Support BSP, Master-Stack, Monocle, Columns, Fibonacci, and more
3. **Optional**: Enable autotiling per-layout or globally
4. **Keyboard-centric**: Full keyboard controls matching i3/Bismuth workflows
5. **Smooth integration**: Leverage existing PlasmaZones architecture

---

## Table of Contents

1. [Architecture Overview](#architecture-overview)
2. [Layout Modes](#layout-modes)
3. [Autotile Algorithms](#autotile-algorithms)
4. [Component Design](#component-design)
5. [Data Structures](#data-structures)
6. [Keyboard Controls](#keyboard-controls)
7. [Settings Design](#settings-design)
8. [KWin Effect Integration](#kwin-effect-integration)
9. [D-Bus API Extensions](#d-bus-api-extensions)
10. [Migration Path for Bismuth Users](#migration-path-for-bismuth-users)
11. [Implementation Phases](#implementation-phases)

---

## Architecture Overview

### High-Level Architecture

```
+------------------------------------------------------------------+
|                        PlasmaZones Daemon                         |
+------------------------------------------------------------------+
|                                                                   |
|  +---------------------+     +-----------------------------+      |
|  |   LayoutManager     |<--->|   AutotileEngine            |      |
|  |   (existing)        |     |   (NEW)                     |      |
|  +---------------------+     +-----------------------------+      |
|            |                            |                         |
|            v                            v                         |
|  +---------------------+     +-----------------------------+      |
|  |   Layout            |     |   TilingAlgorithm (abstract)|      |
|  |   - zones[]         |     |   - BSPAlgorithm            |      |
|  |   - tilingMode      |     |   - MasterStackAlgorithm    |      |
|  |   - autotileConfig  |     |   - MonocleAlgorithm        |      |
|  +---------------------+     |   - ColumnsAlgorithm        |      |
|                              |   - FibonacciAlgorithm      |      |
|                              |   - CustomAlgorithm         |      |
|                              +-----------------------------+      |
|                                         |                         |
|  +---------------------+                |                         |
|  |  WindowTracker      |<---------------+                         |
|  |  (existing)         |                                          |
|  |  - windowToZone     |     +-----------------------------+      |
|  |  - zoneToWindows    |<--->|   TilingState               |      |
|  +---------------------+     |   - windowOrder[]           |      |
|                              |   - masterCount             |      |
|                              |   - splitRatio              |      |
|                              +-----------------------------+      |
+------------------------------------------------------------------+
            |                              |
            v                              v
+------------------------+    +----------------------------+
|   KWin Effect          |    |   D-Bus Interfaces         |
|   - Window events      |    |   - TilingAdaptor (NEW)    |
|   - Keyboard shortcuts |    |   - Navigation signals     |
+------------------------+    +----------------------------+
```

### Core Principles

1. **Separation of Concerns**: Autotiling engine is a separate component, not embedded in Layout
2. **Strategy Pattern**: Algorithms are interchangeable via abstract interface
3. **Event-Driven**: Reacts to window open/close/focus events
4. **State Preservation**: Tiling state persists across sessions
5. **Graceful Degradation**: Falls back to manual mode if autotiling fails

---

## Layout Modes

### TilingMode Enum

```cpp
namespace PlasmaZones {

/**
 * @brief Tiling mode for a layout
 */
enum class TilingMode {
    Manual = 0,      ///< Traditional zone-based (current behavior)
    Autotile = 1,    ///< Automatic tiling with algorithm
    Hybrid = 2       ///< Manual zones with auto-fill
};

} // namespace PlasmaZones
```

### Mode Descriptions

| Mode | Description | Use Case |
|------|-------------|----------|
| **Manual** | User drags windows to zones | Current PlasmaZones behavior |
| **Autotile** | Algorithm arranges all windows | i3/Bismuth-style workflow |
| **Hybrid** | Manual primary zones, auto-fill rest | Best of both worlds |

### Hybrid Mode Details

In Hybrid mode:
- User defines "anchor zones" manually (e.g., master area)
- Algorithm fills remaining screen space with additional windows
- Windows dragged to anchor zones stay put; others auto-arrange

---

## Autotile Algorithms

### Algorithm Interface

```cpp
// src/autotile/TilingAlgorithm.h

namespace PlasmaZones {

class Layout;
class TilingState;

/**
 * @brief Abstract base class for tiling algorithms
 *
 * Each algorithm generates zone geometries based on:
 * - Number of windows to tile
 * - Screen geometry
 * - Algorithm-specific parameters (master ratio, gaps, etc.)
 */
class PLASMAZONES_EXPORT TilingAlgorithm : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(QString description READ description CONSTANT)
    Q_PROPERTY(QString icon READ icon CONSTANT)

public:
    explicit TilingAlgorithm(QObject *parent = nullptr);
    virtual ~TilingAlgorithm() = default;

    // Metadata
    virtual QString name() const = 0;
    virtual QString description() const = 0;
    virtual QString icon() const = 0;

    /**
     * @brief Calculate zone geometries for N windows
     * @param windowCount Number of windows to tile
     * @param screenGeometry Available screen area
     * @param state Current tiling state (master count, split ratio, etc.)
     * @return Vector of zone geometries (relative coordinates 0.0-1.0)
     */
    virtual QVector<QRectF> calculateZones(
        int windowCount,
        const QRectF &screenGeometry,
        const TilingState &state) const = 0;

    /**
     * @brief Get the index of the "master" zone (if applicable)
     * @return Master zone index, or -1 if no master concept
     */
    virtual int masterZoneIndex() const { return 0; }

    /**
     * @brief Check if algorithm supports variable master count
     */
    virtual bool supportsMasterCount() const { return false; }

    /**
     * @brief Check if algorithm supports split ratio adjustment
     */
    virtual bool supportsSplitRatio() const { return false; }

    /**
     * @brief Get default split ratio for this algorithm
     */
    virtual qreal defaultSplitRatio() const { return 0.5; }

    /**
     * @brief Get minimum number of windows for meaningful tiling
     */
    virtual int minimumWindows() const { return 1; }

Q_SIGNALS:
    void zonesChanged();
};

} // namespace PlasmaZones
```

### Algorithm Implementations

#### 1. BSP (Binary Space Partitioning)

```cpp
// src/autotile/algorithms/BSPAlgorithm.h

class BSPAlgorithm : public TilingAlgorithm
{
    Q_OBJECT

public:
    QString name() const override { return QStringLiteral("BSP"); }
    QString description() const override {
        return tr("Binary space partitioning - recursive split layout");
    }
    QString icon() const override { return QStringLiteral("view-grid-symbolic"); }

    QVector<QRectF> calculateZones(
        int windowCount,
        const QRectF &screenGeometry,
        const TilingState &state) const override;

    bool supportsSplitRatio() const override { return true; }
    qreal defaultSplitRatio() const override { return 0.5; }

private:
    struct BSPNode {
        QRectF geometry;
        std::unique_ptr<BSPNode> left;
        std::unique_ptr<BSPNode> right;
        bool splitHorizontal = true;
    };

    void subdivide(BSPNode *node, int depth, int maxDepth, qreal ratio) const;
    void collectLeaves(BSPNode *node, QVector<QRectF> &zones) const;
};
```

**BSP Algorithm Logic:**
```
1. Start with full screen as root node
2. Split root in half (alternating horizontal/vertical)
3. Recursively split children until windowCount leaves exist
4. Apply split ratio to determine split position
5. Return leaf node geometries as zones
```

#### 2. Master-Stack

```cpp
// src/autotile/algorithms/MasterStackAlgorithm.h

class MasterStackAlgorithm : public TilingAlgorithm
{
    Q_OBJECT

public:
    QString name() const override { return QStringLiteral("Master + Stack"); }
    QString description() const override {
        return tr("Large master area with stacked secondary windows");
    }
    QString icon() const override { return QStringLiteral("view-split-left-right"); }

    QVector<QRectF> calculateZones(
        int windowCount,
        const QRectF &screenGeometry,
        const TilingState &state) const override;

    int masterZoneIndex() const override { return 0; }
    bool supportsMasterCount() const override { return true; }
    bool supportsSplitRatio() const override { return true; }
    qreal defaultSplitRatio() const override { return 0.6; } // 60% master
};
```

**Master-Stack Layout:**
```
+------------------+--------+
|                  |   S1   |
|     MASTER       |--------|
|     (60%)        |   S2   |
|                  |--------|
|                  |   S3   |
+------------------+--------+
```

#### 3. Monocle

```cpp
// src/autotile/algorithms/MonocleAlgorithm.h

class MonocleAlgorithm : public TilingAlgorithm
{
    Q_OBJECT

public:
    QString name() const override { return QStringLiteral("Monocle"); }
    QString description() const override {
        return tr("Single fullscreen window, others hidden");
    }
    QString icon() const override { return QStringLiteral("window-maximize"); }

    QVector<QRectF> calculateZones(
        int windowCount,
        const QRectF &screenGeometry,
        const TilingState &state) const override
    {
        // All windows get the same full-screen geometry
        // Window stacking/visibility handled separately
        QVector<QRectF> zones;
        for (int i = 0; i < windowCount; ++i) {
            zones.append(QRectF(0, 0, 1, 1));
        }
        return zones;
    }

    int minimumWindows() const override { return 1; }
};
```

#### 4. Columns

```cpp
// src/autotile/algorithms/ColumnsAlgorithm.h

class ColumnsAlgorithm : public TilingAlgorithm
{
    Q_OBJECT

public:
    QString name() const override { return QStringLiteral("Columns"); }
    QString description() const override {
        return tr("Equal-width vertical columns");
    }
    QString icon() const override { return QStringLiteral("view-split-left-right"); }

    QVector<QRectF> calculateZones(
        int windowCount,
        const QRectF &screenGeometry,
        const TilingState &state) const override
    {
        QVector<QRectF> zones;
        qreal width = 1.0 / windowCount;
        for (int i = 0; i < windowCount; ++i) {
            zones.append(QRectF(i * width, 0, width, 1.0));
        }
        return zones;
    }
};
```

#### 5. Fibonacci (Spiral)

```cpp
// src/autotile/algorithms/FibonacciAlgorithm.h

class FibonacciAlgorithm : public TilingAlgorithm
{
    Q_OBJECT

public:
    QString name() const override { return QStringLiteral("Fibonacci"); }
    QString description() const override {
        return tr("Spiral subdivision inspired by golden ratio");
    }
    QString icon() const override { return QStringLiteral("view-calendar"); }

    QVector<QRectF> calculateZones(
        int windowCount,
        const QRectF &screenGeometry,
        const TilingState &state) const override;

    bool supportsSplitRatio() const override { return true; }
    qreal defaultSplitRatio() const override { return 0.618; } // Golden ratio
};
```

**Fibonacci Layout (5 windows):**
```
+-------------+--------+
|             |   2    |
|     1       +----+---+
|             | 3  | 4 |
|             +----+---+
|             |   5    |
+-------------+--------+
```

#### 6. Three Column

```cpp
// src/autotile/algorithms/ThreeColumnAlgorithm.h

class ThreeColumnAlgorithm : public TilingAlgorithm
{
    Q_OBJECT

public:
    QString name() const override { return QStringLiteral("Three Column"); }
    QString description() const override {
        return tr("Center master with side columns");
    }
    QString icon() const override { return QStringLiteral("view-column-3"); }

    QVector<QRectF> calculateZones(
        int windowCount,
        const QRectF &screenGeometry,
        const TilingState &state) const override;

    int masterZoneIndex() const override { return 1; } // Center column
    bool supportsSplitRatio() const override { return true; }
    qreal defaultSplitRatio() const override { return 0.5; } // Center gets 50%
};
```

**Three Column Layout:**
```
+-------+----------+-------+
| Left  |          | Right |
| Stack |  CENTER  | Stack |
|       |  (50%)   |       |
+-------+----------+-------+
```

### Algorithm Registry

```cpp
// src/autotile/AlgorithmRegistry.h

class AlgorithmRegistry : public QObject
{
    Q_OBJECT

public:
    static AlgorithmRegistry *instance();

    void registerAlgorithm(const QString &id, TilingAlgorithm *algorithm);
    TilingAlgorithm *algorithm(const QString &id) const;
    QStringList availableAlgorithms() const;

    // Built-in algorithms
    static constexpr const char *BSP = "bsp";
    static constexpr const char *MasterStack = "master-stack";
    static constexpr const char *Monocle = "monocle";
    static constexpr const char *Columns = "columns";
    static constexpr const char *Fibonacci = "fibonacci";
    static constexpr const char *ThreeColumn = "three-column";
    static constexpr const char *Rows = "rows";

private:
    QHash<QString, TilingAlgorithm *> m_algorithms;
};
```

---

## Component Design

### AutotileEngine

```cpp
// src/autotile/AutotileEngine.h

namespace PlasmaZones {

class LayoutManager;
class WindowTracker;
class TilingState;
class TilingAlgorithm;

/**
 * @brief Core autotiling engine
 *
 * Manages automatic window tiling by:
 * - Listening to window open/close/focus events
 * - Calculating zone geometries via algorithm
 * - Coordinating with KWin Effect for window placement
 */
class PLASMAZONES_EXPORT AutotileEngine : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool enabled READ isEnabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(QString algorithm READ algorithm WRITE setAlgorithm NOTIFY algorithmChanged)

public:
    explicit AutotileEngine(
        LayoutManager *layoutManager,
        WindowTracker *windowTracker,
        QObject *parent = nullptr);
    ~AutotileEngine() override;

    // Global enable/disable
    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool enabled);

    // Algorithm selection
    QString algorithm() const { return m_algorithmId; }
    void setAlgorithm(const QString &algorithmId);
    TilingAlgorithm *currentAlgorithm() const;

    // Tiling state access
    TilingState *stateForScreen(const QString &screenName) const;

    // Manual operations
    Q_INVOKABLE void retile(const QString &screenName = QString());
    Q_INVOKABLE void swapWindows(const QString &windowId1, const QString &windowId2);
    Q_INVOKABLE void promoteToMaster(const QString &windowId);
    Q_INVOKABLE void demoteFromMaster(const QString &windowId);

    // Focus/window cycling
    Q_INVOKABLE void focusNext();
    Q_INVOKABLE void focusPrevious();
    Q_INVOKABLE void focusMaster();
    Q_INVOKABLE void cycleFocus(int direction); // +1 forward, -1 backward

    // Split ratio adjustment
    Q_INVOKABLE void increaseMasterRatio(qreal delta = 0.05);
    Q_INVOKABLE void decreaseMasterRatio(qreal delta = 0.05);

    // Master count adjustment
    Q_INVOKABLE void increaseMasterCount();
    Q_INVOKABLE void decreaseMasterCount();

    // Window insertion position
    enum InsertPosition { InsertAtEnd, InsertAfterFocused, InsertAsMaster };
    Q_INVOKABLE void setInsertPosition(InsertPosition pos);

Q_SIGNALS:
    void enabledChanged(bool enabled);
    void algorithmChanged(const QString &algorithmId);
    void tilingChanged(const QString &screenName);
    void windowTiled(const QString &windowId, const QRect &geometry);

private Q_SLOTS:
    void onWindowOpened(const QString &windowId, const QRect &geometry);
    void onWindowClosed(const QString &windowId);
    void onWindowFocused(const QString &windowId);
    void onLayoutChanged(Layout *layout);
    void onScreenGeometryChanged(const QString &screenName, const QRect &geometry);

private:
    void insertWindow(const QString &windowId, const QString &screenName);
    void removeWindow(const QString &windowId);
    void recalculateLayout(const QString &screenName);
    void applyTiling(const QString &screenName);
    bool shouldTileWindow(const QString &windowId) const;

    LayoutManager *m_layoutManager;
    WindowTracker *m_windowTracker;

    bool m_enabled = false;
    QString m_algorithmId = AlgorithmRegistry::MasterStack;
    InsertPosition m_insertPosition = InsertAtEnd;

    // Per-screen tiling state
    QHash<QString, std::unique_ptr<TilingState>> m_screenStates;
};

} // namespace PlasmaZones
```

### TilingState

```cpp
// src/autotile/TilingState.h

namespace PlasmaZones {

/**
 * @brief Tracks tiling state for a single screen
 *
 * Maintains:
 * - Window order (insertion order)
 * - Master window count
 * - Split ratio
 * - Per-window floating state
 */
class PLASMAZONES_EXPORT TilingState : public QObject
{
    Q_OBJECT

    Q_PROPERTY(int windowCount READ windowCount NOTIFY windowCountChanged)
    Q_PROPERTY(int masterCount READ masterCount WRITE setMasterCount NOTIFY masterCountChanged)
    Q_PROPERTY(qreal splitRatio READ splitRatio WRITE setSplitRatio NOTIFY splitRatioChanged)

public:
    explicit TilingState(const QString &screenName, QObject *parent = nullptr);

    QString screenName() const { return m_screenName; }

    // Window order management
    int windowCount() const { return m_windowOrder.size(); }
    QStringList windowOrder() const { return m_windowOrder; }
    void addWindow(const QString &windowId, int position = -1);
    void removeWindow(const QString &windowId);
    void moveWindow(int fromIndex, int toIndex);
    void swapWindows(int index1, int index2);
    int windowIndex(const QString &windowId) const;

    // Master management
    int masterCount() const { return m_masterCount; }
    void setMasterCount(int count);
    bool isMaster(const QString &windowId) const;
    QStringList masterWindows() const;
    QStringList stackWindows() const;

    // Split ratio
    qreal splitRatio() const { return m_splitRatio; }
    void setSplitRatio(qreal ratio);

    // Per-window floating
    bool isFloating(const QString &windowId) const;
    void setFloating(const QString &windowId, bool floating);
    QStringList floatingWindows() const;
    int tiledWindowCount() const; // Excludes floating

    // Serialization
    QJsonObject toJson() const;
    static TilingState *fromJson(const QJsonObject &json, QObject *parent = nullptr);

Q_SIGNALS:
    void windowCountChanged();
    void masterCountChanged();
    void splitRatioChanged();
    void windowOrderChanged();
    void floatingChanged(const QString &windowId, bool floating);

private:
    QString m_screenName;
    QStringList m_windowOrder;
    int m_masterCount = 1;
    qreal m_splitRatio = 0.6;
    QSet<QString> m_floatingWindows;
};

} // namespace PlasmaZones
```

### WindowTracker Extension

```cpp
// Extensions to existing WindowTracker for autotiling

class WindowTracker : public QObject
{
    // ... existing members ...

    // NEW: Track windows for autotiling
    Q_INVOKABLE QStringList windowsOnScreen(const QString &screenName) const;
    Q_INVOKABLE QString windowScreen(const QString &windowId) const;
    Q_INVOKABLE QString focusedWindow() const;
    Q_INVOKABLE QRect windowGeometry(const QString &windowId) const;

    // NEW: Window ordering for tiling
    Q_INVOKABLE void recordWindowFocus(const QString &windowId);
    QStringList recentlyFocusedWindows() const;

Q_SIGNALS:
    // NEW: Signals for autotiling
    void windowOpened(const QString &windowId, const QRect &geometry);
    void windowClosed(const QString &windowId);
    void windowFocused(const QString &windowId);
    void windowMoved(const QString &windowId, const QString &fromScreen, const QString &toScreen);
};
```

---

## Data Structures

### Layout Extension

```cpp
// Extensions to existing Layout class

class Layout : public QObject
{
    // ... existing members ...

    // NEW: Autotile configuration
    Q_PROPERTY(TilingMode tilingMode READ tilingMode WRITE setTilingMode NOTIFY tilingModeChanged)
    Q_PROPERTY(QString autotileAlgorithm READ autotileAlgorithm WRITE setAutotileAlgorithm NOTIFY autotileAlgorithmChanged)
    Q_PROPERTY(AutotileConfig autotileConfig READ autotileConfig WRITE setAutotileConfig NOTIFY autotileConfigChanged)

public:
    TilingMode tilingMode() const { return m_tilingMode; }
    void setTilingMode(TilingMode mode);

    QString autotileAlgorithm() const { return m_autotileAlgorithm; }
    void setAutotileAlgorithm(const QString &algorithm);

    AutotileConfig autotileConfig() const { return m_autotileConfig; }
    void setAutotileConfig(const AutotileConfig &config);

private:
    TilingMode m_tilingMode = TilingMode::Manual;
    QString m_autotileAlgorithm = "master-stack";
    AutotileConfig m_autotileConfig;
};
```

### AutotileConfig

```cpp
// src/autotile/AutotileConfig.h

struct AutotileConfig
{
    // Split ratio (0.0 - 1.0)
    qreal splitRatio = 0.6;

    // Master window count
    int masterCount = 1;

    // Gaps between windows (pixels)
    int innerGap = 8;
    int outerGap = 8;

    // Window insertion behavior
    enum InsertPosition { End, AfterFocused, AsMaster };
    InsertPosition insertPosition = End;

    // Focus behavior
    bool focusFollowsMouse = false;
    bool focusNewWindows = true;

    // Border settings
    bool showActiveBorder = true;
    int activeBorderWidth = 2;
    QColor activeBorderColor = QColor("#3daee9");

    // Monocle-specific
    bool monocleHideOthers = false;
    bool monocleShowTabs = true;

    // Serialization
    QJsonObject toJson() const;
    static AutotileConfig fromJson(const QJsonObject &json);
};
```

### Layout JSON Extension

```json
{
    "id": "{uuid}",
    "name": "Master + Stack Auto",
    "type": 6,
    "tilingMode": "autotile",
    "autotileAlgorithm": "master-stack",
    "autotileConfig": {
        "splitRatio": 0.6,
        "masterCount": 1,
        "innerGap": 8,
        "outerGap": 8,
        "insertPosition": "end",
        "focusNewWindows": true
    },
    "zones": []
}
```

---

## Keyboard Controls

### Shortcut Scheme

Following i3/Bismuth conventions with Meta as the primary modifier:

| Action | Shortcut | Description |
|--------|----------|-------------|
| **Focus Navigation** | | |
| Focus left | `Meta+H` | Focus window to the left |
| Focus down | `Meta+J` | Focus window below |
| Focus up | `Meta+K` | Focus window above |
| Focus right | `Meta+L` | Focus window to the right |
| Focus master | `Meta+M` | Focus the master window |
| Focus next | `Meta+Tab` | Cycle focus forward |
| Focus previous | `Meta+Shift+Tab` | Cycle focus backward |
| **Window Movement** | | |
| Move left | `Meta+Shift+H` | Move window left |
| Move down | `Meta+Shift+J` | Move window down |
| Move up | `Meta+Shift+K` | Move window up |
| Move right | `Meta+Shift+L` | Move window right |
| Swap with master | `Meta+Return` | Promote to/swap with master |
| **Layout Control** | | |
| Increase master ratio | `Meta+Shift+Plus` | Expand master area |
| Decrease master ratio | `Meta+Shift+Minus` | Shrink master area |
| Increase master count | `Meta+Shift+I` | Add window to master |
| Decrease master count | `Meta+Shift+D` | Remove window from master |
| **Algorithm Switching** | | |
| Cycle algorithm | `Meta+Space` | Next algorithm |
| BSP layout | `Meta+B` | Switch to BSP |
| Master-Stack | `Meta+T` | Switch to Master-Stack |
| Monocle | `Meta+F` | Toggle monocle/fullscreen |
| Columns | `Meta+C` | Switch to Columns |
| **Window State** | | |
| Toggle float | `Meta+Shift+F` | Float/unfloat window |
| Toggle tiling | `Meta+Shift+T` | Enable/disable autotiling |
| Retile | `Meta+Shift+R` | Re-apply tiling |
| **Gaps** | | |
| Increase gaps | `Meta+Shift+G` | Increase window gaps |
| Decrease gaps | `Meta+Ctrl+G` | Decrease window gaps |
| Reset gaps | `Meta+Shift+Ctrl+G` | Reset to default gaps |

### ShortcutManager Extension

```cpp
// Extensions to existing ShortcutManager

class ShortcutManager : public QObject
{
    // ... existing members ...

    // NEW: Autotiling shortcuts
    void setupAutotileShortcuts();

Q_SIGNALS:
    // NEW: Autotile signals
    void focusDirectionRequested(NavigationDirection direction);
    void moveWindowDirectionRequested(NavigationDirection direction);
    void focusMasterRequested();
    void focusCycleRequested(int direction);
    void swapWithMasterRequested();
    void increaseMasterRatioRequested();
    void decreaseMasterRatioRequested();
    void increaseMasterCountRequested();
    void decreaseMasterCountRequested();
    void cycleAlgorithmRequested();
    void setAlgorithmRequested(const QString &algorithm);
    void toggleMonocleRequested();
    void retileRequested();
    void increaseGapsRequested();
    void decreaseGapsRequested();
    void resetGapsRequested();

private:
    // Autotile shortcut actions
    QAction *m_focusLeftAction = nullptr;
    QAction *m_focusDownAction = nullptr;
    QAction *m_focusUpAction = nullptr;
    QAction *m_focusRightAction = nullptr;
    QAction *m_focusMasterAction = nullptr;
    QAction *m_focusNextAction = nullptr;
    QAction *m_focusPrevAction = nullptr;
    QAction *m_moveLeftAction = nullptr;
    QAction *m_moveDownAction = nullptr;
    QAction *m_moveUpAction = nullptr;
    QAction *m_moveRightAction = nullptr;
    QAction *m_swapMasterAction = nullptr;
    QAction *m_incMasterRatioAction = nullptr;
    QAction *m_decMasterRatioAction = nullptr;
    QAction *m_incMasterCountAction = nullptr;
    QAction *m_decMasterCountAction = nullptr;
    QAction *m_cycleAlgorithmAction = nullptr;
    QAction *m_retileAction = nullptr;
    // ... etc
};
```

---

## Settings Design

### Settings Extension

```cpp
// Extensions to existing Settings class

class Settings : public ISettings
{
    // ... existing members ...

    // NEW: Autotiling settings
    Q_PROPERTY(bool autotileEnabled READ autotileEnabled WRITE setAutotileEnabled NOTIFY autotileEnabledChanged)
    Q_PROPERTY(QString defaultAutotileAlgorithm READ defaultAutotileAlgorithm WRITE setDefaultAutotileAlgorithm NOTIFY defaultAutotileAlgorithmChanged)
    Q_PROPERTY(int autotileInnerGap READ autotileInnerGap WRITE setAutotileInnerGap NOTIFY autotileInnerGapChanged)
    Q_PROPERTY(int autotileOuterGap READ autotileOuterGap WRITE setAutotileOuterGap NOTIFY autotileOuterGapChanged)
    Q_PROPERTY(qreal defaultMasterRatio READ defaultMasterRatio WRITE setDefaultMasterRatio NOTIFY defaultMasterRatioChanged)
    Q_PROPERTY(int defaultMasterCount READ defaultMasterCount WRITE setDefaultMasterCount NOTIFY defaultMasterCountChanged)
    Q_PROPERTY(bool autotileFocusNewWindows READ autotileFocusNewWindows WRITE setAutotileFocusNewWindows NOTIFY autotileFocusNewWindowsChanged)
    Q_PROPERTY(bool autotileFocusFollowsMouse READ autotileFocusFollowsMouse WRITE setAutotileFocusFollowsMouse NOTIFY autotileFocusFollowsMouseChanged)

    // Autotile shortcuts
    Q_PROPERTY(QString focusLeftShortcut READ focusLeftShortcut WRITE setFocusLeftShortcut NOTIFY focusLeftShortcutChanged)
    Q_PROPERTY(QString focusRightShortcut READ focusRightShortcut WRITE setFocusRightShortcut NOTIFY focusRightShortcutChanged)
    // ... more shortcuts ...

public:
    // Global autotiling
    bool autotileEnabled() const { return m_autotileEnabled; }
    void setAutotileEnabled(bool enabled);

    QString defaultAutotileAlgorithm() const { return m_defaultAutotileAlgorithm; }
    void setDefaultAutotileAlgorithm(const QString &algorithm);

    // Gaps
    int autotileInnerGap() const { return m_autotileInnerGap; }
    void setAutotileInnerGap(int gap);

    int autotileOuterGap() const { return m_autotileOuterGap; }
    void setAutotileOuterGap(int gap);

    // Master settings
    qreal defaultMasterRatio() const { return m_defaultMasterRatio; }
    void setDefaultMasterRatio(qreal ratio);

    int defaultMasterCount() const { return m_defaultMasterCount; }
    void setDefaultMasterCount(int count);

    // Focus behavior
    bool autotileFocusNewWindows() const { return m_autotileFocusNewWindows; }
    void setAutotileFocusNewWindows(bool focus);

    bool autotileFocusFollowsMouse() const { return m_autotileFocusFollowsMouse; }
    void setAutotileFocusFollowsMouse(bool follows);

private:
    bool m_autotileEnabled = false;
    QString m_defaultAutotileAlgorithm = "master-stack";
    int m_autotileInnerGap = 8;
    int m_autotileOuterGap = 8;
    qreal m_defaultMasterRatio = 0.6;
    int m_defaultMasterCount = 1;
    bool m_autotileFocusNewWindows = true;
    bool m_autotileFocusFollowsMouse = false;
};
```

### KCM UI Extensions

New "Autotiling" page in the KCM:

```qml
// kcm/ui/AutotilingPage.qml

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kde.kcmutils as KCM

KCM.SimpleKCM {
    id: root

    Kirigami.FormLayout {
        // Enable autotiling
        QQC2.CheckBox {
            Kirigami.FormData.label: i18n("Enable Autotiling:")
            checked: kcm.settings.autotileEnabled
            onToggled: kcm.settings.autotileEnabled = checked
        }

        // Default algorithm
        QQC2.ComboBox {
            Kirigami.FormData.label: i18n("Default Algorithm:")
            model: ["Master + Stack", "BSP", "Monocle", "Columns", "Fibonacci", "Three Column"]
            currentIndex: algorithmToIndex(kcm.settings.defaultAutotileAlgorithm)
            onActivated: kcm.settings.defaultAutotileAlgorithm = indexToAlgorithm(currentIndex)
        }

        Kirigami.Separator {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18n("Gaps")
        }

        // Inner gap
        QQC2.SpinBox {
            Kirigami.FormData.label: i18n("Inner Gap:")
            from: 0
            to: 50
            value: kcm.settings.autotileInnerGap
            onValueModified: kcm.settings.autotileInnerGap = value
        }

        // Outer gap
        QQC2.SpinBox {
            Kirigami.FormData.label: i18n("Outer Gap:")
            from: 0
            to: 50
            value: kcm.settings.autotileOuterGap
            onValueModified: kcm.settings.autotileOuterGap = value
        }

        Kirigami.Separator {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18n("Master Area")
        }

        // Master ratio
        QQC2.Slider {
            Kirigami.FormData.label: i18n("Master Ratio:")
            from: 0.3
            to: 0.8
            value: kcm.settings.defaultMasterRatio
            onMoved: kcm.settings.defaultMasterRatio = value

            QQC2.Label {
                anchors.right: parent.right
                text: Math.round(parent.value * 100) + "%"
            }
        }

        // Master count
        QQC2.SpinBox {
            Kirigami.FormData.label: i18n("Master Count:")
            from: 1
            to: 5
            value: kcm.settings.defaultMasterCount
            onValueModified: kcm.settings.defaultMasterCount = value
        }

        Kirigami.Separator {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18n("Behavior")
        }

        // Focus new windows
        QQC2.CheckBox {
            Kirigami.FormData.label: i18n("Focus new windows:")
            checked: kcm.settings.autotileFocusNewWindows
            onToggled: kcm.settings.autotileFocusNewWindows = checked
        }

        // Focus follows mouse
        QQC2.CheckBox {
            Kirigami.FormData.label: i18n("Focus follows mouse:")
            checked: kcm.settings.autotileFocusFollowsMouse
            onToggled: kcm.settings.autotileFocusFollowsMouse = checked
        }
    }
}
```

---

## KWin Effect Integration

### Effect Extension

```cpp
// Extensions to existing PlasmaZonesEffect

class PlasmaZonesEffect : public KWin::Effect
{
    // ... existing members ...

private Q_SLOTS:
    // NEW: Autotiling event handlers
    void slotWindowActivated(KWin::EffectWindow *w);
    void slotWindowMinimized(KWin::EffectWindow *w);
    void slotWindowUnminimized(KWin::EffectWindow *w);

    // NEW: Autotiling D-Bus signal handlers
    void slotAutotileWindowRequested(const QString &windowId, const QRect &geometry);
    void slotFocusWindowRequested(const QString &windowId);
    void slotSwapWindowsRequested(const QString &windowId1, const QString &windowId2);

private:
    // NEW: Autotiling helpers
    void notifyWindowOpened(KWin::EffectWindow *w);
    void notifyWindowClosed(KWin::EffectWindow *w);
    void notifyWindowFocused(KWin::EffectWindow *w);

    // NEW: Apply geometry with animation
    void applyAutotileGeometry(KWin::EffectWindow *window, const QRect &geometry);

    // Animation support
    struct WindowAnimation {
        QRect startGeometry;
        QRect endGeometry;
        qreal progress = 0;
    };
    QHash<KWin::EffectWindow *, WindowAnimation> m_animations;
    void animateWindowToGeometry(KWin::EffectWindow *w, const QRect &geometry);
};
```

### D-Bus Interface for Autotiling

```xml
<!-- org.plasmazones.Autotile.xml -->
<interface name="org.plasmazones.Autotile">
    <!-- Properties -->
    <property name="enabled" type="b" access="readwrite"/>
    <property name="algorithm" type="s" access="readwrite"/>
    <property name="masterRatio" type="d" access="readwrite"/>
    <property name="masterCount" type="i" access="readwrite"/>
    <property name="innerGap" type="i" access="readwrite"/>
    <property name="outerGap" type="i" access="readwrite"/>

    <!-- Methods -->
    <method name="retile">
        <arg name="screenName" type="s" direction="in"/>
    </method>

    <method name="setAlgorithm">
        <arg name="algorithmId" type="s" direction="in"/>
    </method>

    <method name="swapWindows">
        <arg name="windowId1" type="s" direction="in"/>
        <arg name="windowId2" type="s" direction="in"/>
    </method>

    <method name="promoteToMaster">
        <arg name="windowId" type="s" direction="in"/>
    </method>

    <method name="focusMaster"/>
    <method name="focusNext"/>
    <method name="focusPrevious"/>

    <method name="increaseMasterRatio"/>
    <method name="decreaseMasterRatio"/>
    <method name="increaseMasterCount"/>
    <method name="decreaseMasterCount"/>

    <!-- Signals -->
    <signal name="autotileWindowRequested">
        <arg name="windowId" type="s"/>
        <arg name="geometry" type="(iiii)"/>
    </signal>

    <signal name="focusWindowRequested">
        <arg name="windowId" type="s"/>
    </signal>

    <signal name="tilingChanged">
        <arg name="screenName" type="s"/>
    </signal>
</interface>
```

---

## D-Bus API Extensions

### New Adaptor: AutotileAdaptor

```cpp
// src/dbus/autotileadaptor.h

class AutotileAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.plasmazones.Autotile")

    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled)
    Q_PROPERTY(QString algorithm READ algorithm WRITE setAlgorithm)
    Q_PROPERTY(double masterRatio READ masterRatio WRITE setMasterRatio)
    Q_PROPERTY(int masterCount READ masterCount WRITE setMasterCount)
    Q_PROPERTY(int innerGap READ innerGap WRITE setInnerGap)
    Q_PROPERTY(int outerGap READ outerGap WRITE setOuterGap)

public:
    explicit AutotileAdaptor(AutotileEngine *engine);

public Q_SLOTS:
    void retile(const QString &screenName);
    void setAlgorithm(const QString &algorithmId);
    void swapWindows(const QString &windowId1, const QString &windowId2);
    void promoteToMaster(const QString &windowId);
    void focusMaster();
    void focusNext();
    void focusPrevious();
    void increaseMasterRatio();
    void decreaseMasterRatio();
    void increaseMasterCount();
    void decreaseMasterCount();

Q_SIGNALS:
    void autotileWindowRequested(const QString &windowId, QRect geometry);
    void focusWindowRequested(const QString &windowId);
    void tilingChanged(const QString &screenName);

private:
    AutotileEngine *m_engine;
};
```

---

## Migration Path for Bismuth Users

### Migration Script

```bash
#!/bin/bash
# migrate-from-bismuth.sh
# Helps former Bismuth users configure PlasmaZones

echo "PlasmaZones Bismuth Migration Helper"
echo "====================================="

# Check for existing Bismuth config
BISMUTH_CONFIG="$HOME/.config/bismuthrc"
if [[ -f "$BISMUTH_CONFIG" ]]; then
    echo "Found Bismuth configuration at $BISMUTH_CONFIG"

    # Parse Bismuth settings
    BISMUTH_LAYOUT=$(grep "^layout=" "$BISMUTH_CONFIG" | cut -d= -f2)
    BISMUTH_GAPS=$(grep "^tileGap=" "$BISMUTH_CONFIG" | cut -d= -f2)
    BISMUTH_RATIO=$(grep "^masterRatio=" "$BISMUTH_CONFIG" | cut -d= -f2)

    # Map Bismuth layouts to PlasmaZones algorithms
    case "$BISMUTH_LAYOUT" in
        "tile")     ALGO="master-stack" ;;
        "monocle")  ALGO="monocle" ;;
        "spread")   ALGO="columns" ;;
        "stair")    ALGO="bsp" ;;
        "spiral")   ALGO="fibonacci" ;;
        "threeColumn") ALGO="three-column" ;;
        *)          ALGO="master-stack" ;;
    esac

    echo "Detected Bismuth layout: $BISMUTH_LAYOUT -> PlasmaZones: $ALGO"
    echo "Detected gap size: ${BISMUTH_GAPS:-8}"
    echo "Detected master ratio: ${BISMUTH_RATIO:-0.55}"
fi

# Configure PlasmaZones via D-Bus
echo ""
echo "Configuring PlasmaZones..."

dbus-send --session --type=method_call \
    --dest=org.plasmazones \
    /Settings \
    org.plasmazones.Settings.setAutotileEnabled \
    boolean:true

dbus-send --session --type=method_call \
    --dest=org.plasmazones \
    /Settings \
    org.plasmazones.Settings.setDefaultAutotileAlgorithm \
    string:"$ALGO"

echo "Migration complete!"
echo ""
echo "Recommended shortcuts to add to System Settings > Shortcuts:"
echo "  Meta+H: Focus Left"
echo "  Meta+J: Focus Down"
echo "  Meta+K: Focus Up"
echo "  Meta+L: Focus Right"
echo "  Meta+Return: Swap with Master"
echo "  Meta+Space: Cycle Algorithm"
```

### Shortcut Import

```xml
<!-- bismuth-shortcuts.kksrc -->
<!-- Import via System Settings > Shortcuts > Import -->
[kwin][PlasmaZones Autotile]
Focus Left=Meta+H
Focus Down=Meta+J
Focus Up=Meta+K
Focus Right=Meta+L
Focus Master=Meta+M
Move Left=Meta+Shift+H
Move Down=Meta+Shift+J
Move Up=Meta+Shift+K
Move Right=Meta+Shift+L
Swap with Master=Meta+Return
Toggle Float=Meta+Shift+F
Cycle Algorithm=Meta+Space
Increase Master Ratio=Meta+Shift+Plus
Decrease Master Ratio=Meta+Shift+Minus
Increase Master Count=Meta+Shift+I
Decrease Master Count=Meta+Shift+D
Toggle Monocle=Meta+F
Retile=Meta+Shift+R
```

### Documentation for Bismuth Users

```markdown
# Migrating from Bismuth to PlasmaZones

## Key Differences

| Feature | Bismuth | PlasmaZones |
|---------|---------|-------------|
| **Primary Mode** | Auto-tiling only | Manual zones + auto-tiling |
| **Layout Source** | Algorithm presets | WYSIWYG editor + algorithms |
| **Plasma Support** | Up to 5.26 | 5.x and 6.x |
| **Maintenance** | Archived | Active |

## Layout Mapping

| Bismuth | PlasmaZones |
|---------|-------------|
| Tile | Master + Stack |
| Monocle | Monocle |
| Spread | Columns |
| Stair | BSP |
| Spiral | Fibonacci |
| Three Column | Three Column |

## Getting Started

1. Enable autotiling in Settings > Autotiling
2. Select your preferred algorithm
3. Import the Bismuth-compatible shortcuts (optional)
4. Adjust master ratio and gaps to match your Bismuth config

## Shortcuts Reference

PlasmaZones supports the same keyboard-centric workflow as Bismuth:
- Focus navigation: Meta + H/J/K/L
- Window movement: Meta + Shift + H/J/K/L
- Master swap: Meta + Return
- Algorithm cycle: Meta + Space
```

---

## Implementation Phases

### Phase 1: Core Engine (2-3 weeks)

1. **TilingAlgorithm interface** and basic implementations
   - BSP
   - Master-Stack
   - Columns

2. **TilingState** class for per-screen state management

3. **AutotileEngine** core with:
   - Window open/close handling
   - Basic retiling
   - Algorithm switching

4. **Layout extension** for tilingMode and autotileConfig

### Phase 2: KWin Integration (1-2 weeks)

1. **KWin Effect extensions**
   - Window activation tracking
   - Geometry application with animation
   - D-Bus signal handling

2. **AutotileAdaptor** D-Bus interface

3. **Window focus management**
   - Focus navigation
   - Focus cycling

### Phase 3: Keyboard Controls (1 week)

1. **ShortcutManager extensions**
   - All autotile shortcuts
   - Algorithm-specific shortcuts

2. **Settings integration**
   - Shortcut configuration
   - Defaults for new users

### Phase 4: UI and Polish (1-2 weeks)

1. **KCM Autotiling page**
   - Settings UI
   - Algorithm preview

2. **Additional algorithms**
   - Fibonacci
   - Three Column
   - Monocle
   - Rows

3. **Migration tools**
   - Bismuth config import
   - Shortcut presets

4. **Documentation**
   - User guide
   - Migration guide

### Phase 5: Advanced Features (Optional)

1. **Per-window rules**
   - Float specific apps
   - Force zones for apps

2. **Monocle enhancements**
   - Tab bar
   - Window cycling UI

3. **Dynamic gap adjustment**
   - Runtime gap changes
   - Per-layout gaps

---

## Success Criteria

1. **Functional**: All 6 algorithms work correctly
2. **Performance**: Retiling completes in < 50ms
3. **Compatibility**: No regressions in manual zone mode
4. **Usability**: Keyboard shortcuts match Bismuth expectations
5. **Stability**: No crashes during rapid window operations

---

## Open Questions

1. **Should autotile layouts support manual zone additions?**
   - Hybrid mode addresses this, but implementation complexity increases

2. **How to handle multi-monitor with different algorithms?**
   - Current design: per-layout algorithm, different layouts per screen

3. **Should monocle show a window list/tabs?**
   - Could add as enhancement in Phase 5

4. **Animation timing for retiling?**
   - Suggest 150ms ease-out animation

---

## References

- [Bismuth Source](https://github.com/Bismuth-Forge/bismuth)
- [i3 User Guide](https://i3wm.org/docs/userguide.html)
- [dwm Layouts](https://dwm.suckless.org/tutorial/)
- [PlasmaZones Feature Roadmap](./feature-roadmap.md)
- [Bismuth Comparison](./feature-comparison-bismuth.md)

---

## Appendix A: Algorithm Visualization

### BSP (4 windows)
```
+-------------+-------------+
|             |             |
|      1      |      2      |
|             |             |
+-------------+------+------+
|             |      |      |
|      3      |  4   |  5   |
|             |      |      |
+-------------+------+------+
```

### Master-Stack (4 windows)
```
+------------------+--------+
|                  |   2    |
|        1         +--------+
|     (Master)     |   3    |
|                  +--------+
|                  |   4    |
+------------------+--------+
```

### Fibonacci (5 windows)
```
+-------------+--------+
|             |   2    |
|      1      +----+---+
|             | 3  | 4 |
|             +----+---+
|             |   5    |
+-------------+--------+
```

### Three Column (5 windows)
```
+-----+----------+-----+
|  2  |          |  4  |
+-----+    1     +-----+
|  3  | (Master) |  5  |
+-----+----------+-----+
```

### Monocle
```
+----------------------+
|                      |
|     [1/4] Window     |
|                      |
|    (fullscreen)      |
|                      |
+----------------------+
```
