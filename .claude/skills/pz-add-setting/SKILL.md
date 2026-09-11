---
name: "PZ Add Setting"
description: "Add, rename or remove a PlasmaZones setting across every file the value has to touch, in the store-backed shape the config actually uses. Use when: add a setting, new config option, new preference, expose this in settings, make this configurable, add a toggle."
---

<!--
SPDX-FileCopyrightText: 2026 fuddlesworth
SPDX-License-Identifier: GPL-3.0-or-later
-->

# PZ Add Setting

A setting is spread over six files. Getting five of six right produces a value
that reads back as its type-default with no error anywhere, so work down the
list and verify each one.

**The CLAUDE.md summary of this is out of date.** It describes a member-variable
setting with explicit load/save/reset arms. The live pattern is store-backed:
the getter reads through `m_store` on every call, there is **no member
variable**, and there is **no load/save/reset arm** to write. Defaults and
clamping come from the schema. Follow this file, not that summary.

## The six files

### 1. `src/config/configdefaults_<area>.h` — the default value

`configdefaults.h` is split by area: `_appearance`, `_gaps`, `_limits`,
`_screens`, `_scrolling`, `_scrolling_behavior`, `_scrolling_shortcuts`,
`_shaders`. Pick the matching one.

```cpp
static bool enableAudioVisualizer() { return false; }
static int  audioSpectrumBarCount() { return 64; }
// A clamped numeric setting also declares its bounds here, constexpr:
static constexpr int audioSpectrumBarCountMin() { return 16; }
static constexpr int audioSpectrumBarCountMax() { return 256; }
```

### 2. `src/config/configdefaults.h` — the group and key accessors

Only if the group or key is new. Group names are v2 dot-paths mirroring the UI
hierarchy (`"Snapping.Behavior.ZoneSpan"`). Key accessors are generic
(`enabledKey()`, `triggersKey()`) because the group disambiguates them.

Never inline a config path as a `QStringLiteral`. `scripts/check-conventions.py`
fails the build on that.

### 3. `src/config/settingsschema*.cpp` — register the key

This is the step that is easy to miss and the reason a setting silently reads
back as `false` or `0`: **the store gets its default and its type from the
schema**, not from the getter. Split across `settingsschema.cpp`,
`settingsschema_tiling.cpp`, `settingsschema_scrolling.cpp`,
`settingsschema_overlayshaders.cpp`.

```cpp
schema.groups[CD::shadersAudioGroup()] = {
    {CD::enabledKey(), CD::enableAudioVisualizer(), QMetaType::Bool,
     QStringLiteral("Capture system audio so the audio-reactive shader packs "
                    "have a signal to follow. Off, those shaders render but stay still.")},
    {CD::barsKey(), CD::audioSpectrumBarCount(), QMetaType::Int,
     QStringLiteral("Number of frequency bands in the audio visualization."),
     clampInt(CD::audioSpectrumBarCountMin(), CD::audioSpectrumBarCountMax())},
};
```

The 4th field is a **user-facing description** that settings UIs and generated
docs surface, so it is held to the plain-prose rules: no em-dash splice, no
clause-splicing semicolon, no spaced hyphen. The conventions checker enforces
that. The 5th field is the coercion applied on **every read and every write**,
which is what makes the clamped-setter idiom below necessary.

### 4. `src/core/interfaces/isettings.h` — the signal

Add it under `Q_SIGNALS:`, past tense, named `<property>Changed`.

### 5. `src/config/settings.h` — the property

```cpp
Q_PROPERTY(bool enableAudioVisualizer READ enableAudioVisualizer
           WRITE setEnableAudioVisualizer NOTIFY enableAudioVisualizerChanged)
...
bool enableAudioVisualizer() const override;
void setEnableAudioVisualizer(bool enable) override;
```

No member. `settings.h` is already 2600+ lines and grandfathered in the
file-size baseline, so the conventions checker fails if it grows. Adding a
property to it means shrinking something else, or the addition belongs in a
different header.

### 6. `src/config/settings/<concern>.cpp` — getter and setter

Never `src/config/settings.cpp`. Pick the file matching the concern:
`setters.cpp`, `storescalars.cpp`, `shortcuts.cpp`, `scrolling.cpp`,
`triggers.cpp`, `perscreen.cpp`, `disable.cpp`, `uienums.cpp`,
`profiletrees.cpp`, `animationprofile.cpp`, `systemcolors.cpp`.

Note there are three files named `settings.cpp` in the tree (`src/config/`,
`src/daemon/overlayservice/`, `src/editor/controller/`). Always use full paths.

**Unclamped setting** — compare, early-return, write, emit:

```cpp
bool Settings::enableAudioVisualizer() const
{
    return m_store->read<bool>(ConfigDefaults::shadersAudioGroup(), ConfigDefaults::enabledKey());
}

void Settings::setEnableAudioVisualizer(bool enable)
{
    if (enableAudioVisualizer() == enable) {
        return;
    }
    m_store->write(ConfigDefaults::shadersAudioGroup(), ConfigDefaults::enabledKey(), enable);
    Q_EMIT enableAudioVisualizerChanged();
    Q_EMIT settingsChanged();
}
```

**Clamped setting** — write first, then compare, because the schema's coercion
runs on the write and the stored value may not be the value passed in. Comparing
before writing would suppress the signal on a value that actually changed, and
emit on one that did not:

```cpp
void Settings::setAudioSpectrumBarCount(int count)
{
    const int before = audioSpectrumBarCount();
    m_store->write(ConfigDefaults::shadersAudioGroup(), ConfigDefaults::barsKey(), count);
    if (audioSpectrumBarCount() == before) {
        return;
    }
    Q_EMIT audioSpectrumBarCountChanged();
    Q_EMIT settingsChanged();
}
```

Both `<property>Changed` and `settingsChanged` are emitted, and only when the
value actually changed.

## Renaming or removing a setting

No ad-hoc migration. Within a schema version, just use the new key; old values
are silently dropped and users get the default. Do not add a fallback read, do
not write an empty string to clear the old key, do not special-case the rename.

Only a `ConfigSchemaVersion` bump is different: exactly one migration function
in `configmigration.cpp` plus one `MigrationStep` registry entry, transforming
the whole JSON root and stamping the new `_version`.

## Persistence is sparse

A write whose value equals the default **deletes the key** rather than storing
it. A test that asserts "the key is present after setting it to the default
value" is asserting the opposite of the intended behaviour.

## Then

- Settings that must reach the daemon or the effect need the D-Bus side too.
- `cmake --build build --target update-ts` if you added translatable strings.
- `python3 scripts/check-conventions.py`
- Build and test via the `pz-build` skill.
