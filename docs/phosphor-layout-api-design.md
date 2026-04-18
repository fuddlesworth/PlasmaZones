# PhosphorLayoutApi — API Design Document

## Overview

PhosphorLayoutApi is a small Qt6/C++20 library defining the shared *shape*
that every layout-producing component in the PlasmaZones ecosystem speaks. It
is the root of the `phosphor-*` dependency graph: the daemon, the editor, the
settings app, `phosphor-zones` (manual zone layouts), and `phosphor-tiles`
(autotile algorithms) all agree on a single renderer-ready preview type,
a single reactive enumeration interface, and a handful of common value types
(edge gaps, aspect-ratio classification, JSON key strings, layout-id
namespacing).

The library contains no rendering, no policy, and no domain logic. Its
responsibility is to be the *contract* — a header-light surface that two
completely separate producer libraries (manual layouts, autotile algorithms)
can implement and that three completely separate consumers (daemon, editor,
settings) can program against without branching on `isAutotile` and without
depending on either producer directly.

**License:** LGPL-2.1-or-later (matches the rest of the `phosphor-*`
libraries)

**Namespace:** `PhosphorLayout`

**Depends on:** Qt6::Core only (PUBLIC). No Qt Gui. No Qt Quick. No
Wayland. Nothing else.

**Size:** ~250 lines of C++ across eight public headers and two small
translation units. Deliberately so — the contract should stay small and
stable.

---

## Dependency Graph

PhosphorLayoutApi is the base layer. Every sibling library that produces or
consumes `LayoutPreview` values depends on it.

```
                   ┌──────────────────────┐
                   │  PhosphorLayoutApi   │
                   │  (LayoutPreview,     │
                   │   ILayoutSource,     │
                   │   EdgeGaps, LayoutId,│
                   │   AspectRatioClass)  │
                   └──────┬──────┬────────┘
                          │      │   PUBLIC
                          │      │
             PUBLIC ┌─────┘      └─────┐ PUBLIC
                    ▼                   ▼
         ┌────────────────┐   ┌────────────────┐
         │ phosphor-zones │   │ phosphor-tiles │
         │  (manual       │   │  (autotile     │
         │   layouts +    │   │   algorithms + │
         │   Zones-       │   │   Autotile-    │
         │   LayoutSource)│   │   LayoutSource)│
         └───────┬────────┘   └────────┬───────┘
                 │                     │
                 └──────────┬──────────┘
                            ▼
                ┌───────────────────────┐
                │  PlasmaZones daemon / │
                │  editor / settings    │
                │  (holds ILayoutSource*)│
                └───────────────────────┘

      phosphor-identity — sibling library, does NOT depend on
      phosphor-layout-api (window-id concerns are orthogonal to
      layout-preview concerns).
```

`phosphor-zones` links PhosphorLayoutApi PUBLIC because the source-adapter
class `ZonesLayoutSource` inherits `PhosphorLayout::ILayoutSource` and
returns `PhosphorLayout::LayoutPreview` through its public headers.
`phosphor-tiles` does the same for `AutotileLayoutSource`. Consumers of
either library automatically see the PhosphorLayoutApi headers they need.

`phosphor-identity` is a sibling at the same level but has no layout-preview
concerns; it deliberately keeps its dependency surface minimal and does
*not* pull in PhosphorLayoutApi.

---

## Design Principles

1. **Contract, not implementation.** The library defines shapes
   (`LayoutPreview`, `ILayoutSource`) and constants (wire-format key
   strings, aspect-ratio thresholds). It implements exactly one
   convenience type (`CompositeLayoutSource`) whose sole job is to
   aggregate other sources. Everything else is for implementers.
2. **Minimal transitive surface.** Qt6::Core only. Pulling PhosphorLayoutApi
   into a translation unit costs `<QObject>`, `<QRectF>`, `<QString>`,
   `<QVector>`, and a handful of std headers — nothing more. The KCM, the
   editor's QML plugin, and the daemon all tolerate this transitively;
   Qt Gui / Qt Quick would be unacceptable.
3. **Single namespace for layout IDs.** Manual layouts use UUID strings;
   autotile algorithms use the prefixed form `"autotile:<algorithmId>"`.
   One flat keyspace means a consumer holding an `ILayoutSource*` can
   pass an id from `availableLayouts()` straight back into `previewAt()`
   without knowing which underlying producer minted it.
