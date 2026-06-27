# phosphor-rule Table-Driven Refactor — Implementation Plan

Branch: `refactor/rule-table-driven`
Base: `v3.1`

## Goal

Reduce the number of edit sites when adding a new field or action to the window
rule system.  Fields drop from **14 sites → 5** (8 → 2 in the library); actions
drop from **5+ scattered functions → 2 files** (constexpr+descriptor in lib,
one label entry in settings).

## Non-Goals

- Full PZ decoupling / generic extension registries (no second consumer exists).
- Changing the daemon ↔ KWin D-Bus transport (appropriate separation, not duplication).
- Changing `WindowQuery` storage from named members to a variant array (cosmetic
  benefit only; `hasWindow()` can be driven by the table without changing storage).

---

## Phase 1 — Field Table (`MatchTypes.h`)

### 1.1  Introduce `FieldDescriptor` and `kFieldTable`

**File:** `libs/phosphor-rule/include/PhosphorRule/MatchTypes.h`

Add after the `Field` enum and before the existing inline functions:

```cpp
enum class FieldType : int { String, Bool, Int, Enum };
enum class FieldSource : int { Window, Context };

struct FieldDescriptor {
    Field field;
    QLatin1StringView wire;
    FieldType type;
    FieldSource source;
};

inline constexpr FieldDescriptor kFieldTable[] = {
    // Identity
    {Field::AppId,         QLatin1StringView("appId"),         FieldType::String, FieldSource::Window},
    {Field::WindowClass,   QLatin1StringView("windowClass"),   FieldType::String, FieldSource::Window},
    {Field::DesktopFile,   QLatin1StringView("desktopFile"),   FieldType::String, FieldSource::Window},
    {Field::WindowRole,    QLatin1StringView("windowRole"),    FieldType::String, FieldSource::Window},
    {Field::Pid,           QLatin1StringView("pid"),           FieldType::Int,    FieldSource::Window},
    {Field::Title,         QLatin1StringView("title"),         FieldType::String, FieldSource::Window},
    // Type
    {Field::WindowType,    QLatin1StringView("windowType"),    FieldType::Enum,   FieldSource::Window},
    // State
    {Field::IsSticky,      QLatin1StringView("isSticky"),      FieldType::Bool,   FieldSource::Window},
    {Field::IsFullscreen,  QLatin1StringView("isFullscreen"),  FieldType::Bool,   FieldSource::Window},
    {Field::IsMinimized,   QLatin1StringView("isMinimized"),   FieldType::Bool,   FieldSource::Window},
    // Context
    {Field::ScreenId,      QLatin1StringView("screenId"),      FieldType::String, FieldSource::Context},
    {Field::VirtualDesktop,QLatin1StringView("virtualDesktop"),FieldType::Int,    FieldSource::Context},
    {Field::Activity,      QLatin1StringView("activity"),      FieldType::String, FieldSource::Context},
    // State (appended)
    {Field::IsMaximized,   QLatin1StringView("isMaximized"),   FieldType::Bool,   FieldSource::Window},
    {Field::IsFocused,     QLatin1StringView("isFocused"),     FieldType::Bool,   FieldSource::Window},
    {Field::IsTransient,   QLatin1StringView("isTransient"),   FieldType::Bool,   FieldSource::Window},
    {Field::IsNotification,QLatin1StringView("isNotification"),FieldType::Bool,   FieldSource::Window},
    // Geometry
    {Field::Width,         QLatin1StringView("width"),         FieldType::Int,    FieldSource::Window},
    {Field::Height,        QLatin1StringView("height"),        FieldType::Int,    FieldSource::Window},
    // Stacking / accessory
    {Field::KeepAbove,     QLatin1StringView("keepAbove"),     FieldType::Bool,   FieldSource::Window},
    {Field::KeepBelow,     QLatin1StringView("keepBelow"),     FieldType::Bool,   FieldSource::Window},
    {Field::SkipTaskbar,   QLatin1StringView("skipTaskbar"),   FieldType::Bool,   FieldSource::Window},
    {Field::SkipPager,     QLatin1StringView("skipPager"),     FieldType::Bool,   FieldSource::Window},
    {Field::SkipSwitcher,  QLatin1StringView("skipSwitcher"),  FieldType::Bool,   FieldSource::Window},
    {Field::IsModal,       QLatin1StringView("isModal"),       FieldType::Bool,   FieldSource::Window},
    {Field::HasDecoration, QLatin1StringView("hasDecoration"), FieldType::Bool,   FieldSource::Window},
    {Field::IsResizable,   QLatin1StringView("isResizable"),   FieldType::Bool,   FieldSource::Window},
    // Position
    {Field::PositionX,     QLatin1StringView("positionX"),     FieldType::Int,    FieldSource::Window},
    {Field::PositionY,     QLatin1StringView("positionY"),     FieldType::Int,    FieldSource::Window},
    // Content
    {Field::CaptionNormal, QLatin1StringView("captionNormal"), FieldType::String, FieldSource::Window},
    // PlasmaZones extension [30, 32]
    {Field::IsFloating,    QLatin1StringView("isFloating"),    FieldType::Bool,   FieldSource::Window},
    {Field::IsSnapped,     QLatin1StringView("isSnapped"),     FieldType::Bool,   FieldSource::Window},
    {Field::Zone,          QLatin1StringView("zone"),          FieldType::String, FieldSource::Window},
};
static_assert(std::size(kFieldTable) == FieldCount, "kFieldTable must have one entry per Field");
```

