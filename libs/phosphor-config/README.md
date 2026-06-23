<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-config

> Pluggable configuration backends: JSON-on-disk, QSettings, or a
> custom `IBackend`, with a schema-validated `Store` and a versioned
> migration runner.

## Responsibility

Structured settings addressed by `(group, key)`, where the group name may be
flat (`General`) or dot-path (`Snapping.Behavior.ZoneSpan`), with typed values
(bool, int, double, color, string, map, list), defaults, and validation. The
library provides:

- **A `Store` front-end** with `read<T>(group, key)`, `readVariant(group, key)`,
  and `write(group, key, value)` over a pluggable `IBackend`. The library ships
  `JsonBackend` and `QSettingsBackend`, and tests can supply their own in-memory
  `IBackend`.
- **Schema-driven defaults and validation.** The `Schema` declares the groups
  and, per `KeyDef`, the default value, optional expected type, and an optional
  validator applied on both read and write.
- **Versioned migrations.** `MigrationRunner` runs the `Schema`'s
  `MigrationStep` chain `v1 -> v2 -> v3 ...` against the raw JSON root. Each
  schema-version bump lands one migration step, and consumers never have to write
  per-key fallback reads.
- **Group-path resolution.** An optional `IGroupPathResolver` on the `Schema`
  translates group names into a lookup the backend understands, regardless of
  whether the backend stores by path or nested object.

## Key types

| Type | Purpose |
|------|---------|
| `PhosphorConfig::Store`             | Front-end API: `read<T>()`, `readVariant()`, `write()`, `reset()`, `changed()` signal |
| `PhosphorConfig::IBackend`          | Abstract backend. Shipped: `JsonBackend`, `QSettingsBackend` |
| `PhosphorConfig::JsonBackend`       | JSON-on-disk, with the path chosen by the consumer (e.g. `$XDG_CONFIG_HOME/<app>/config.json`) |
| `PhosphorConfig::QSettingsBackend`  | QSettings-backed, useful in Qt-only (non-KF6) builds |
| `PhosphorConfig::Schema`            | Declarative struct: groups of `KeyDef` (default + expected type + validator), version, migration chain |
| `PhosphorConfig::MigrationRunner`   | Runs the schema's `MigrationStep` chain, one step per schema bump |
| `PhosphorConfig::IGroupPathResolver`| Group-name to backend-key mapping |

## Typical use

```cpp
#include <PhosphorConfig/Store.h>
#include <PhosphorConfig/JsonBackend.h>
#include <PhosphorConfig/Schema.h>

using namespace PhosphorConfig;

// The backend is borrowed: the caller keeps it alive for the store's lifetime.
auto backend = std::make_unique<JsonBackend>(configPath);
Store settings(backend.get(), myAppSchema());   // consumer builds its own Schema

// Read a declared key; the schema default applies when it is unset.
bool zoneSpanEnabled =
    settings.read<bool>(QStringLiteral("Snapping.Behavior.ZoneSpan"),
                        QStringLiteral("enabled"));

// Write + auto-persist
settings.write(QStringLiteral("Snapping.Behavior.ZoneSpan"),
               QStringLiteral("enabled"), true);

// React to any write or reset
connect(&settings, &Store::changed,
        this, [](const QString &group, const QString &key) {
            qDebug() << "changed:" << group << key;
        });
```

Migrations are declared as `MigrationStep` entries on the `Schema` and run by
`MigrationRunner` (one step per schema bump):

```cpp
// Run once at startup, before opening the backend against the same path.
Schema schema = myAppSchema();
schema.version = 2;
schema.migrations.push_back(MigrationStep{
    .fromVersion = 1,
    .migrate = [](QJsonObject &root) {
        // v1 had a single "snap.enabled" flag; v2 splits into per-edge flags.
        bool was = root.take(QStringLiteral("snap.enabled")).toBool();
        QJsonObject snap;
        snap.insert(QStringLiteral("left"),  was);
        snap.insert(QStringLiteral("right"), was);
        snap.insert(QStringLiteral("top"),   was);
        snap.insert(QStringLiteral("bottom"), was);
        root.insert(QStringLiteral("snap"), snap);
        root.insert(QStringLiteral("_version"), 2);  // each step bumps the version
    },
});

MigrationRunner(schema).runOnFile(configPath);
```

## Design notes

- **No ad-hoc backwards compatibility.** The library enforces one migration
  per schema bump and nothing else. No per-key fallback reads outside
  migration functions. Within a schema version, renaming a key means users
  get the default for the new key, with no silent rescue. This keeps the
  config-reading code trivial.
- **Per-key validators.** Each `KeyDef` may carry a validator applied on both
  read and write, giving consumers one place to clamp ranges (`qBound`),
  normalize enum-style strings, or fall back to the default on invalid input.
  Validators must be idempotent so `write()` can short-circuit no-op saves.
- **Schema carries the defaults.** Each `KeyDef` declares its own default value,
  so a settings UI can introspect the schema to generate widgets and the read
  path can fall back without re-declared defaults at call sites.
- **Backends are completely mockable.** `Store` borrows an `IBackend*`, so tests
  construct a `Store` over their own in-memory `IBackend` without touching disk.

## Dependencies

- `QtCore`, `QtGui`. Zero Phosphor deps. This is a leaf library.