4. **Reactive but not auto-wired.** `ILayoutSource` is a `QObject` that
   emits `contentsChanged`, but the library does not arrange for the
   signal to fire automatically. Producers expose a public slot (or
   free method) that callers invoke from the underlying domain-model
   change signal. The split is deliberate (see the ILayoutSource
   section below for the full rationale).
5. **Plain data over objects.** `LayoutPreview` is a struct. No Q_OBJECT,
   no QML registration, no lifecycle. It moves through `QVector`,
   serialises trivially, and can be cached without worrying about parent
   ownership.
6. **One source of truth for cross-library strings and thresholds.** JSON
   key strings (`GapKeys`), the `"autotile:"` prefix (`LayoutId`), and
   aspect-ratio boundary constants (`ScreenClassification`) all live
   here so no two sibling libraries can disagree on them.
7. **No generated-file pollution in public includes.** The export header
   (`phosphorlayoutapi_export.h`) is generated into the build directory
   and installed alongside the public headers, but consumer code only
   touches it transitively through the `PHOSPHORLAYOUTAPI_EXPORT` macro
   on the two symbols that need it (`ILayoutSource`,
   `CompositeLayoutSource`).

---

## Public API

### 1. `LayoutPreview` — the renderer-ready shape

`<PhosphorLayoutApi/LayoutPreview.h>`

A plain struct. Both `phosphor-zones` (manual `Layout` → preview) and
`phosphor-tiles` (autotile algorithm @ N windows → preview) produce values
of this type. Editor / settings / overlay code consumes `LayoutPreview`
uniformly — the only renderer-relevant difference between manual and
autotile is whether `algorithm` is populated.

```cpp
namespace PhosphorLayout {

inline constexpr int DefaultPreviewWindowCount = 4;

struct LayoutPreview
{
    static constexpr int UnlimitedZoneCount = 0;

    QString id;                            // UUID (manual) or "autotile:<algId>"
    QString displayName;
    QString description;

    QVector<QRectF> zones;                 // 0.0–1.0 relative coordinates
    QVector<int>    zoneNumbers;           // parallel to zones, or empty

    int   zoneCount = 0;                   // UnlimitedZoneCount == 0 == "no cap"
    bool  recommended = true;
    qreal referenceAspectRatio = 0.0;      // 0 = adapts to any canvas
    AspectRatioClass aspectRatioClass = AspectRatioClass::Any;

    // Picker section-grouping metadata
    QString sectionKey;
    QString sectionLabel;
    int     sectionOrder = 0;

    bool autoAssign = false;               // daemon-side hint; renderer ignores
    bool isSystem   = false;               // drives the lock badge

    std::optional<AlgorithmMetadata> algorithm;

    bool isAutotile() const noexcept { return algorithm.has_value(); }
    bool isValid() const noexcept;         // structural consistency check
};

} // namespace PhosphorLayout
```

Coordinate system: every `QRectF` in `zones` is in 0.0–1.0 relative space,
ready to be scaled into any pixel rectangle. For autotile entries the
"preview" geometry is computed for some specific window count (see
`ILayoutSource::previewAt`) — different counts yield different previews
from the same algorithm.

`zoneCount` carries a logical "this layout supports N windows" value that
is distinct from `zones.size()`. The sentinel `UnlimitedZoneCount` (== 0)
means "no hard limit on window count" and is used by unlimited autotile
algorithms whose rendered preview still contains a fixed example geometry.
`isValid()` encodes the consistency contract:

- `zoneCount` is either `UnlimitedZoneCount` or matches `zones.size()`.
- `zoneNumbers` is either empty or exactly matches `zones.size()`.
- Default-constructed (empty-`id`) previews are valid — they are the
  "id not mine" return value from `ILayoutSource::previewAt`.

`isAutotile()` is computed, not stored. The invariant
`isAutotile() == algorithm.has_value()` holds by construction — we chose
a computed accessor so a future refactor can never let the two fields
drift.

#### Why not two separate types?

A naïve design would have `ManualLayoutPreview` and `AutotilePreview`,
and consumers would branch on the type. We rejected this. The fields that
*differ* between the two (the optional `algorithm` block) are narrow; the
fields that are *identical* (coordinates, id, display name, section
grouping, system flag) dominate. Introducing a pair of types would force
every consumer — picker, overlay, persistence, IPC — to either template
itself or dispatch a visitor. Either is a significant cost for zero
benefit over a single struct with one optional field.