**Ordering invariant:** entries are in `static_cast<int>(field)` order so the
table is directly indexable: `kFieldTable[static_cast<int>(f)]`.  The
`static_assert` + a unit test enforce this.

### 1.2  Rewrite classifier functions in terms of the table

Replace the bodies of the 5 existing inline functions.  Function SIGNATURES
stay identical — no downstream edits.

```cpp
inline QString fieldToString(Field field)
{
    const int idx = static_cast<int>(field);
    if (idx >= 0 && idx < FieldCount) {
        return QString(kFieldTable[idx].wire);
    }
    return QStringLiteral("appId");
}

inline std::optional<Field> fieldFromString(QStringView s)
{
    for (const auto& d : kFieldTable) {
        if (s.compare(d.wire, Qt::CaseInsensitive) == 0) {
            return d.field;
        }
    }
    return std::nullopt;
}

inline bool fieldIsString(Field field)
{
    const int idx = static_cast<int>(field);
    return idx >= 0 && idx < FieldCount && kFieldTable[idx].type == FieldType::String;
}

inline bool fieldIsNumeric(Field field)
{
    const int idx = static_cast<int>(field);
    return idx >= 0 && idx < FieldCount && kFieldTable[idx].type == FieldType::Int;
}

inline bool fieldIsBool(Field field)
{
    const int idx = static_cast<int>(field);
    return idx >= 0 && idx < FieldCount && kFieldTable[idx].type == FieldType::Bool;
}

inline bool fieldIsContext(Field field)
{
    const int idx = static_cast<int>(field);
    return idx >= 0 && idx < FieldCount && kFieldTable[idx].source == FieldSource::Context;
}
```

### 1.3  Table-driven `hasWindow()` on `WindowQuery`

**File:** `libs/phosphor-rule/include/PhosphorRule/WindowQuery.h`

Replace the manual OR-chain in `hasWindow()` with a call to `valueForField`
over all `FieldSource::Window` entries:

```cpp
bool hasWindow() const
{
    for (const auto& d : kFieldTable) {
        if (d.source == FieldSource::Window && valueForField(d.field).has_value()) {
            return true;
        }
    }
    return false;
}
```

**Note:** `valueForField` remains a manual switch for now — `WindowQuery` keeps
its named typed members.  This is deliberate: the members provide compile-time
type safety at population sites (`query.appId = ...`).  Converting to a variant
bag is a separable future step.

### 1.4  Test updates

**File:** `libs/phosphor-rule/tests/test_matchtypes.cpp`

Add a new test:

```cpp
void testFieldTableOrdering()
{
    // Verify the table is indexed by enum value.
    for (int i = 0; i < FieldCount; ++i) {
        QCOMPARE(static_cast<int>(kFieldTable[i].field), i);
    }
}
```

Existing tests (`testFieldRoundTrip`, `testFieldClassification`,
`testFieldIsContext`) continue to pass unchanged — they call the same public
functions which now delegate to the table.

### 1.5  Verification

- Docker build + `ctest --output-on-failure` (all existing tests pass)
- No API signature changes — zero downstream edits needed

---

## Phase 2 — Action Descriptor Metadata (`RuleAction.h` / `ruleaction.cpp`)

### 2.1  Add `category`, `displayOrder`, `tags` to `ActionDescriptor`

**File:** `libs/phosphor-rule/include/PhosphorRule/RuleAction.h`

Add three new fields at the end of the struct (after `userAuthorable`):

