# PhosphorConfig — API Design Document

## Overview

PhosphorConfig is a Qt6/C++20 library that replaces hand-rolled load/save code
with a declarative schema. A consumer describes each configuration key once —
group, default, expected type, optional validator — and PhosphorConfig handles
reads, writes, reset-to-default, export/import, change notification, and
schema-version migrations.

**License:** LGPL-2.1-or-later
**Namespace:** `PhosphorConfig`
**Depends on:** QtCore, QtGui (for `QColor`)

## Dependency Graph

```
PhosphorConfig                       PlasmaZones
(declarative schema,                 (Settings, per-screen resolver,
 pluggable backends,                  v1→v2 migration chain)
 migration runner)
        │                                   │
        └──────── PUBLIC link ──────────────┘
```

PhosphorConfig ships with two built-in backends (`JsonBackend`,
`QSettingsBackend`) and is entirely free of PlasmaZones concepts. A consumer
plugs in its own naming conventions through `IGroupPathResolver` and its own
migration functions through `Schema::migrations`.

---

## Design Principles

1. **Declarative, not imperative** — consumers describe keys once; the library
   handles every I/O operation on them.
2. **One validator slot, uniform application** — the same closure runs on
   every read, every write, and every import, so clamping and canonicalisation
   can never diverge between code paths.
3. **No lock-in** — `IBackend` is the abstraction; consumers can swap
   `JsonBackend` for `QSettingsBackend` (or a custom implementation) without
   touching schema declarations.
4. **Migrations are explicit and versioned** — schema bumps pair with a
   migration function that must stamp the new version. A missing step aborts
   the chain with a critical log, not a silent downgrade.
5. **Crash-safe writes** — `JsonBackend` commits via `QSaveFile` (temp file +
   atomic rename) and preserves file permissions across saves.
6. **Defensive at the boundary, trusting inside** — the library validates
   config coming from disk or from import calls, but assumes in-process
   callers obey its contracts (single-active-group, migrated-before-use).

---

## Public API

### 1. `Schema` — declarative configuration description

Plain-data struct aggregating everything the library needs to know about a
store: current version, version key, groups + keys, migration chain, optional
name resolver.

```cpp
PhosphorConfig::Schema schema;
schema.version = 2;
schema.groups[QStringLiteral("Window")] = {
    {QStringLiteral("Width"),  800,  QMetaType::Int},
    {QStringLiteral("Height"), 600,  QMetaType::Int},
    {QStringLiteral("Title"),  QStringLiteral("Hello"), QMetaType::QString},
};
```

`KeyDef` fields:

| Field              | Purpose                                                   |
|--------------------|-----------------------------------------------------------|
| `key`              | Leaf key name                                             |
| `defaultValue`     | Returned on missing / unparseable values                  |
| `expectedType`     | Optional type hint (warn on mismatched writes)            |
| `description`      | Free text (surfaced in generated UIs / docs)              |
| `validator`        | Coercion applied on every read *and* write               |

**Validator contract:** idempotent. `validator(validator(x)) == validator(x)`.
Non-idempotent validators would defeat `Store::write`'s equality-skip and
cause repeated writes to fire `changed()` on every save.

### 2. `IBackend` / `IGroup` — the storage abstraction

```cpp
class IBackend {
  virtual std::unique_ptr<IGroup> group(const QString& name) = 0;
  virtual void reparseConfiguration() = 0;
  virtual bool sync() = 0;
  virtual void deleteGroup(const QString& name) = 0;
  virtual QStringList groupList() const = 0;
  virtual QString readRootString(...) const = 0;
  virtual void writeRootString(...) = 0;
  virtual void removeRootKey(...) = 0;
};
```

`IGroup` is a scoped view into one group: typed read/write accessors, a
structured-value pair (`readJson`/`writeJson`), `hasKey`/`deleteKey`, and
`keyList()` (scalar leaves only — nested objects are sub-groups).

**Single-active-group invariant.** Concrete backends may only hand out one
live `IGroup` at a time. Debug builds assert; release builds mark subsequent
groups read-only (writes drop with a warning) so the already-live group
retains sole ownership of mutations.

### 3. `Store` — the declarative facade

```cpp
auto backend = std::make_unique<PhosphorConfig::JsonBackend>(path);
PhosphorConfig::Store store(backend.get(), buildSchema());

int w = store.read<int>("Window", "Width");   // returns 800 if unset
store.write("Window", "Width", 1024);
store.reset("Window", "Width");                // back to schema default
QObject::connect(&store, &Store::changed, ui, &Ui::refresh);

QJsonObject snapshot = store.exportToJson();
store.importFromJson(snapshot);                // backup / dotfile sync
```

