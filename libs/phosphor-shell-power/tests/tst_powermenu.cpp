// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Behavioural cover for PowerMenu's grid and its default focus.
//
// tst_power.cpp can only assert that the module publishes the type names,
// because PowerMenu imports Phosphor.Service.Session and a unit test has no
// shell process to register it. This binary closes that gap by registering a
// FAKE SessionHost under the same URI: same type name, same `Availability`
// enum, same `Can*` property names and `capabilitiesChanged` notify. PowerMenu
// binds to names, so it cannot tell the difference.
//
// The three properties pinned here are the ones that broke in review and would
// otherwise break again silently, because every one of them still LOOKS right
// on screen at a glance:
//
//   * six tiles, not twelve. An earlier revision ran the six-entry model
//     inside both row delegates and hid the off-row half, so half the tiles
//     existed only to be invisible.
//   * the focused tile is the VISIBLE Lock. With twelve tiles the row-1 copy
//     completed last, won the default-focus assignment, and the menu opened
//     focused on something nobody could see. Space still worked, which is
//     exactly why it survived a review pass.
//   * a tile appears when logind changes its mind. The action table folds
//     availability into a closure per entry, and a fold that broke binding
//     capture would leave the grid frozen at whatever was true on open.

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>

#include <memory>

// Stands in for PhosphorServiceSession::SessionHost. Deliberately NOT in an
// anonymous namespace: TestPowerMenu holds one by value in a unique_ptr, and an
// internal-linkage member type in an external-linkage class is
// -Wsubobject-linkage. Only the surface
// PowerMenu actually touches, and deliberately not a subclass of the real
// thing: linking the real service into this test would drag in logind.
class FakeSessionHost : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(SessionHost)

public:
    // Mirrors SessionHost::Availability, including the ordering, because
    // PowerMenu compares against SessionHost.Yes and SessionHost.Challenge by
    // name and those resolve through this metaobject.
    enum class Availability {
        Unknown,
        Yes,
        No,
        NotApplicable,
        Challenge,
    };
    Q_ENUM(Availability)

    Q_PROPERTY(Availability canPowerOff MEMBER m_canPowerOff NOTIFY capabilitiesChanged)
    Q_PROPERTY(Availability canReboot MEMBER m_canReboot NOTIFY capabilitiesChanged)
    Q_PROPERTY(Availability canSuspend MEMBER m_canSuspend NOTIFY capabilitiesChanged)
    Q_PROPERTY(Availability canHibernate MEMBER m_canHibernate NOTIFY capabilitiesChanged)

    using QObject::QObject;

    Q_INVOKABLE void refreshCapabilities()
    {
        ++refreshCount;
    }
    Q_INVOKABLE void suspend()
    {
    }
    Q_INVOKABLE void hibernate()
    {
    }
    Q_INVOKABLE void lock()
    {
    }
    Q_INVOKABLE void logout()
    {
    }
    Q_INVOKABLE void reboot()
    {
    }
    Q_INVOKABLE void powerOff()
    {
    }

    void setAll(Availability value)
    {
        m_canPowerOff = value;
        m_canReboot = value;
        m_canSuspend = value;
        m_canHibernate = value;
        Q_EMIT capabilitiesChanged();
    }

    int refreshCount = 0;

Q_SIGNALS:
    void capabilitiesChanged();

private:
    Availability m_canPowerOff = Availability::Unknown;
    Availability m_canReboot = Availability::Unknown;
    Availability m_canSuspend = Availability::Unknown;
    Availability m_canHibernate = Availability::Unknown;
};

namespace {

// Every PowerTile in the menu. Identified by type name rather than by a C++
// type, since PowerTile is QML-defined.
//
// Walks the VISUAL tree, not findChildren: a Repeater's delegates are not
// QObject children of anything in the document, so findChildren stops at the
// Repeater itself and reports zero tiles.
void collectPowerTiles(QQuickItem* item, QList<QQuickItem*>& out)
{
    const auto children = item->childItems();
    for (QQuickItem* child : children) {
        // The generated name is PowerTile_QMLTYPE_<n>; matching the underscore
        // too keeps a future PowerTileGroup from counting as a tile.
        if (QString::fromUtf8(child->metaObject()->className()).startsWith(QLatin1String("PowerTile_"))) {
            out.append(child);
        }
        collectPowerTiles(child, out);
    }
}

QList<QQuickItem*> powerTiles(QQuickItem* root)
{
    QList<QQuickItem*> tiles;
    collectPowerTiles(root, tiles);
    return tiles;
}

} // namespace

