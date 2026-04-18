# PhosphorZones — API Design Document

## Overview

PhosphorZones is a Qt6/C++20 library for the **manual zone layout** data
model: user-authored rectangular zones, the containing `Layout` aggregate,
JSON persistence, spatial detection, and the abstract contracts that let
the application route snap / overlay / picker code without knowing which
concrete layout manager is behind them.

It is the passive-data / geometry layer of PlasmaZones' tiling stack —
deliberately **engine-free**. There is no window-snapping runtime, no
autotile algorithm, no compositor binding. That machinery lives (or
will live) in sibling libraries: `phosphor-snap` for the manual-snap
runtime, `phosphor-tiles` for autotile algorithms. PhosphorZones stops
at the `ILayoutSource` boundary so editors, previews, settings, and the
KCM can render manual layouts without linking against any of those
consumers.

At ~3.5 k LOC across 17 public headers and 9 source files,
PhosphorZones is the largest surface in the `libs/` family — almost
entirely Q_PROPERTY, JSON serialization, and geometry math lifted out
of `src/core/` during the decoupling sweep.

**License:** LGPL-2.1-or-later (KDE ecosystem standard for libraries)
**Namespace:** `PhosphorZones`
**Build:** `libs/phosphor-zones/` — compiled as `SHARED` via
`generate_export_header` (`PHOSPHORZONES_EXPORT`). In-tree today,
separable later — PlasmaZones links `PhosphorZones::PhosphorZones` via
the installed CMake package.

---

## Dependency Graph

```
 PhosphorLayoutApi                   PhosphorIdentity
 (LayoutPreview, ILayoutSource,      (WindowId::appIdMatches —
  EdgeGaps, AspectRatioClass,         segment-aware pattern match)
  GapKeys)
        │                                    │
        │  PUBLIC                            │  PRIVATE
        │  (appears in                       │  (only inside
        │   ZonesLayoutSource /              │   Layout::matchAppRule —
        │   Layout headers)                  │   never in public headers)
        ▼                                    ▼
 ┌──────────────────────────────────────────────────┐
 │                 PhosphorZones                     │
 │                                                   │
 │  Zone • Layout • AppRule • ZoneDetector          │
 │  ZoneHighlighter • ZonesLayoutSource             │
 │  ILayoutRegistry / Assignments / QuickLayouts /  │
 │  BuiltInLayouts / Persistence • ILayoutManager   │
 │  IZoneDetector / IZoneDetection                  │
 │  ZoneDefaults • ZoneJsonKeys • LayoutUtils       │
 └──────────────────────────────────────────────────┘
        │                                  │
        ▼                                  ▼
 PlasmaZones daemon            PlasmaZones editor / settings / KCM
 (LayoutManager implements     (reads ILayoutRegistry, builds
  ILayoutManager; overlay       ZonesLayoutSource for previews)
  service consumes IZoneDetector)
```

The only transitive Qt requirement is `Qt6::Core` + `Qt6::Gui` (for
`QColor`, `QRectF`, `QUuid`, `QJsonObject`). No Qt Quick, no QML
engine, no Widgets — manual-layout primitives are consumable from
headless tools, CLI flows, and anywhere the application decides to stop.

---

## Design Principles

1. **Data, not behaviour.** Zone and Layout are Q_PROPERTY-rich
   QObjects; they compute geometry and serialize to JSON. They do not
   know what a window is, do not move anything, and do not talk to a
   compositor.
2. **Interfaces over concretes.** Every capability of the
   application's concrete `LayoutManager` is exposed here as an
   abstract interface — callers type against the narrowest one they
   need.
3. **ISP over convenience.** The manager interface is split into
   five sibling contracts along real capability boundaries
   (`ILayoutRegistry`, `ILayoutAssignments`, `IQuickLayouts`,
   `IBuiltInLayouts`, `ILayoutPersistence`); tests stub what they
   need and nothing more.