---

### 2. `ILayoutSource` — the reactive producer interface

`<PhosphorLayoutApi/ILayoutSource.h>`

```cpp
namespace PhosphorLayout {

class PHOSPHORLAYOUTAPI_EXPORT ILayoutSource : public QObject
{
    Q_OBJECT
public:
    ~ILayoutSource() override;

    /// Enumerate every layout this source can render.
    virtual QVector<LayoutPreview> availableLayouts() const = 0;

    /// Produce a fully-realised preview for one layout entry.
    ///   id          - the LayoutPreview::id from availableLayouts()
    ///   windowCount - for autotile entries, the window count to render at
    ///   canvas      - optional aspect-ratio + size hint
    /// Returns a default-constructed preview (empty id) when the id is
    /// not known to this source.
    virtual LayoutPreview previewAt(const QString& id,
                                    int windowCount = DefaultPreviewWindowCount,
                                    const QSize& canvas = {}) = 0;

Q_SIGNALS:
    void contentsChanged();

protected:
    explicit ILayoutSource(QObject* parent = nullptr);
};

} // namespace PhosphorLayout
```

Two-method contract. `availableLayouts` is the lightweight enumeration
path — populated enough for the picker UI to render rows (id, displayName,
aspect-ratio class, autotile flag, optional algorithm metadata).
`previewAt` re-runs the algorithm at a caller-chosen window count;
calling it on a manual source is a thin lookup that ignores the
parameter.

`previewAt` is deliberately non-const. Implementations are expected to
populate a preview cache in this call (re-executing a scripted autotile
algorithm on every picker redraw would be prohibitive). Callers holding
a `const ILayoutSource*` cannot query previews — which matches the
intent: querying a preview is an observable-effect operation on the
source.

#### Reactive, but not auto-wired

`ILayoutSource` emits `contentsChanged` when its preview set changes, but
the library does not auto-connect the signal to any underlying
domain-model signal. Adapter implementations expose a public slot that
the composition root connects by hand:

```cpp
// In the composition root — e.g. daemon startup:
connect(layoutManager, &LayoutManager::layoutsChanged,
        zonesSource,   &ZonesLayoutSource::notifyContentsChanged);
```

The reason is structural. The manual-layout registry in `phosphor-zones`
is intentionally split into non-`QObject` interfaces (`ILayoutRegistry`,
`ILayoutAssignments`, `IQuickLayouts`, `ILayoutPersistence`,
`IBuiltInLayouts`). That split exists because Qt's new-style
`connect(...)` with function pointers has a subtle heap-corruption mode
when the same signal is re-declared in an abstract-interface hierarchy
(signal shadowing in virtual bases). Keeping the registry interfaces
`QObject`-less avoids that class of bug entirely, but it also means they
have no signals for `ILayoutSource` to auto-connect to. Handing the
wiring back to the caller is the honest resolution: the caller always
has a concrete signal source (`LayoutManager*`) when it also has a
registry interface, because it constructed both.

A secondary benefit: the caller controls *batching*. A bulk-import of
twelve layouts should fire `contentsChanged` once, not twelve times.
The caller decides when to call `notifyContentsChanged` and the source
stays out of that policy.

#### Why QObject if the registry interfaces are not?

The `QObject` base on `ILayoutSource` is necessary: the signal is the
entire point of the interface. The registry interfaces never needed a
signal — they are pure enumeration/query surfaces, with the owning
`LayoutManager` QObject carrying the notifications. `ILayoutSource` is
the *adaptation layer* that re-exposes those notifications in a form
consumers can program against polymorphically.

---

### 3. `CompositeLayoutSource` — aggregated view

`<PhosphorLayoutApi/CompositeLayoutSource.h>`

```cpp
namespace PhosphorLayout {

class PHOSPHORLAYOUTAPI_EXPORT CompositeLayoutSource : public ILayoutSource
{
    Q_OBJECT
public:
    explicit CompositeLayoutSource(QObject* parent = nullptr);
    ~CompositeLayoutSource() override;

    void addSource(ILayoutSource* source);           // idempotent
    void removeSource(ILayoutSource* source);        // no-op if not present
    void setSources(QVector<ILayoutSource*> sources); // bulk, single signal
    void clearSources();

    QVector<LayoutPreview> availableLayouts() const override;
    LayoutPreview previewAt(const QString& id,
                            int windowCount = DefaultPreviewWindowCount,
                            const QSize& canvas = {}) override;
};

} // namespace PhosphorLayout
```

