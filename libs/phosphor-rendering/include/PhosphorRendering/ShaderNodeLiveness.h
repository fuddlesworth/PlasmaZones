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
 * first. ~ShaderNodeRhi takes @c mutex and nulls @c node as its FIRST
 * statement; ShaderEffect takes the same mutex across every use of the
 * pointer. A node therefore cannot be destroyed mid-call, and a null read is
 * positive proof the node is gone rather than a guess.
 *
 * Lock ordering: ShaderEffect::m_renderNodeMutex is taken BEFORE this one, and
 * ShaderNodeRhi::m_itemMutex AFTER it. Nothing acquires them in the reverse
 * order, so the three cannot deadlock.
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
