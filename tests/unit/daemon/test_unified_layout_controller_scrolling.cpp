// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_unified_layout_controller_scrolling.cpp
 * @brief UnifiedLayoutController's Templates (scrolling) semantics.
 *
 * Pins the two controller members the daemon's scrolling gates lean on:
 *
 *  - displayIdForAssignment: the bare "scrolling:" sentinel substitutes the
 *    context's assigned TEMPLATE layout UUID (so the picker highlights the
 *    template card and the cycle anchors on it); a template-less context and
 *    every non-sentinel id pass through unchanged.
 *  - applyEntry's manual-layout branch: with the LIVE capability pushed as
 *    Templates AND the cascade in Scrolling mode, a manual pick is REFUSED
 *    (since the native-template pivot a layout is not a template, and a
 *    store-validated template write would silently clear the slot); with
 *    the capability downgraded to Placement the same pick applies as an
 *    ordinary snapping assignment, matching what the router says the screen
 *    actually runs.
 *
 * The controller is constructed the DI-lean way its own doc allows: null
 * ScreenManager / algorithm registry / autotile engine, since none of the
 * paths under test touch them.
 */

#include <QScopedPointer>
#include <QTest>

#include <PhosphorLayoutApi/LayoutId.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/ScrollingTemplate.h>
#include <PhosphorZones/ScrollingTemplateStore.h>

#include "config/settings.h"
#include "daemon/controllers/unifiedlayoutcontroller.h"
#include "helpers/IsolatedConfigGuard.h"
#include "helpers/LayoutRegistryTestHelpers.h"

#include <QDir>
#include <memory>
#include <vector>

#include <PhosphorZones/Zone.h>

using namespace PlasmaZones;
using LayoutSupport = PhosphorEngine::IPlacementEngine::LayoutSupport;

class TestUnifiedLayoutControllerScrolling : public QObject
{
    Q_OBJECT

private:
    std::vector<std::unique_ptr<TestHelpers::IsolatedConfigGuard>> m_guards;
    std::vector<std::unique_ptr<PhosphorZones::ScrollingTemplateStore>> m_stores;

    PhosphorZones::ScrollingTemplateStore* attachTemplateStore(PhosphorZones::LayoutRegistry* mgr)
    {
        m_stores.emplace_back(std::make_unique<PhosphorZones::ScrollingTemplateStore>());
        mgr->setScrollingTemplateStore(m_stores.back().get());
        return m_stores.back().get();
    }

    QUuid createTestTemplate(PhosphorZones::ScrollingTemplateStore* store, const QString& name)
    {
        PhosphorZones::ScrollingTemplate templ;
        templ.name = name;
        templ.presetColumnWidths = {0.333, 0.5, 0.667};
        const QUuid id = store->saveTemplate(templ);
        // Same guard as LayoutManagerAssignmentFixture's twin: QVERIFY expands
        // to a bare `return;`, which a QUuid-returning helper cannot use, so
        // call the macro's own reporting entry point. A refused save is then
        // the FIRST reported failure instead of whatever the null id goes on
        // to break. The caller is not aborted and still receives a null id.
        if (!QTest::qVerify(!id.isNull(), "!id.isNull()", "template save was refused", __FILE__, __LINE__)) {
            return {};
        }
        return id;
    }

    PhosphorZones::Layout* createTestLayout(const QString& name)
    {
        auto* layout = new PhosphorZones::Layout(name);
        auto* zone = new PhosphorZones::Zone();
        zone->setRelativeGeometry(QRectF(0, 0, 1, 1));
        layout->addZone(zone);
        return layout;
    }

    PhosphorZones::LayoutRegistry* createManager()
    {
        m_guards.emplace_back(std::make_unique<TestHelpers::IsolatedConfigGuard>());
        auto* mgr = TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts"));
        const QString layoutDir = m_guards.back()->dataPath() + QStringLiteral("/plasmazones/layouts");
        QDir().mkpath(layoutDir);
        mgr->setLayoutDirectory(layoutDir);
        return mgr;
    }

private Q_SLOTS:
    void cleanup()
    {
        // Stores first: a store's destructor still resolves paths under the
        // guard's isolated XDG dirs, so the guard has to outlive it.
        m_stores.clear();
        m_guards.clear();
    }

    void displayIdForAssignment_substitutesTemplateForSentinel()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        Settings settings(nullptr);
        auto* store = attachTemplateStore(mgr.data());
        const QUuid templId = createTestTemplate(store, QStringLiteral("Template"));

        UnifiedLayoutController controller(mgr.data(), &settings, nullptr, nullptr);

        const QString sentinel = QString(PhosphorLayout::LayoutId::ScrollingId);
        const int desktop = mgr->currentVirtualDesktopForScreen(QStringLiteral("DP-1"));

        // Template-less context: the sentinel passes through (matches no
        // card, correctly).
        QCOMPARE(controller.displayIdForAssignment(QStringLiteral("DP-1"), sentinel), sentinel);

