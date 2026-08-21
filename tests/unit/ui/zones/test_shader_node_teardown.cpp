// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>

#include <PhosphorRendering/ShaderEffect.h>
#include <PhosphorRendering/ShaderNodeLiveness.h>
#include <PhosphorRendering/ShaderNodeRhi.h>

#include <memory>

using namespace PhosphorRendering;

namespace {

/// Exposes the protected registration hook so a test can stand in for the
/// scene graph, which never runs headless (updatePaintNode is only called with
/// a live QQuickWindow on a compositor).
class TrackingEffect : public ShaderEffect
{
public:
    using ShaderEffect::registerRenderNode;
};

} // namespace

/**
 * @brief Item/render-node teardown contract for ShaderEffect + ShaderNodeRhi.
 *
 * The scene graph owns render nodes and deletes them on the render thread
 * without telling the item, so the two objects can die in either order. Both
 * orders must leave the survivor with no pointer to the corpse:
 *
 *   • item first  → the node's m_item back-pointer must be severed, or the
 *                   next prepare() dereferences a freed QQuickItem;
 *   • node first  → the item's tracking must go dead, or a later teardown
 *                   calls invalidateItem() on a freed node.
 *
 * The first order is a regression guard. ~ShaderEffect used to sever only when
 * `window()` was non-null, treating a detached item as proof that the scene
 * graph had already deleted the node. It is not: an item reparented out of its
 * window and then destroyed left a live node holding a dangling back-pointer,
 * which the render thread walked on the next frame (SIGSEGV in
 * QQuickWindow::rhi(), reached from ShaderNodeRhi::prepare()).
 */
class TestShaderNodeTeardown : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    /// The regression: a windowless item must still sever its node's
    /// back-pointer. Headless items never have a window, so this is exactly
    /// the state the old `node && window()` guard skipped on.
    void testTeardown_severesBackPointerWhenItemHasNoWindow()
    {
        auto* effect = new TrackingEffect;
        QVERIFY(effect->window() == nullptr);

        // Stand in for the scene graph: hand the item a node the way
        // updatePaintNode would, then keep the node alive past the item.
        auto node = std::make_unique<ShaderNodeRhi>(effect);
        effect->registerRenderNode(node.get());
        QVERIFY(node->hasValidItem());

        delete effect;

        // Before the fix this stayed true and node->m_item dangled.
        QVERIFY(!node->hasValidItem());
    }

    /// A node destroyed before its item must retract itself, so the item's
    /// later teardown does not call into freed memory. Without the node's
    /// half of the contract the item would still hold a raw pointer here.
    void testTeardown_nodeDeletedFirstLeavesNoStaleTracking()
    {
        auto effect = std::make_unique<TrackingEffect>();

        auto* node = new ShaderNodeRhi(effect.get());
        // Take a share of the liveness block BEFORE the node dies. The block
        // outlives the node by design, so this stays readable afterwards and
        // gives the node's half of the contract a direct assertion rather than
        // only an ASAN-visible one.
        const std::shared_ptr<ShaderNodeLiveness> link = node->liveness();
        QCOMPARE(link->node, node);

        effect->registerRenderNode(node);
        delete node;

        // The node retracted itself as it went, so the item now reads "gone"
        // instead of holding a raw pointer into freed memory.
        QVERIFY(link->node == nullptr);

        // And the item's own teardown must skip the sever rather than walking
        // the freed node — an ASAN use-after-free here is the second failure
        // mode this case covers.
        effect.reset();
    }

    /// The other half of the regression, and the only case here that gives the
    /// item a window. `windowChanged` used to clear the tracked node on every
    /// window transition, on the theory that the old window's scene graph had
    /// already taken it for deletion. It had not, and the clear dropped the
    /// only handle to a live node — permanently disarming the destructor's
    /// sever. Detach-then-destroy is exactly the shape the core dump showed.
    /// The windowless cases above all pass with that clear restored, so
    /// without this one the second root cause has no guard at all.
    void testTeardown_severesAfterItemLeavesItsWindow()
    {
        QQuickWindow window;
        auto* effect = new TrackingEffect;
        effect->setParentItem(window.contentItem());
        QCOMPARE(effect->window(), &window);

        auto node = std::make_unique<ShaderNodeRhi>(effect);
        effect->registerRenderNode(node.get());
        QVERIFY(node->hasValidItem());

        // windowChanged(nullptr) fires here. The node is untouched by that —
        // the scene graph, not the reparent, is what deletes nodes.
        effect->setParentItem(nullptr);
        QCOMPARE(effect->window(), nullptr);

        delete effect;

        QVERIFY(!node->hasValidItem());
    }

    /// Re-registering the same node every frame is the documented contract for
    /// subclasses that reimplement updatePaintNode. It must stay idempotent —
    /// and must not disarm the teardown it exists to keep armed.
    void testTeardown_repeatedRegistrationStaysArmed()
    {
        auto* effect = new TrackingEffect;
        auto node = std::make_unique<ShaderNodeRhi>(effect);

        for (int frame = 0; frame < 8; ++frame) {
            effect->registerRenderNode(node.get());
        }
        QVERIFY(node->hasValidItem());

        delete effect;
        QVERIFY(!node->hasValidItem());
    }

    /// Explicit deregistration (updatePaintNode's zero-size branch) must leave
    /// the item with nothing to sever, so a node the item no longer owns is
    /// never touched by its destructor.
    void testTeardown_deregisteredNodeIsLeftAlone()
    {
        auto* effect = new TrackingEffect;
        auto node = std::make_unique<ShaderNodeRhi>(effect);

        effect->registerRenderNode(node.get());
        effect->registerRenderNode(nullptr);

        delete effect;

        // Still valid: the item disowned this node, so it had no business
        // invalidating it. (The zero-size branch severs explicitly, before
        // deregistering, because it deletes the node itself.)
        QVERIFY(node->hasValidItem());
    }
};

QTEST_MAIN(TestShaderNodeTeardown)
#include "test_shader_node_teardown.moc"
