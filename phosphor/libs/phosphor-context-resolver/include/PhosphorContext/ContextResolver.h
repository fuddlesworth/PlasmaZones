// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "IContextInputs.h"
#include "IContextResolver.h"
#include "phosphorcontextresolver_export.h"

namespace PhosphorContext {

/**
 * @brief Default @ref IContextResolver implementation backed by three
 *        adapter pointers.
 *
 * Holds non-owning raw pointers to one of each input interface and routes
 * every public call into the right combination of them. No state of its
 * own — every snapshot is computed fresh from the bound adapters at the
 * moment `handleFor()` is called. This is the only concrete resolver the
 * project ships; tests subclass it (or implement @ref IContextResolver
 * directly with their own fakes) when they need to inject behaviour the
 * adapter shims do not surface.
 *
 * The three adapter pointers are all non-null preconditions of the
 * constructor. Passing `nullptr` is an assertion failure in debug builds
 * and a documented crash in release builds — the resolver has no useful
 * behaviour without all three axes.
 */
class PHOSPHORCONTEXTRESOLVER_EXPORT ContextResolver : public IContextResolver
{
public:
    /**
     * @brief Bind the resolver to its three adapter inputs.
     *
     * The adapters must outlive the resolver. Recommended wiring is for
     * Daemon to own the resolver as a `std::unique_ptr` data-member and
     * construct it after the routers/managers it depends on, then tear
     * it down in `~Daemon()` before any of them.
     */
    ContextResolver(IWorkspaceState* workspaceState, IModeProvider* modeProvider, IContextGateSource* gateSource);
    ~ContextResolver() override = default;

    // IContextResolver
    ContextHandle handleFor(const QString& screenId) const override;
    ContextHandle globalHandle() const override;
    ContextHandle handleForMode(const QString& screenId, PhosphorZones::AssignmentEntry::Mode mode) const override;
    ContextHandle handleForPersisted(const QString& screenId, int virtualDesktop,
                                     const QString& activity) const override;
    int currentVirtualDesktop() const override;
    QString currentActivity() const override;
    DisabledReason disabledReason(const ContextHandle& handle) const override;
    bool isLocked(const ContextHandle& handle) const override;

private:
    IWorkspaceState* m_workspaceState; ///< Non-owning; outlives this resolver.
    IModeProvider* m_modeProvider; ///< Non-owning; outlives this resolver.
    IContextGateSource* m_gateSource; ///< Non-owning; outlives this resolver.
};

} // namespace PhosphorContext
