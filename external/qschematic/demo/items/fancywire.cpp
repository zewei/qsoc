#include "itemtypes.hpp"
#include "fancywire.hpp"

#include <qschematic/commands/wirenet_rename.hpp>
#include <qschematic/wire_system/point.hpp>
#include <qschematic/items/connector.hpp>
#include <qschematic/scene.hpp>

#include <QPen>
#include <QPainter>
#include <QVector2D>
#include <QInputDialog>

#define SIZE (_settings.gridSize/3)

FancyWire::FancyWire(QGraphicsItem* parent) :
    QSchematic::Items::WireRoundedCorners(::ItemType::FancyWireType, parent)
{
    auto action = new QAction("Rename ...", this);
    connect(action, &QAction::triggered, this, [this] {
        bool ok = false;
        const QString name = QInputDialog::getText(
            nullptr,
            "Set WireNet name",
            "Enter the new name",
            QLineEdit::Normal, net()->name(),
            &ok
        );
        if (!ok)
            return;

        if (auto wireNet = std::dynamic_pointer_cast<Items::WireNet>(net())) {
            scene()->undoStack()->push(new QSchematic::Commands::WirenetRename(wireNet, name));
        }
    });
    setRenameAction(action);
    setZValue(1);
}

gpds::container FancyWire::to_container() const
{
    // Root
    gpds::container root;
    addItemTypeIdToContainer(root);
    root.add_value("wire", QSchematic::Items::Wire::to_container());

    return root;
}

void FancyWire::from_container(const gpds::container& container)
{
    QSchematic::Items::Wire::from_container(*container.get_value<gpds::container*>("wire").value());
}

std::shared_ptr<QSchematic::Items::Item> FancyWire::deepCopy() const
{
    auto clone = std::make_shared<FancyWire>(parentItem());
    copyAttributes(*clone);

    return clone;
}

void FancyWire::copyAttributes(FancyWire& dest) const
{
    QSchematic::Items::WireRoundedCorners::copyAttributes(dest);
}

void FancyWire::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    // Base class
    QSchematic::Items::WireRoundedCorners::paint(painter, option, widget);

    // Nothing to do if we can't retrieve the wire manager
    if (!scene())
        return;

    // Get the wire manager
    const auto& wireManager = scene()->wire_manager();
    if (!wireManager)
        return;

    QPen pen(Qt::NoPen);

    QBrush brush;
    brush.setColor(Qt::black);
    brush.setStyle(Qt::SolidPattern);

    // Make points fancy if they are on top of one of our connectors
    painter->setPen(pen);
    painter->setBrush(brush);

    const auto& points = pointsRelative();

    // Sanity check!
    if (points.size() != points_count())
        return;

    // Draw a circle on wire points connected to a connectable
    for (int i = 0; i < points_count(); i++) {
        if (wireManager->point_is_attached(this, i))
            painter->drawEllipse(points.at(i), SIZE, SIZE);
    }
}