Combines multiple `ILayoutSource` implementations behind one pointer.
`availableLayouts()` returns the concatenation of each child's list in
insertion order. `previewAt()` walks the children in order and returns
the first non-empty result — children return an empty preview when they
don't know an id, per the `ILayoutSource` contract.

#### ID-namespace precedence

Two children returning a non-empty preview for the same id is a
configuration bug. In practice the namespaces are disjoint — manual
layouts use UUID strings (`{c7dfbbf7-...}`) while autotile entries use
the `autotile:<algorithmId>` prefix — so the "first non-empty wins"
rule never collides in-tree. The composite does not police the
namespaces itself; `LayoutId::isAutotile` / `LayoutId::makeAutotileId`
are the single source of truth for callers that care.

#### Borrow, don't own

The composite borrows its children. Callers own each source and keep it
alive for the composite's lifetime. `addSource` is idempotent (passing
the same pointer twice is a no-op); `removeSource` is safe to call with
a source that was never added.

The composite defensively auto-removes a source if the caller destroys
it without calling `removeSource` first. The documented contract says
callers keep sources alive, but a dangling raw pointer here would turn
a caller bug into a use-after-free on the next `availableLayouts()`
call — too steep a cost for a one-line safety net. The auto-drop is
implemented via a `QObject::destroyed` connection keyed on pointer
identity (the source is mid-destruction, so the `ILayoutSource`
subobject can't be dereferenced safely).

#### Bulk wiring

`setSources` exists as a batch path so that composition roots wiring up
N children don't emit N signals. It diffs the filtered candidate set
against `m_sources` and no-ops on a true no-op, honouring the
project-wide "only emit when value changes" rule.

---

### 4. `EdgeGaps` — shared margin shape

`<PhosphorLayoutApi/EdgeGaps.h>`

```cpp
namespace PhosphorLayout {

struct EdgeGaps
{
    static constexpr int UseGlobal = -1;

    int top    = 8;
    int bottom = 8;
    int left   = 8;
    int right  = 8;

    bool operator==(const EdgeGaps&) const = default;
    bool isUniform() const;
    static EdgeGaps uniform(int gap);
};

} // namespace PhosphorLayout
```

Per-side edge-gap values. Both manual zone layouts (`PhosphorZones::Layout`)
and tiling-algorithm output (`PhosphorTiles::TilingAlgorithm`) consume this
shape, so it lives in the shared API library that both depend on.

The `UseGlobal` sentinel (-1) means "fall back to the global edge-gap
setting for this side" rather than an explicit pixel override. Manual
layouts store this in any of the four fields; `Layout::getEffectiveOuterGaps`
resolves it to a concrete pixel value before geometry calculations
consume the struct. Autotile consumers treat the sentinel the same way
through their own resolution step. Downstream overlay / animation code
never sees an unresolved `EdgeGaps` — the producer libraries resolve
before handing out.

Default values (8 px) represent the application default.

---

### 5. `AspectRatioClass` + `ScreenClassification`

`<PhosphorLayoutApi/AspectRatioClass.h>`

```cpp
namespace PhosphorLayout {

enum class AspectRatioClass {
    Any            = 0,   // default
    Standard       = 1,   // ~16:10 to ~16:9 (1.5–1.9)
    Ultrawide      = 2,   // ~21:9     (1.9–2.8)
    SuperUltrawide = 3,   // ~32:9     (2.8+)
    Portrait       = 4,   // rotated monitors (< 1.0)
};

namespace ScreenClassification {

constexpr qreal PortraitMax       = 1.0;
constexpr qreal UltrawideMin      = 1.9;
constexpr qreal SuperUltrawideMin = 2.8;

AspectRatioClass classify(qreal aspectRatio);
AspectRatioClass classify(int width, int height);
QString          toString(AspectRatioClass);
AspectRatioClass fromString(const QString&);
qreal            aspectRatioForClass(AspectRatioClass,
                                     qreal fallback = 16.0 / 9.0);
bool             matches(AspectRatioClass layoutClass,
                         AspectRatioClass screenClass);

} // namespace ScreenClassification
} // namespace PhosphorLayout
```

Aspect-ratio classification for layouts and screens. The same enum is
the type of `LayoutPreview::aspectRatioClass`, so consumers can group /
filter previews uniformly regardless of which provider produced them.

The threshold constants and the `classify` helpers live together in one
header because the *rule* must be shared across the codebase — the
picker classifies the current screen and then filters the layout list
by class, and if the two halves disagreed on where 21:9 ends and 32:9
begins the filter would silently misbehave. Centralising the
thresholds means a screen's class is computed once from the same
numeric boundaries that authors saw when picking a layout's intended
class.

`classify` is defensive about non-finite input — NaN never compares
true under `<`, which would silently fall through every branch and
land at `SuperUltrawide`. We treat non-finite and non-positive inputs
as `AspectRatioClass::Any` so callers that compute aspect ratios from
suspect inputs get a stable sentinel rather than a wildly wrong
category.

The enum / namespace split exists so consumers can pull just the
enum (`#include <.../AspectRatioClass.h>` and ignore the namespace)
without dragging threshold constants into their namespace pollution.

---

### 6. `AlgorithmMetadata` + `ZoneNumberDisplay`

`<PhosphorLayoutApi/AlgorithmMetadata.h>`

Capability + display metadata embedded in `LayoutPreview::algorithm` on
autotile previews only. Manual previews leave the optional empty.

```cpp
namespace PhosphorLayout {

enum class ZoneNumberDisplay {
    RendererDecides, ///< Empty on the wire.
    All,
    Last,
    FirstAndLast,
    None,
};

QString           zoneNumberDisplayToString(ZoneNumberDisplay);
ZoneNumberDisplay zoneNumberDisplayFromString(QStringView);

struct AlgorithmMetadata
{
    bool supportsMasterCount       = false;
    bool supportsSplitRatio        = false;
    bool producesOverlappingZones  = false;
    bool supportsCustomParams      = false;
    bool supportsMemory            = false;
    bool isScripted                = false;
    bool isUserScript              = false;
    ZoneNumberDisplay zoneNumberDisplay = ZoneNumberDisplay::RendererDecides;
};

} // namespace PhosphorLayout
```

The fields here are strictly what a layout-picker UI needs to know about
an algorithm to render its row correctly (icons, "supports master count"
parameter editor, system-vs-user lock badge). Tuning parameters that
affect the algorithm's actual computation (split ratio, master count,
custom knobs) live in per-algorithm settings, not here. This keeps the
cross-library shape stable even when an individual algorithm grows new
parameters.

