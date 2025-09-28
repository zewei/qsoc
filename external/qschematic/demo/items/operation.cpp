#include "operation.hpp"
#include "operationconnector.hpp"
#include "popup/popup_operation.hpp"
#include "../commands/node_add_connector.hpp"

#include <qschematic/scene.hpp>
#include <qschematic/items/label.hpp>
#include <qschematic/commands/item_remove.hpp>
#include <qschematic/commands/item_visibility.hpp>
#include <qschematic/commands/label_rename.hpp>

#include <QPainter>
#include <QMenu>
#include <QGraphicsSceneContextMenuEvent>
#include <QInputDialog>
#include <QGraphicsDropShadowEffect>

const QColor COLOR_BODY_FILL   = QColor( QStringLiteral( "#e0e0e0" ) );
const QColor COLOR_BODY_BORDER = QColor(Qt::black);
const QColor SHADOW_COLOR      = QColor(63, 63, 63, 100);
const qreal PEN_WIDTH          = 1.5;
const qreal SHADOW_OFFSET      = 7;
const qreal SHADOW_BLUR_RADIUS = 10;

Operation::Operation(int type, QGraphicsItem* parent) :
    QSchematic::Items::Node(type, parent)
{
    // Label
    _label = std::make_shared<QSchematic::Items::Label>();
    _label->setParentItem(this);
    _label->setVisible(true);
    _label->setMovable(true);
    _label->setPos(0, 120);
    _label->setText(QStringLiteral("Generic"));
    connect(this, &QSchematic::Items::Node::sizeChanged, [this]{
        label()->setConnectionPoint(sizeRect().center());
        alignLabel();
    });
    connect(this, &QSchematic::Items::Item::settingsChanged, [this]{
        label()->setConnectionPoint(sizeRect().center());
        label()->setSettings(_settings);
    });

    // Misc
    setSize(160, 80);
    setAllowMouseResize(true);
    setAllowMouseRotate(true);
    setConnectorsMovable(true);
    setConnectorsSnapPolicy(QSchematic::Items::Connector::NodeSizerectOutline);
    setConnectorsSnapToGrid(true);

    // Drop shadow
    auto graphicsEffect = new QGraphicsDropShadowEffect(this);
    graphicsEffect->setOffset(SHADOW_OFFSET);
    graphicsEffect->setBlurRadius(SHADOW_BLUR_RADIUS);
    graphicsEffect->setColor(SHADOW_COLOR);
    setGraphicsEffect(graphicsEffect);
}

Operation::~Operation()
{
    dissociate_item(_label);
}

gpds::container Operation::to_container() const
{
    // Root
    gpds::container root;
    addItemTypeIdToContainer(root);
    root.add_value("node", QSchematic::Items::Node::to_container());
    root.add_value("label", _label->to_container());

    return root;
}

void Operation::from_container(const gpds::container& container)
{
    // Root
    QSchematic::Items::Node::from_container(*container.get_value<gpds::container*>("node").value());
    _label->from_container(*container.get_value<gpds::container*>("label").value());
}

std::unique_ptr<QWidget> Operation::popup() const
{
    return std::make_unique<PopupOperation>(*this);
}

std::shared_ptr<QSchematic::Items::Item> Operation::deepCopy() const
{
    auto clone = std::make_shared<Operation>(::ItemType::OperationType, parentItem());
    copyAttributes(*clone);

    return clone;
}

void Operation::copyAttributes(Operation& dest) const
{
    QSchematic::Items::Node::copyAttributes(dest);

    // Label
    dest._label = std::dynamic_pointer_cast<QSchematic::Items::Label>(_label->deepCopy());
    dest._label->setParentItem(&dest);
}

void Operation::alignLabel()
{
    if (!_label)
        return;

    const QRectF& tr = _label->textRect();
    const qreal x = (width() - tr.width()) / 2.0;
    const qreal y = -10.0;

    _label->setPos(x, y);
}

std::shared_ptr<QSchematic::Items::Label> Operation::label() const
{
    return _label;
}


void Operation::setText(const QString& text)
{
    Q_ASSERT(_label);

    _label->setText(text);
}

QString Operation::text() const
{
    Q_ASSERT(_label);

    return _label->text();
}