**Store ownership model.** `Store` borrows its backend; callers own the
backend via `std::unique_ptr` (or equivalent) and ensure it outlives the
store. Multiple `Store`s can share one backend — the first attached resolver
wins, later stores must agree.

**Equality-skip.** `Store::write` compares the incoming (validator-coerced)
value against the raw on-disk value. If they match, the write is skipped and
`changed()` does not fire. The comparison intentionally uses the *raw* disk
value, not a re-validated one, so a canonicalising flush loop can rewrite a
non-canonical value on disk with its canonical equivalent.

**Undeclared keys.** `Store::read<T>` on an undeclared (group, key) returns
`T{}` — the schema default takes precedence over whatever the backend
happens to hold. `Store::write` rejects undeclared keys with a warning, so
values can't be persisted through the store that `read` would refuse to
return.

### 4. `MigrationRunner` — schema-version chain

```cpp
Schema schema;
schema.version = 3;
schema.migrations = {
    {1, &migrateV1ToV2},
    {2, &migrateV2ToV3},
};

PhosphorConfig::MigrationRunner(schema).runOnFile(path);
```

Each `MigrationStep::migrate` callable is responsible for transforming the
root JSON document in-place and stamping the new version under
`Schema::versionKey`. `MigrationRunner` verifies the bump happened before
advancing; a missing bump aborts the chain with a `qCritical` log.

**Construction-time sort.** Steps are sorted by `fromVersion` at
`MigrationRunner` construction, so registration order doesn't matter and
`runInMemory` doesn't pay the sort cost per invocation.

