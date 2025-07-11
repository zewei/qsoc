// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2023-2025 Huang Rui <vowstar@gmail.com>

#include "socmoduleitem.h"
#include "socmoduleconnector.h"

#include <qschematic/items/connector.hpp>
#include <qschematic/items/label.hpp>

#include <QBrush>
#include <QDebug>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPen>

using namespace ModuleLibrary;

SocModuleItem::SocModuleItem(
    const QString &moduleName, const YAML::Node &moduleYaml, int type, QGraphicsItem *parent)
    : QSchematic::Items::Node(type, parent)
    , m_moduleName(moduleName)
    , m_moduleYaml(moduleYaml)
{
    // Create module name label
    m_label = std::make_shared<QSchematic::Items::Label>();
    m_label->setParentItem(this);
    m_label->setVisible(true);
    m_label->setMovable(true); // Make label draggable
    m_label->setText(m_moduleName);
    m_label->setHasConnectionPoint(false);

    // Set initial properties
    setAllowMouseResize(true);
    setAllowMouseRotate(true);
    setConnectorsMovable(true);
    setConnectorsSnapPolicy(QSchematic::Items::Connector::NodeSizerectOutline);
    setConnectorsSnapToGrid(true);

    // Create ports from YAML data
    createPortsFromYaml();

    // Connect signals
    connect(this, &QSchematic::Items::Node::sizeChanged, this, &SocModuleItem::updateLabelPosition);
    connect(this, &QSchematic::Items::Item::settingsChanged, [this] {
        m_label->setSettings(_settings);
    });
}

QString SocModuleItem::moduleName() const
{
    return m_moduleName;
}

void SocModuleItem::setModuleName(const QString &name)
{
    m_moduleName = name;
    if (m_label) {
        m_label->setText(name);
    }
    update();
}

YAML::Node SocModuleItem::moduleYaml() const
{
    return m_moduleYaml;
}

void SocModuleItem::setModuleYaml(const YAML::Node &yaml)
{
    m_moduleYaml = yaml;

    // Clear existing ports
    for (auto &port : m_ports) {
        removeConnector(port);
    }
    m_ports.clear();

    // Recreate ports
    createPortsFromYaml();
}

std::shared_ptr<QSchematic::Items::Item> SocModuleItem::deepCopy() const
{
    auto copy = std::make_shared<SocModuleItem>(m_moduleName, m_moduleYaml, type());
    copy->setPos(pos());
    copy->setRotation(rotation());
    copy->setSize(size());

    return copy;
}

void SocModuleItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    Q_UNUSED(option)
    Q_UNUSED(widget)

    // Draw the bounding rect if debug mode is enabled
    if (_settings.debug) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(QBrush(Qt::red));
        painter->drawRect(boundingRect());
    }

    QRectF rect = sizeRect();

    // Body pen (Color4DBodyEx: #840000)
    QPen bodyPen;
    bodyPen.setWidthF(1.5);
    bodyPen.setStyle(Qt::SolidLine);
    bodyPen.setColor(QColor(132, 0, 0));

    // Body brush (Color4DBodyBgEx: #FFFFC2)
    QBrush bodyBrush;
    bodyBrush.setStyle(Qt::SolidPattern);
    bodyBrush.setColor(QColor(255, 255, 194));

    // Draw the component body (sharp rectangle, no rounded corners)
    painter->setPen(bodyPen);
    painter->setBrush(bodyBrush);
    painter->drawRect(rect);

    // Draw module name (Color4DReferenceEx: #008484)
    painter->setPen(QPen(QColor(0, 132, 132)));
    QFont font = painter->font();
    font.setBold(true);
    font.setPointSize(10);
    painter->setFont(font);

    QRectF textRect(0, 5, rect.width(), LABEL_HEIGHT);
    painter->drawText(textRect, Qt::AlignCenter, m_moduleName);

    // Draw separator line (Color4DGridEx: #848484)
    QPen separatorPen(QColor(132, 132, 132), 1.0);
    painter->setPen(separatorPen);
    painter->drawLine(10, LABEL_HEIGHT + 5, rect.width() - 10, LABEL_HEIGHT + 5);

    // Resize handles
    if (isSelected() && allowMouseResize()) {
        paintResizeHandles(*painter);
    }

    // Rotate handle
    if (isSelected() && allowMouseRotate()) {
        paintRotateHandle(*painter);
    }
}

