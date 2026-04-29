# PhosphorIdentity — API Design Document

## Overview

PhosphorIdentity is a header-only Qt6/C++20 library that defines the canonical
composite window-id wire format used across every PlasmaZones process plus
the string helpers that build, parse, and pattern-match it. It is the single
source of truth for how a window is named on disk, on the D-Bus wire, and in
every in-memory hash table across the KWin effect, the daemon, PhosphorZones,
and the future PhosphorTiles runtime.

**License:** LGPL-2.1-or-later
**Namespace:** `PhosphorIdentity::WindowId`
**Depends on:** Qt6::Core only (no Gui, no Wayland, no KWin, no Widgets)
**Build artefact:** None — this is a CMake `INTERFACE` library; every helper
is `inline` and lives in a single header.

---

## Dependency Graph

```
                  PhosphorIdentity (INTERFACE, Qt6::Core only)
                                │
   ┌─────────────┬──────────────┼───────────────┬────────────────┐
   │             │              │               │                │
 KWin effect   daemon       PhosphorZones   PhosphorTiles    test fixtures
 (separate     (WTS,         (Layout::      (runtime engine,   (unit tests
  process —    autotile,      matchAppRule,  future)            for every
  cost-        snap,          app-rule                          consumer)
  sensitive    session        matching)
  about deps)  persistence)
```

Every consumer links `PhosphorIdentity::PhosphorIdentity`. Because the target
is INTERFACE, the link cost is an include path and a transitive `Qt6::Core` —
no shared-object load at process start, no symbol-resolution work, no runtime
cost. The KWin effect in particular cares: it runs in-process with the
compositor and avoids pulling in anything that would enlarge its
transitive-dependency closure.

---

## Design Principles

1. **One spelling of the wire format.** `"appId|instanceId"` is chosen in
   exactly one place. Consumers call `buildCompositeId()` / `extractAppId()`
   / `extractInstanceId()` rather than hand-concatenating with `|`. If the
   format ever changes, it changes in one header.
2. **Symmetric builder / extractor pair.** `buildCompositeId(a, b)` round-
   trips through `extractAppId` and `extractInstanceId`. Empty parts collapse
   predictably: empty instance yields a bare appId; empty appId preserves the
   separator so the instance id is still recoverable.
3. **Header-only, INTERFACE target.** Every helper is small enough to inline;
   an INTERFACE library exposes the include dir and the `Qt6::Core`
   requirement without producing a `.so`.
4. **Minimal transitive dependency surface.** Qt6::Core only. No Gui, no
   QtQuick, no Wayland, no KWin. That keeps the library cheap to link from a
   KWin effect plugin, which is the strictest of the consumers.
5. **Compositor-agnostic.** Nothing in the API references KWin, Wayland, X11,
   `QWindow`, or any window-system concept. The helpers operate on
   `QString` / `QStringView` and make no assumption about where the appId
   came from.
6. **Explicit over clever for matcher semantics.** The `appIdMatches` rules
   (exact, trailing dot-segment, last-segment prefix with a 5-character gate)
   are spelled out in the doc comment and pinned by unit tests. No regex, no
   globs, no surprises.

---

## Public API

The entire public API lives in `PhosphorIdentity::WindowId`. Five free
functions, all `inline`, all pure — no state, no side effects, no QObject
involvement.

```cpp
#include <PhosphorIdentity/WindowId.h>

namespace PhosphorIdentity::WindowId {
    QString buildCompositeId(QStringView appId, QStringView instanceId);
    QString extractAppId(const QString& windowId);
    QString extractInstanceId(const QString& windowId);
    QString deriveShortName(const QString& windowClass);
    bool    appIdMatches(const QString& appId, const QString& pattern);
}
```

### 1. `buildCompositeId(appId, instanceId)`

Builds the canonical composite from its two halves. This is the symmetric
counterpart to `extractAppId` / `extractInstanceId` — holding all three in one
header makes the format single-sourced.

```cpp
inline QString buildCompositeId(QStringView appId, QStringView instanceId)
{
    if (instanceId.isEmpty()) {
        return appId.toString();
    }
    QString out;
    out.reserve(appId.size() + 1 + instanceId.size());
    out.append(appId);
    out.append(QLatin1Char('|'));
    out.append(instanceId);
    return out;
}
```

Behaviour:
- Empty `instanceId` returns a bare `appId` — no trailing `|`. This matches
  what `extractAppId` would reconstruct from a separator-less input.