**Atomicity.** `runOnFile` only rewrites the on-disk file when the version
advanced. If the chain aborts halfway (a step's side-effect write fails),
the in-memory mutations are dropped and the file remains at its previous
version. The next startup retries from that version.

### 5. `IGroupPathResolver` — custom group-name semantics

```cpp
class MyResolver : public PhosphorConfig::IGroupPathResolver {
    std::optional<QStringList> toJsonPath(const QString& groupName) const override {
        if (groupName.startsWith(QStringLiteral("Screen:"))) {
            return QStringList{QStringLiteral("Screens"), groupName.mid(7)};
        }
        return std::nullopt;  // fall back to dot-path
    }
    QStringList reservedRootKeys() const override { return {QStringLiteral("Screens")}; }
    QStringList enumerate(const QJsonObject& root) const override { /* ... */ }
};
```

Used by PlasmaZones to map `"ZoneSelector:EDID-..."`-style external names to
a nested `PerScreen/<Category>/<ScreenId>` JSON path. Keeps the per-screen
convention out of PhosphorConfig itself.

**Return conventions:**
- `std::nullopt` — decline, backend falls back to dot-path
- empty list — claim the name but refuse the operation (malformed)
- non-empty list — explicit JSON path

---

## Built-in Backends

### `JsonBackend`

Reads and writes a single JSON document. Writes are deferred until `sync()`
and then committed atomically via `QSaveFile`.

**Dot-path groups.** `"Snapping.Behavior.ZoneSpan"` resolves to a nested
object tree `root["Snapping"]["Behavior"]["ZoneSpan"]`. Depth is capped at
8 segments to guard against pathologically deep paths.

**Permissions preservation.** `writeJsonAtomically` captures the source
file's permissions and re-applies them after commit. A user's
`chmod 600 config.json` survives every subsequent save.

**Warn-once deduplication.** Per-backend `QSet<QString>` keyed by
`<tag>:<key>` suppresses repeated warnings for the same key. Capped at 1024
entries per instance to prevent unbounded growth in long-running daemons.

**Version stamping.** `setVersionStamp(key, version)` causes `sync()` to
insert `key = version` when the root doesn't already contain it. Pairs
with a `Schema` so fresh stores carry the current version from day one.

### `QSettingsBackend`

INI-backed, primarily for reading legacy config during migration. Registers
a custom `QSettings` format that reads standard INI but writes KConfig-style
unquoted `Key=Value` pairs so config files can be inspected by KDE-aware
tools.

**Single-instance invariant.** `QSettings`'s `QConfFile` cache misbehaves
with multiple instances pointing at the same file. Debug builds warn when
the constructor detects a duplicate.

---

## Consumer Pattern: PlasmaZones Settings

PlasmaZones uses PhosphorConfig through three layers:

1. **`buildSettingsSchema()`** (in `settingsschema.cpp`) assembles the
   complete PZ schema via `append*Schema` helpers, one per group.
2. **`Settings`** (in `settings.cpp`) exposes every key as a `Q_PROPERTY`.
   Getters call `m_store->read<T>(group, key)`; setters call
   `m_store->write(...)` and emit `NOTIFY`. The mechanical boilerplate is
   factored into `PZ_STORE_SET_*` macros.
3. **`ConfigMigration`** (in `configmigration.cpp`) registers PZ's v1→v2
   migration with a `MigrationStep` and runs the chain at startup via
   `ensureJsonConfig()`.

**Migration-before-construction contract.** `Settings` comes in two
flavours:
- Owning ctor (standalone tools, tests): runs `ensureJsonConfig()` itself
  and creates the backend.
- Non-owning ctor (daemon sharing one backend across components): assumes
  the caller has already migrated. The daemon's `main.cpp` calls
  `ensureJsonConfig()` before instantiating any component.

**Per-screen path resolver.** PZ attaches `PerScreenPathResolver` to the
shared backend so `"ZoneSelector:DP-1"`-style names route to
`PerScreen/ZoneSelector/DP-1` on disk. The resolver also exposes
`isMalformedPerScreen` / `isPerScreenPrefix` helpers so PZ-side code
(`purgeStaleKeys`, `ConfigMigration::iniMapToJson`) can recognise
per-screen groups without duplicating the convention.

---

## Invariants & Anti-Patterns

### Invariants consumers must uphold

- **Backend lifetime.** The `IBackend*` handed to `Store` must outlive the
  store.
- **Migrated backend.** When the non-owning `Settings` ctor is used, the
  backend must already be at the current schema version.
- **Idempotent validators.** Non-idempotent validators break the
  equality-skip in `Store::write`.
- **Single active group.** Only one live `IGroup` per backend at a time.
- **Migration must stamp the version.** A migration that advances the data
  shape without updating `Schema::versionKey` aborts the chain.

### Anti-patterns

- **Reading raw values in place of Store reads.** The schema validator
  exists to canonicalise values uniformly. Bypassing it via
  `backend->group()->read*` skips clamping and enum-normalisation.
- **Stamping non-numeric version keys.** `Store::importFromJson` refuses
  snapshots whose `_version` is not a JSON number — don't try to store
  `"_version": "2"` (a string).
- **Ad-hoc migration inside setters.** If a key's on-disk shape changes,
  bump the schema version and add a migration step. Per-key fallback
  reads outside of `configmigration.cpp` rot and never get removed
  (see CLAUDE.md `No Ad-Hoc Backwards Compatibility`).
- **Sharing a backend across mutually-configured stores without
  agreeing on the resolver.** First attached resolver wins; later
  stores get a warning and silently use the first store's resolver.

---

## Testing

The library ships three test suites under `phosphorconfig/tests/`:

- `test_json_backend.cpp` — atomic writes, dot-path round-trips,
  permissions preservation, native-JSON storage, key-list shape,
  resolver plumbing.
- `test_migration_runner.cpp` — chain ordering, version stamping,
  bump-mismatch detection, `runOnFile` atomicity.
- `test_store.cpp` — read/write/reset/import/export, validator
  clamping and canonicalisation, undeclared-key handling, native
  QVariantList/QVariantMap round-trips, int64/uint range fallbacks,
  migration-on-construction, concurrent-group fallback (release only),
  version-type import rejection.

PZ-side coverage in `tests/unit/config/test_configmigration.cpp` locks
the INI→JSON + v1→v2 shape, and a schema/migration cross-check
(`testSchemaCoversEveryMigrationDestinationKey`) asserts that every
key the migration writes into the v2 tree is declared in
`buildSettingsSchema()`, catching silent drift at test time.

---

## Future Work

- **Drop the `dynamic_cast<JsonBackend*>` in `Store::Store`.** Version
  stamping, resolver wiring, and in-memory migration are JsonBackend-only
  today. Exposing these through the `IBackend` interface (default
  no-ops; JsonBackend overrides) removes the RTTI downcast and lets a
  future backend participate in the same ceremony.
- **Structured-value round-trip on `QSettingsBackend`.** The default
  `IGroup::writeJson`/`readJson` implementation now stores strings in
  quoted form so the pair round-trips uniformly. A future hardening
  could distinguish "stored via writeString" from "stored via writeJson"
  with a sidecar type annotation.
- **Async sync.** `sync()` is synchronous. A daemon with high write
  frequency could benefit from a debounced or background-thread commit
  path, with the existing dirty flag as the source of truth.

---

## License Boundary

PhosphorConfig is LGPL-2.1-or-later so that proprietary consumers can
link it dynamically. PlasmaZones (GPL-3.0-or-later) links it statically
inside its own binary; the license permits this because PlasmaZones
itself is GPL and the LGPL's "derivative work" clause is satisfied.
External consumers that want a proprietary-compatible option dynamic-link
it and ship their own schema.