4. **Non-QObject interfaces by design.** Signal shadowing with
   Qt's function-pointer `connect` causes heap corruption in
   abstract-interface hierarchies — the library's contracts are
   therefore pure abstracts with no signals. See the [rationale
   below](#rationale-why-ilayoutmanager-is-not-a-qobject).
5. **Injectable side-effects.** The one genuinely process-global
   piece of state — the legacy-connector-to-stable-EDID screen-id
   resolver — is a mutex-guarded static `std::function`, default-unset
   so headless code is fully deterministic.
6. **JSON is the canonical serialization format.** Everything with
   a wire representation routes through `QJsonObject`. The
   `QVariantList` surfaces for QML binding are thin adapters over the
   JSON form.
7. **Qt parent-ownership for aggregates.** `Layout` owns its `Zone`s
   via `setParent`; `m_zones` is `QVector<Zone*>`; removal uses
   `deleteLater`. No shared ownership, no manual `delete`.

---

## Rationale: Why `ILayoutManager` is not a QObject

Quoting `ILayoutManager.h:18-23` verbatim:

> Qt's signal system doesn't work well with abstract interfaces because
> signal shadowing between base and derived classes causes heap
> corruption when using new-style `Qt::connect` with function pointers.
> These interfaces are all non-QObject for that reason. Components
> needing signals should use `LayoutManager*` directly.

The symptom: when an interface declares a signal and the concrete
subclass re-declares it, both declarations get independent metaobject
entries. Function-pointer `connect`s through the interface and through
the concrete resolve to different slot entry points, and on emit one
stomps the other's stack frame. We chased that class of crash for
months; the resolution adopted here is that **none of the library's
abstract contracts are QObjects**.

Consumers that need reactivity (the KCM's "layout list changed" wiring,
the overlay service's active-layout tracking) take the concrete
`LayoutManager*` owned by the application. Fixture tests that only
exercise enumeration or assignment never need signals.

The one narrow exception is `IZoneDetector`, which is a QObject because
the detector genuinely emits `layoutChanged` / `zoneHighlighted` /
`highlightsCleared`. `ZoneDetector` inherits from `IZoneDetector` once
and **does not** re-declare those signals — `ZoneDetector.h:96-101`
explicitly flags the constraint so nobody breaks it.

---

## Rationale: Why the manager interface is split into five

Concrete `LayoutManager` implements every capability in the library;
`ILayoutManager` is the umbrella. Most callers only need a few of the
capabilities, and the split gives:

- **Narrower fixture tests.** A test on the editor's save path stubs
  `ILayoutPersistence` (four methods) instead of the 30-method
  umbrella. Dead-code overrides disappear from mocks.
- **ISP compliance on read paths.** The overlay service's zone picker
  depends on `ILayoutAssignments` (9 methods) instead of the whole
  manager.
- **Independent evolution.** New capabilities become sixth siblings
  rather than growth rings on one interface — blast radius per change
  stays small.

### Inheritance model: non-virtual multiple inheritance

From `ILayoutManager.h:53-65`:

> The siblings share no state (none of them define member variables),
> so virtual bases are unnecessary and would only cost an extra
> indirection.

Consequences the header calls out:

- Upcasting `ILayoutManager*` to any sibling is free and unambiguous.
- Downcasting a sibling back to `ILayoutManager*` is **not** supported
  via `static_cast` — use `dynamic_cast` (with cross-SO `typeinfo`
  caveats) or restructure the call site to keep the wide pointer.
- **No sibling may ever grow member state.** Adding a data member
  would silently multiply the base subobject in the derived class and
  break the free-upcast invariant. There is no compile-time guard for
  this — the header spells out the rule so a future contributor gets a
  warning before compiling.

---

## Public API

### 1. Zone — `PhosphorZones::Zone`

A single rectangular zone. QObject, Q_PROPERTY-rich, non-copyable (Qt
object model — use `clone()` for duplicates).

```cpp
class PHOSPHORZONES_EXPORT Zone : public QObject {
    Q_OBJECT
    Q_PROPERTY(QUuid  id                READ id CONSTANT)
    Q_PROPERTY(QString name             READ name              WRITE setName             NOTIFY nameChanged)
    Q_PROPERTY(QRectF geometry          READ geometry          WRITE setGeometry         NOTIFY geometryChanged)
    Q_PROPERTY(QRectF relativeGeometry  READ relativeGeometry  WRITE setRelativeGeometry NOTIFY relativeGeometryChanged)
    Q_PROPERTY(int    zoneNumber        READ zoneNumber        WRITE setZoneNumber       NOTIFY zoneNumberChanged)
    Q_PROPERTY(QColor highlightColor    READ highlightColor    WRITE setHighlightColor   NOTIFY highlightColorChanged)
    Q_PROPERTY(QColor borderColor       READ borderColor       WRITE setBorderColor      NOTIFY borderColorChanged)
    Q_PROPERTY(qreal  activeOpacity     READ activeOpacity     WRITE setActiveOpacity    NOTIFY activeOpacityChanged)
    Q_PROPERTY(int    borderWidth       READ borderWidth       WRITE setBorderWidth      NOTIFY borderWidthChanged)
    Q_PROPERTY(int    borderRadius      READ borderRadius      WRITE setBorderRadius     NOTIFY borderRadiusChanged)
    Q_PROPERTY(bool   isHighlighted     READ isHighlighted     WRITE setHighlighted      NOTIFY highlightedChanged)
    Q_PROPERTY(int    geometryMode      READ geometryModeInt   WRITE setGeometryModeInt  NOTIFY geometryModeChanged)
    Q_PROPERTY(QRectF fixedGeometry     READ fixedGeometry     WRITE setFixedGeometry    NOTIFY fixedGeometryChanged)
    // …plus inactiveColor, inactiveOpacity, useCustomColors, overlayDisplayMode

public:
    explicit Zone(QObject* parent = nullptr);
    explicit Zone(const QRectF& geometry, QObject* parent = nullptr);
    Zone(const Zone&) = delete;                       // use clone() for duplicates
    Zone& operator=(const Zone&) = delete;

    Q_INVOKABLE Zone* clone(QObject* parent = nullptr) const;
    void copyPropertiesFrom(const Zone& other);

    // Normalized 0..1 regardless of geometry mode
    Q_INVOKABLE QRectF normalizedGeometry(const QRectF& referenceGeometry) const;
    Q_INVOKABLE bool   containsPoint(const QPointF& point) const;
    Q_INVOKABLE qreal  distanceToPoint(const QPointF& point) const;
    Q_INVOKABLE QRectF calculateAbsoluteGeometry(const QRectF& screenGeometry) const;
    Q_INVOKABLE QRectF applyPadding(int padding) const;

    // Pure helper for off-thread use (LayoutWorker can't touch QObjects).
    static QRectF computeAbsoluteGeometry(ZoneGeometryMode mode, const QRectF& relativeGeometry,
                                          const QRectF& fixedGeometry, const QRectF& screenGeometry);

    QJsonObject  toJson(const QRectF& referenceGeometry = QRectF()) const;
    static Zone* fromJson(const QJsonObject& json, QObject* parent = nullptr);
};

enum class ZoneGeometryMode { Relative = 0, Fixed = 1 };
```

`Relative` (default) is 0.0–1.0 normalized coords, resolution-independent.
`Fixed` is pixel coords relative to screen origin. Zone construction
seeds defaults from `ZoneDefaults::HighlightColor`, `::BorderWidth`,
etc. — single source of truth across library, KCM, and layout files.

---

### 2. Layout — `PhosphorZones::Layout`

A collection of `Zone`s plus layout-wide metadata. QObject; owns its
zones via Qt parent-ownership. Full header is ~540 lines — abridged
here to the shape:

```cpp
class PHOSPHORZONES_EXPORT Layout : public QObject {
    Q_OBJECT
    // Identity + metadata
    Q_PROPERTY(QUuid   id          READ id CONSTANT)
    Q_PROPERTY(QString name        READ name        WRITE setName        NOTIFY nameChanged)
    Q_PROPERTY(QString description READ description WRITE setDescription NOTIFY descriptionChanged)
    Q_PROPERTY(QString sourcePath  READ sourcePath  WRITE setSourcePath  NOTIFY sourcePathChanged)
    Q_PROPERTY(bool    isSystemLayout READ isSystemLayout NOTIFY sourcePathChanged)

    // Gaps (per-side + global; -1 means "use global setting")
    Q_PROPERTY(int  zonePadding         READ zonePadding        WRITE setZonePadding        NOTIFY zonePaddingChanged)
    Q_PROPERTY(int  outerGap            READ outerGap           WRITE setOuterGap           NOTIFY outerGapChanged)
    Q_PROPERTY(bool usePerSideOuterGap  READ usePerSideOuterGap WRITE setUsePerSideOuterGap NOTIFY outerGapChanged)

    // Zones + display
    Q_PROPERTY(int  zoneCount       READ zoneCount       NOTIFY zonesChanged)
    Q_PROPERTY(bool showZoneNumbers READ showZoneNumbers WRITE setShowZoneNumbers NOTIFY showZoneNumbersChanged)
    Q_PROPERTY(bool useFullScreenGeometry READ useFullScreenGeometry WRITE setUseFullScreenGeometry
                    NOTIFY useFullScreenGeometryChanged)

    // Shader binding + app-rules
    Q_PROPERTY(QString      shaderId     READ shaderId     WRITE setShaderId     NOTIFY shaderIdChanged)
    Q_PROPERTY(QVariantMap  shaderParams READ shaderParams WRITE setShaderParams NOTIFY shaderParamsChanged)
    Q_PROPERTY(QVariantList appRules     READ appRulesVariant WRITE setAppRulesVariant NOTIFY appRulesChanged)
    Q_PROPERTY(bool         autoAssign   READ autoAssign   WRITE setAutoAssign   NOTIFY autoAssignChanged)

    // Visibility filters
    Q_PROPERTY(int         aspectRatioClass  READ aspectRatioClassInt WRITE setAspectRatioClassInt
                           NOTIFY aspectRatioClassChanged)
    Q_PROPERTY(QStringList allowedScreens    READ allowedScreens    WRITE setAllowedScreens    NOTIFY allowedScreensChanged)
    Q_PROPERTY(QList<int>  allowedDesktops   READ allowedDesktops   WRITE setAllowedDesktops   NOTIFY allowedDesktopsChanged)
    Q_PROPERTY(QStringList allowedActivities READ allowedActivities WRITE setAllowedActivities NOTIFY allowedActivitiesChanged)

public:
    explicit Layout(QObject* parent = nullptr);
    explicit Layout(const QString& name, QObject* parent = nullptr);
    Layout(const Layout& other);
    Layout& operator=(const Layout& other);

    // Zone management (Q_INVOKABLE for QML editor bindings)
    Q_INVOKABLE void  addZone(Zone* zone);           // parent-ownership transfer
    Q_INVOKABLE void  removeZone(Zone* zone);         // deleteLater after renumber
    Q_INVOKABLE void  moveZone(int fromIndex, int toIndex);
    Q_INVOKABLE void  renumberZones();
    QVector<Zone*>    zones() const;
    Zone*             zoneById(const QUuid& id) const;
    Zone*             zoneByNumber(int number) const;

    // Spatial queries
    Q_INVOKABLE Zone*          zoneAtPoint  (const QPointF& point) const;
    Q_INVOKABLE Zone*          nearestZone  (const QPointF& point, qreal maxDistance = -1) const;
    Q_INVOKABLE QVector<Zone*> zonesInRect  (const QRectF& rect) const;
    Q_INVOKABLE QVector<Zone*> adjacentZones(const QPointF& point, qreal threshold = 20) const;

    // Gap-override accessors + raw per-side values for GeometryUtils
    bool hasZonePaddingOverride() const;
    bool hasOuterGapOverride() const;
    ::PhosphorLayout::EdgeGaps rawOuterGaps() const;

    // App-to-zone auto-snap rules
    QVector<AppRule> appRules() const;
    void             setAppRules(const QVector<AppRule>& rules);
    AppRuleMatch     matchAppRule(const QString& windowClass) const;

    // Serialization + factories
    QJsonObject    toJson() const;
    static Layout* fromJson(const QJsonObject& json, QObject* parent = nullptr);
    static Layout* createColumnsLayout     (int columns, QObject* parent = nullptr);
    static Layout* createRowsLayout        (int rows,    QObject* parent = nullptr);
    static Layout* createGridLayout        (int columns, int rows, QObject* parent = nullptr);
    static Layout* createPriorityGridLayout(QObject* parent = nullptr);
    static Layout* createFocusLayout       (QObject* parent = nullptr);

    // Process-global screen-id resolver — see "Injected Interfaces"
    using ScreenIdResolver = std::function<QString(const QString&)>;
    static void             setScreenIdResolver(ScreenIdResolver resolver);
    static ScreenIdResolver screenIdResolver();

    // Geometry recalc + batch modification
    void recalculateZoneGeometries(const QRectF& screenGeometry);
    void beginBatchModify();
    void endBatchModify();
    bool isDirty() const;

Q_SIGNALS:
    void zonesChanged();
    void zoneAdded   (Zone* zone);
    void zoneRemoved (Zone* zone);
    void layoutModified();
    // …one signal per Q_PROPERTY
};

enum class LayoutCategory { Manual = 0, Autotile = 1 };
```

`LayoutCategory::Autotile` is part of the JSON wire format (every manual
layout sets `"category": 0`), not a value PhosphorZones itself ever
constructs — autotile layouts come from `phosphor-tiles`.

#### Ownership model

From `layout.cpp:464-497` — what `addZone` / `removeZone` actually do:

```cpp
void Layout::addZone(Zone* zone) {
    if (zone && !m_zones.contains(zone)) {
        zone->setParent(this);             // parent-ownership transfer
        m_zones.append(zone);
        if (zone->zoneNumber() <= 0) {
            zone->setZoneNumber(m_zones.size());  // next 1-based slot
        }
        m_lastRecalcGeometry = QRectF();   // invalidate geometry cache
        Q_EMIT zoneAdded(zone);
        Q_EMIT zonesChanged();
        emitModifiedIfNotBatched();
    }
}

void Layout::removeZone(Zone* zone) {
    if (zone && m_zones.removeOne(zone)) {
        m_lastRecalcGeometry = QRectF();
        renumberZones();                   // coherent post-state first
        Q_EMIT zoneRemoved(zone);
        zone->deleteLater();               // parent-owned, deferred delete
        Q_EMIT zonesChanged();
        emitModifiedIfNotBatched();
    }
}
```

Raw `Zone*` pointers in a QVector work because the Qt parent-child
graph is the authoritative ownership record. Callers never delete;
`~Layout()` walks its children automatically; `deleteLater` lets
in-flight signal emissions complete before destruction.

#### AppRule / AppRuleMatch

```cpp
struct PHOSPHORZONES_EXPORT AppRule {
    QString pattern;        ///< Case-insensitive segment-aware substring
    int     zoneNumber = 0; ///< 1-based target
    QString targetScreen;   ///< Optional: snap to zone on this screen instead

    bool operator==(const AppRule&) const = default;

    QJsonObject             toJson() const;
    static AppRule          fromJson(const QJsonObject& obj);
    static QVector<AppRule> fromJsonArray(const QJsonArray& array);
};

struct PHOSPHORZONES_EXPORT AppRuleMatch {
    int     zoneNumber = 0;
    QString targetScreen;
    bool matched() const { return zoneNumber > 0; }
};
```

`Layout::matchAppRule` iterates the rule list and delegates pattern
matching to `PhosphorIdentity::WindowId::appIdMatches`
(`layout.cpp:396-413`):

```cpp
// Segment-aware: "firefox" matches "org.mozilla.firefox" (dot-boundary),
// "org.mozilla.firefox" matches "firefox", exact match always works.
// Prevents "fire" from matching "firefox" (no dot boundary).
if (::PhosphorIdentity::WindowId::appIdMatches(windowClass, rule.pattern)) {
    return {rule.zoneNumber, rule.targetScreen};
}
```

PhosphorIdentity is linked **PRIVATE** — it's never named in a public
PhosphorZones header, so consumers of `Layout::matchAppRule` don't
have to include the identity library themselves.

#### Rationale: `AppRule` variant routes through JSON

From `libs/phosphor-zones/src/layout/serialization.cpp:19-22`:

> JSON is the canonical shape for AppRule (disk persistence,
> import/export, wire format). The QVariantList accessors below exist
> only to satisfy the Qt property / QML binding path — they route
> through the JSON form so there is a single source of truth for keys,
> defaults, and validation rules.

Before consolidation, `toJson` and `appRulesVariant` had independent
key constants and field-coercion rules — which drifted (a zone-number
positivity check landed in JSON but not the variant setter and
negative numbers reached downstream code). The fix routes QML through
JSON: `appRulesVariant()` calls `rule.toJson().toVariantMap()`;
`setAppRulesVariant` calls `QJsonObject::fromVariantMap` →
`AppRule::fromJsonArray`. One place decides what a valid rule looks
like.

---

### 3. ZoneDetector — `PhosphorZones::ZoneDetector`

Pure-geometry zone lookup: given a cursor and a `Layout*`, return the
zone(s) a window would snap to. Does not move windows, does not know
about a compositor.

```cpp
class PHOSPHORZONES_EXPORT ZoneDetector : public IZoneDetector {
    Q_OBJECT
    Q_PROPERTY(PhosphorZones::Layout* layout READ layout WRITE setLayout NOTIFY layoutChanged)

public:
    explicit ZoneDetector(QObject* parent = nullptr);

    Layout* layout() const override;
    void    setLayout(Layout* layout) override;

    // Fine-grained injection: daemon wires this to live Settings so users
    // can tune sensitivity without reconstructing the detector. An int
    // (rather than ISettings*) keeps the detector free of the settings layer.
    void setAdjacentThreshold(int px);
    int  adjacentThreshold() const;

    Q_INVOKABLE ZoneDetectionResult detectZone     (const QPointF&) const override;
    Q_INVOKABLE ZoneDetectionResult detectMultiZone(const QPointF&) const override;
    Q_INVOKABLE Zone*               zoneAtPoint    (const QPointF&) const override;
    Q_INVOKABLE Zone*               nearestZone    (const QPointF&) const override;

    /// Paint-to-snap: expand seeds to all zones intersecting the bounding
    /// rect (same raycasting as editor multi-select).
    Q_INVOKABLE QVector<Zone*> expandPaintedZonesToRect(const QVector<Zone*>&) const override;

    // Delegates to private ZoneHighlighter (backward-compat shims).
    Q_INVOKABLE void highlightZone  (Zone*)                    override;
    Q_INVOKABLE void highlightZones (const QVector<Zone*>&)     override;
    Q_INVOKABLE void clearHighlights()                          override;
};

struct PHOSPHORZONES_EXPORT ZoneDetectionResult {
    Zone*          primaryZone      = nullptr;
    QVector<Zone*> adjacentZones;            ///< For multi-zone snap
    QVector<Zone*> overlappingZones;         ///< All zones containing the cursor
    QRectF         snapGeometry;             ///< Combined rect
    qreal          distance          = -1;
    bool           isMultiZone       = false;
};
```

The detector holds a raw `Layout*` plus a `destroyed` signal connection
that clears the pointer and emits `layoutChanged` on layout destruction
— callers can delete the `Layout` without dangling.

---

### 4. ZoneHighlighter — `PhosphorZones::ZoneHighlighter`

UI-state companion to `ZoneDetector`. Holds "which zones are currently
highlighted" as pure state + change signals, split out so detection
logic stays free of UI lifecycle.

```cpp
class PHOSPHORZONES_EXPORT ZoneHighlighter : public QObject {
    Q_OBJECT
public:
    explicit ZoneHighlighter(QObject* parent = nullptr);

    Q_INVOKABLE void highlightZone  (Zone* zone);
    Q_INVOKABLE void highlightZones (const QVector<Zone*>& zones);
    Q_INVOKABLE void clearHighlights();

Q_SIGNALS:
    void zoneHighlighted (Zone* zone);
    void zonesHighlighted(const QVector<Zone*>& zones);
    void highlightsCleared();
};
```

`ZoneDetector` owns a `unique_ptr<ZoneHighlighter>` and forwards
signals for backward compatibility with older call sites that only
held a detector pointer.

---

### 5. ZonesLayoutSource — `PhosphorZones::ZonesLayoutSource`

`PhosphorLayout::ILayoutSource` adapter over an `ILayoutRegistry`.
Editor / settings / overlay code renders manual-layout previews
alongside autotile-algorithm previews — both satisfy the same
interface.

```cpp
class PHOSPHORZONES_EXPORT ZonesLayoutSource : public PhosphorLayout::ILayoutSource {
    Q_OBJECT
public:
    /// @param registry Borrowed — caller owns it and must keep it
    ///                 alive for this source's lifetime.
    explicit ZonesLayoutSource(PhosphorZones::ILayoutRegistry* registry, QObject* parent = nullptr);

    QVector<PhosphorLayout::LayoutPreview> availableLayouts() const override;

    /// @p windowCount is ignored — manual layouts have authored zones.
    /// @p canvas threads through to previewFromLayout so fixed-pixel
    ///           zones project deterministically per-call.
    PhosphorLayout::LayoutPreview previewAt(const QString& id,
                                            int windowCount = PhosphorLayout::DefaultPreviewWindowCount,
                                            const QSize& canvas = {}) override;

public Q_SLOTS:
    /// Caller-driven re-emit of contentsChanged.
    void notifyContentsChanged();
};

/// Free projector — useful when the caller already holds a Layout*.
PHOSPHORZONES_EXPORT PhosphorLayout::LayoutPreview previewFromLayout(
    PhosphorZones::Layout* layout, const QSize& canvas = {});
```

#### Why `notifyContentsChanged` is wired manually

Because `ILayoutRegistry` is not a QObject, it cannot emit
`contentsChanged` directly. Consumers wire it up in the composition
root:

```cpp
connect(layoutManager, &LayoutManager::layoutsChanged,
        zonesSource,   &ZonesLayoutSource::notifyContentsChanged);
```

One `connect` per adapter, in exchange for removing an entire class of
signal-shadowing bugs from the sibling-interface hierarchy. This is
the intended consequence of the "interfaces-have-no-signals" decision.

#### Canvas parameter rules

Straight from `zoneslayoutsource.cpp:37-43`:

```cpp
const QRectF refGeo = !canvas.isEmpty()
    ? QRectF(0, 0, canvas.width(), canvas.height())
    : (layout->hasFixedGeometryZones() ? layout->lastRecalcGeometry() : QRectF());
```

Caller-supplied canvas wins — the preview is deterministic per call.
Fallback is `Layout::lastRecalcGeometry()`, which is stale if two
screens share a `Layout*` and query alternately. Overlay-renderer
callers pass their canvas; the fallback is the historical
single-screen path.

---

### 6. ZoneDefaults — `PhosphorZones::ZoneDefaults`

Canonical defaults. Single source of truth shared between the library's
zone constructors and the KCM's "what a zone looks like by default"
display.

```cpp
namespace ZoneDefaults {

// Alpha
constexpr int HighlightAlpha = 128;
constexpr int InactiveAlpha  = 64;
constexpr int BorderAlpha    = 200;
constexpr int OpaqueAlpha    = 255;

// Colors — `inline` so they're defined exactly once across TUs
inline const QColor HighlightColor{0, 120, 212, HighlightAlpha}; ///< Windows blue
inline const QColor InactiveColor {128, 128, 128, InactiveAlpha};
inline const QColor BorderColor   {255, 255, 255, BorderAlpha};
inline const QColor LabelFontColor{255, 255, 255, OpaqueAlpha};

// Per-zone appearance
constexpr qreal Opacity         = 0.5;
constexpr qreal InactiveOpacity = 0.3;
constexpr int   BorderWidth     = 2;
constexpr int   BorderRadius    = 8;

// Detection
constexpr int AdjacentThreshold = 20; ///< Adjacency threshold (px)

// Layout-factory split ratios
constexpr qreal PriorityGridMainRatio      = 0.667;
constexpr qreal PriorityGridSecondaryRatio = 0.333;
constexpr qreal FocusSideRatio             = 0.2;
constexpr qreal FocusMainRatio             = 0.6;

} // namespace ZoneDefaults
```

Same pattern as `PhosphorTiles::AutotileDefaults`. The application's
config layer delegates its user-facing zone-default accessors downward
to these.

---

### 7. ZoneJsonKeys — `PhosphorZones::ZoneJsonKeys`

Wire-format keys for zone / layout serialisation. ~35 constexpr
`QLatin1String` keys covering zone identity (`Id`, `Name`,
`ZoneNumber`, `RelativeGeometry`), per-zone appearance
(`HighlightColor`, `BorderWidth`, `BorderRadius`, …), layout-level
metadata (`Zones`, `ZonePadding`, `OuterGap`, `Category`, `ShaderId`),
app-rule fields (`AppRules`, `Pattern`, `TargetScreen`, `AutoAssign`),
visibility allow-lists (`AllowedScreens`, `AllowedDesktops`,
`AllowedActivities`), and geometry mode (`GeometryMode`,
`FixedGeometry`, `UseFullScreenGeometry`).

Per-side outer-gap keys are re-exported from phosphor-layout-api's
`GapKeys.h` (shared canonical definition with phosphor-tiles):

```cpp
using PhosphorLayout::GapKeys::OuterGapTop;
using PhosphorLayout::GapKeys::OuterGapBottom;
using PhosphorLayout::GapKeys::OuterGapLeft;
using PhosphorLayout::GapKeys::OuterGapRight;
using PhosphorLayout::GapKeys::UsePerSideOuterGap;
```

Owning the zone/layout keys here keeps the wire format under the
library's source control. Keys that aren't part of the zone/layout file
format proper (window-assignment runtime state, virtual-screen
configuration, autotile overrides) live with their owners — the header
comment makes the split explicit.

---

### 8. LayoutUtils — `PhosphorZones::LayoutUtils`

Pure `Layout` / `Zone` operations shared across multiple surfaces.

```cpp
enum class ZoneField {
    None       = 0,
    Name       = 1 << 0,
    Appearance = 1 << 1,  ///< colors + opacities + border props

    Minimal = None,
    Full    = Name | Appearance,
};
Q_DECLARE_FLAGS(ZoneFields, ZoneField)

namespace LayoutUtils {

PHOSPHORZONES_EXPORT void serializeAllowLists(QJsonObject& json,
    const QStringList& screens, const QList<int>& desktops, const QStringList& activities);

PHOSPHORZONES_EXPORT void deserializeAllowLists(const QJsonObject& json,
    QStringList& screens, QList<int>& desktops, QStringList& activities);

PHOSPHORZONES_EXPORT QVariantList zonesToVariantList(Layout* layout,
    ZoneFields fields = ZoneField::Minimal, const QRectF& referenceGeometry = QRectF());

PHOSPHORZONES_EXPORT QVariantMap layoutToVariantMap(Layout* layout,
    ZoneFields zoneFields = ZoneField::Minimal);

} // namespace LayoutUtils
```

`Minimal` vs `Full` lets preview thumbnails avoid serializing 14
per-zone colour fields the renderer will immediately throw away.

---

## Injected Interfaces

### ILayoutRegistry

Enumeration + mutation of the in-memory layout set.

```cpp
class PHOSPHORZONES_EXPORT ILayoutRegistry {
public:
    virtual ~ILayoutRegistry();

    virtual QVector<Layout*> layouts() const = 0;         // borrowed
    virtual int              layoutCount() const = 0;
    virtual Layout*          layout(int index) const = 0;
    virtual Layout*          layoutByName(const QString& name) const = 0;
    virtual Layout*          layoutById(const QUuid& id) const = 0;

    virtual void     addLayout   (Layout* layout) = 0;   ///< ownership transferred
    virtual void     removeLayout(Layout* layout) = 0;   ///< borrowed, caller retains
    virtual void     removeLayoutById(const QUuid& id) = 0;
    virtual Layout*  duplicateLayout (Layout* source) = 0;

    virtual Layout* activeLayout() const = 0;
    virtual void    setActiveLayout    (Layout* layout) = 0;
    virtual void    setActiveLayoutById(const QUuid& id) = 0;
};
```

### ILayoutAssignments

Per-(screen, desktop, activity) routing + default-layout fallback.

```cpp
class PHOSPHORZONES_EXPORT ILayoutAssignments {
public:
    virtual ~ILayoutAssignments();

    virtual Layout* defaultLayout() const = 0;
    virtual int     currentVirtualDesktop() const = 0;
    virtual QString currentActivity() const = 0;

    /// Convenience — resolves for current desktop/activity with default fallback.
    Layout* resolveLayoutForScreen(const QString& screenId) const;

    virtual Layout* layoutForScreen(const QString& screenId, int virtualDesktop = 0,
                                    const QString& activity = QString()) const = 0;

    virtual void assignLayout    (const QString& screenId, int vd, const QString& act, Layout* layout) = 0;
    virtual void assignLayoutById(const QString& screenId, int vd, const QString& act, const QString& layoutId) = 0;
    virtual void clearAssignment (const QString& screenId, int vd = 0, const QString& act = QString()) = 0;
    virtual bool hasExplicitAssignment(const QString& screenId, int vd = 0, const QString& act = QString()) const = 0;

    /// Raw id — manual UUID (braced) or "autotile:<algorithmId>". Lets
    /// callers branch on assignment kind before resolving.
    virtual QString assignmentIdForScreen(const QString& screenId, int vd = 0, const QString& act = QString()) const = 0;

    // Batch setters — save once at end.
    virtual void setAllScreenAssignments  (const QHash<QString, QString>&) = 0;
    virtual void setAllDesktopAssignments (const QHash<QPair<QString, int>, QString>&) = 0;
    virtual void setAllActivityAssignments(const QHash<QPair<QString, QString>, QString>&) = 0;
};
```

Assignment keys are triples (screenId, virtualDesktop, activity).
`virtualDesktop == 0` means any desktop; empty `activity` means any
activity. Narrower keys win per the concrete manager's resolution
order.

### IQuickLayouts

Numbered shortcut slots (1–9).

```cpp
class PHOSPHORZONES_EXPORT IQuickLayouts {
public:
    virtual ~IQuickLayouts();

    virtual Layout* layoutForShortcut(int number) const = 0;
    virtual void    applyQuickLayout (int number, const QString& screenId) = 0;
    virtual void    setQuickLayoutSlot(int number, const QString& layoutId) = 0;
    virtual void    setAllQuickLayoutSlots(const QHash<int, QString>& slots) = 0;
    virtual QHash<int, QString> quickLayoutSlots() const = 0;
};
```

Slots store layout IDs — UUID strings for manual layouts,
`"autotile:<algorithmId>"` for autotile algorithms.

### IBuiltInLayouts

System-installed templates.

```cpp
class PHOSPHORZONES_EXPORT IBuiltInLayouts {
public:
    virtual ~IBuiltInLayouts();
    virtual void             createBuiltInLayouts() = 0;   ///< idempotent
    virtual QVector<Layout*> builtInLayouts() const = 0;
};
```

### ILayoutPersistence

Disk I/O and import/export. Non-reactive — no change signals. Callers
that watch the layout directory subscribe on the concrete
`LayoutManager`.

```cpp
class PHOSPHORZONES_EXPORT ILayoutPersistence {
public:
    virtual ~ILayoutPersistence();

    virtual QString layoutDirectory() const = 0;
    virtual void    setLayoutDirectory(const QString& directory) = 0;

    virtual void loadLayouts() = 0;
    virtual void saveLayouts() = 0;
    virtual void saveLayout  (Layout* layout) = 0;   ///< single-layout save

    virtual void loadAssignments() = 0;
    virtual void saveAssignments() = 0;

    virtual void importLayout(const QString& filePath) = 0;
    virtual void exportLayout(Layout* layout, const QString& filePath) = 0;
};
```

### ILayoutManager — the umbrella

```cpp
class PHOSPHORZONES_EXPORT ILayoutManager : public ILayoutRegistry,
                                            public ILayoutAssignments,
                                            public IQuickLayouts,
                                            public IBuiltInLayouts,
                                            public ILayoutPersistence {
public:
    ILayoutManager() = default;
    ~ILayoutManager() override;
};
```

Use this only when a caller genuinely exercises every sibling (D-Bus
adaptors that route to every capability); prefer narrower interfaces
everywhere else.

### IZoneDetector / IZoneDetection

Split for ISP — pure queries vs mutating highlight management.

```cpp
// Non-QObject pure-query slice
class PHOSPHORZONES_EXPORT IZoneDetection {
public:
    virtual ~IZoneDetection();
    virtual Layout*             layout() const = 0;
    virtual ZoneDetectionResult detectZone     (const QPointF&) const = 0;
    virtual ZoneDetectionResult detectMultiZone(const QPointF&) const = 0;
    virtual Zone*               zoneAtPoint    (const QPointF&) const = 0;
    virtual Zone*               nearestZone    (const QPointF&) const = 0;
    virtual QVector<Zone*>      expandPaintedZonesToRect(const QVector<Zone*>& seeds) const = 0;
};

// QObject — highlights are reactive
class PHOSPHORZONES_EXPORT IZoneDetector : public QObject, public IZoneDetection {
    Q_OBJECT
public:
    explicit IZoneDetector(QObject* parent = nullptr);
    virtual void setLayout       (Layout* layout) = 0;
    virtual void highlightZone   (Zone* zone) = 0;
    virtual void highlightZones  (const QVector<Zone*>& zones) = 0;
    virtual void clearHighlights () = 0;
Q_SIGNALS:
    void layoutChanged();
    void zoneHighlighted(Zone* zone);
    void highlightsCleared();
};
```

### ScreenIdResolver — the one process-global hook

```cpp
// Static members on Layout
using ScreenIdResolver = std::function<QString(const QString&)>;
static void             setScreenIdResolver(ScreenIdResolver resolver);
static ScreenIdResolver screenIdResolver();
```

**Why it exists.** Layout files persist `allowedScreens` entries as
strings. Some files carry legacy Wayland connector names (`"DP-2"`,
`"HDMI-A-1"`) — names that change when the user reseats a cable or
swaps GPUs. Newer files store stable EDID-based identifiers
(`"LG:UltraFine:12345"`). The library normalizes on read:
`Layout::fromJson` calls the resolver for every entry, so in-memory
layouts always hold the stable form regardless of what the file
stores.

**Why it's static.** The resolver typically needs a live
`QGuiApplication` to enumerate connected `QScreen`s. Plumbing a
resolver pointer through every `Layout::fromJson` call site would
force headless tools — which don't have a `QGuiApplication` and don't
care — to wire a stub. The default-unset static design lets tests
leave it alone and get verbatim string storage.

**Thread safety.** `fromJson` may run off-thread (the
`QFileSystemWatcher` worker path). The resolver is a static
`std::function` guarded by a mutex; readers get a copy of the function
rather than a reference so a concurrent `setScreenIdResolver()` cannot
pull the rug from under an in-flight resolution. Install from daemon /
editor / settings / KCM processes; leave default in tests.

---

## Composition Example

What PlasmaZones' daemon writes to put the pieces together:

```cpp
#include <PhosphorZones/ILayoutManager.h>
#include <PhosphorZones/ZoneDetector.h>
#include <PhosphorZones/ZonesLayoutSource.h>

// 1. Install the screen-id resolver so Layout::fromJson can normalize
//    legacy connector names to stable EDIDs.
Layout::setScreenIdResolver([screenInfo = m_screenInfoService](const QString& raw) {
    return screenInfo->resolveStableId(raw);
});

// 2. The concrete LayoutManager implements ILayoutManager — owned by
//    the daemon, not the library.
auto* manager = new LayoutManager(settings, screenInfo, this);
manager->loadLayouts();
manager->loadAssignments();

// 3. Detector — takes the active layout directly, not the whole manager.
auto* detector = new ZoneDetector(this);
detector->setLayout(manager->activeLayout());
detector->setAdjacentThreshold(settings->adjacentThreshold());
connect(settings, &ISettings::adjacentThresholdChanged,
        detector, &ZoneDetector::setAdjacentThreshold);

// 4. ILayoutSource adapter — wire notifyContentsChanged manually
//    because ILayoutRegistry is not a QObject and cannot emit signals.
auto* zonesSource = new ZonesLayoutSource(manager, this);
connect(manager, &LayoutManager::layoutsChanged,
        zonesSource, &ZonesLayoutSource::notifyContentsChanged);

// 5. Preview renderer consumes the source through the phosphor-layout-api
//    contract — it doesn't know whether the source is manual or autotile.
m_pickerModel->addSource(zonesSource);
m_pickerModel->addSource(new PhosphorTiles::AutotileLayoutSource(autotileEngine, this));
```

Editor / settings / KCM composition is narrower — they typically need
`ILayoutRegistry` for enumeration + `ILayoutPersistence` for
save/load, and that's all their fixtures stub.

---

## Threading Model

- **Zone / Layout**: main (GUI) thread only. QObjects with signal
  emissions and parent-child graphs — not safe for concurrent touch.
- **`Zone::computeAbsoluteGeometry` (static)**: thread-safe pure
  function. Exists specifically so the application's off-thread
  `LayoutWorker` can compute rectangles without touching the QObject
  tree.
- **`Layout::fromJson`**: may run off-thread (the
  `QFileSystemWatcher` worker). The allocated `Layout*` is handed
  back to the main thread via queued signal — typical Qt
  ownership-handoff pattern.
- **`Layout::setScreenIdResolver` / `screenIdResolver`**: guarded by
  a library-private `std::mutex`. Readers get a copy of the
  `std::function` so they can invoke without holding the lock.
- **`ZoneDetector` / `ZonesLayoutSource`**: main thread only.
  `notifyContentsChanged` may be called via `Qt::QueuedConnection`
  from whatever thread the registry lives on.

No public API performs its own thread hopping. Consumers that need to
cross a thread boundary do so with queued connections they own.

---

## Testing Strategy

All abstract interfaces have mock-friendly footprints — the five-way
`ILayoutManager` split was sized specifically for this. A persistence
test stubs four methods, not thirty.

```cpp
class MockLayoutRegistry : public PhosphorZones::ILayoutRegistry {
public:
    QVector<PhosphorZones::Layout*> layouts() const override { return m_layouts; }
    PhosphorZones::Layout* layoutById(const QUuid& id) const override {
        auto it = std::find_if(m_layouts.begin(), m_layouts.end(),
                               [&](auto* l) { return l && l->id() == id; });
        return it == m_layouts.end() ? nullptr : *it;
    }
    // …other methods — all one-liners against QVector<Layout*>
    QVector<PhosphorZones::Layout*> m_layouts;
};

void test_zones_source_round_trip() {
    MockLayoutRegistry reg;
    auto* layout = PhosphorZones::Layout::createGridLayout(2, 2);
    reg.m_layouts.append(layout);

    PhosphorZones::ZonesLayoutSource src(&reg);
    auto previews = src.availableLayouts();
    QCOMPARE(previews.size(), 1);
    QCOMPARE(previews[0].zoneCount, 4);
    QCOMPARE(previews[0].zoneNumbers, (QList<int>{1, 2, 3, 4}));
}
```

### Coverage targets

- Zone geometry modes: Fixed vs Relative projection,
  `normalizedGeometry` round-trip, cache invalidation on mode switch
- Layout zone ownership: parent-child graph correctness,
  renumber-on-remove, `deleteLater` timing
- App-rule pattern matching: `firefox` matches `org.mozilla.firefox`
  but `fire` does not; empty pattern and zero zone-number rejected
- JSON round-trip: every `Q_PROPERTY` survives `toJson` → `fromJson`
  with no loss; unknown keys tolerated
- Screen-id resolver: installed resolver runs on `fromJson`, unset
  resolver passes strings verbatim, concurrent
  set-during-resolve is race-free
- `ZoneDetector::setLayout`: null assignment, layout-destroyed
  auto-clear, `layoutChanged` emitted exactly once per real change
- `ZonesLayoutSource` canvas: caller-supplied canvas wins over
  `lastRecalcGeometry`; two alternating callers on different canvases
  never poison each other's preview

Existing coverage in `tests/unit/core/test_layout_*.cpp` and
`test_zone_*.cpp` (pre-extraction) exercises most of this. Moving
those tests to `libs/phosphor-zones/tests/` is follow-up.

---

## Migration Path

PhosphorZones has already been extracted from `src/core/` — the
library compiles, PlasmaZones links it. What's left is incremental
cleanup and test-harness migration.

### Done

- Move `zone.*`, `layout.*`, `zonedetector.*`, `zonehighlighter.*`,
  `layoututils.*` from `src/core/` to `libs/phosphor-zones/src/`.
- Move `ILayoutManager` + the five siblings out of
  `src/core/interfaces.h` into per-interface headers.
- Split the umbrella into five siblings (was: monolithic 30-method
  `ILayoutManager`; now: five narrower contracts).
- Move `IZoneDetector` + `ZoneDetectionResult` out of the umbrella
  `interfaces.h` so zones code includes just what it needs.
- Consolidate `AppRule` variant/JSON serialization through a single
  JSON canonical form.
- Relocate `ZoneDefaults` and `ZoneJsonKeys` to library-owned headers.
- Extract `ZonesLayoutSource` (the `ILayoutSource` adapter).
- Wire `PhosphorIdentity::WindowId::appIdMatches` as a PRIVATE link —
  segment-aware pattern matching without leaking identity into the
  public API surface.

### Remaining

- Move `tests/unit/core/test_layout_*.cpp` and `test_zone_*.cpp` to
  `libs/phosphor-zones/tests/`, keeping Qt Test as the harness.
- Add library-private mocks for each sibling interface (currently
  scattered as local subclasses across the application test tree).
- Publish `PhosphorZonesConfig.cmake` to the install prefix so
  downstream tools can `find_package(PhosphorZones)`.
- Drop the in-tree `add_subdirectory` fallback once the library ships
  as a proper distro package.

---

## Rejected Alternatives (with rationale)

### Single monolithic `ILayoutManager`

Rejected after a fixture-test sprawl audit. Mocking 30 methods to run
a three-assertion test was visibly costly, and keeping "does this
method belong here?" consistent across four capability groupings
hiding inside one interface was worse. The five-way ISP split maps to
real capability boundaries — each sibling has at least one caller
that uses *only* that surface.

### QObject interfaces with signals

Rejected because of signal shadowing. If `ILayoutManager` declared a
signal and `LayoutManager` re-declared it (even just to narrow
parameter types, which `moc` sometimes pattern-matches as distinct),
both entries landed in the metaobject and function-pointer `connect`s
resolved to different slots depending on which pointer they were made
through. On emit, one stomped the other's stack frame. The exact
write-up is in `ILayoutManager.h:18-23`.

### Virtual inheritance in `ILayoutManager`

Rejected: costs an extra indirection with no benefit. The five
siblings define no member state, so the diamond doesn't share
anything. Non-virtual inheritance keeps free upcasts; the tradeoff
(no `static_cast` downcast) is paid by the tiny population of call
sites that need it — they reach for `dynamic_cast` or restructure to
hold the wider pointer. The invariant is documented in the header.

### Embedded `IScreenInfoService*` on `Layout`

Rejected: couples every Layout to a service that only matters during
deserialization. The static `ScreenIdResolver` hook is set once at
startup and consulted only in `fromJson` — no per-instance plumbing,
no Layout-to-service lifetime knot, and headless code gets
string-verbatim behaviour by leaving it unset.

### Separate validation on the `QVariantList` surface

Rejected. Two copies drifted once already (zone-number positivity
check landed in JSON but not the variant setter, causing downstream
crashes). Consolidating on JSON as the canonical form with the
variant surface as a thin adapter puts validation in exactly one
place.

### Shared ownership (`shared_ptr<Zone>`) inside Layout

Rejected. The Qt parent-child graph is the authoritative ownership
model. `zone->setParent(layout); m_zones.append(zone);` is two lines
that make ownership visible at every touch point; `~Layout()` walks
its children automatically; `QObject::destroyed` gives callers
weak-reference semantics without reference-counting overhead.
`shared_ptr<QObject>` duplicates what the parent graph already
provides and makes signal-slot disconnect timing ambiguous.

### Autotile concepts inside `Layout`

Rejected. An earlier spike tried to treat autotile as "a layout with
zero authored zones and a computed zone list" — it bled autotile
engine state into Layout and forced every manual-layout consumer to
learn about tiling algorithms. The current split puts autotile in
`phosphor-tiles`, exposing an `AutotileLayoutSource : ILayoutSource`
that the picker treats identically to `ZonesLayoutSource`.

---

## Extensions (shipped in v0.1.0)

- **Built-in layout factories** — `createColumnsLayout`,
  `createRowsLayout`, `createGridLayout`, `createPriorityGridLayout`,
  `createFocusLayout`. Split ratios live in `ZoneDefaults` so the KCM
  preview uses the same numbers.
- **Aspect-ratio classification** — `Layout::matchesAspectRatio`,
  backed by either explicit min/max bounds or a
  `PhosphorLayout::AspectRatioClass` enum. Lets the picker surface
  "layouts suitable for this ultrawide" without recomputing filters
  at render time.
- **Batch modification** — `Layout::beginBatchModify` /
  `endBatchModify` suppress the per-setter `layoutModified` storm
  during multi-field edits (e.g. 20 zone tweaks from a JSON paste).
- **Cached system-layout classification** — `isSystemLayout()` is
  O(1) because `setSourcePath` precomputes the `QStandardPaths`
  lookup. QML preview rebuilds hit it hard.
- **Paint-to-snap expansion** —
  `IZoneDetection::expandPaintedZonesToRect`: painted seeds plus any
  zone intersecting the bounding rect, via the same raycasting
  algorithm the editor's multi-select uses.

---

## Directory Layout

```
libs/phosphor-zones/
├── CMakeLists.txt
├── PhosphorZonesConfig.cmake.in
├── include/
│   └── PhosphorZones/
│       ├── PhosphorZones.h          (umbrella)
│       ├── Zone.h
│       ├── Layout.h                  (AppRule, LayoutCategory, ScreenIdResolver)
│       ├── ZoneDetector.h
│       ├── ZoneHighlighter.h
│       ├── ZonesLayoutSource.h
│       ├── ZoneDefaults.h
│       ├── ZoneJsonKeys.h
│       ├── LayoutUtils.h
│       ├── IZoneDetector.h           (IZoneDetection + IZoneDetector)
│       ├── ILayoutRegistry.h
│       ├── ILayoutAssignments.h
│       ├── IQuickLayouts.h
│       ├── IBuiltInLayouts.h
│       ├── ILayoutPersistence.h
│       └── ILayoutManager.h           (umbrella — inherits all five)
├── src/
│   ├── zone.cpp
│   ├── layout.cpp                     (core QObject + matchAppRule)
│   ├── layout/
│   │   ├── factories.cpp              (createColumns / Rows / Grid / …)
│   │   └── serialization.cpp          (to/fromJson + AppRule helpers)
│   ├── zonedetector.cpp
│   ├── zonehighlighter.cpp
│   ├── zoneslayoutsource.cpp
│   ├── layoututils.cpp
│   ├── interfaces.cpp                 (out-of-line virtual destructors —
│   │                                  anchors vtables in this TU)
│   └── zoneslogging.h / .cpp          (lcZonesLib logging category)
└── tests/                             (follow-up — currently in
                                        tests/unit/core/)
```

---

## Open Questions

Resolved during extraction:

- Umbrella vs split interfaces → split (five siblings + one umbrella
  for true polyglot callers)
- QObject interfaces vs non-QObject abstracts → non-QObject; signal
  shadowing is a hard blocker
- AppRule canonical form → JSON, with a QVariant adapter routing
  through it
- How `Layout` owns `Zone` → Qt parent-ownership, raw pointers,
  `deleteLater` on removal
- `ScreenIdResolver` scope → process-global static, mutex-guarded,
  default-unset
- PhosphorIdentity dep visibility → PRIVATE; identity types never
  appear in public headers

Deferred:

- Whether `ZonesLayoutSource` should batch `contentsChanged`
  (coalesce rapid `notifyContentsChanged` calls) or rely on the
  consumer's connection. Current answer: consumer decides — connect
  `Qt::QueuedConnection` or add a `QTimer::singleShot(0, …)` hop if
  coalescing is wanted.
- Moving `LayoutWorker` (off-thread geometry recalc, currently in
  `src/core/`) into this library. Depends on whether any other
  consumer wants the off-thread pipeline.
- Tests-directory migration sequencing — the existing
  `tests/unit/core/test_layout_*.cpp` suite is still wired into the
  application's `tests/unit/CMakeLists.txt`. Moving them cleanly
  needs the library's own test harness first.
- Whether `ZoneDefaults` values should expose runtime setters so the
  KCM's "pick custom default" UI can overwrite them at runtime.
  Current position: no — runtime-configurable defaults belong in the
  application's `ISettings` layer, which reads the library's defaults
  as the seed value. Exposing setters here would make defaults
  non-constant across processes and break the single-source-of-truth
  invariant.
