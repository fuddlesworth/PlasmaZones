// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShell/PerScreenPanels.h>

#include <PhosphorShell/PanelWindow.h>

#include <QAbstractListModel>
#include <QLoggingCategory>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QScreen>
#include <QVariantMap>

namespace PhosphorShell {

namespace {
Q_LOGGING_CATEGORY(lcPerScreenPanels, "phosphorshell.perscreenpanels")
} // namespace

PerScreenPanels::PerScreenPanels(QQuickItem* parent)
    : QQuickItem(parent)
{
}

PerScreenPanels::~PerScreenPanels() = default;

QAbstractListModel* PerScreenPanels::model() const
{
    return m_model.data();
}

void PerScreenPanels::setModel(QAbstractListModel* model)
{
    if (m_model.data() == model) {
        return;
    }
    m_model = model;
    warnIfAlreadyBuilt("model");
    Q_EMIT modelChanged();
}

QQmlComponent* PerScreenPanels::delegate() const
{
    return m_delegate.data();
}

void PerScreenPanels::setDelegate(QQmlComponent* delegate)
{
    if (m_delegate.data() == delegate) {
        return;
    }
    m_delegate = delegate;
    warnIfAlreadyBuilt("delegate");
    Q_EMIT delegateChanged();
}

int PerScreenPanels::count() const
{
    return static_cast<int>(m_instances.size());
}

void PerScreenPanels::warnIfAlreadyBuilt(const char* property) const
{
    if (!m_built) {
        return;
    }
    // Enumeration is one-shot, so a late write changes nothing. Silence
    // would leave a QML author staring at a component that simply never
    // appears, with the assignment looking perfectly correct.
    qCWarning(lcPerScreenPanels) << "'" << property << "' was assigned after the panels were built;"
                                 << "PerScreenPanels enumerates once and will not rebuild";
}

void PerScreenPanels::componentComplete()
{
    QQuickItem::componentComplete();
    // Build only after the QML tree is complete, so `model` and `delegate`
    // have their assigned values. Building in the constructor would see
    // both null. ShellEngine runs materializePanels() after
    // QQmlComponent::create() returns, which is after every
    // componentComplete in the tree, so the instances exist by the time
    // discovery walks the children.
    build();
}

void PerScreenPanels::build()
{
    // Latch unconditionally, before any early return. Keying the guard on
    // whether instances exist would fail to latch in exactly the cases it
    // was written for — an empty model, a null delegate, no engine, or every
    // create() failing — leaving a second componentComplete free to rebuild.
    if (m_built) {
        return;
    }
    m_built = true;
    if (!m_delegate) {
        qCWarning(lcPerScreenPanels) << "No delegate set — no per-screen panels will be created";
        return;
    }
    if (!m_model) {
        qCWarning(lcPerScreenPanels) << "No model set — no per-screen panels will be created";
        return;
    }
    auto* engine = qmlEngine(this);
    if (!engine) {
        qCWarning(lcPerScreenPanels) << "No QML engine — no per-screen panels will be created";
        return;
    }

    const int rows = m_model->rowCount();
    const QHash<int, QByteArray> roles = m_model->roleNames();

    for (int row = 0; row < rows; ++row) {
        const QModelIndex idx = m_model->index(row, 0);

        QVariantMap modelData;
        QScreen* rowScreen = nullptr;
        for (auto it = roles.cbegin(); it != roles.cend(); ++it) {
            const QString roleName = QString::fromUtf8(it.value());
            // The `screen` role is deliberately NOT exposed through
            // modelData: the map snapshots a raw QScreen*, and on monitor
            // hot-unplug that pointer dangles for the window until
            // ShellEngine's debounced reload tears the engine down — a
            // delegate binding reading `modelData.screen.name` would
            // dereference a freed QScreen. Delegates that need the output
            // read the PanelWindow.screen property instead, which is
            // QPointer-backed and reads null once the screen dies. The
            // value is still used below to point the panel at its output.
            if (roleName == QLatin1String("screen")) {
                rowScreen = m_model->data(idx, it.key()).value<QScreen*>();
                continue;
            }
            modelData.insert(roleName, m_model->data(idx, it.key()));
        }

        // Context parented to `this` so it dies with us. The instance is
        // parented to `this` too, but ShellEngine detaches it later, and a
        // context owned by the instance would then be destroyed by the
        // Surface at an unrelated time.
        auto* context = new QQmlContext(qmlContext(this), this);
        context->setContextProperty(QStringLiteral("modelData"), modelData);

        QObject* obj = m_delegate->create(context);
        if (!obj) {
            qCWarning(lcPerScreenPanels) << "Failed to create panel for screen row" << row << "—"
                                         << m_delegate->errorString();
            // deleteLater, not delete: a failed create() may have run
            // partial construction that still references this context while
            // QML unwinds, and the project rule is never to manually delete
            // a QObject. It is parented to `this`, so it is reclaimed either
            // way.
            context->deleteLater();
            continue;
        }

        // Parent so ShellEngine::materializePanels()'s findChildren walk
        // reaches it. This is the whole point of the type; without it the
        // panel is created, never materialized, and silently renders
        // nothing. Deliberately NOT setParentItem: the panel is not a
        // visual child of this item, and materialization clears the visual
        // parent anyway.
        obj->setParent(this);
        // The engine must not decide this is garbage. It has a QObject
        // parent, so C++ ownership is what Qt would infer, but the object
        // came out of QQmlComponent::create() where the default is JS
        // ownership until a parent is seen. State it outright rather than
        // depending on the order above surviving a future edit.
        QQmlEngine::setObjectOwnership(obj, QQmlEngine::CppOwnership);

        // Point the panel at its output. Only when the delegate left it
        // unset — assigning over an explicit `screen:` binding in the
        // delegate would sever that binding.
        if (auto* panel = qobject_cast<PanelWindow*>(obj)) {
            if (!panel->screen()) {
                panel->setScreen(rowScreen);
            }
        } else {
            // Not fatal — a non-panel delegate still gets created and
            // parented — but it is almost certainly a mistake, and the
            // symptom (nothing appears) gives no clue on its own.
            qCWarning(lcPerScreenPanels) << "Delegate for screen row" << row
                                         << "is not a PanelWindow; it will never be materialized";
        }

        m_instances.append(QPointer<QObject>(obj));
    }

    qCDebug(lcPerScreenPanels) << "Created" << m_instances.size() << "panel(s) across" << rows << "screen(s)";
    // Change-gated per "only emit signals when value actually changes":
    // `count` starts at 0 and build() only ever appends, so a build that
    // produced nothing left it at 0 and owes no notification. The early
    // returns above emit nothing for the same reason.
    if (!m_instances.isEmpty()) {
        Q_EMIT countChanged();
    }
}

} // namespace PhosphorShell