`ZoneNumberDisplay` is the typed internal form. Wire-format strings
("all", "last", "firstAndLast", "none", or empty for `RendererDecides`)
appear in JS script front-matter, D-Bus `AlgorithmInfoEntry`, JSON
serialisation, and QML property strings — the conversion helpers are
the single conversion point for every boundary.

---

### 7. `LayoutId` — namespace helpers

`<PhosphorLayoutApi/LayoutId.h>`

```cpp
namespace PhosphorLayout::LayoutId {

inline constexpr QLatin1String AutotilePrefix{"autotile:"};

bool    isAutotile(const QString& id);
QString extractAlgorithmId(const QString& id);   // warns on misuse
QString makeAutotileId(const QString& algorithmId);

} // namespace PhosphorLayout::LayoutId
```

`LayoutPreview::id` namespace utilities. Manual zone layouts use UUID
strings; autotile-algorithm previews use the prefixed form
`"autotile:<algorithmId>"` so manual + autotile IDs share a single
namespace at the consumer level.

Everyone who needs to build / parse / classify a `LayoutPreview` id
goes through the helpers here — no inline `"autotile:"` literals
outside this namespace. `extractAlgorithmId` is defensive: it warns on
misuse (passing a non-autotile id) and returns an empty string rather
than asserting, so the misuse is loud but the process stays alive.
`makeAutotileId` likewise warns and returns empty on an empty input.

The helpers live here rather than in `phosphor-tiles` because every
consumer of `ILayoutSource` must be able to classify an id without
pulling in either library-specific header. Putting them in the tiles
library would force the settings app, the editor, the KCM, and the
daemon's persistence layer to all link `phosphor-tiles` just to ask
"is this id autotile?".

---

### 8. `GapKeys` — canonical JSON key strings

`<PhosphorLayoutApi/GapKeys.h>`

