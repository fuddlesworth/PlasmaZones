// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PhosphorShortcuts/Registry.h"

#include "PhosphorShortcuts/IBackend.h"
#include "shortcutslogging.h"

namespace Phosphor::Shortcuts {

Registry::Registry(IBackend* backend, QObject* parent)
    : QObject(parent)
    , m_backend(backend)
{
    Q_ASSERT_X(backend, "Phosphor::Shortcuts::Registry", "backend must not be null");

    connect(backend, &IBackend::activated, this, &Registry::onBackendActivated);
    connect(backend, &IBackend::ready, this, &Registry::onBackendReady);
}

Registry::~Registry() = default;

void Registry::bind(const QString& id, const QKeySequence& defaultSeq, const QString& description,
                    std::function<void()> callback)
{
    auto& entry = m_entries[id];
    entry.binding.id = id;
    entry.binding.defaultSeq = defaultSeq;
    entry.binding.currentSeq = defaultSeq;
    entry.binding.description = description;
    entry.callback = std::move(callback);
    entry.dirty = true;
}

void Registry::rebind(const QString& id, const QKeySequence& seq)
{
    auto it = m_entries.find(id);
    if (it == m_entries.end()) {
        qCWarning(lcPhosphorShortcuts) << "rebind(): unknown shortcut id" << id;
        return;
    }
    if (it->binding.currentSeq == seq) {
        return;
    }
    it->binding.currentSeq = seq;
    it->dirty = true;
}

void Registry::unbind(const QString& id)
{
    if (m_entries.remove(id) > 0 && m_backend) {
        m_backend->unregisterShortcut(id);
    }
}

void Registry::flush()
{
    if (!m_backend) {
        return;
    }

    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        if (!it->dirty) {
            continue;
        }
        // First flush for an id is a registerShortcut(); subsequent flushes
        // are updateShortcut(). The backend distinguishes internally — both
        // codepaths are idempotent and queue until the batch commit below.
        m_backend->registerShortcut(it->binding.id, it->binding.currentSeq, it->binding.description);
        m_backend->updateShortcut(it->binding.id, it->binding.currentSeq);
        it->dirty = false;
    }

    m_backend->flush();
}

QKeySequence Registry::shortcut(const QString& id) const
{
    const auto it = m_entries.constFind(id);
    if (it == m_entries.constEnd()) {
        return {};
    }
    return it->binding.currentSeq;
}

QVector<Registry::Binding> Registry::bindings() const
{
    QVector<Binding> out;
    out.reserve(m_entries.size());
    for (const auto& e : m_entries) {
        out.push_back(e.binding);
    }
    return out;
}

void Registry::onBackendActivated(QString id)
{
    const auto it = m_entries.constFind(id);
    if (it != m_entries.constEnd() && it->callback) {
        it->callback();
    }
    Q_EMIT triggered(id);
}

void Registry::onBackendReady()
{
    Q_EMIT ready();
}

} // namespace Phosphor::Shortcuts