```cpp
    /// Untranslated category key for the UI picker (e.g. "layout-engine",
    /// "gaps", "window", "appearance", "animation"). The GPL settings layer
    /// maps this to a translated label. Empty = "Other".
    QString category{};
    /// Display order within the category. Picker sorts by (categoryOrder, displayOrder).
    int displayOrder = 0;
    /// Semantic tags for consumer-side filtering. Canonical tags:
    ///   "effect"        — the KWin effect loads these into its rule set
    ///   "border"        — per-window border/title-bar appearance override
    ///   "animation"     — animation override (shader/timing/curve)
    ///   "layout-engine" — context-domain layout/engine pinning
    ///   "gap"           — per-context gap override
    QStringList tags{};
```

### 2.2  Add tag query API to `ActionRegistry`

**File:** `libs/phosphor-rule/include/PhosphorRule/RuleAction.h`

Add to the `ActionRegistry` public section:

```cpp
    /// True if the descriptor for @p type carries @p tag.
    bool hasTag(const QString& type, QLatin1StringView tag) const;

    /// All registered type ids carrying @p tag, sorted alphabetically.
    QStringList typesWithTag(QLatin1StringView tag) const;
```

**File:** `libs/phosphor-rule/src/ruleaction.cpp`

Implement:

```cpp
bool ActionRegistry::hasTag(const QString& type, QLatin1StringView tag) const
{
    auto it = m_descriptors.constFind(type);
    return it != m_descriptors.constEnd() && it->tags.contains(QString(tag));
}

QStringList ActionRegistry::typesWithTag(QLatin1StringView tag) const
{
    QStringList result;
    for (auto it = m_descriptors.constBegin(); it != m_descriptors.constEnd(); ++it) {
        if (it->tags.contains(QString(tag))) {
            result.append(it.key());
        }
    }
    result.sort();
    return result;
}
```

### 2.3  Define canonical tag constants

**File:** `libs/phosphor-rule/include/PhosphorRule/RuleAction.h`

Add inside `namespace ActionType`:

```cpp
namespace Tag {
inline constexpr QLatin1StringView Effect{"effect"};
inline constexpr QLatin1StringView Border{"border"};
inline constexpr QLatin1StringView Animation{"animation"};
inline constexpr QLatin1StringView LayoutEngine{"layout-engine"};
inline constexpr QLatin1StringView Gap{"gap"};
} // namespace Tag
```

### 2.4  Populate metadata on all existing descriptors

**File:** `libs/phosphor-rule/src/ruleaction.cpp` — in `registerBuiltins()`

Every existing `registerAction(...)` call gets `.category`, `.displayOrder`, and
`.tags` populated.  Example:

```cpp
registerAction(ActionDescriptor{
    .type = QString(ActionType::SetEngineMode),
    // ... existing fields unchanged ...
    .category = QStringLiteral("layout-engine"),
    .displayOrder = 10,
    .tags = {QString(ActionType::Tag::LayoutEngine)},
});

registerAction(ActionDescriptor{
    .type = QString(ActionType::OverrideAnimationShader),
    // ... existing fields unchanged ...
    .category = QStringLiteral("animation"),
    .displayOrder = 10,
    .tags = {QString(ActionType::Tag::Effect), QString(ActionType::Tag::Animation)},
});

registerAction(ActionDescriptor{
    .type = QString(ActionType::SetBorderColor),
    // ... existing fields unchanged ...
    .category = QStringLiteral("appearance"),
    .displayOrder = 50,
    .tags = {QString(ActionType::Tag::Effect), QString(ActionType::Tag::Border)},
});
```

Complete mapping (all 26 registered actions):

| Action | category | tags |
|--------|----------|------|
| SetEngineMode | `layout-engine` | `layout-engine` |
| SetSnappingLayout | `layout-engine` | `layout-engine` |
| SetTilingAlgorithm | `layout-engine` | `layout-engine` |
| DisableEngine | `layout-engine` | `layout-engine` |
| LockContext | `layout-engine` | `layout-engine` |
| SetZonePadding | `gaps` | `gap` |
| SetOuterGap | `gaps` | `gap` |
| SetUsePerSideOuterGap | `gaps` | `gap` |
| SetOuterGapTop | `gaps` | `gap` |
| SetOuterGapBottom | `gaps` | `gap` |
| SetOuterGapLeft | `gaps` | `gap` |
| SetOuterGapRight | `gaps` | `gap` |
| Exclude | `window` | — |
| Float | `window` | — |
| RestorePosition | `window` | — |
| SetOpacity | `appearance` | `effect` |
| SetHideTitleBar | `appearance` | `effect`, `border` |
| SetBorderVisible | `appearance` | `effect`, `border` |
| SetBorderWidth | `appearance` | `effect`, `border` |
| SetBorderRadius | `appearance` | `effect`, `border` |
| SetBorderColor | `appearance` | `effect`, `border` |
| OverrideAnimationShader | `animation` | `effect`, `animation` |
| OverrideAnimationTiming | `animation` | `effect`, `animation` |
| OverrideAnimationCurve | `animation` | `effect`, `animation` |
| ExcludeAnimations | `animation` | — |
| TilingAlgorithm (alias) | — | — |

