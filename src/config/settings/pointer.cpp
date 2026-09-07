// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "config/settings.h"
#include "config/configdefaults.h"
#include "core/platform/logging.h"

#include "config/settings/settings_detail.h"

#include <PhosphorPointer/PointerProfile.h>

#include <QJsonDocument>
#include <QJsonObject>

namespace PlasmaZones {

// ── Pointer chain (PhosphorConfig::Store-backed) ────────────────────────────
// Two leaf keys under the Pointer group: Enabled (the master switch) and Chain
// (the PointerProfile blob). Persisted the same way the decoration tree is, as
// a nested JSON object written through the Store, so the schema default handles
// sparse persistence: writing a value equal to the schema default drops the key
// rather than materializing it.
//
// Unlike the decoration tree there is no seed overlay. A pointer pack decorates
// the cursor, which is always a deliberate user choice rather than default
// chrome, so the stored blob and the resolved chain are the same value and the
// shipped default is the empty chain.

P_STORE_GET(bool, pointerEnabled, pointerGroup, enabledKey, bool)
P_STORE_SET_BOOL(setPointerEnabled, pointerGroup, enabledKey, pointerEnabledChanged)

PhosphorPointerShaders::PointerProfile Settings::pointerChain() const
{
    const QVariantMap map = m_store->read<QVariantMap>(ConfigDefaults::pointerGroup(), ConfigDefaults::chainKey());
    return PhosphorPointerShaders::PointerProfile::fromJson(QJsonObject::fromVariantMap(map));
}

void Settings::setPointerChain(const PhosphorPointerShaders::PointerProfile& chain)
{
    refreshCleanBackendFromDisk();
    // Canonicalize at the persistence boundary the same way the decoration tree
    // does: fromJson is the profile's own filter (it drops layers with an empty
    // effectId), so a toJson→fromJson round trip is the prune. The read side
    // passes through the same filter, which makes the compare below
    // pruned-vs-pruned and a write-back of a just-read value a genuine no-op.
    const auto pruned = PhosphorPointerShaders::PointerProfile::fromJson(chain.toJson());
    if (pruned == pointerChain()) {
        return;
    }
    m_store->write(ConfigDefaults::pointerGroup(), ConfigDefaults::chainKey(), pruned.toJson().toVariantMap());
    Q_EMIT pointerChainChanged();
    Q_EMIT settingsChanged();
}

QString Settings::pointerChainJson() const
{
    return QString::fromUtf8(QJsonDocument(pointerChain().toJson()).toJson(QJsonDocument::Compact));
}

void Settings::setPointerChainJson(const QString& json)
{
    if (json.isEmpty()) {
        // Empty string = reset to the canonical default, exactly like the
        // decoration tree facade: drop every layer.
        setPointerChain(ConfigDefaults::pointerChain());
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isObject()) {
        qCWarning(lcConfig) << "setPointerChainJson: malformed JSON, ignoring";
        return;
    }
    setPointerChain(PhosphorPointerShaders::PointerProfile::fromJson(doc.object()));
}

} // namespace PlasmaZones