```cpp
namespace PhosphorLayout::GapKeys {

inline constexpr QLatin1String UsePerSideOuterGap{"usePerSideOuterGap"};
inline constexpr QLatin1String OuterGapTop       {"outerGapTop"};
inline constexpr QLatin1String OuterGapBottom    {"outerGapBottom"};
inline constexpr QLatin1String OuterGapLeft      {"outerGapLeft"};
inline constexpr QLatin1String OuterGapRight     {"outerGapRight"};

} // namespace PhosphorLayout::GapKeys
```

The wire format for per-side outer-gap fields is intentionally shared
between manual zone layouts (`phosphor-zones`) and autotile algorithm
configs (`phosphor-tiles`) so a downstream overlay / animation / KCM
layer can read either kind without branching. Both libraries depend on
PhosphorLayoutApi — this header is the single source of truth for
these strings.

Any addition to the shared wire format (new per-side field, new JSON
key) lands here first and then ripples outward to consumers. The
convention rules out two sibling libraries quietly agreeing on a new
key in parallel and then diverging on the spelling.

---

## Injected Interfaces

PhosphorLayoutApi does not *inject* anything. It *is* the injection
shape — the type that the daemon, editor, and settings app receive via
constructor parameter. Their dependency-inversion arrows all point at
`PhosphorLayout::ILayoutSource*`, and their composition roots wire a
`CompositeLayoutSource` of concrete adapters at startup.

Sibling libraries make those adapters:
- `PhosphorZones::ZonesLayoutSource` — wraps `ILayoutRegistry`.
- `PhosphorTiles::AutotileLayoutSource` — wraps `AlgorithmRegistry`.

Neither adapter is defined in this library; PhosphorLayoutApi has no
knowledge of zones, layouts, algorithms, scripts, registries, or any
other domain concept. It defines only the shape they all conform to.

---

## Composition Example

```cpp
// In the daemon's composition root:
auto* layoutManager  = new PhosphorZones::LayoutManager(parent);
auto* algorithmReg   = PhosphorTiles::AlgorithmRegistry::instance();

auto* zonesSource    = new PhosphorZones::ZonesLayoutSource(
                         layoutManager, parent);
auto* autotileSource = new PhosphorTiles::AutotileLayoutSource(
                         algorithmReg, parent);

auto* composite      = new PhosphorLayout::CompositeLayoutSource(parent);
composite->setSources({zonesSource, autotileSource});

// Wire the underlying domain-model signals into the adapters. The
// library deliberately does not auto-connect these — see the
// ILayoutSource section for the rationale.
QObject::connect(layoutManager, &PhosphorZones::LayoutManager::layoutsChanged,
                 zonesSource,   &PhosphorZones::ZonesLayoutSource::notifyContentsChanged);

// Downstream consumers (picker controller, settings model, overlay) hold
// a PhosphorLayout::ILayoutSource* — they do not know which adapters
// back it, and they re-query on contentsChanged.
myPickerController->setLayoutSource(composite);
```

The settings app and editor do the same wiring in their own composition
roots. Because the composite forwards `contentsChanged`, consumers only
need to listen at the composite level.

---

## Threading Model

All public API: **owning thread only.** `ILayoutSource` is a `QObject`;
its signal/slot connections use `AutoConnection`, which resolves to
direct dispatch within a single thread. The library does not arrange
cross-thread synchronisation, does not lock, and does not spawn
threads. Producers that run expensive work off-thread (e.g. a scripted
autotile algorithm compiling JS) are expected to marshal results back
to the owning thread before emitting `contentsChanged`.

`LayoutPreview` is a plain struct with no shared state; values are
copyable and safe to pass across threads once constructed. `EdgeGaps`
and the enums / namespaces are all value-like — no lifecycle concerns.

---

## Testing Strategy

The library ships no in-tree tests of its own today — it contains one
out-of-line destructor (`ILayoutSource::~ILayoutSource`) and one
container class (`CompositeLayoutSource`), and both are exercised
comprehensively by the sibling libraries' tests
(`tests/unit/core/test_layout_core.cpp` and the `phosphor-zones` /
`phosphor-tiles` test suites). The shape types (`LayoutPreview`,
`EdgeGaps`, the enums) are plain-old-data and need no unit coverage on
their own.

Recommended test additions when the library grows:

- **LayoutPreview::isValid** — every permutation of the three consistency
  cases (empty, bounded-match, unlimited) plus the negative-`zoneCount`
  reject.
- **CompositeLayoutSource** — add/remove/setSources idempotency, the
  contentsChanged forwarding count, the destroyed-auto-drop path, the
  bulk no-op diff.
