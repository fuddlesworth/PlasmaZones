// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QSignalSpy>
#include <QTest>
#include <QUrl>

#include "PhosphorSettingsUi/PageController.h"
#include "PhosphorSettingsUi/PageRegistry.h"

using PhosphorSettingsUi::PageController;
using PhosphorSettingsUi::PageRegistry;

namespace {

/** Minimal PageController concretion that lets tests pretend to be dirty
 *  on demand. The lib's PageController is abstract, so tests need a stub. */
class StubPage : public PageController
{
    Q_OBJECT
public:
    explicit StubPage(QString id, QObject* parent = nullptr)
        : PageController(std::move(id), parent)
    {
    }

    bool isDirty() const override
    {
        return m_dirty;
    }
    void apply() override
    {
        setDirty(false);
    }
    void discard() override
    {
        setDirty(false);
    }

    void setDirty(bool d)
    {
        if (m_dirty == d) {
            return;
        }
        m_dirty = d;
        Q_EMIT dirtyChanged();
    }

private:
    bool m_dirty = false;
};

} // namespace

class TestPageRegistry : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void registersTopLevelPage()
    {
        PageRegistry reg;
        QSignalSpy spy(&reg, &PageRegistry::pageRegistered);

        auto* page = new StubPage(QStringLiteral("general"), &reg);
        reg.registerPage({QStringLiteral("general"),
                          QString(),
                          QStringLiteral("General"),
                          {},
                          QUrl(QStringLiteral("qrc:/General.qml")),
                          page});

        QCOMPARE(spy.count(), 1);
        QVERIFY(reg.hasPage(QStringLiteral("general")));
        QCOMPARE(reg.controller(QStringLiteral("general")), page);
        QCOMPARE(reg.topLevelPages().size(), 1);
        QCOMPARE(reg.topLevelPages().first().title, QStringLiteral("General"));
    }

    void rejectsDuplicateId()
    {
        PageRegistry reg;
        auto* a = new StubPage(QStringLiteral("dup"), &reg);
        auto* b = new StubPage(QStringLiteral("dup"), &reg);

        reg.registerPage({QStringLiteral("dup"), {}, QStringLiteral("A"), {}, QUrl(), a});
        reg.registerPage({QStringLiteral("dup"), {}, QStringLiteral("B"), {}, QUrl(), b});

        // First registration wins; second is dropped.
        QCOMPARE(reg.controller(QStringLiteral("dup")), a);
        QCOMPARE(reg.allPages().size(), 1);
    }

    void rejectsUnknownParent()
    {
        PageRegistry reg;
        auto* child = new StubPage(QStringLiteral("child"), &reg);
        reg.registerPage(
            {QStringLiteral("child"), QStringLiteral("ghost-parent"), QStringLiteral("Child"), {}, QUrl(), child});

        QVERIFY(!reg.hasPage(QStringLiteral("child")));
    }

    void exposesChildPages()
    {
        PageRegistry reg;
        auto* parent = new StubPage(QStringLiteral("snap"), &reg);
        auto* childA = new StubPage(QStringLiteral("snap.behavior"), &reg);
        auto* childB = new StubPage(QStringLiteral("snap.appearance"), &reg);
        auto* other = new StubPage(QStringLiteral("anim"), &reg);

        reg.registerPage({QStringLiteral("snap"), {}, QStringLiteral("Snapping"), {}, QUrl(), parent});
        reg.registerPage(
            {QStringLiteral("snap.behavior"), QStringLiteral("snap"), QStringLiteral("Behavior"), {}, QUrl(), childA});
        reg.registerPage({QStringLiteral("snap.appearance"),
                          QStringLiteral("snap"),
                          QStringLiteral("Appearance"),
                          {},
                          QUrl(),
                          childB});
        reg.registerPage({QStringLiteral("anim"), {}, QStringLiteral("Animations"), {}, QUrl(), other});

        const auto children = reg.childPages(QStringLiteral("snap"));
        QCOMPARE(children.size(), 2);
        QCOMPARE(reg.topLevelPages().size(), 2);
    }

    void rejectsEmptyId()
    {
        PageRegistry reg;
        auto* page = new StubPage(QStringLiteral("ignored"), &reg);
        reg.registerPage({QString(), {}, QStringLiteral("Empty"), {}, QUrl(), page});

        QVERIFY(reg.allPages().isEmpty());
    }
};

QTEST_MAIN(TestPageRegistry)
#include "test_page_registry.moc"
