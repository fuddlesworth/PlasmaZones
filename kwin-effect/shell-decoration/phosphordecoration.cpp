// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#include "PhosphorDecoration.h"
#include <PhosphorTheme/AppearanceWatcher.h>
#include <PhosphorTheme/FontFaces.h>
#include <KDecoration3/DecoratedWindow>
#include <KDecoration3/DecorationButton>
#include <KDecoration3/DecorationButtonGroup>
#include <KDecoration3/DecorationSettings>
#include <KPluginFactory>
#include <QCoreApplication>
#include <QDynamicPropertyChangeEvent>
#include <QFontDatabase>
#include <QPainter>
#include <cmath>

namespace PhosphorWindow {
namespace {
QColor alpha(QColor color, qreal value)
{
    color.setAlphaF(value);
    return color;
}
}

class Style final : public QObject
{
    Q_OBJECT
public:
    explicit Style(QObject* parent)
        : QObject(parent)
    {
        connect(&m_appearance, &PhosphorTheme::AppearanceWatcher::changed, this, &Style::reload);
        reload();
    }
    static Style* instance()
    {
        static auto* style = new Style(qApp);
        return style;
    }
    PhosphorTheme::ShellPalette palette;
    QVariantMap values;
    QFont font;
    qreal radius = 18;
Q_SIGNALS:
    void changed();

private:
    void reload()
    {
        const auto next = m_appearance.values();
        if (next == values)
            return;
        values = next;
        palette = PhosphorTheme::ShellPalette::fromSettings(values);
        radius = values.value(QStringLiteral("radius"), 18).toReal();
        QString family = values.value(QStringLiteral("uiFont")).toString();
        if (family.isEmpty())
            family = PhosphorTheme::FontFaces::resolve(PhosphorTheme::FontFaces::uiCandidates(),
                                                       QFontDatabase::systemFont(QFontDatabase::GeneralFont).family());
        font = QFont(family);
        font.setPixelSize(qRound(11 * values.value(QStringLiteral("textScale"), 100).toReal() / 100));
        Q_EMIT changed();
    }
    PhosphorTheme::AppearanceWatcher m_appearance;
};

class Button final : public KDecoration3::DecorationButton
{
public:
    Button(KDecoration3::DecorationButtonType type, KDecoration3::Decoration* decoration, QObject* parent)
        : DecorationButton(type, decoration, parent)
    {
        setGeometry(QRectF(0, 0, 24, 43));
    }
    void paint(QPainter* painter, const QRectF&) override
    {
        if (!isVisible())
            return;
        auto* frame = static_cast<Decoration*>(decoration());
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const QRectF box = geometry();
        if (isHovered() || isPressed()) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(alpha(frame->palette().text, isPressed() ? 0.14 : 0.08));
            painter->drawRoundedRect(QRectF(box.x(), box.center().y() - 12, box.width(), 24), 5, 5);
        }
        const auto type = this->type();
        if (type == KDecoration3::DecorationButtonType::Menu) {
            frame->paintApplicationIcon(painter, QRectF(box.center().x() - 7.5, box.center().y() - 7.5, 15, 15));
        } else {
            painter->translate(box.center());
            painter->setPen(QPen(alpha(frame->palette().text, isHovered() ? 1 : 0.6), 1, Qt::SolidLine, Qt::RoundCap,
                                 Qt::RoundJoin));
            painter->setBrush(Qt::NoBrush);
            using Type = KDecoration3::DecorationButtonType;
            switch (type) {
            case Type::Close:
                painter->drawLine(QPointF(-2, -2), QPointF(2, 2));
                painter->drawLine(QPointF(-2, 2), QPointF(2, -2));
                break;
            case Type::Minimize:
                painter->drawLine(QPointF(-2.5, 0), QPointF(2.5, 0));
                break;
            case Type::Maximize:
                painter->drawPolygon(QPolygonF{{0, -3.5}, {3.5, 0}, {0, 3.5}, {-3.5, 0}});
                break;
            case Type::KeepAbove:
                painter->drawPolyline(QPolygonF{{-3, 2}, {0, -2}, {3, 2}});
                break;
            case Type::KeepBelow:
                painter->drawPolyline(QPolygonF{{-3, -2}, {0, 2}, {3, -2}});
                break;
            case Type::Spacer:
                break;
            default:
                painter->drawEllipse(QPointF(0, 0), 3, 3);
                break;
            }
        }
        painter->restore();
    }
};