- **LayoutId round-trips** — `makeAutotileId` / `extractAlgorithmId` /
  `isAutotile` over a matrix of empty / prefix-only / full ids.
- **ScreenClassification** — boundary values at each threshold, NaN /
  zero / negative inputs land on `Any`, string round-trip, `matches`
  behaviour under `Any`.

Because the library depends only on Qt6::Core, all of the above runs
headless with no display server.

---

## Migration Path

This library did not migrate out of a single existing file — it is the
*extraction point* that let `phosphor-zones` and `phosphor-tiles` be
split from one another while preserving the shared shapes that the
daemon, editor, and settings app already consumed.

The historical migration (already complete, for reference):

1. **Shape extraction.** `LayoutPreview`, `AlgorithmMetadata`,
   `AspectRatioClass`, `EdgeGaps`, and the layout-id helpers moved out
   of the daemon's `src/core/` into `libs/phosphor-layout-api/`.
   The daemon and KCM code continued to compile with updated
   includes.
2. **Interface extraction.** `ILayoutSource` was introduced as the
   polymorphic enumeration point. Existing daemon code that branched on
   "manual vs autotile" inside the picker controller and overlay models
   switched to holding an `ILayoutSource*` and invoking the two
   interface methods.
3. **Adapter placement.** `ZonesLayoutSource` landed in
   `phosphor-zones`; `AutotileLayoutSource` landed in `phosphor-tiles`.
   Each library linked PhosphorLayoutApi PUBLIC. Composition roots
   gained the `CompositeLayoutSource` wiring shown above.
4. **GapKeys centralisation.** The five JSON key string-literals that
   used to live in inline `QStringLiteral`s inside each library moved
   into `GapKeys.h`. A single grep over the two libraries now finds
   zero inline string copies.

The only migration still open is **test extraction**: the shape tests
currently live inside the sibling libraries' suites and should move
into a dedicated `libs/phosphor-layout-api/tests/` subtree before the
library is considered stable at 1.0.

---

## Rejected Alternatives

### Two separate preview types (`ManualLayoutPreview` + `AutotilePreview`)

Rejected. The shared fields (coordinates, id, section grouping, system
flag, aspect-ratio class) dominate the structure; the differing fields
(algorithm metadata) fit naturally behind a single `std::optional`.
Consumer code with two types would require either templating or a
visitor at every render site, for no benefit over the computed
`isAutotile()` accessor.

### Header-only library

Rejected. `ILayoutSource` needs a `Q_OBJECT` macro, which requires
AUTOMOC processing and at minimum one translation unit per consumer
that uses the header. Worse, an out-of-line destructor must anchor the
vtable in one TU to prevent every consumer `.cpp` emitting its own
weak-symbol vtable copy. Both constraints demand a real library target.
The shared-library cost is trivial (~250 LOC compiled), and in exchange
the generated export header carries the visibility declarations
consistently across platforms.

### Signals on the non-`QObject` registry interfaces

Rejected. `PhosphorZones::ILayoutRegistry` and siblings are deliberately
non-`QObject` because Qt's new-style `connect` with function pointers
suffers a heap-corruption mode when the same signal is re-declared in
an abstract-interface hierarchy (signal shadowing in virtual bases).
The concrete `LayoutManager` QObject carries the change signals; the
`ILayoutSource` adapter bridges to those signals via a caller-wired
slot. See `libs/phosphor-zones/include/PhosphorZones/ILayoutManager.h`
for the upstream rationale.

### Auto-wiring inside `ILayoutSource` adapters

Rejected. The adapter would need to downcast or dynamic-cast the
borrowed registry interface to discover a concrete signalling object,
and callers lose control over batching. Making the caller wire one slot
costs one line of `connect(...)` at the composition root and keeps the
library out of the policy-vs-mechanism debate.

### Owning child sources in `CompositeLayoutSource`

Rejected. Ownership of the concrete adapters (`ZonesLayoutSource`,
`AutotileLayoutSource`) belongs to the composition root, which also
owns the underlying registries they wrap. Giving the composite
`std::unique_ptr` ownership would force the composition root to either
surrender its own lifetime control or invent a two-step release
dance. Borrowing is the simpler rule: caller keeps sources alive,
composite keeps a stable pointer. The defensive auto-drop on
`destroyed` is a belt-and-braces safety net for caller bugs, not a
replacement for the ownership rule.

