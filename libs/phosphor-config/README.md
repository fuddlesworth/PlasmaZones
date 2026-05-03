<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-config

> Pluggable configuration backends: JSON-on-disk (default), QSettings, or a
> custom `IBackend`, with a schema-validated `Store` and a versioned
> migration runner.

## Responsibility

Applications built on Phosphor tend to have structured settings: hierarchical
groups like `Snapping`, `Snapping.Behavior`, `Snapping.Behavior.ZoneSpan`,
typed values (bool, int, color, string), defaults, and validation.
`phosphor-config` gives consumers:

- **A `Store` front-end** with `value(key, default)` and `setValue(key, value)`
  over a pluggable `IBackend`. Backends ship for JSON files, QSettings, and
  an in-memory mock.
- **Schema-driven validation.** The `Schema` declares the tree of groups
  and the type and range of each leaf. Invalid values on load are rejected,
  not silently coerced.
- **Versioned migrations.** `MigrationRunner` chains `v1 -> v2 -> v3 ...`
  transforms against the raw JSON root. Each schema-version bump lands one
  migration function; consumers never have to write per-key fallback reads.
- **Group-path resolution.** `IGroupPathResolver` turns
  `"Snapping.Behavior.ZoneSpan.enabled"` into a lookup the backend
  understands, regardless of whether the backend stores by path or nested
  object.

## Key types

| Type | Purpose |
|------|---------|
| `PhosphorConfig::Store`             | Front-end API: `value()`, `setValue()`, `valueChanged` signal |
| `PhosphorConfig::IBackend`          | Abstract backend. Shipped: `JsonBackend`, `QSettingsBackend`, in-memory mock |
| `PhosphorConfig::JsonBackend`       | JSON-on-disk; path chosen by the consumer (e.g. `$XDG_CONFIG_HOME/<app>/config.json`) |
| `PhosphorConfig::QSettingsBackend`  | QSettings-backed; useful in Qt-only (non-KF6) builds |
| `PhosphorConfig::Schema`            | Declarative group tree with leaf type and range constraints |
| `PhosphorConfig::MigrationRunner`   | Versioned JSON transforms, one function per schema bump |
| `PhosphorConfig::IGroupPathResolver`| Dotted path to backend key mapping |

## Typical use

```cpp
#include <PhosphorConfig/Store.h>
#include <PhosphorConfig/JsonBackend.h>
#include <PhosphorConfig/Schema.h>

using namespace PhosphorConfig;

auto backend = std::make_unique<JsonBackend>(configPath);
Store settings(std::move(backend), myAppSchema());   // consumer builds its own Schema

// Read a value with a type-safe default
bool zoneSpanEnabled =
    settings.value(QStringLiteral("Snapping.Behavior.ZoneSpan.enabled"), false).toBool();

// Write + auto-persist
settings.setValue(QStringLiteral("Snapping.Behavior.ZoneSpan.enabled"), true);

// React to any external change
connect(&settings, &Store::valueChanged,
        this, [](const QString &key) { qDebug() << "changed:" << key; });
```

Migrating between schema versions is one function per bump:

```cpp
// runs automatically on load if on-disk version < 2
MigrationRunner::registerStep(1, 2,
    [](QJsonObject &root) {
        // v1 had a single "snap.enabled" flag; v2 splits into per-edge flags.
        bool was = root.take(QStringLiteral("snap.enabled")).toBool();
        QJsonObject snap;
        snap.insert(QStringLiteral("left"),  was);
        snap.insert(QStringLiteral("right"), was);
        snap.insert(QStringLiteral("top"),   was);
        snap.insert(QStringLiteral("bottom"),was);
        root.insert(QStringLiteral("snap"), snap);
    });
```

## Design notes

- **No ad-hoc backwards compatibility.** The library enforces one migration
  per schema bump and nothing else. No per-key fallback reads outside
  migration functions. Within a schema version, renaming a key means users
  get the default for the new key; no silent rescue. This keeps the
  config-reading code trivial.
- **Schema lives separately from defaults.** The `Schema` declares *what
  exists*; a consumer's `ConfigDefaults` declares *what value*. A settings
  UI can introspect the schema to generate widgets without pulling in the
  application's default values.
- **Backends are completely mockable.** Tests construct a `Store` over an
  in-memory `IBackend` without touching disk.

## Dependencies

- `QtCore` only. Zero Phosphor deps; this is a leaf library.
