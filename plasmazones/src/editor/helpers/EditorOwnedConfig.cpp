// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "EditorOwnedConfig.h"

#include "../../config/configdefaults.h"
#include "../../config/settingsschema.h"

#include <PhosphorConfig/IBackend.h>
#include <PhosphorConfig/Store.h>

#include <QKeySequence>

namespace PlasmaZones {

namespace {

using CD = ConfigDefaults;

/// An empty or unparseable stored shortcut is INVALID rather than "disabled":
/// the capture field offers no way to clear one, so such a value only comes
/// from a hand-edited config. QKeySequence::fromString does not answer
/// garbage with an empty sequence (it parses to an entry holding
/// Qt::Key_unknown), so every chord is checked for that sentinel.
bool isValidShortcut(const QString& value)
{
    if (value.isEmpty()) {
        return false;
    }
    const QKeySequence parsed = QKeySequence::fromString(value, QKeySequence::PortableText);
    if (parsed.isEmpty()) {
        return false;
    }
    for (int i = 0; i < parsed.count(); ++i) {
        if (parsed[i].key() == Qt::Key_unknown) {
            return false;
        }
    }
    return true;
}

QString readShortcut(const PhosphorConfig::Store& store, const QString& key, const QString& fallback,
                     QStringList* invalidKeys)
{
    const QString value = store.read<QString>(CD::editorShortcutsGroup(), key);
    if (isValidShortcut(value)) {
        return value;
    }
    if (invalidKeys) {
        invalidKeys->append(key);
    }
    return fallback;
}

} // namespace

EditorOwnedConfig::Keys EditorOwnedConfig::allKeys()
{
    return Key::GridSnapping | Key::EdgeSnapping | Key::SnapIntervalX | Key::SnapIntervalY | Key::SnapOverrideModifier
        | Key::DuplicateShortcut | Key::SplitHorizontalShortcut | Key::SplitVerticalShortcut | Key::FillShortcut
        | Key::FillOnDropEnabled | Key::FillOnDropModifier | Key::BackgroundOpacity;
}

EditorOwnedConfig EditorOwnedConfig::read(PhosphorConfig::IBackend& backend, QStringList* invalidShortcutKeys)
{
    // A Store over the caller's backend runs every read through the schema
    // (default for an absent key, the key's validator on a present one). It
    // borrows the backend and adds nothing to it the factory has not already
    // installed, so it is safe to build per call.
    const PhosphorConfig::Store store(&backend, cachedSettingsSchema());

    EditorOwnedConfig c;
    c.gridSnappingEnabled = store.read<bool>(CD::editorSnappingGroup(), CD::gridEnabledKey());
    c.edgeSnappingEnabled = store.read<bool>(CD::editorSnappingGroup(), CD::edgeEnabledKey());
    c.snapIntervalX = store.read<double>(CD::editorSnappingGroup(), CD::intervalXKey());
    c.snapIntervalY = store.read<double>(CD::editorSnappingGroup(), CD::intervalYKey());
    c.snapOverrideModifier = store.read<int>(CD::editorSnappingGroup(), CD::overrideModifierKey());
    c.duplicateShortcut = readShortcut(store, CD::duplicateKey(), CD::editorDuplicateShortcut(), invalidShortcutKeys);
    c.splitHorizontalShortcut =
        readShortcut(store, CD::splitHorizontalKey(), CD::editorSplitHorizontalShortcut(), invalidShortcutKeys);
    c.splitVerticalShortcut =
        readShortcut(store, CD::splitVerticalKey(), CD::editorSplitVerticalShortcut(), invalidShortcutKeys);
    c.fillShortcut = readShortcut(store, CD::fillKey(), CD::editorFillShortcut(), invalidShortcutKeys);
    c.fillOnDropEnabled = store.read<bool>(CD::editorFillOnDropGroup(), CD::enabledKey());
    c.fillOnDropModifier = store.read<int>(CD::editorFillOnDropGroup(), CD::modifierKey());
    c.backgroundOpacity = store.read<double>(CD::editorBackgroundGroup(), CD::opacityKey());
    return c;
}

void EditorOwnedConfig::write(PhosphorConfig::IBackend& backend, Keys keys) const
{
    // Through the schema for the same reasons as read(), and for one more:
    // Store::write stores a default-equal value as absence, so the editor
    // never freezes a default into the file the way a raw write would.
    PhosphorConfig::Store store(&backend, cachedSettingsSchema());

    const auto put = [&](Key key, const QString& group, const QString& name, const QVariant& value) {
        if (keys.testFlag(key)) {
            store.write(group, name, value);
        }
    };
    put(Key::GridSnapping, CD::editorSnappingGroup(), CD::gridEnabledKey(), gridSnappingEnabled);
    put(Key::EdgeSnapping, CD::editorSnappingGroup(), CD::edgeEnabledKey(), edgeSnappingEnabled);
    put(Key::SnapIntervalX, CD::editorSnappingGroup(), CD::intervalXKey(), snapIntervalX);
    put(Key::SnapIntervalY, CD::editorSnappingGroup(), CD::intervalYKey(), snapIntervalY);
    put(Key::SnapOverrideModifier, CD::editorSnappingGroup(), CD::overrideModifierKey(), snapOverrideModifier);
    put(Key::DuplicateShortcut, CD::editorShortcutsGroup(), CD::duplicateKey(), duplicateShortcut);
    put(Key::SplitHorizontalShortcut, CD::editorShortcutsGroup(), CD::splitHorizontalKey(), splitHorizontalShortcut);
    put(Key::SplitVerticalShortcut, CD::editorShortcutsGroup(), CD::splitVerticalKey(), splitVerticalShortcut);
    put(Key::FillShortcut, CD::editorShortcutsGroup(), CD::fillKey(), fillShortcut);
    put(Key::FillOnDropEnabled, CD::editorFillOnDropGroup(), CD::enabledKey(), fillOnDropEnabled);
    put(Key::FillOnDropModifier, CD::editorFillOnDropGroup(), CD::modifierKey(), fillOnDropModifier);
    put(Key::BackgroundOpacity, CD::editorBackgroundGroup(), CD::opacityKey(), backgroundOpacity);
}

} // namespace PlasmaZones
