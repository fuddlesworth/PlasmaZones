<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-config

> Pluggable configuration backends: JSON-on-disk or a
> custom `IBackend`, with a schema-validated `Store` and a versioned
> migration runner.

## Responsibility

Structured settings addressed by `(group, key)`, where the group name may be
flat (`General`) or dot-path (`Snapping.Behavior.ZoneSpan`), with typed values
(bool, int, double, color, string, map, list), defaults, and validation. The
library provides:

- **A `Store` front-end** with `read<T>(group, key)`, `readVariant(group, key)`,
  and `write(group, key, value)` over a pluggable `IBackend`. The library ships
  `JsonBackend`, and tests can supply their own in-memory `IBackend`.
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
| `PhosphorConfig::IBackend`          | Abstract backend. Shipped: `JsonBackend` |
| `PhosphorConfig::JsonBackend`       | JSON-on-disk, with the path chosen by the consumer (e.g. `$XDG_CONFIG_HOME/<app>/config.json`). `setOverlay()` lays read-only in-memory values over the file |
| `PhosphorConfig::QSettingsBackend`  | Read-only reader for a legacy KConfig-style INI file, for migration onto `JsonBackend` |
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

Command-line style overrides go in an overlay rather than a rewritten file.
The overlay has the document's shape, wins over the file for every read
(nested objects merge key by key), survives `reparseConfiguration()`, and is
never written back:

```cpp
// afterglow -o Audio.enabled=false
backend->setOverlay(QJsonObject{
    {QStringLiteral("Audio"), QJsonObject{{QStringLiteral("enabled"), false}}},
});
settings.read<bool>(QStringLiteral("Audio"), QStringLiteral("enabled")); // false
```

`Store`'s writes, resets and imports decide against the file
(`IBackend::persistedGroup()`), so writing a value equal to the overlay's
still persists it.

A file that exists but does not parse, or whose root is not an object, is
never overwritten. `loadFailed()` turns on, the backend reads as the last
document it read or wrote cleanly (empty on a first load), and `sync()`,
`commit()` and `applyMigration()` refuse rather than write over the user's
file. `flushNow` also re-checks the file's size and mtime against the last
load, so a file broken by hand after it was read is refused too. The overlay
stays on top of the fallback document. A later `reparseConfiguration()` or
`reload()` that reads the file cleanly clears the flag. `reload()` is the same
reload for a caller that reports problems itself: it logs nothing and returns
the reason the file could not be read. A file that holds only white space reads
as an empty document, as a missing one does, and `blankFile()` tells it from a
file that holds `{}`: an editor that saves in place empties the file before it
writes. A caller that reloads at once constructs the backend with
`JsonBackend::Load::Deferred`, so the file is read and reported one time. Until
that first reload the backend is empty and refuses writes. A machine-written file with
nothing worth keeping opts out with `setOverwriteUnreadable(true)`.

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
