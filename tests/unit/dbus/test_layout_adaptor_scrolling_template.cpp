// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_layout_adaptor_scrolling_template.cpp
 * @brief LayoutAdaptor behavioral tests for the scrolling-template surface.
 *
 * Drives the real adaptor methods against a real LayoutRegistry (no bus):
 * the setScrollingTemplateLayout / getScrollingTemplateLayout pair with its
 * validation and clear forms, the mode gate on the getter, and the widened
 * {layoutId, scrollingTemplate} value shape of the three flat batch getters.
 * Also covers the two boundary behaviors the template CRUD verbs owe: the
 * description clamp saveScrollingTemplate applies (the editor's
 * TemplatePropertyPanel.qml maximumLength is advisory, a D-Bus caller skips
 * it entirely) and the quickLayoutSlotsChanged refresh hint
 * deleteScrollingTemplate emits after the
 * id-scrub sweeps a bound quick slot, alongside the resurface guard that keeps
 * that scrub off a shadowed bundled template.
 * Fixture cribbed from test_layout_adaptor_signals.cpp.
 *
 * KNOWN GAP: getAllScreenAssignments' own "scrollingTemplate" field is NOT
 * covered here. That getter iterates m_screenManager->effectiveScreenIds(),
 * and this fixture wires no ScreenManager, so its loop body never runs and a
 * leg written against it would pass vacuously. Covering it needs a
 * ScreenManager stub the fixture does not have. The three FLAT batch getters
 * below take no such dependency and are covered.
 */

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "core/types/constants.h"
#include "dbus/layoutadaptor/layoutadaptor.h"
#include <PhosphorLayoutApi/LayoutId.h>
#include <PhosphorZones/AssignmentEntry.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/ScrollingTemplate.h>
#include <PhosphorZones/ScrollingTemplateStore.h>
#include <PhosphorZones/Zone.h>
#include "helpers/IsolatedConfigGuard.h"
#include "helpers/LayoutRegistryTestHelpers.h"

#include <memory>

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