- Empty `appId` with non-empty `instanceId` preserves the separator:
  `buildCompositeId("", "uuid-1")` returns `"|uuid-1"`. A window that
  briefly has no resolved app class must still be disambiguable by instance
  id alone.
- The reservation sizes the target so the append sequence is a single
  allocation — this runs in the KWin effect's hot path whenever a new
  window is observed.

Used by the KWin effect to stamp a stable identity on each tracked
`KWin::Window` the first time it is seen (see
`kwin-effect/plasmazoneseffect.cpp:1495`).

### 2. `extractAppId(windowId)`

Returns the portion of the composite before the first `|`, or the input
verbatim if no separator is present.

```cpp
inline QString extractAppId(const QString& windowId)
{
    if (windowId.isEmpty()) {
        return windowId;
    }
    int sep = windowId.indexOf(QLatin1Char('|'));
    return (sep >= 0) ? windowId.left(sep) : windowId;
}
```

Passthrough on separator-less input is deliberate: the production wire
format is `appId|instanceId`, but legacy fixtures, migration paths, and
logging can all carry bare strings. The helper must not throw data away.
Consumers that want to distinguish "was a composite?" from "was bare?"
check for `|` themselves before calling.

### 3. `extractInstanceId(windowId)`

Dual of `extractAppId` — returns everything after the first `|`, or the
input verbatim if no separator is present.

```cpp
inline QString extractInstanceId(const QString& windowId)
{
    if (windowId.isEmpty()) {
        return windowId;
    }
    int sep = windowId.indexOf(QLatin1Char('|'));
    return (sep >= 0) ? windowId.mid(sep + 1) : windowId;
}
```

Per discussion #271, runtime keys now use the bare instance id. The
`appId|instanceId` composite remains on disk (session persistence, rule
fixtures) and in older D-Bus payloads, so this helper continues to exist
for compat paths.

### 4. `deriveShortName(windowClass)`

Produces a display name suitable for icons or status text from an appId.

```cpp
// "org.kde.dolphin"  →  "dolphin"
// "com.example.app"  →  "app"
// "firefox"          →  "firefox"
// "org.kde.foo."     →  "foo"     (trailing dot stripped first)
// ""                 →  ""
// "...."             →  ""
```

The trailing-dot strip runs first so `"org.kde.foo."` behaves the same as
`"org.kde.foo"`. Without that step, `lastIndexOf('.')` would hit the final
dot and return an empty last segment, which triggers the "no dot found"
branch and yields the whole string verbatim — the opposite of what callers
want.

**Important:** operates on a bare appId, not a composite window id. Callers
holding `"org.kde.foo|uuid-1"` must run it through `extractAppId` first,
otherwise `deriveShortName` would return `"uuid-1"` (the portion after the
last `.` or nothing depending on the uuid format).

Used by the KWin effect for OSD / overlay labels
(`kwin-effect/plasmazoneseffect.cpp:2874`).

### 5. `appIdMatches(appId, pattern)`

Segment-aware matcher used by `Layout::matchAppRule` to decide whether an
app rule applies to a window. This is the subtlest function in the library
and warrants worked examples.

```cpp
// Exact match, case-insensitive
appIdMatches("Firefox", "firefox")                              // true
appIdMatches("firefox", "FIREFOX")                              // true

// Trailing dot-segment — reverse-DNS matched by bare last segment
appIdMatches("org.mozilla.firefox", "firefox")                  // true
// …and the reverse direction
appIdMatches("firefox", "org.mozilla.firefox")                  // true

// Last-segment prefix — one side ends with a numeric/version suffix
appIdMatches("org.kde.systemsettings5", "systemsettings")       // true
appIdMatches("systemsettings", "org.kde.systemsettings5")       // true

// Short-prefix collision is rejected
appIdMatches("fire", "firefox")                                 // false
appIdMatches("firefox", "fire")                                 // false
appIdMatches("org.mozilla.firefox", "fire")                     // false
appIdMatches("fire", "org.mozilla.firefox")                     // false

// Middle segments don't match — scoping to "firefox" never catches "thunderbird"
appIdMatches("org.mozilla.firefox", "mozilla")                  // false
```

