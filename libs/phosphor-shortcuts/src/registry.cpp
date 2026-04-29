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
    // A null backend yields a permanently-broken registry; flag it loudly in
    // both debug and release builds so misuse doesn't hide behind silent
    // flush short-circuits.
    Q_ASSERT_X(backend, "Phosphor::Shortcuts::Registry", "backend must not be null");
    if (!backend) {
        qCCritical(lcPhosphorShortcuts)
            << "Registry constructed with null backend — all bind/flush calls will be silently dropped";
        return;
    }

    connect(backend, &IBackend::activated, this, &Registry::onBackendActivated);
    connect(backend, &IBackend::ready, this, &Registry::onBackendReady);
}

Registry::~Registry() = default;

void Registry::bind(const QString& id, const QKeySequence& defaultSeq, const QString& description,
                    std::function<void()> callback, bool persistent)
{
    auto it = m_entries.find(id);
    if (it == m_entries.end()) {
        Entry entry;
        entry.binding.id = id;
        entry.binding.defaultSeq = defaultSeq;
        entry.binding.currentSeq = defaultSeq;
        entry.binding.description = description;
        entry.callback = std::move(callback);
        entry.persistent = persistent;
        // lastSent* stay empty; flush() compares against the binding's current
        // values to decide what to send. A bind() with an empty default stays
        // out of the backend (see flush()) until a later rebind() supplies a
        // non-empty sequence — that closes the stale-grab hazard from
        // discussion #155 at the bind entry point.
        m_entries.insert(id, std::move(entry));
        return;
    }

    // Update in place but preserve currentSeq so a prior rebind (e.g. from
    // the user's config) is not clobbered when consumers re-apply compiled-in
    // defaults via bind() for a hot-reload. Description is stored but only
    // propagated to the backend on the next registerShortcut call (it's not
    // carried by updateShortcut); callers that rely on live description edits
    // should unbind + bind.
    it->binding.defaultSeq = defaultSeq;
    it->binding.description = description;
    it->callback = std::move(callback);
    it->persistent = persistent;
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
        // Backend died after Registry construction (it's a QPointer). Without
        // a backend we can't honour any flush; warn so the misuse is visible
        // in production logs instead of silently dropping every grab.
        qCWarning(lcPhosphorShortcuts)
            << "Registry::flush(): backend is gone — entries will not reach any backend until a new Registry is built";
        return;
    }

    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        auto& entry = *it;
        if (entry.binding.currentSeq.isEmpty()) {
            // Don't ask the backend to grab nothing — skips the stale-grab
            // hazard on KGlobalAccel. Entry stays in the registry (so a
            // later rebind() to a non-empty seq will register it) and we
            // don't mark it sent because nothing has been sent.
            continue;
        }

        const bool needsRegister = !entry.registered || entry.lastSentDefault != entry.binding.defaultSeq;
        if (needsRegister) {
            // First flush for this id, OR the compiled-in default has changed
            // since the last send. Call registerShortcut in both cases: on the
            // first flush it's the only way to hand both the compiled-in
            // default (for KGlobalAccel's setDefaultShortcut / portal's
            // preferred_trigger) and the current user value (what actually
            // gets grabbed) to the backend in one shot. On a default change,
            // re-calling registerShortcut lets the backend refresh its
            // "reset to default" target — KGlobalAccelBackend and
            // PortalBackend both handle this idempotently.
            m_backend->registerShortcut(entry.binding.id, entry.binding.defaultSeq, entry.binding.currentSeq,
                                        entry.binding.description);
            entry.registered = true;
            entry.lastSentDefault = entry.binding.defaultSeq;
            entry.lastSentCurrent = entry.binding.currentSeq;
        } else if (entry.lastSentCurrent != entry.binding.currentSeq) {
            m_backend->updateShortcut(entry.binding.id, entry.binding.defaultSeq, entry.binding.currentSeq);
            entry.lastSentCurrent = entry.binding.currentSeq;
        }
        // else: nothing changed for this id since the last flush — skip.
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

QVector<Registry::Binding> Registry::bindings(bool persistentOnly) const
{
    QVector<Binding> out;
    out.reserve(m_entries.size());
    for (const auto& e : m_entries) {
        if (persistentOnly && !e.persistent) {
            continue;
        }
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