void Operation::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    Q_UNUSED(option)
    Q_UNUSED(widget)

    // Draw the bounding rect if debug mode is enabled
    if (_settings.debug) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(QBrush(Qt::red));
        painter->drawRect(boundingRect());
    }

    // Body
    {
        // Common stuff
        qreal radius = _settings.gridSize/2;

        // Body
        {
            // Pen
            QPen pen;
            pen.setWidthF(PEN_WIDTH);
            pen.setStyle(Qt::SolidLine);
            pen.setColor(COLOR_BODY_BORDER);

            // Brush
            QBrush brush;
            brush.setStyle(Qt::SolidPattern);
            brush.setColor(COLOR_BODY_FILL);

            // Draw the component body
            painter->setPen(pen);
            painter->setBrush(brush);
            painter->drawRoundedRect(sizeRect(), radius, radius);
        }
    }

    // Resize handles
    if (isSelected() && allowMouseResize()) {
        paintResizeHandles(*painter);
    }

    // Rotate handle
    if (isSelected() && allowMouseRotate()) {
        paintRotateHandle(*painter);
    }
}

void Operation::contextMenuEvent(QGraphicsSceneContextMenuEvent* event)
{
    // Create the menu
    QMenu menu;
    {
        // Text
        QAction* text = new QAction;
        text->setText("Rename ...");
        connect(text, &QAction::triggered, [this] {
            if (!scene())
                return;

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

            scene()->undoStack()->push(new QSchematic::Commands::LabelRename(label().get(), newText));
        });

        QAction* isMovable = new QAction;
        isMovable->setCheckable(true);
        isMovable->setChecked(this->isMovable());
        isMovable->setText("Is Movable");
        connect(isMovable, &QAction::toggled, [this](bool enabled) {
            setMovable(enabled);
        });

        // Label visibility
        QAction* labelVisibility = new QAction;
        labelVisibility->setCheckable(true);
        labelVisibility->setChecked(label()->isVisible());
        labelVisibility->setText("Label visible");
        connect(labelVisibility, &QAction::toggled, [this](bool enabled) {
            if (!scene())
                return;

            scene()->undoStack()->push(new QSchematic::Commands::ItemVisibility(label(), enabled));
        });

        // Align label
        QAction* alignLabel = new QAction;
        alignLabel->setText("Align label");
        connect(alignLabel, &QAction::triggered, [this] {
            this->alignLabel();
        });

        // Align connector labels
        QAction* alignConnectorLabels = new QAction;
        alignConnectorLabels->setText("Align connector labels");
        connect(alignConnectorLabels, &QAction::triggered, [this] {
            this->alignConnectorLabels();
        });

        // Show all connectors
        QAction* showAllConnectors = new QAction;
        showAllConnectors->setText("Show all connectors");
        connect(showAllConnectors, &QAction::triggered, [this] {
            if (!scene())
                return;

            for (const std::shared_ptr<Items::Connector>& conn : connectors())
                scene()->undoStack()->push(new QSchematic::Commands::ItemVisibility(conn, true));
        });

        // Add connector
        QAction* newConnector = new QAction;
        newConnector->setText("Add connector");
        connect(newConnector, &QAction::triggered, [this, event] {
            if (!scene())
                return;

            auto connector = std::make_shared<OperationConnector>(event->pos().toPoint(), QStringLiteral("Unnamed"), this);

            scene()->undoStack()->push(new ::Commands::NodeAddConnector(this, connector));
        });

        // Duplicate
        QAction* duplicate = new QAction;
        duplicate->setText("Duplicate");
        connect(duplicate, &QAction::triggered, [this] {
            if (!scene())
                return;

            auto clone = deepCopy();
            clone->setPos( pos() + QPointF(5*_settings.gridSize, 5*_settings.gridSize));
            scene()->addItem(std::move(clone));
        });

        // Delete
        QAction* deleteFromModel = new QAction;
        deleteFromModel->setText("Delete");
        connect(deleteFromModel, &QAction::triggered, [this] {
            if (!scene())
                return;

            // Retrieve smart pointer
            std::shared_ptr<QSchematic::Items::Item> itemPointer;
            for (auto& i : scene()->items()) {
                if (i.get() == this) {
                    itemPointer = i;
                    break;
                }
            }
            if (!itemPointer) {
                return;
            }

            // Issue command
            scene()->undoStack()->push(new QSchematic::Commands::ItemRemove(scene(), itemPointer));
        });

        // Assemble
        menu.addAction(text);
        menu.addAction(labelVisibility);
        menu.addAction(alignLabel);
        menu.addSeparator();
        menu.addAction(newConnector);
        menu.addAction(alignConnectorLabels);
        menu.addAction(showAllConnectors);
        menu.addSeparator();
        menu.addAction(duplicate);
        menu.addAction(deleteFromModel);
        menu.addSeparator();
        menu.addAction(isMovable);
    }

    // Sow the menu
    menu.exec(event->screenPos());
}