**Why the 5-character prefix gate?** Without it, any short pattern would
match any longer appId that happened to start with the same letters. The
most load-bearing example is `"fire"` vs `"firefox"`: a user who wrote
`"fire"` as a rule pattern almost certainly did not mean to tile every
Firefox window. The gate draws a hard line — a prefix candidate must be at
least five characters before the last-segment-prefix branch can fire.

The gate is applied **asymmetrically to whichever operand is the prefix
candidate.** When matching a pattern against an appId's last segment, the
pattern is the prefix and must be ≥5 chars. When matching an appId against
a pattern's last segment (reverse direction), the appId is the prefix and
must be ≥5 chars. The gate is always on the shorter-or-equal string in the
prefix relationship — otherwise the two directions would be inconsistent.

The exact-match and trailing-dot-segment branches run before the prefix
gate, so legitimate matches like `"firefox" == "firefox"` and
`"org.mozilla.firefox" ends-with ".firefox"` are unaffected.

Used by PhosphorZones in `Layout::matchAppRule`
(`libs/phosphor-zones/src/layout.cpp:408`) to dispatch app-rule decisions.

---

## Threading Model

All five helpers are pure functions over their inputs: no shared state, no
QObject, no signals, no globals. They are safe to call from any thread,
concurrently, with the usual caveats about the QString inputs themselves
(implicit sharing / COW is thread-safe in Qt when each thread holds its own
copy, which is the normal case here).

The KWin effect calls `buildCompositeId` on the compositor thread as soon
as a window is observed; the daemon calls `extractAppId` on the main
thread while processing D-Bus messages; tests drive all five from Qt Test's
single-threaded runner. None of those call sites needs synchronisation.

---

## Testing Strategy

`tests/unit/core/test_window_identity.cpp` exercises all five helpers via
`QTEST_MAIN`. Because the library has no Qt Gui / Wayland / KWin
dependencies, the test runs headless in any CI environment that provides
Qt6::Core and QTest.

Covered scenarios:

| Helper               | Pinned behaviours |
|----------------------|-------------------|
| `extractAppId`       | Normal composite parsing, UUID stripping, empty input, no-pipe passthrough, pipe-at-end, multi-dot appIds, pipe-at-start, bare instance ID passthrough (the production wire format), legacy composite compatibility, multiple pipes (only first split), hyphenated appIds |
| `extractInstanceId`  | Round-trip symmetry with `buildCompositeId` |
| `buildCompositeId`   | Round-trip with extractors, empty instance yields bare appId (no trailing separator), empty appId preserves separator |
| `appIdMatches`       | Case-insensitive exact match, trailing dot-segment in both directions, last-segment prefix in both directions, short-prefix rejection (the 5-char gate invariant), empty inputs, no cross-segment matching (middle segments never match) |
| `deriveShortName`    | Reverse-DNS extraction, bare name passthrough, trailing-dot handling, empty / dot-only inputs |

Coverage targets:
- Every documented invariant in the header comment has at least one pinning test.
- Both directions of each asymmetric branch (e.g. `pattern` longer than
  `appId` and vice versa in `appIdMatches`) are exercised.
- The 5-char gate is tested with a 4-char prefix candidate in every
  direction — this is the single most important invariant and the easiest
  to regress.

---

## Migration Path

The library already ships; there is no pending migration for consumers
inside this repository. For external consumers the checklist is:

1. Add `find_package(PhosphorIdentity REQUIRED)` to your CMake.
2. `target_link_libraries(your-target PRIVATE PhosphorIdentity::PhosphorIdentity)`.
   This pulls in the include dir plus a transitive `Qt6::Core`.
3. `#include <PhosphorIdentity/WindowId.h>` where you need the helpers.
4. Replace any hand-rolled `appId + "|" + instanceId` concatenation with
   `PhosphorIdentity::WindowId::buildCompositeId`.
5. Replace any `indexOf('|')` / `left` / `mid` window-id splitting with
   `extractAppId` / `extractInstanceId`.

Historical note: an earlier iteration of this library shipped a `SHARED`
target. The resulting `.so` was empty — it existed only to anchor a
generated export header — and added a real cost to the KWin effect's load
time (extra `dlopen`, extra symbol-resolution work at process start). The
INTERFACE migration removed the artefact without removing any public API.

---

## Rejected Alternatives

### Keep `SHARED` with an export header
Rejected: there is no compiled object code to ship. `inline` + header-only
is the honest description of what this library is, and `INTERFACE` is the
CMake primitive that expresses it. Keeping `SHARED` purely for
cosmetic/symmetry reasons with sibling libraries (PhosphorShell etc.)
would add a real `dlopen` cost to every consumer for zero functional gain.