class TestLayoutAdaptorScrollingTemplate : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_guard = std::make_unique<IsolatedConfigGuard>();
        m_parent = new QObject(nullptr);
        m_layoutManager = PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts"), m_parent);
        m_store = std::make_unique<PhosphorZones::ScrollingTemplateStore>();
        m_layoutManager->setScrollingTemplateStore(m_store.get());
        PhosphorZones::ScrollingTemplate templ;
        templ.name = QStringLiteral("Template");
        templ.presetColumnWidths = {0.5};
        m_templateId = m_store->saveTemplate(templ).toString();
        m_adaptor = new LayoutAdaptor(m_layoutManager, m_parent);
    }

    void cleanup()
    {
        delete m_parent;
        m_parent = nullptr;
        m_layoutManager = nullptr;
        m_adaptor = nullptr;
        m_store.reset();
        m_guard.reset();
    }

    void testSetGet_roundTripFlipsModeToScrolling()
    {
        m_adaptor->setScrollingTemplateLayout(QStringLiteral("DP-1"), 0, QString(), m_templateId);
        QCOMPARE(m_adaptor->getScrollingTemplateLayout(QStringLiteral("DP-1"), 0, QString()), m_templateId);
        QCOMPARE(m_layoutManager->modeForScreen(QStringLiteral("DP-1"), 0), PhosphorZones::AssignmentEntry::Scrolling);
    }

    void testSet_emptyIdClears()
    {
        m_adaptor->setScrollingTemplateLayout(QStringLiteral("DP-1"), 0, QString(), m_templateId);
        m_adaptor->setScrollingTemplateLayout(QStringLiteral("DP-1"), 0, QString(), QString());
        QCOMPARE(m_adaptor->getScrollingTemplateLayout(QStringLiteral("DP-1"), 0, QString()), QString());
        // The clear drops the template and leaves the context Scrolling. That
        // is a FORCE, not a preservation: the clear routes the same
        // assignScrollingTemplate the assigning form does, which stamps
        // AssignmentEntry::Scrolling on the entry it upserts whatever the id
        // is. On an already-Scrolling context the two are indistinguishable,
        // so the Autotile arm below is what pins the force.
        QCOMPARE(m_layoutManager->modeForScreen(QStringLiteral("DP-1"), 0), PhosphorZones::AssignmentEntry::Scrolling);
    }

    void testSet_emptyIdOnNonScrollingContextForcesScrollingMode()
    {
        // The discriminator for the force above. A clear aimed at a context
        // that is NOT scrolling flips its mode and materializes a Scrolling
        // entry where there was none, which is the documented (and published)
        // side effect of routing the clear through the assigning verb.
        m_layoutManager->assignLayoutById(QStringLiteral("DP-2"), 0, QString(), QStringLiteral("autotile:bsp"));
        QCOMPARE(m_layoutManager->modeForScreen(QStringLiteral("DP-2"), 0), PhosphorZones::AssignmentEntry::Autotile);

        m_adaptor->setScrollingTemplateLayout(QStringLiteral("DP-2"), 0, QString(), QString());

        QCOMPARE(m_layoutManager->modeForScreen(QStringLiteral("DP-2"), 0), PhosphorZones::AssignmentEntry::Scrolling);
        QCOMPARE(m_adaptor->getScrollingTemplateLayout(QStringLiteral("DP-2"), 0, QString()), QString());
        // The lossless-toggle contract still holds through the flip: the
        // autotile choice the context carried is preserved, not wiped.
        QCOMPARE(m_layoutManager->tilingAlgorithmForScreen(QStringLiteral("DP-2"), 0), QStringLiteral("bsp"));
    }

    void testSet_unknownUuidRejected()
    {
        m_adaptor->setScrollingTemplateLayout(QStringLiteral("DP-1"), 0, QString(), m_templateId);
        m_adaptor->setScrollingTemplateLayout(QStringLiteral("DP-1"), 0, QString(),
                                              QStringLiteral("{99999999-9999-9999-9999-999999999999}"));
        // Rejected: the stored template is unchanged.
        QCOMPARE(m_adaptor->getScrollingTemplateLayout(QStringLiteral("DP-1"), 0, QString()), m_templateId);
    }

    void testGet_modeGateAnswersEmptyOffScrolling()
    {
        m_adaptor->setScrollingTemplateLayout(QStringLiteral("DP-1"), 0, QString(), m_templateId);
        // Flip the context to Autotile: the template is preserved in the
        // entry (lossless toggle) but the GETTER routes the mode-gated
        // resolver, so it answers empty — the raw field stays readable
        // through the registry's scrollingTemplateLayoutForScreen.
        m_layoutManager->assignLayoutById(QStringLiteral("DP-1"), 0, QString(), QStringLiteral("autotile:bsp"));
        QCOMPARE(m_adaptor->getScrollingTemplateLayout(QStringLiteral("DP-1"), 0, QString()), QString());
        QCOMPARE(m_layoutManager->scrollingTemplateLayoutForScreen(QStringLiteral("DP-1"), 0), m_templateId);
    }

    void testBatchGetters_carryLayoutIdAndTemplate()
    {
        // Desktop projection: a templated Scrolling context on desktop 2.
        m_layoutManager->assignLayoutById(QStringLiteral("DP-1"), 2, QString(),
                                          QString(PhosphorLayout::LayoutId::ScrollingId));
        m_adaptor->setScrollingTemplateLayout(QStringLiteral("DP-1"), 2, QString(), m_templateId);

        const QVariantMap desktops = m_adaptor->getAllDesktopAssignments();
        const QVariantMap desktopValue = desktops.value(QStringLiteral("DP-1|2")).toMap();
        QCOMPARE(desktopValue.value(QStringLiteral("layoutId")).toString(),
                 QString(PhosphorLayout::LayoutId::ScrollingId));
        QCOMPARE(desktopValue.value(QStringLiteral("scrollingTemplate")).toString(), m_templateId);

        // Activity projection: desktop 0 with an activity is the pure-Activity
        // context (the strict classifier keeps desktop-pinned rules out), and
        // its key is screen|activity.
        m_layoutManager->assignLayoutById(QStringLiteral("DP-1"), 0, QStringLiteral("act-y"),
                                          QString(PhosphorLayout::LayoutId::ScrollingId));
        m_adaptor->setScrollingTemplateLayout(QStringLiteral("DP-1"), 0, QStringLiteral("act-y"), m_templateId);

        const QVariantMap activities = m_adaptor->getAllActivityAssignments();
        const QVariantMap activityValue = activities.value(QStringLiteral("DP-1|act-y")).toMap();
        QCOMPARE(activityValue.value(QStringLiteral("layoutId")).toString(),
                 QString(PhosphorLayout::LayoutId::ScrollingId));
        QCOMPARE(activityValue.value(QStringLiteral("scrollingTemplate")).toString(), m_templateId);

        // Combined projection: same shape at the triple-pinned tuple.
        m_adaptor->setScrollingTemplateLayout(QStringLiteral("DP-1"), 3, QStringLiteral("act-x"), m_templateId);
        const QVariantMap combined = m_adaptor->getAllCombinedAssignments();
        const QVariantMap combinedValue = combined.value(QStringLiteral("DP-1|3|act-x")).toMap();
        QCOMPARE(combinedValue.value(QStringLiteral("layoutId")).toString(),
                 QString(PhosphorLayout::LayoutId::ScrollingId));
        QCOMPARE(combinedValue.value(QStringLiteral("scrollingTemplate")).toString(), m_templateId);
    }

    void testSave_clampsOverlongDescription()
    {
        PhosphorZones::ScrollingTemplate templ;
        templ.name = QStringLiteral("Described");
        templ.presetColumnWidths = {0.5};
        templ.description = QString(600, QLatin1Char('a'));
        const QString saved = m_adaptor->saveScrollingTemplate(
            QString::fromUtf8(QJsonDocument(templ.toJson()).toJson(QJsonDocument::Compact)));
        QVERIFY(!saved.isEmpty());

        // Read the description back over the same wire the pickers use, so the
        // clamp is asserted on what a client actually receives.
        bool found = false;
        const QJsonObject stored = storedTemplate(saved, found);
        QVERIFY(found);
        QCOMPARE(stored.value(QLatin1String("description")).toString().size(),
                 PlasmaZones::MaxTemplateDescriptionLength);
    }

    void testSave_clampsOverlongName()
    {
        // The name arm of the same boundary clamp. saveScrollingTemplate calls
        // clampName on BOTH fields, and only the description half was pinned,
        // so a dropped name clamp would have gone unnoticed.
        PhosphorZones::ScrollingTemplate templ;
        templ.name = QString(60, QLatin1Char('n'));
        templ.presetColumnWidths = {0.5};
        const QString saved = m_adaptor->saveScrollingTemplate(
            QString::fromUtf8(QJsonDocument(templ.toJson()).toJson(QJsonDocument::Compact)));
        QVERIFY(!saved.isEmpty());

        bool found = false;
        const QJsonObject stored = storedTemplate(saved, found);
        QVERIFY(found);
        QCOMPARE(stored.value(QLatin1String("name")).toString().size(), PlasmaZones::MaxLayoutNameLength);
    }

    void testSave_descriptionCutAstrideSurrogatePairDropsThePair()
    {
        // The reason the clamp is clampName and not a bare left(): the cut
        // lands BETWEEN the two halves of a non-BMP character, and a stored
        // lone high surrogate serializes as U+FFFD. 499 filler units plus a
        // two-unit emoji is 501, so left(500) would keep the high surrogate
        // alone; the clamp must drop the whole pair and stop at 499.
        const QString emoji = QString::fromUcs4(U"\U0001F600", 1);
        QCOMPARE(emoji.size(), 2);

        PhosphorZones::ScrollingTemplate templ;
        templ.name = QStringLiteral("Astride");
        templ.presetColumnWidths = {0.5};
        templ.description = QString(PlasmaZones::MaxTemplateDescriptionLength - 1, QLatin1Char('a')) + emoji;
        QCOMPARE(templ.description.size(), PlasmaZones::MaxTemplateDescriptionLength + 1);

        const QString saved = m_adaptor->saveScrollingTemplate(
            QString::fromUtf8(QJsonDocument(templ.toJson()).toJson(QJsonDocument::Compact)));
        QVERIFY(!saved.isEmpty());

        bool found = false;
        const QJsonObject stored = storedTemplate(saved, found);
        QVERIFY(found);
        const QString description = stored.value(QLatin1String("description")).toString();
        QCOMPARE(description.size(), PlasmaZones::MaxTemplateDescriptionLength - 1);
        QVERIFY(!description.isEmpty());
        QVERIFY(!description.back().isSurrogate());
    }

    void testDelete_sweepsQuickSlotAndSignals()
    {
        // Bind the template to a scrolling quick slot BEFORE the spy: the
        // setter emits the same signal, and the leg under test is the delete.
        m_adaptor->setQuickLayoutSlot(PhosphorZones::AssignmentEntry::Scrolling, 1, m_templateId);
        QCOMPARE(m_adaptor->getQuickLayoutSlot(PhosphorZones::AssignmentEntry::Scrolling, 1), m_templateId);

        QSignalSpy spy(m_adaptor, &LayoutAdaptor::quickLayoutSlotsChanged);
        QVERIFY(m_adaptor->deleteScrollingTemplate(m_templateId));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(m_adaptor->getQuickLayoutSlot(PhosphorZones::AssignmentEntry::Scrolling, 1), QString());
    }

    void testDelete_shadowedBundledTemplateKeepsReferences()
    {
        // The other side of the delete-scrub's guard
        // (assignment.cpp: `if (!store->contains(parsed))`). Deleting a USER
        // file that shadowed a bundled template does not retire the id: the
        // store rescans, the bundled original resurfaces under the SAME id,
        // and every assignment and quick slot pointing at it is still live.
        // Scrubbing here would drop references to a template the user can
        // still see, so nothing may be swept and no refresh hint may fire.
        QTemporaryDir systemRoot;
        QVERIFY(systemRoot.isValid());
        const QString systemDir =
            systemRoot.path() + QLatin1Char('/') + PhosphorZones::ScrollingTemplateStore::templateSubdirectory();
        QVERIFY(QDir().mkpath(systemDir));

        // XDG_DATA_DIRS pinned to the temp root alone, so the scan sees this
        // bundled template and nothing from a real install on the machine.
        // XDG_DATA_HOME stays on the fixture guard's isolated dir.
        const QByteArray oldDataDirs = qgetenv("XDG_DATA_DIRS");
        qputenv("XDG_DATA_DIRS", systemRoot.path().toUtf8());
        const auto restoreDataDirs = qScopeGuard([&oldDataDirs] {
            if (oldDataDirs.isEmpty()) {
                qunsetenv("XDG_DATA_DIRS");
            } else {
                qputenv("XDG_DATA_DIRS", oldDataDirs);
            }
        });

        PhosphorZones::ScrollingTemplate bundled;
        bundled.id = QUuid::fromString(QStringLiteral("{11111111-1111-1111-1111-111111111111}"));
        bundled.name = QStringLiteral("Bundled");
        bundled.presetColumnWidths = {0.5};
        QFile file(systemDir + QStringLiteral("/bundled.json"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.write(QJsonDocument(bundled.toJson()).toJson()) > 0);
        file.close();

        m_store->loadTemplates();
        QVERIFY(m_store->contains(bundled.id));
        QVERIFY(m_store->templateById(bundled.id).isSystem);

        // Shadow it: a save always writes a USER file, whatever the entry's
        // origin was.
        PhosphorZones::ScrollingTemplate edited = bundled;
        edited.name = QStringLiteral("Bundled (edited)");
        QCOMPARE(m_store->saveTemplate(edited), bundled.id);
        QVERIFY(!m_store->templateById(bundled.id).isSystem);

        const QString shadowedId = bundled.id.toString();
        m_adaptor->setQuickLayoutSlot(PhosphorZones::AssignmentEntry::Scrolling, 2, shadowedId);
        m_adaptor->setScrollingTemplateLayout(QStringLiteral("DP-9"), 0, QString(), shadowedId);
        QCOMPARE(m_adaptor->getScrollingTemplateLayout(QStringLiteral("DP-9"), 0, QString()), shadowedId);

        QSignalSpy spy(m_adaptor, &LayoutAdaptor::quickLayoutSlotsChanged);
        QVERIFY(m_adaptor->deleteScrollingTemplate(shadowedId));

        // The id still resolves, now to the bundled original again.
        QVERIFY(m_store->contains(bundled.id));
        QVERIFY(m_store->templateById(bundled.id).isSystem);
        QCOMPARE(m_store->templateById(bundled.id).name, QStringLiteral("Bundled"));
        // So neither reference was scrubbed, and no refresh hint fired.
        QCOMPARE(spy.count(), 0);
        QCOMPARE(m_adaptor->getQuickLayoutSlot(PhosphorZones::AssignmentEntry::Scrolling, 2), shadowedId);
        QCOMPARE(m_adaptor->getScrollingTemplateLayout(QStringLiteral("DP-9"), 0, QString()), shadowedId);
    }

private:
    /// The stored template with @p id as the pickers receive it, read back
    /// over getScrollingTemplates rather than out of the store, so the
    /// boundary clamps are asserted on what a client actually sees.
    QJsonObject storedTemplate(const QString& id, bool& found) const
    {
        found = false;
        const QJsonArray listed = QJsonDocument::fromJson(m_adaptor->getScrollingTemplates().toUtf8()).array();
        for (const QJsonValue& value : listed) {
            const QJsonObject json = value.toObject();
            if (json.value(QLatin1String("id")).toString() == id) {
                found = true;
                return json;
            }
        }
        return {};
    }

    std::unique_ptr<IsolatedConfigGuard> m_guard;
    std::unique_ptr<PhosphorZones::ScrollingTemplateStore> m_store;
    QObject* m_parent = nullptr;
    PhosphorZones::LayoutRegistry* m_layoutManager = nullptr;
    LayoutAdaptor* m_adaptor = nullptr;
    QString m_templateId;
};

QTEST_GUILESS_MAIN(TestLayoutAdaptorScrollingTemplate)
#include "test_layout_adaptor_scrolling_template.moc"