### 2.5  Tests

**File:** `libs/phosphor-rule/tests/test_actionregistry.cpp`

Add tests:

```cpp
void testHasTag()
{
    auto& reg = ActionRegistry::instance();
    QVERIFY(reg.hasTag(QString(ActionType::SetOpacity), ActionType::Tag::Effect));
    QVERIFY(!reg.hasTag(QString(ActionType::Exclude), ActionType::Tag::Effect));
    QVERIFY(reg.hasTag(QString(ActionType::SetBorderColor), ActionType::Tag::Border));
}

void testTypesWithTag()
{
    auto& reg = ActionRegistry::instance();
    const auto effectTypes = reg.typesWithTag(ActionType::Tag::Effect);
    QVERIFY(effectTypes.contains(QString(ActionType::SetOpacity)));
    QVERIFY(effectTypes.contains(QString(ActionType::OverrideAnimationShader)));
    QVERIFY(!effectTypes.contains(QString(ActionType::Exclude)));
}

void testDescriptorCategory()
{
    auto& reg = ActionRegistry::instance();
    auto desc = reg.descriptor(QString(ActionType::SetEngineMode));
    QVERIFY(desc.has_value());
    QCOMPARE(desc->category, QStringLiteral("layout-engine"));
    QVERIFY(desc->displayOrder > 0);
}
```

### 2.6  Verification

- Docker build + `ctest --output-on-failure`
- No API signature changes to existing methods

---

## Phase 3 — Settings Layer Consolidation (`ruleauthoring.cpp`)

### 3.1  Rewrite `actionCategory()` to read from descriptor metadata

Replace the ~25-line if-chain with:

```cpp
PickerCategory actionCategory(const QString& type)
{
    static const QHash<QString, PickerCategory> kCategoryLabels = {
        {QStringLiteral("layout-engine"), {PhosphorI18n::tr("Layout & engine"), 0}},
        {QStringLiteral("gaps"),          {PhosphorI18n::tr("Gaps"), 1}},
        {QStringLiteral("window"),        {PhosphorI18n::tr("Window"), 2}},
        {QStringLiteral("appearance"),    {PhosphorI18n::tr("Appearance"), 3}},
        {QStringLiteral("animation"),     {PhosphorI18n::tr("Animation"), 4}},
    };
    auto desc = PhosphorRule::ActionRegistry::instance().descriptor(type);
    if (desc.has_value() && !desc->category.isEmpty()) {
        auto it = kCategoryLabels.constFind(desc->category);
        if (it != kCategoryLabels.constEnd()) {
            return *it;
        }
    }
    return {PhosphorI18n::tr("Other"), 99};
}
```

### 3.2  Consolidate `actionTypeLabelImpl` / `paramLabel` / `enumOptionLabel`

Replace the three scattered if-chains with a single `ActionLabels` lookup table:

```cpp
struct ActionLabels {
    QString typeLabel;
    QHash<QString, QString> paramLabels;
    QHash<QString, QHash<QString, QString>> enumLabels;
};

static const QHash<QString, ActionLabels>& actionLabelRegistry()
{
    static const QHash<QString, ActionLabels> s = {
        {QString(ActionType::SetEngineMode), {
            .typeLabel = PhosphorI18n::tr("Set engine mode"),
            .paramLabels = {{QString(ActionParam::Mode), PhosphorI18n::tr("Engine mode")}},
            .enumLabels = {{QString(ActionParam::Mode), {
                {QStringLiteral("snapping"), PhosphorI18n::tr("Snapping")},
                {QStringLiteral("autotile"), PhosphorI18n::tr("Autotile")},
                {QStringLiteral("scrolling"), PhosphorI18n::tr("Scrolling")},
            }}},
        }},
        // ... one entry per action type ...
    };
    return s;
}

QString actionTypeLabelImpl(const QString& type)
{
    auto it = actionLabelRegistry().constFind(type);
    if (it != actionLabelRegistry().constEnd()) {
        return it->typeLabel;
    }
    return RuleModel::actionTypeFallbackLabel(type);
}

QString paramLabel(const QString& type, const QString& key)
{
    auto it = actionLabelRegistry().constFind(type);
    if (it != actionLabelRegistry().constEnd()) {
        return it->paramLabels.value(key, key);
    }
    return key;
}

QString enumOptionLabel(const QString& type, const QString& key, const QString& wireValue)
{
    auto it = actionLabelRegistry().constFind(type);
    if (it != actionLabelRegistry().constEnd()) {
        return it->enumLabels.value(key).value(wireValue, wireValue);
    }
    return wireValue;
}
```