class TestPowerMenu : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        // Same URI the real service registers, so PowerMenu's import resolves
        // to this fake without the file knowing.
        qmlRegisterTypesAndRevisions<FakeSessionHost>("Phosphor.Service.Session", 1);
    }

    void init()
    {
        m_engine = std::make_unique<QQmlEngine>();
        m_session = std::make_unique<FakeSessionHost>();
    }

    void cleanup()
    {
        m_menu.reset();
        m_session.reset();
        m_engine.reset();
    }

    void buildsOneTilePerAction()
    {
        buildMenu();
        // Six actions, six tiles. Twelve means the row delegates are each
        // instantiating the whole model again.
        QCOMPARE(powerTiles(m_menu.get()).size(), 6);
    }

    void focusesTheVisibleLockTile()
    {
        buildMenu();

        QQuickItem* focused = nullptr;
        const auto tiles = powerTiles(m_menu.get());
        for (QQuickItem* tile : tiles) {
            if (tile->hasActiveFocus()) {
                QVERIFY2(focused == nullptr, "more than one tile holds active focus");
                focused = tile;
            }
        }

        QVERIFY2(focused != nullptr, "no tile holds active focus; the menu opens with nothing selected");
        QCOMPARE(focused->property("label").toString(), QStringLiteral("Lock"));
        // The point of the test. A hidden tile can hold focus and still run
        // its action on Space, so asserting the label alone would not have
        // caught the bug this exists for.
        QVERIFY2(focused->isVisible(), "the focused tile is not visible");
        QVERIFY2(focused->property("primary").toBool(), "the focused tile is not the primary (filled) one");
    }

    void tilesFollowLogindCapabilities()
    {
        // Nothing permitted: only the two actions that never consult logind.
        m_session->setAll(FakeSessionHost::Availability::No);
        buildMenu();

        const auto tiles = powerTiles(m_menu.get());
        QCOMPARE(visibleLabels(tiles), (QStringList{QStringLiteral("Lock"), QStringLiteral("Log out")}));

        // logind changes its mind while the menu is already up. The grid must
        // follow, which is what proves the per-action `available` closures
        // still register a binding dependency on the Can* properties.
        m_session->setAll(FakeSessionHost::Availability::Yes);
        QCOMPARE(visibleLabels(tiles).size(), 6);

        // And back. Challenge counts as offered, because SessionHost's own
        // gate accepts it.
        m_session->setAll(FakeSessionHost::Availability::NotApplicable);
        QCOMPARE(visibleLabels(tiles).size(), 2);
        m_session->setAll(FakeSessionHost::Availability::Challenge);
        QCOMPARE(visibleLabels(tiles).size(), 6);
    }

    // The transport does NOT create the menu the way the cases above do: it
    // calls beginCreate against the ENGINE ROOT context, not the component's
    // own creation context. A `pragma ComponentBehavior: Bound` anywhere in
    // the chain makes exactly that fail, with "Cannot instantiate bound
    // component outside its creation context" — and it fails SILENTLY, as a
    // null return the shell logs and skips, so the menu simply never appears.
    // That shipped once in shell.qml and cost every bar on the screen.
    void buildsFromTheEngineRootContextTheWayTheTransportDoes()
    {
        QQmlComponent component(m_engine.get(), QStringLiteral("Phosphor.Power"), QStringLiteral("PowerMenu"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));

        std::unique_ptr<QObject> object(component.beginCreate(m_engine->rootContext()));
        QVERIFY2(object != nullptr,
                 qPrintable(QStringLiteral("PowerMenu cannot be built against the engine root context, "
                                           "which is how the transport builds it: %1")
                                .arg(component.errorString())));
        object->setProperty("session", QVariant::fromValue(m_session.get()));
        component.completeCreate();
        QVERIFY(qobject_cast<QQuickItem*>(object.get()) != nullptr);
    }

    void refreshesCapabilitiesOnConstruction()
    {
        buildMenu();
        // Exactly one: at construction. The menu refreshes again when a popout
        // host reports itself open, and there is no host here, so a second
        // refresh would mean something fires it unconditionally.
        QCOMPARE(m_session->refreshCount, 1);
    }

private:
    static QStringList visibleLabels(const QList<QQuickItem*>& tiles)
    {
        QStringList labels;
        for (QQuickItem* tile : tiles) {
            if (tile->isVisible()) {
                labels.append(tile->property("label").toString());
            }
        }
        return labels;
    }

    // A real window so activeFocus resolves. Focus is window state, and
    // hasActiveFocus() is false for every item in a window-less tree, which
    // would make focusesTheVisibleLockTile fail for the wrong reason.
    void buildMenu()
    {
        QQmlComponent component(m_engine.get(), QStringLiteral("Phosphor.Power"), QStringLiteral("PowerMenu"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));

        std::unique_ptr<QObject> object(
            component.createWithInitialProperties({{QStringLiteral("session"), QVariant::fromValue(m_session.get())}}));
        QVERIFY2(object != nullptr, qPrintable(component.errorString()));

        auto* item = qobject_cast<QQuickItem*>(object.get());
        QVERIFY(item != nullptr);
        object.release();
        m_menu.reset(item);

        m_window = std::make_unique<QQuickWindow>();
        m_menu->setParentItem(m_window->contentItem());
        m_window->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
    }

    std::unique_ptr<QQmlEngine> m_engine;
    std::unique_ptr<FakeSessionHost> m_session;
    std::unique_ptr<QQuickItem> m_menu;
    std::unique_ptr<QQuickWindow> m_window;
};

QTEST_MAIN(TestPowerMenu)
#include "tst_powermenu.moc"