### Fold helpers into `src/core/utils.h`
Rejected: the KWin effect lives in a different process and cannot include
daemon private headers. Before this library existed, the effect had its own
copy of `extractAppId`, and the two copies drifted — a bug fix to one
didn't reach the other. Extracting the helpers into a library with no Qt
Gui / Wayland / KWin dependency is what makes one-spelling-of-the-format
enforceable at compile time rather than by code-review vigilance.

### Regex or glob patterns for `appIdMatches`
Rejected: the matcher's job is pattern-rule dispatch for user-written
rules, and the set of cases that matter in practice is small and
enumerable (exact, trailing reverse-DNS segment, versioned suffix). A
regex engine would introduce the risks we actually care about avoiding —
catastrophic backtracking, implicit anchoring choices, escaping rules —
without giving real users anything they need. The five-line matcher is
cheaper to read and cheaper to run.

### Lower the 5-character prefix gate
Rejected: the gate is calibrated against the "fire" / "firefox" collision,
which is the canonical real-world case reported from user rule files. Four
characters is too short (lets "fire" in); six would reject legitimate
short app names we have not yet seen but which are likely to exist.

### Drop the symmetric `buildCompositeId`
Rejected: without a builder, every consumer re-invents the concatenation
with `|`, and the wire-format choice leaks out of this header and into
every call site. The point of the library is to own the format; the
builder closes the loop.

### Link `Qt6::Gui` for `QIcon` / display helpers
Rejected: cost-sensitive consumers (notably the KWin effect plugin) avoid
Gui-stack transitive deps. Display-name helpers like `deriveShortName`
return plain `QString`; rendering that into an icon is the consumer's job.

### `QObject` API with signals
Rejected: there is no state to notify about. These are pure functions over
their inputs; wrapping them in a QObject would add thread-affinity
constraints, moc overhead, and zero value.

---

## Extensions (deferred to future iterations)

- **PII-safe logging wrapper.** An `inline QString redactedInstanceId(const
  QString&)` that returns the first/last few chars plus a hash, for log
  lines that want to reference a window without leaking its full uuid.
  Deferred until logging volume justifies it — today's `qCDebug` call sites
  are not numerous enough for full uuids to be a problem.
- **`QStringView` overloads for `extractAppId` / `extractInstanceId`.**
  Would let callers avoid materialising a temporary `QString` on the hot
  path. Deferred: the existing call sites already hold `QString` and
  conversions to/from `QStringView` are cheap enough that there is no
  measurable win yet.
- **Compile-time parsing for constant composite literals.**
  `constexpr buildCompositeId` would be nice for test fixtures. Deferred
  on Qt's side — `QString` is not `constexpr`-friendly, and the helpers
  can stay inline without it.

---

## Directory Layout

```
libs/phosphor-identity/
├── CMakeLists.txt
├── PhosphorIdentityConfig.cmake.in
└── include/
    └── PhosphorIdentity/
        ├── PhosphorIdentity.h      (umbrella; includes WindowId.h)
        └── WindowId.h              (the entire public API)

tests/unit/core/
└── test_window_identity.cpp        (QTest coverage for all five helpers)
```

No `src/` directory, no generated export header, no compiled artefact.
The `PhosphorIdentityConfig.cmake.in` + installed targets / headers are
what consumers use when finding the package via `find_package`.

---

## Open Questions

Resolved during this design pass:
- ✅ SHARED vs INTERFACE → INTERFACE (no compiled code, no `.so` needed)
- ✅ Single namespace vs nested → nested (`PhosphorIdentity::WindowId`) so
  future identity primitives (surface ids? session ids?) can live
  alongside `WindowId` without crowding the top level.
- ✅ Regex vs explicit matcher → explicit; the cases are enumerable.
- ✅ 5-char prefix gate → calibrated against the "fire" / "firefox"
  collision; pinned by tests.

Deferred to implementation:
- Whether to add a `QStringView`-returning variant of `extractAppId` that
  borrows from the input — depends on whether any caller materialises the
  result only to throw it away. Current call sites all keep the `QString`.
- Whether `deriveShortName` should also normalise case — today it returns
  the original-case segment. Matching real-world usage (which is always
  "compare case-insensitively downstream") suggests not.