### 3.3  Remove `kPreferredOrder` if it still exists

The picker now sorts by `(descriptor.category, descriptor.displayOrder)` —
any hand-maintained `kPreferredOrder` list is obsolete.

### 3.4  Verification

- Docker build + ctest
- Manual: the settings UI produces the same action picker grouping/ordering

---

## Phase 4 — Migrate Grouping Helper Call-Sites

### 4.1  KWin effect: `shader_transitions.cpp`

Replace:
```cpp
if (PhosphorRule::ActionType::isEffectRuleAction(action.type)) {
```
With:
```cpp
if (PhosphorRule::ActionRegistry::instance().hasTag(action.type, PhosphorRule::ActionType::Tag::Effect)) {
```

### 4.2  Settings: `rulemodel.cpp`

Replace the two call-sites (`isAnimationOverrideAction`, `isLayoutEngineContextAction`)
with `hasTag` queries against `Tag::Animation` and `Tag::LayoutEngine`.

### 4.3  Mark old helpers `[[deprecated]]`

In `RuleAction.h`, mark `isEffectRuleAction`, `isBorderAppearanceAction`,
`isAnimationOverrideAction`, `isLayoutEngineContextAction` with:

```cpp
[[deprecated("Use ActionRegistry::hasTag(type, Tag::Effect)")]]
```

### 4.4  Verification

- Docker build with `-Werror` off (deprecated warnings expected at this point)
- ctest passes
- All deprecated-warning sites have been migrated
- Remove the deprecated functions (or leave deprecated for one release cycle)

---

## Phase 5 — Cleanup & Documentation

### 5.1  Range-comment the Field enum

```cpp
enum class Field : int {
    // ── Generic window properties [0, 29] ──
    AppId = 0,
    ...
    CaptionNormal = 29,

    // ── PlasmaZones extension fields [30, 32] ──
    IsFloating = 30,
    IsSnapped = 31,
    Zone = 32,
};
```

### 5.2  Delete dead code

- Remove the old `fieldFromString` static table (replaced by `kFieldTable` iteration)
- Remove the old switch bodies from `fieldToString`, `fieldIsString`, etc. (already
  replaced in Phase 1)

### 5.3  Commit sequence

One commit per phase.  Each passes `ctest --output-on-failure`.

---

## Resulting "Add a New Field" Checklist (After Refactor)

1. `MatchTypes.h` — enum value + `kFieldTable` row **(2 lines, 1 file)**
2. `MatchTypes.h` — bump `FieldCount` **(1 line, same file)**
3. `WindowQuery.h` — add the `std::optional<T>` member **(1 line)**
4. `WindowQuery.h` — add the `case` in `valueForField()` **(2 lines)**
5. `window_query.cpp` (KWin effect) — populate from `EffectWindow*` **(~3 lines)**
6. `ruleauthoring.cpp` (GPL) — `fieldDescription()` + `fieldCategory()` **(~4 lines)**

**Total: 6 sites, down from 14.  Library-internal: 4 lines in 2 cases of the same
file, down from 8 edits across 6 switch/function bodies.**

## Resulting "Add a New Action" Checklist (After Refactor)

1. `RuleAction.h` — constexpr type + slot **(2 lines)**
2. `ruleaction.cpp` — `registerAction({...category, tags, displayOrder...})` **(~15 lines)**
3. `ruleauthoring.cpp` — one `ActionLabels` entry **(~5 lines)**

**Total: 3 sites in 3 files, down from 5+ sites scattered across 4+ functions.**

---

## Risk Assessment

| Phase | Risk | Mitigation |
|-------|------|------------|
| 1 | Table ordering invariant violated | `static_assert` + test enforce |
| 1 | `fieldFromString` perf (linear scan vs old linear scan) | Same asymptotic; table is 33 entries |
| 2 | Designated initializers with QStringList | C++20 required (already mandated) |
| 3 | i18n string extraction breaks | `PhosphorI18n::tr(...)` calls remain literal — `lupdate` extracts them |
| 4 | Missed call-site migration | `-Wdeprecated` catches them at compile time |