Decoration::Decoration(QObject* parent, const QVariantList& args)
    : KDecoration3::Decoration(parent, args)
    , m_style(Style::instance())
{
}
bool Decoration::init()
{
    m_left = new KDecoration3::DecorationButtonGroup(this);
    m_left->addButton(new Button(KDecoration3::DecorationButtonType::Menu, this, this));
    m_right = new KDecoration3::DecorationButtonGroup(KDecoration3::DecorationButtonGroup::Position::Right, this,
                                                      [](auto type, auto* decoration, auto* parent) {
                                                          return new Button(type, decoration, parent);
                                                      });
    m_right->setSpacing(0);
    connect(m_right, &KDecoration3::DecorationButtonGroup::geometryChanged, this, &Decoration::layoutButtons);
    connect(window(), &KDecoration3::DecoratedWindow::sizeChanged, this, &Decoration::refresh);
    connect(this, &KDecoration3::Decoration::bordersChanged, this, &Decoration::layoutButtons);
    connect(window(), &KDecoration3::DecoratedWindow::captionChanged, this, [this] {
        update();
    });
    connect(window(), &KDecoration3::DecoratedWindow::activeChanged, this, &Decoration::refresh);
    connect(window(), &KDecoration3::DecoratedWindow::maximizedChanged, this, &Decoration::refresh);
    connect(m_style, &Style::changed, this, &Decoration::refresh);
    refresh();
    return true;
}
const PhosphorTheme::ShellPalette& Decoration::palette() const
{
    return m_style->palette;
}
QString Decoration::applicationId() const
{
    return window()->windowClass().section(u' ', -1).toLower();
}
QString Decoration::application() const
{
    QString name = applicationId().section(u'.', -1);
    if (!name.isEmpty())
        name[0] = name[0].toUpper();
    return name;
}
bool Decoration::focused() const
{
    const auto value = property("phosphorFocused");
    return value.isValid() ? value.toBool() : window()->isActive();
}
QColor Decoration::accent() const
{
    const auto colorIndex = property("phosphorColorIndex");
    return palette().windowColor(colorIndex.isValid() ? colorIndex.toInt() : int(qHash(applicationId()) % 4));
}
void Decoration::layoutButtons()
{
    if (!m_left || !m_right)
        return;
    m_left->setPos(QPointF(13, 0));
    m_right->setPos(QPointF(std::max(40.0, size().width() - m_right->geometry().width() - 14), 0));
    setTitleBar(QRectF(0, 0, size().width(), 43));
    update();
}
void Decoration::refresh()
{
    const bool maximized = window()->isMaximized();
    setBorders(QMarginsF(maximized ? 0 : 1, 43, maximized ? 0 : 1, maximized ? 0 : 1));
    setResizeOnlyBorders(QMarginsF(6, 6, 6, 6));
    // The surface-shader chain clips the fully composited window and paints its
    // border and shadow. A native radius would clip the client and its Wayland
    // subsurfaces separately, and cannot follow the user's shader profile.
    setOpaque(false);
    setBlurRegion(m_style->values.value(QStringLiteral("material")).toString() == QStringLiteral("solid")
                      ? QRegion()
                      : QRegion(QRect(0, 0, std::ceil(size().width()), 43)));
    layoutButtons();
}
bool Decoration::event(QEvent* event)
{
    if (event->type() == QEvent::DynamicPropertyChange && m_left) {
        auto* change = static_cast<QDynamicPropertyChangeEvent*>(event);
        if (change->propertyName() == "phosphorColorIndex" || change->propertyName() == "phosphorFocused")
            refresh();
    }
    return KDecoration3::Decoration::event(event);
}
void Decoration::paintApplicationIcon(QPainter* painter, const QRectF& rect) const
{
    painter->save();
    painter->translate(rect.topLeft());
    painter->scale(rect.width() / 24, rect.height() / 24);
    painter->setPen(QPen(accent(), 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->setBrush(Qt::NoBrush);
    const QString app = applicationId();
    if (app.contains(QStringLiteral("firefox")) || app.contains(QStringLiteral("chrom"))) {
        painter->drawEllipse(QRectF(3, 3, 18, 18));
        painter->drawEllipse(QRectF(8, 3, 8, 18));
        painter->drawLine(QPointF(3, 12), QPointF(21, 12));
    } else if (app.contains(QStringLiteral("dolphin")) || app.contains(QStringLiteral("files"))) {
        painter->drawPolygon(QPolygonF{{3, 6}, {10, 6}, {12, 9}, {21, 9}, {21, 20}, {3, 20}});
        painter->drawPolyline(QPolygonF{{3, 6}, {3, 4}, {10, 4}, {12, 6}, {20, 6}, {20, 9}});
    } else {
        painter->drawRoundedRect(QRectF(2, 3, 20, 18), 4, 4);
        painter->drawPolyline(QPolygonF{{6, 8}, {10, 12}, {6, 16}});
        painter->drawLine(QPointF(14, 16), QPointF(18, 16));
    }
    painter->restore();
}
void Decoration::paint(QPainter* painter, const QRectF& repaintArea)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setClipRect(repaintArea);
    const qreal radius = window()->isMaximized() ? 0 : m_style->radius;
    painter->fillRect(rect(), alpha(palette().surface, palette().opacity));
    QLinearGradient tint(rect().topLeft(), QPointF(rect().width() * 0.6, 43));
    tint.setColorAt(0, alpha(accent(), 0.06));
    tint.setColorAt(1, Qt::transparent);
    painter->fillRect(QRectF(0, 0, rect().width(), 43), tint);
    painter->setPen(palette().outline);
    painter->drawLine(QPointF(0, 42.5), QPointF(rect().width(), 42.5));
    QLinearGradient rail(QPointF(radius, 0), QPointF(rect().width() - radius, 0));
    const bool focused = this->focused();
    rail.setColorAt(0, alpha(accent(), focused ? 0.9 : 0.45));
    rail.setColorAt(0.5, alpha(focused ? palette().text : accent(), focused ? 0.9 : 0.45));
    rail.setColorAt(1, alpha(accent(), focused ? 0.9 : 0.45));
    painter->fillRect(QRectF(radius, 0, std::max(0.0, rect().width() - 2 * radius), focused ? 2 : 1), rail);
    painter->setFont(m_style->font);
    const QFontMetricsF metrics(m_style->font);
    const QColor textColor = focused ? palette().text : palette().muted;
    const QString app = application();
    qreal x = 42;
    const qreal baseline = (43 - metrics.height()) / 2 + metrics.ascent();
    painter->setPen(textColor);
    painter->drawText(QPointF(x, baseline), app);
    x += metrics.horizontalAdvance(app) + 10;
    painter->setPen(palette().muted);
    painter->drawText(QPointF(x, baseline), QStringLiteral("/"));
    x += metrics.horizontalAdvance(QStringLiteral("/")) + 10;
    QString caption = window()->caption();
    for (const auto& separator : {QStringLiteral(" — "), QStringLiteral(" – "), QStringLiteral(" - ")}) {
        if (caption.endsWith(separator + app, Qt::CaseInsensitive)) {
            caption.chop(separator.size() + app.size());
            break;
        }
    }
    painter->setPen(textColor);
    painter->drawText(QPointF(x, baseline),
                      metrics.elidedText(caption, Qt::ElideRight, std::max(0.0, m_right->pos().x() - x - 10)));
    m_left->paint(painter, repaintArea);
    m_right->paint(painter, repaintArea);
    painter->restore();
}
}

K_PLUGIN_FACTORY_WITH_JSON(PhosphorDecorationFactory, "metadata.json", registerPlugin<PhosphorWindow::Decoration>();)
#include "phosphordecoration.moc"