        // With a template assigned, the sentinel resolves to its UUID.
        mgr->assignScrollingTemplate(QStringLiteral("DP-1"), desktop, QString(), templId.toString());
        QCOMPARE(controller.displayIdForAssignment(QStringLiteral("DP-1"), sentinel), templId.toString());

        // Every non-sentinel id is identity, template or not.
        QCOMPARE(controller.displayIdForAssignment(QStringLiteral("DP-1"), QStringLiteral("autotile:bsp")),
                 QStringLiteral("autotile:bsp"));
        const QString uuid = templId.toString();
        QCOMPARE(controller.displayIdForAssignment(QStringLiteral("DP-1"), uuid), uuid);
        // An empty screen id cannot resolve a context; sentinel passes through.
        QCOMPARE(controller.displayIdForAssignment(QString(), sentinel), sentinel);
    }

    void applyEntry_templatesScreen_refusesManualPick()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        Settings settings(nullptr);
        auto* layout = createTestLayout(QStringLiteral("Manual"));
        mgr->addLayout(layout);
        auto* store = attachTemplateStore(mgr.data());
        const QUuid templId = createTestTemplate(store, QStringLiteral("Template"));

        UnifiedLayoutController controller(mgr.data(), &settings, nullptr, nullptr);
        controller.setCurrentScreenName(QStringLiteral("DP-1"));
        controller.setCurrentLayoutSupport(LayoutSupport::Templates);

        const int desktop = mgr->currentVirtualDesktopForScreen(QStringLiteral("DP-1"));
        mgr->assignLayoutById(QStringLiteral("DP-1"), desktop, QString(),
                              QString(PhosphorLayout::LayoutId::ScrollingId));
        mgr->assignScrollingTemplate(QStringLiteral("DP-1"), desktop, QString(), templId.toString());

        // A manual-layout pick is refused on a live-Templates screen: since
        // the native-template pivot a layout is not a template, and the
        // context's ASSIGNED template must survive the refused press.
        QVERIFY(!controller.applyLayoutById(layout->id().toString()));
        QCOMPARE(mgr->modeForScreen(QStringLiteral("DP-1"), desktop), PhosphorZones::AssignmentEntry::Scrolling);
        QCOMPARE(mgr->scrollingTemplateForContext(QStringLiteral("DP-1"), desktop, QString()).id, templId);
    }

    void applyEntry_downgradedScrollingScreen_appliesAsSnapping()
    {
        // Cascade Scrolling but the LIVE capability says Placement (the
        // router downgraded a disabled/switched-off scrolling assignment):
        // the pick must apply as an ordinary snapping assignment — writing a
        // dead template would change nothing the user can see.
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        Settings settings(nullptr);
        auto* layout = createTestLayout(QStringLiteral("Manual"));
        mgr->addLayout(layout);
        // A store with a real template assigned, so the two readers below can
        // actually disagree. Without a wired store every template resolve
        // answers "no template" and the closing assertions would hold whatever
        // the downgrade did to the stored field.
        auto* store = attachTemplateStore(mgr.data());
        const QUuid templId = createTestTemplate(store, QStringLiteral("Template"));

        UnifiedLayoutController controller(mgr.data(), &settings, nullptr, nullptr);
        controller.setCurrentScreenName(QStringLiteral("DP-1"));
        controller.setCurrentLayoutSupport(LayoutSupport::Placement);

        const int desktop = mgr->currentVirtualDesktopForScreen(QStringLiteral("DP-1"));
        // assignScrollingTemplate flips the mode to Scrolling and stores the
        // template id, which is the cascade state a router downgrade sits on
        // top of.
        mgr->assignScrollingTemplate(QStringLiteral("DP-1"), desktop, QString(), templId.toString());
        QCOMPARE(mgr->modeForScreen(QStringLiteral("DP-1"), desktop), PhosphorZones::AssignmentEntry::Scrolling);
        QCOMPARE(mgr->scrollingTemplateForContext(QStringLiteral("DP-1"), desktop, QString()).id, templId);

        QVERIFY(controller.applyLayoutById(layout->id().toString()));

        QCOMPARE(mgr->modeForScreen(QStringLiteral("DP-1"), desktop), PhosphorZones::AssignmentEntry::Snapping);
        QCOMPARE(mgr->layoutForScreen(QStringLiteral("DP-1"), desktop, QString()), layout);
        // The lossless-toggle contract: the downgrade applies as snapping and
        // must NOT clear the stored template. The raw field reader still holds
        // it, and only the mode-gated resolver goes quiet, so flipping the
        // screen back to scrolling restores the same template.
        QCOMPARE(mgr->scrollingTemplateLayoutForScreen(QStringLiteral("DP-1"), desktop, QString()), templId.toString());
        QVERIFY(!mgr->scrollingTemplateForContext(QStringLiteral("DP-1"), desktop, QString()).isValid());
    }
};

// QTEST_MAIN, not GUILESS: the Settings ctor applies the system colour
// scheme through QGuiApplication::palette(), which needs a Gui application
// (the offscreen QPA the test env installs keeps it headless).
QTEST_MAIN(TestUnifiedLayoutControllerScrolling)
#include "test_unified_layout_controller_scrolling.moc"
