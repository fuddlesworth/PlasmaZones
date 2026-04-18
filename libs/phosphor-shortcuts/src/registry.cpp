// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PhosphorShortcuts/Registry.h"

#include "PhosphorShortcuts/IBackend.h"
#include "shortcutslogging.h"

#include <algorithm>

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
    auto it = m_entries.find(id);
    if (it == m_entries.end()) {
        Entry entry;
        entry.binding.id = id;
        entry.binding.defaultSeq = defaultSeq;
        entry.binding.currentSeq = defaultSeq;
        entry.binding.description = description;
        entry.callback = std::move(callback);
        entry.dirty = true;
        entry.registered = false;
        m_entries.insert(id, std::move(entry));
        return;
    }

    // Update in place but preserve currentSeq so a prior rebind (e.g. from
    // the user's config) is not clobbered when consumers re-apply compiled-in
    // defaults via bind() for a hot-reload.
    const bool defaultChanged = (it->binding.defaultSeq != defaultSeq);
    const bool descriptionChanged = (it->binding.description != description);
    it->binding.defaultSeq = defaultSeq;
    it->binding.description = description;
    it->callback = std::move(callback);
    if (defaultChanged || descriptionChanged) {
        it->dirty = true;
    }
}

void Registry::rebind(const QString& id, const QKeySequence& seq)
{
    auto it = m_entries.find(id);
    if (it == m_entries.end()) {
        qCWarning(lcPhosphorShortcuts) << "rebind(): unknown shortcut id" << id;
        return;
    }
    if (seq.isEmpty()) {
        // An empty sequence would otherwise leave the grab registered with no
        // key (the pre-library stale-Wayland-grab hazard from discussion #155).
        // Route through unbind() so the backend drops the grab cleanly.
        unbind(id);
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
        if (!it->registered) {
            m_backend->registerShortcut(it->binding.id, it->binding.currentSeq, it->binding.description);
            it->registered = true;
        } else {
            m_backend->updateShortcut(it->binding.id, it->binding.currentSeq);
        }
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
    // QHash iteration order is unspecified — sort by id so consumer UIs
    // (KCM lists, settings dialogs) get a deterministic, stable order
    // across runs.
    std::sort(out.begin(), out.end(), [](const Binding& a, const Binding& b) {
        return a.id < b.id;
    });
    return out;
}

void Registry::onBackendActivated(QString id)
{
    // Guard against callbacks that destroy the Registry before we emit
    // triggered(). Q_EMIT on a dangling `this` is UB; a QPointer-based
    // check here is cheap and standard Qt hygiene.
    QPointer<Registry> guard(this);

    const auto it = m_entries.constFind(id);
    if (it != m_entries.constEnd() && it->callback) {
        it->callback();
    }
    if (!guard) {
        return;
    }
    Q_EMIT triggered(id);
}

void Registry::onBackendReady()
{
    Q_EMIT ready();
}

} // namespace Phosphor::Shortcuts