void SocModuleItem::createPortsFromYaml()
{
    if (!m_moduleYaml) {
        qDebug() << "No module YAML data found for" << m_moduleName;
        return;
    }

    QStringList inputPorts;
    QStringList outputPorts;
    QStringList inoutPorts;
    QStringList busPorts;

    // Process regular ports
    if (m_moduleYaml["port"]) {
        const YAML::Node &ports = m_moduleYaml["port"];

        for (const auto &portPair : ports) {
            const std::string portName = portPair.first.as<std::string>();
            const YAML::Node &portData = portPair.second;

            if (portData["direction"]) {
                const std::string direction  = portData["direction"].as<std::string>();
                const QString     portNameQt = QString::fromStdString(portName);

                if (direction == "in" || direction == "input") {
                    inputPorts.append(portNameQt);
                } else if (direction == "out" || direction == "output") {
                    outputPorts.append(portNameQt);
                } else if (direction == "inout") {
                    inoutPorts.append(portNameQt);
                }
            }
        }
    }

    // Process bus ports
    if (m_moduleYaml["bus"]) {
        const YAML::Node &buses = m_moduleYaml["bus"];

        for (const auto &busPair : buses) {
            const std::string busName   = busPair.first.as<std::string>();
            const QString     busNameQt = QString::fromStdString(busName);
            busPorts.append(busNameQt);
        }
    }

    // Calculate required size
    const int   leftSidePorts  = inputPorts.size() + busPorts.size();
    const int   rightSidePorts = outputPorts.size() + inoutPorts.size();
    const int   maxPorts       = qMax(leftSidePorts, rightSidePorts);
    const qreal requiredHeight = qMax(MIN_HEIGHT, LABEL_HEIGHT + 30 + maxPorts * PORT_SPACING);
    const qreal requiredWidth  = MIN_WIDTH;

    setSize(requiredWidth, requiredHeight);

    // Get grid size for proper positioning
    const int gridSize = _settings.gridSize > 0 ? _settings.gridSize : 20;

    // Create input ports (left side)
    int leftPortIndex = 0;
    for (int i = 0; i < inputPorts.size(); ++i) {
        const qreal yPos = LABEL_HEIGHT + 20 + leftPortIndex * PORT_SPACING;
        QPoint      gridPos(
            0,                                  // Left edge
            static_cast<int>(yPos / gridSize)); // Convert to grid coordinates
        auto connector = std::make_shared<SocModuleConnector>(
            gridPos, inputPorts[i], SocModuleConnector::Input, SocModuleConnector::Left, this);
        addConnector(connector);
        m_ports.append(connector);
        leftPortIndex++;
    }

    // Create bus ports (left side, after input ports)
    for (int i = 0; i < busPorts.size(); ++i) {
        const qreal yPos = LABEL_HEIGHT + 20 + leftPortIndex * PORT_SPACING;
        QPoint      gridPos(
            0,                                  // Left edge
            static_cast<int>(yPos / gridSize)); // Convert to grid coordinates
        auto connector = std::make_shared<SocModuleConnector>(
            gridPos, busPorts[i], SocModuleConnector::Bus, SocModuleConnector::Left, this);
        addConnector(connector);
        m_ports.append(connector);
        leftPortIndex++;
    }

    // Create output ports (right side)
    int rightPortIndex = 0;
    for (int i = 0; i < outputPorts.size(); ++i) {
        const qreal yPos = LABEL_HEIGHT + 20 + rightPortIndex * PORT_SPACING;
        QPoint      gridPos(
            static_cast<int>(requiredWidth / gridSize), // Right edge
            static_cast<int>(yPos / gridSize));         // Convert to grid coordinates
        auto connector = std::make_shared<SocModuleConnector>(
            gridPos, outputPorts[i], SocModuleConnector::Output, SocModuleConnector::Right, this);
        addConnector(connector);
        m_ports.append(connector);
        rightPortIndex++;
    }

    // Create inout ports (right side, after output ports)
    for (int i = 0; i < inoutPorts.size(); ++i) {
        const qreal yPos = LABEL_HEIGHT + 20 + rightPortIndex * PORT_SPACING;
        QPoint      gridPos(
            static_cast<int>(requiredWidth / gridSize), // Right edge
            static_cast<int>(yPos / gridSize));         // Convert to grid coordinates
        auto connector = std::make_shared<SocModuleConnector>(
            gridPos, inoutPorts[i], SocModuleConnector::InOut, SocModuleConnector::Right, this);
        addConnector(connector);
        m_ports.append(connector);
        rightPortIndex++;
    }

    updateLabelPosition();
}

QSizeF SocModuleItem::calculateRequiredSize() const
{
    if (!m_moduleYaml) {
        return QSizeF(MIN_WIDTH, MIN_HEIGHT);
    }

    int inputCount  = 0;
    int outputCount = 0;
    int inoutCount  = 0;
    int busCount    = 0;

    // Count regular ports
    if (m_moduleYaml["port"]) {
        const YAML::Node &ports = m_moduleYaml["port"];

        for (const auto &portPair : ports) {
            const YAML::Node &portData = portPair.second;

            if (portData["direction"]) {
                const std::string direction = portData["direction"].as<std::string>();

                if (direction == "in" || direction == "input") {
                    inputCount++;
                } else if (direction == "out" || direction == "output") {
                    outputCount++;
                } else if (direction == "inout") {
                    inoutCount++;
                }
            }
        }
    }

    // Count bus ports
    if (m_moduleYaml["bus"]) {
        const YAML::Node &buses = m_moduleYaml["bus"];
        busCount                = buses.size();
    }

    const int   leftSidePorts  = inputCount + busCount;
    const int   rightSidePorts = outputCount + inoutCount;
    const int   maxPorts       = qMax(leftSidePorts, rightSidePorts);
    const qreal requiredHeight = qMax(MIN_HEIGHT, LABEL_HEIGHT + 20 + maxPorts * PORT_SPACING);

    return QSizeF(MIN_WIDTH, requiredHeight);
}

void SocModuleItem::arrangePorts()
{
    // This function is called after ports are created to arrange them properly
    // Implementation is already handled in createPortsFromYaml()
}

void SocModuleItem::updateLabelPosition()
{
    if (m_label) {
        QRectF rect = sizeRect();
        m_label->setPos(rect.center().x() - m_label->boundingRect().width() / 2, 8);
    }
}
