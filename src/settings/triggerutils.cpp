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
        QVariantMap converted;
        converted[ConfigDefaults::triggerModifierField()] =
            ModifierUtils::dragModifierToBitmask(map.value(ConfigDefaults::triggerModifierField(), 0).toInt());
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

} // namespace PlasmaZones::TriggerUtils
