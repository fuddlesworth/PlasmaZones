// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <mutex>

namespace PhosphorRendering {

class ShaderNodeRhi;

/**
 * @brief Shared liveness block linking a render node to the ShaderEffect that
 *        tracks it.
 *
 * The scene graph owns render nodes and deletes them on the render thread
 * without telling the item. An item that tracked its node with a plain pointer
 * therefore could not tell "already deleted" from "still live" and had to
 * guess from proxies (does the item still have a window?). That guess failed
 * open on the detach-then-destroy path: the node outlived the item, its
 * back-pointer was never severed, and the next prepare() walked a freed
 * QQuickItem.
 *
 * Both sides hold this block by shared_ptr, so it outlives whichever dies
 * first. The node nulls @c node under @c mutex via
 * ShaderNodeRhi::retractLiveness(); ShaderEffect takes the same mutex across
 * every use of the pointer. A node therefore cannot be destroyed mid-call, and
 * a null read is positive proof the node is gone rather than a guess.
 *
 * Retraction is per-destructor, not once at the base. C++ runs the
 * most-derived destructor body (and its member teardown) BEFORE the base's, so
 * a retract that lived only in ~ShaderNodeRhi would leave the block
 * advertising a node whose derived half was already gone. Every ShaderNodeRhi
 * subclass therefore calls retractLiveness() as the first statement of its own
 * destructor; the call is idempotent, so the base's repeat costs one
 * uncontended lock.
 *
 * Lock ordering: ShaderNodeRhi::m_itemMutex is taken AFTER this one, never
 * before, so the two cannot deadlock. ShaderEffect::m_renderNodeMutex is a
 * third mutex that is never held at the same time as this one at all — see
 * ShaderEffect::withTrackedNode, which copies the share out and releases it
 * before locking here.
 *
 * This lives in its own header so both ShaderEffect.h and ShaderNodeRhi.h can
 * hold a `std::shared_ptr<ShaderNodeLiveness>` member with the type complete —
 * an incomplete type there would make every translation unit that merely
 * destroys a ShaderEffect subclass depend on transitively pulling in
 * ShaderNodeRhi.h (and with it qrhi.h).
 */
struct ShaderNodeLiveness
{
    std::mutex mutex;
    ShaderNodeRhi* node = nullptr;
};

} // namespace PhosphorRendering
