#include "operationconnector.hpp"
#include "operation.hpp"
#include "popup/popup_connector.hpp"

#include <qschematic/items/label.hpp>
#include <qschematic/scene.hpp>
#include <qschematic/commands/item_remove.hpp>
#include <qschematic/commands/item_visibility.hpp>
#include <qschematic/commands/label_rename.hpp>

#include <QPainter>
#include <QGraphicsSceneContextMenuEvent>
#include <QMenu>
#include <QInputDialog>

#define SIZE (_settings.gridSize/2)
#define RECT (QRectF(-SIZE, -SIZE, 2*SIZE, 2*SIZE))

const QColor COLOR_BODY_FILL   = QColor(Qt::white);
const QColor COLOR_BODY_BORDER = QColor(Qt::black);
const qreal PEN_WIDTH          = 1.5;

OperationConnector::OperationConnector(const QPoint& gridPoint, const QString& text, QGraphicsItem* parent) :
    QSchematic::Items::Connector(::ItemType::OperationConnectorType, gridPoint, text, parent)
{
    label()->setVisible(true);
    setForceTextDirection(false);
}

gpds::container OperationConnector::to_container() const
{
    // Root
    gpds::container root;
    addItemTypeIdToContainer(root);
    root.add_value("connector", QSchematic::Items::Connector::to_container());

    return root;
}

void OperationConnector::from_container(const gpds::container& container)
{
    // Root
    QSchematic::Items::Connector::from_container(*container.get_value<gpds::container*>("connector").value());
}

std::shared_ptr<QSchematic::Items::Item> OperationConnector::deepCopy() const
{
    auto clone = std::make_shared<OperationConnector>(gridPos(), text(), parentItem());
    copyAttributes(*clone);

    return clone;
}

void OperationConnector::copyAttributes(OperationConnector& dest) const
{
    QSchematic::Items::Connector::copyAttributes(dest);
}

std::unique_ptr<QWidget> OperationConnector::popup() const
{
    return std::make_unique<PopupConnector>(*this);
}

QRectF OperationConnector::boundingRect() const
{
    qreal adj = 1.5;

    return RECT.adjusted(-adj, -adj, adj, adj);
}

void OperationConnector::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    Q_UNUSED(option)
    Q_UNUSED(widget)

    // Draw the bounding rect if debug mode is enabled
    if (_settings.debug) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(QBrush(Qt::red));
        painter->drawRect(boundingRect());
    }

    // Body pen
    QPen bodyPen;
    bodyPen.setWidthF(PEN_WIDTH);
    bodyPen.setStyle(Qt::SolidLine);
    bodyPen.setColor(COLOR_BODY_BORDER);

    // Body brush
    QBrush bodyBrush;
    bodyBrush.setStyle(Qt::SolidPattern);
    bodyBrush.setColor(COLOR_BODY_FILL);

    // Draw the component body
    painter->setPen(bodyPen);
    painter->setBrush(bodyBrush);
    painter->drawEllipse(RECT);
}

void OperationConnector::contextMenuEvent(QGraphicsSceneContextMenuEvent* event)
{
    // Create the menu
    QMenu menu;
    {
        // Connector visibility
        QAction* visibility = new QAction;
        visibility->setCheckable(true);
        visibility->setChecked(isVisible());
        visibility->setText("Visible");
        connect(visibility, &QAction::toggled, [this](const bool enabled) {
            if (scene())
                scene()->undoStack()->push(new QSchematic::Commands::ItemVisibility(this->shared_from_this(), enabled));
            else
                setVisible(enabled);
        });

        // Label visibility
        QAction* labelVisibility = new QAction;
        labelVisibility->setCheckable(true);
        labelVisibility->setChecked(label()->isVisible());
        labelVisibility->setText("Label visible");
        connect(labelVisibility, &QAction::toggled, [this](bool enabled) {
            if (scene()) {
                scene()->undoStack()->push(new QSchematic::Commands::ItemVisibility(label(), enabled));
            } else {
                label()->setVisible(enabled);
            }
        });

        // Text
        QAction* text = new QAction;
        text->setText("Rename ...");
        connect(text, &QAction::triggered, [this] {
            bool ok = false;
            const QString& newText = QInputDialog::getText(
                nullptr,
                "Rename Connector",
                "New connector text",
                QLineEdit::Normal,
                label()->text(),
                &ok
            );
            if (!ok)
                return;

            if (scene()) {
                scene()->undoStack()->push(new QSchematic::Commands::LabelRename(label().get(), newText));
            } else {
                label()->setText(newText);
            }
        });

        // Align label
        QAction* alignLabel = new QAction;
        alignLabel->setText("Align Label");
        connect(alignLabel, &QAction::triggered, [this] {
            this->alignLabel();
        });

        // Delete
        QAction* deleteFromModel = new QAction;
        deleteFromModel->setText("Delete");
        connect(deleteFromModel, &QAction::triggered, [this] {
            if (scene()) {
                // Retrieve smart pointer
                std::shared_ptr<QSchematic::Items::Item> itemPointer;
                {
                    // Retrieve parent (Operation)
                    const Operation* operation = qgraphicsitem_cast<const Operation*>(parentItem());
                    if (!operation) {
                        return;
                    }

                    // Retrieve connector
                    for (auto& i : operation->connectors()) {
                        if (i.get() == this) {
                            itemPointer = i;
                            break;
                        }
                    }
                    if (!itemPointer) {
                        return;
                    }
                }

                // Issue command
                scene()->undoStack()->push(new QSchematic::Commands::ItemRemove(scene(), itemPointer));
            }
        });

        // Assemble
        menu.addAction(visibility);
        menu.addAction(labelVisibility);
        menu.addAction(text);
        menu.addAction(alignLabel);
        menu.addAction(deleteFromModel);
    }

    // Sow the menu
    menu.exec(event->screenPos());
}
