// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "triggerutils.h"

#include "../config/configdefaults.h"
#include "../core/enums.h"
#include "../core/modifierutils.h"

namespace PlasmaZones::TriggerUtils {

QVariantList convertTriggersForQml(const QVariantList& triggers)
{
    QVariantList result;
    for (const auto& t : triggers) {
        auto map = t.toMap();
        const int modifierValue = map.value(ConfigDefaults::triggerModifierField(), 0).toInt();
        // The header contract requires callers to strip the
        // AlwaysActive sentinel (DragModifier::AlwaysActive == 8)
        // first — `dragModifierToBitmask(8)` returns 0, which would
        // surface as a phantom "no-modifier" chip in QML. Skip the
        // entry defensively so a forgetful caller can't silently
        // poison the QML view.
        if (modifierValue == static_cast<int>(DragModifier::AlwaysActive))
            continue;
        QVariantMap converted;
        converted[ConfigDefaults::triggerModifierField()] = ModifierUtils::dragModifierToBitmask(modifierValue);
        converted[ConfigDefaults::triggerMouseButtonField()] = map.value(ConfigDefaults::triggerMouseButtonField(), 0);
        result.append(converted);
    }
    return result;
}

QVariantList convertTriggersForStorage(const QVariantList& triggers)
{
    QVariantList result;
    for (const auto& t : triggers) {
        auto map = t.toMap();
        QVariantMap stored;
        stored[ConfigDefaults::triggerModifierField()] =
            ModifierUtils::bitmaskToDragModifier(map.value(ConfigDefaults::triggerModifierField(), 0).toInt());
        stored[ConfigDefaults::triggerMouseButtonField()] = map.value(ConfigDefaults::triggerMouseButtonField(), 0);
        result.append(stored);
    }
    return result;
}

bool hasAlwaysActiveTrigger(const QVariantList& triggers)
{
    const int alwaysActive = static_cast<int>(DragModifier::AlwaysActive);
    for (const auto& t : triggers) {
        if (t.toMap().value(ConfigDefaults::triggerModifierField(), 0).toInt() == alwaysActive) {
            return true;
        }
    }
    return false;
}

QVariantList makeAlwaysActiveTriggerList()
{
    QVariantMap trigger;
    trigger[ConfigDefaults::triggerModifierField()] = static_cast<int>(DragModifier::AlwaysActive);
    trigger[ConfigDefaults::triggerMouseButtonField()] = 0;
    return {trigger};
}

QVariantList stripAlwaysActiveTrigger(const QVariantList& triggers)
{
    QVariantList result;
    result.reserve(triggers.size());
    const int alwaysActive = static_cast<int>(DragModifier::AlwaysActive);
    for (const auto& t : triggers) {
        if (t.toMap().value(ConfigDefaults::triggerModifierField(), 0).toInt() == alwaysActive) {
            continue;
        }
        result.append(t);
    }
    return result;
}

QVariantList mergeAlwaysActiveTrigger(const QVariantList& nonSentinelTriggers)
{
    constexpr int max = ConfigDefaults::maxTriggersPerAction();
    QVariantList result;
    result.reserve(qMin(nonSentinelTriggers.size() + 1, max));
    QVariantMap sentinel;
    sentinel[ConfigDefaults::triggerModifierField()] = static_cast<int>(DragModifier::AlwaysActive);
    sentinel[ConfigDefaults::triggerMouseButtonField()] = 0;
    result.append(sentinel);
    for (const auto& t : nonSentinelTriggers) {
        if (result.size() >= max) {
            break;
        }
        result.append(t);
    }
    return result;
}

QVariantList applyAlwaysActiveToggle(const QVariantList& currentStored, bool enabled,
                                     const QVariantList& factoryDefault)
{
    const QVariantList nonSentinel = stripAlwaysActiveTrigger(currentStored);
    if (enabled) {
        return mergeAlwaysActiveTrigger(nonSentinel);
    }
    if (nonSentinel.isEmpty()) {
        return factoryDefault;
    }
    return nonSentinel;
}

QVariantList normaliseExplicitEdit(const QVariantList& fromQml, bool masterToggleOn)
{
    // Strip the sentinel from the QML-authored edit (the widget shouldn't
    // include it — the master toggle owns that bit), then re-merge if the
    // master toggle is currently on. Without the re-merge, the sentinel-
    // free explicit list would silently flip the master toggle off as a
    // side effect of the user editing the non-sentinel chips.
    const QVariantList nonSentinel = stripAlwaysActiveTrigger(convertTriggersForStorage(fromQml));
    return masterToggleOn ? mergeAlwaysActiveTrigger(nonSentinel) : nonSentinel;
}

} // namespace PlasmaZones::TriggerUtils