### Putting `LayoutId` helpers in `phosphor-tiles`

Rejected. The settings app, the editor, the KCM, and the daemon's
persistence layer all classify layout ids without needing any other
tiles-library symbol. Locating the helpers there would force all four
to link `phosphor-tiles` just to answer `isAutotile(id)`. Instead the
namespace prefix lives in the root contract library, and both the
zones and tiles libraries agree on it via their shared dependency.

### `LayoutPreview` as a `Q_GADGET` with `Q_PROPERTY`s

Rejected. `Q_GADGET` would enable QML consumption, but every QML
consumer we have already receives a `QVariantMap` via their
controller's model interface. Adding Q_GADGET would drag Qt's meta-
object machinery into the plain-data struct with no current use case.
If a future QML binding needs gadget-style access, we can add it as a
sibling type rather than mutating the core shape.

---

## Extensions (Future)

None currently planned. The contract is designed to stay small.

Candidates that have been discussed and deferred:

- **`LayoutPreview::thumbnail`** — an optional pre-rendered
  `QImage` / `QByteArray` for picker rows that want to cache a paint
  result across opens. Blocked on deciding whether the preview's
  paint is deterministic enough to cache (shader-driven zone
  decorations are a moving target).
- **`ILayoutSource::invalidate(id)`** — a targeted cache-flush slot
  for sources that hold per-id caches. Not yet needed; the current
  composition always invalidates the whole set on
  `contentsChanged`.
- **Gadget accessors on `LayoutPreview`** — would let QML bind to
  individual fields without going through a `QVariantMap` wrapper.
  Deferred until a concrete QML site wants it.

Each extension must pass the same bar as the initial surface: does
every sibling library genuinely share it, and does it belong in the
root contract rather than in one producer library?

---

## Directory Layout

```
libs/phosphor-layout-api/
├── CMakeLists.txt
├── PhosphorLayoutApiConfig.cmake.in
├── include/
│   └── PhosphorLayoutApi/
│       ├── PhosphorLayoutApi.h         (umbrella)
│       ├── AlgorithmMetadata.h
│       ├── AspectRatioClass.h
│       ├── CompositeLayoutSource.h
│       ├── EdgeGaps.h
│       ├── GapKeys.h
│       ├── ILayoutSource.h
│       ├── LayoutId.h
│       └── LayoutPreview.h
└── src/
    ├── compositelayoutsource.cpp
    └── ilayoutsource.cpp               (out-of-line dtor to anchor vtable)
```

Build artefact: a single `SHARED` library target,
`PhosphorLayoutApi::PhosphorLayoutApi`. `SOVERSION 0`, `VERSION 0.1.0`,
C++20 PUBLIC, symbol visibility hidden with export macros on exactly
two symbols (`ILayoutSource`, `CompositeLayoutSource`). Unity builds
are disabled — the two translation units are small enough that unity
buys nothing and surfaces spurious "defined but not used" warnings on
anonymous-namespace helpers that GCC cannot track across the merge.

Generated export header (`phosphorlayoutapi_export.h`) lives in the
build directory, is installed alongside the public headers, and carries
the `PHOSPHORLAYOUTAPI_EXPORT` macro.

---

## Open Questions

Resolved during extraction:

- `LayoutPreview` as one struct vs two — single struct with optional
  algorithm block.
- `ILayoutSource` as `QObject` while sibling registry interfaces are
  not — yes, because the reactive notification is `ILayoutSource`'s
  entire purpose.
- Who wires `contentsChanged` from the underlying domain signal — the
  composition root, explicitly, via a public slot on the adapter.
- Where the `"autotile:"` prefix lives — in the root contract library,
  not in `phosphor-tiles`.

Deferred:

- Dedicated test suite under `libs/phosphor-layout-api/tests/`. The
  shapes are currently covered transitively by sibling libraries; a
  local suite would catch regressions in `isValid` / `CompositeLayoutSource`
  diffing earlier.
- Whether `LayoutPreview` should grow a `Q_GADGET` accessor for direct
  QML binding. Current consumers go through controller-provided
  `QVariantMap`s; a concrete QML site wanting struct-level binding is
  the trigger for reopening this.
- Whether `ILayoutSource::previewAt` should accept an opaque
  `PreviewRequest` aggregate instead of three loose parameters. The
  current signature is stable at three arguments; a fourth would tip
  the balance toward a struct.
