<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-fsloader

> Generic filesystem-backed registry skeleton: directory walking, file
> watching, debounced rescans, user-wins-over-system layering, and a
> pluggable scan strategy. The single owner of "live-reload a directory
> tree of user-editable JSON" across the suite.

## Responsibility

Walk a set of directories, parse each file (flat `*.json`, or a
metadata-pack subdirectory) into a typed record, watch the directories
for live edits, debounce rescans, apply user files that shadow bundled
ones with the same ID, and emit one bulk-update signal per scan.

The library factors the work into:

- **`WatchedDirectorySet`** is the base mechanism. It owns the
  `QFileSystemWatcher`, debounces rescan triggers (50 ms), promotes
  watches to a parent directory when the target doesn't exist yet,
  guards against rescan-during-rescan races, and re-arms individual
  file watches after a scan.
- **`IScanStrategy`** is the pluggable policy for how to enumerate the
  registered directories, parse each entry, and commit results to the
  consumer's registry. The base owns *when* to scan, and the strategy owns
  *what scanning means*.
- **`DirectoryLoader`** is the flat `*.json` specialisation that pairs with
  an `IDirectoryLoaderSink` (the schema-specific parse + commit
  strategy). Used by curve and profile loaders.
- **`MetadataPackScanStrategy<Payload>`** is the templated specialisation for
  "subdirectory-with-metadata-json" packs. There is one pack per top-level
  subfolder, validated by a `metadata.json`, with the payload type chosen by the
  registry. Hosted by `PhosphorRegistry::MetadataPackLoader<T>`, which owns the
  strategy and the watcher and adds the per-id fingerprint diff. Used by
  [`phosphor-shaders`](../phosphor-shaders/README.md)' `ShaderRegistry`,
  [`phosphor-animation`](../phosphor-animation/README.md)'s
  `AnimationShaderRegistry`, and `PhosphorSurfaceShaders::SurfaceShaderRegistry`.
- **`validateJsonEnvelope`** is the shared envelope validation. It parses the
  file, checks the `"name"` field is non-empty and matches the
  filename, and returns a `JsonEnvelope` carrying the rest of the JSON
  object for the sink's schema-specific `fromJson`.

A metadata-pack registry does not inherit anything from this library: it
COMPOSES a `PhosphorRegistry::MetadataPackLoader<T>`, which owns the
`WatchedDirectorySet` plus the strategy and provides the search-path management
surface (`addSearchPath`, `setUserPath`, `refresh`) every consumer was otherwise
hand-rolling.

## Key types

| Type | Purpose |
|------|---------|
| `PhosphorFsLoader::WatchedDirectorySet`           | Base mechanism: watcher, debounce, parent-watch, race guard |
| `PhosphorFsLoader::IScanStrategy`                 | Pluggable enumerate / parse / commit policy |
| `PhosphorFsLoader::DirectoryLoader`               | Flat `*.json` specialisation (sink-driven) |
| `PhosphorFsLoader::IDirectoryLoaderSink`          | Per-schema strategy: `parseFile()` + `commitBatch()` |
| `PhosphorFsLoader::ParsedEntry`                   | Parse-result value type with source-path metadata and `std::any` payload |
| `PhosphorFsLoader::MetadataPackScanStrategy<P>`   | Subdirectory-with-`metadata.json` strategy |
| `PhosphorFsLoader::validateJsonEnvelope`          | Shared `"name"`-field envelope validator returning a `JsonEnvelope` |

## Typical use

A profile loader sink (flat `*.json` mode):

```cpp
#include <PhosphorFsLoader/DirectoryLoader.h>
#include <PhosphorFsLoader/IDirectoryLoaderSink.h>

using namespace PhosphorFsLoader;

class CurveLoaderSink : public IDirectoryLoaderSink {
    std::optional<ParsedEntry> parseFile(const QString& filePath) override {
        // parse one curve file; nothing registry-side here.
    }
    void commitBatch(const QStringList& removedKeys,
                     const QList<ParsedEntry>& currentEntries) override {
        // single mutation point; emit one reloadAll() signal here.
    }
};

CurveLoaderSink sink{...};
DirectoryLoader loader(sink);  // sink is borrowed for the loader's lifetime
loader.loadFromDirectories({systemDir, userDir}, LiveReload::On);
loader.requestRescan();
```

A metadata-pack registry (used by shader / animation-shader registries):

```cpp
// `MyPack` must derive PhosphorRegistry::IFactoryBase — the loader
// static_asserts on it. The registry is OWNED by the consumer and BORROWED by
// the loader, so declare it first.
class MyPackRegistry : public QObject {
    Q_OBJECT
public:
    MyPackRegistry()
        : m_loader(std::make_unique<PhosphorRegistry::MetadataPackLoader<MyPack>>(
              &m_registry,
              [](const QString& subdir, const QJsonObject& root, bool isUser)
                  -> std::shared_ptr<MyPack> {
                  return parseMyPack(subdir, root, isUser);
              },
              myLogCategory()))
    {
        m_loader->setOnCommitted([this] { Q_EMIT packsChanged(); });
    }

    void addSearchPath(const QString& dir) { m_loader->addSearchPath(dir); }
    void setUserPath(const QString& dir) { m_loader->setUserPath(dir); }
    // … expose payload-typed lookups over m_registry

Q_SIGNALS:
    void packsChanged();

private:
    PhosphorRegistry::Registry<MyPack> m_registry;
    std::unique_ptr<PhosphorRegistry::MetadataPackLoader<MyPack>> m_loader;
};
// Composition root then wires:
registry.addSearchPath(systemDir);
registry.setUserPath(userDir);
registry.refresh();
```

## Design notes

- **Watcher is opt-in.** `LiveReload::On` installs a
  `QFileSystemWatcher` on every scanned directory (or its parent, if
  the target doesn't exist yet, so fresh installs that create the
  user-data dir later still pick up edits without a restart). `LiveReload::Off`
  disables it for tests.
- **`commitBatch` is the one mutation point.** The sink only touches
  its target registry inside `commitBatch`, so bulk signals (e.g. a
  QML `reloadAll`) coalesce to one emit per scan.
- **User wins on collision.** When a system file and a user file share
  an ID, the user file commits. Search-path order is consumer-chosen, and
  the loader honours it.
- **Type-erased payloads.** `ParsedEntry::payload` is `std::any` so the
  loader stays schema-agnostic. The sink produces it, the sink
  consumes it, nobody in between peeks. The metadata-pack variant adds
  a typed `Payload` template parameter for registries that don't need
  the erasure.

## Dependencies

- `QtCore`

## See also

- [`phosphor-animation`](../phosphor-animation/README.md) — `ProfileLoader`, `CurveLoader`, and `AnimationShaderRegistry` are clients.
- [`phosphor-shaders`](../phosphor-shaders/README.md) — `ShaderRegistry` composes a `MetadataPackLoader` over this library's strategy.
