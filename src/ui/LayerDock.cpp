#include "ui/LayerDock.hpp"

#include "document/DocumentController.hpp"
#include "document/DocumentLimits.hpp"
#include "ui/Icons.hpp"
#include "ui/LayerItemDelegate.hpp"
#include "ui/LayerListWidget.hpp"
#include "ui/LayerThumbnailRenderer.hpp"

#include <QAbstractItemView>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidgetItem>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariant>
#include <QWidget>

#include <cmath>

namespace wobble
{

namespace
{

QToolButton *makeLayerButton(
    QWidget *parent, IconGlyph glyph, const QString &name, const QString &label)
{
    auto *button = new QToolButton(parent);
    button->setObjectName(name);
    button->setIcon(Icons::icon(glyph));
    button->setIconSize(QSize(16, 16));
    button->setToolTip(label);
    button->setAccessibleName(label);
    return button;
}

}

LayerDock::LayerDock(DocumentController *controller, QWidget *parent)
    : QDockWidget(tr("Layers"), parent)
    , m_controller(controller)
{
    setObjectName(QStringLiteral("LayerDock"));
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    auto *title = new QLabel(tr("LAYERS"), this);
    title->setObjectName(QStringLiteral("LayerDockTitle"));
    setTitleBarWidget(title);

    buildContent();
    connectControls();

    m_thumbnailTimer.setSingleShot(true);
    m_thumbnailTimer.setInterval(180);
    connect(&m_thumbnailTimer,
        &QTimer::timeout,
        this,
        &LayerDock::regenerateThumbnails);

    rebuild();
    scheduleAllThumbnails();
}

void LayerDock::buildContent()
{
    auto *content = new QWidget(this);
    content->setObjectName(QStringLiteral("LayerDockBody"));
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 8);
    layout->setSpacing(8);

    m_layerList = new LayerListWidget(content);
    m_layerList->setEditTriggers(
        QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    m_layerList->setItemDelegate(new LayerItemDelegate(m_layerList));
    layout->addWidget(m_layerList, 1);

    auto *buttonLayout = new QHBoxLayout;
    buttonLayout->setContentsMargins(8, 0, 8, 0);
    buttonLayout->setSpacing(2);

    m_addButton = makeLayerButton(content,
        IconGlyph::Add,
        QStringLiteral("layerAddButton"),
        tr("Add layer"));
    buttonLayout->addWidget(m_addButton);

    m_duplicateButton = makeLayerButton(content,
        IconGlyph::Duplicate,
        QStringLiteral("layerDuplicateButton"),
        tr("Duplicate layer"));
    buttonLayout->addWidget(m_duplicateButton);

    m_deleteButton = makeLayerButton(content,
        IconGlyph::Remove,
        QStringLiteral("layerDeleteButton"),
        tr("Delete layer"));
    buttonLayout->addWidget(m_deleteButton);

    buttonLayout->addStretch(1);

    m_moveUpButton = makeLayerButton(content,
        IconGlyph::MoveUp,
        QStringLiteral("layerMoveUpButton"),
        tr("Move layer up"));
    buttonLayout->addWidget(m_moveUpButton);

    m_moveDownButton = makeLayerButton(content,
        IconGlyph::MoveDown,
        QStringLiteral("layerMoveDownButton"),
        tr("Move layer down"));
    buttonLayout->addWidget(m_moveDownButton);

    layout->addLayout(buttonLayout);

    auto *blendModeLayout = new QHBoxLayout;
    blendModeLayout->setContentsMargins(10, 0, 10, 0);
    blendModeLayout->setSpacing(6);

    auto *blendModeLabel = new QLabel(tr("BLEND MODE"), content);
    blendModeLabel->setProperty("fieldLabel", true);
    blendModeLayout->addWidget(blendModeLabel);

    m_blendModeCombo = new QComboBox(content);
    m_blendModeCombo->setObjectName(QStringLiteral("layerBlendModeCombo"));
    m_blendModeCombo->setAccessibleName(tr("Layer blend mode"));
    m_blendModeCombo->addItem(
        tr("Normal"), static_cast<int>(LayerBlendMode::Normal));
    m_blendModeCombo->addItem(
        tr("Multiply"), static_cast<int>(LayerBlendMode::Multiply));
    m_blendModeCombo->addItem(
        tr("Screen"), static_cast<int>(LayerBlendMode::Screen));
    m_blendModeCombo->addItem(
        tr("Overlay"), static_cast<int>(LayerBlendMode::Overlay));
    blendModeLabel->setBuddy(m_blendModeCombo);
    blendModeLayout->addWidget(m_blendModeCombo, 1);

    layout->addLayout(blendModeLayout);

    auto *opacityLayout = new QHBoxLayout;
    opacityLayout->setContentsMargins(10, 0, 10, 0);
    opacityLayout->setSpacing(6);

    auto *opacityLabel = new QLabel(tr("OPACITY"), content);
    opacityLabel->setProperty("fieldLabel", true);
    opacityLayout->addWidget(opacityLabel);

    m_opacitySlider = new QSlider(Qt::Horizontal, content);
    m_opacitySlider->setRange(0, 100);
    m_opacitySlider->setSingleStep(1);
    m_opacitySlider->setPageStep(10);
    m_opacitySlider->setTracking(true);
    opacityLabel->setBuddy(m_opacitySlider);
    opacityLayout->addWidget(m_opacitySlider, 1);

    m_opacityValue = new QLabel(content);
    m_opacityValue->setMinimumWidth(
        m_opacityValue->fontMetrics().horizontalAdvance(
            QStringLiteral("100%")));
    m_opacityValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    opacityLayout->addWidget(m_opacityValue);

    layout->addLayout(opacityLayout);
    setWidget(content);
}

void LayerDock::connectControls()
{
    connect(m_layerList,
        &QListWidget::currentItemChanged,
        this,
        [this](QListWidgetItem *current)
        {
            if (m_syncing || !m_controller || !current)
            {
                updateControls();
                return;
            }
            const QUuid id = current->data(LayerItemRoles::LayerId).toUuid();
            if (!id.isNull())
            {
                m_controller->setActiveLayer(id);
            }
            updateControls();
        });

    connect(m_layerList,
        &QListWidget::itemChanged,
        this,
        [this](QListWidgetItem *item)
        {
            if (m_syncing || !m_controller || !item)
            {
                return;
            }
            const QUuid id = item->data(LayerItemRoles::LayerId).toUuid();
            const Layer *layer = m_controller->document().layer(id);
            if (!layer)
            {
                return;
            }
            const QString name = item->text().trimmed();
            if (!name.isEmpty() && name != layer->name)
            {
                m_controller->renameLayer(id, name);
            }
            else if (item->text() != layer->name)
            {
                QSignalBlocker blocker(m_layerList);
                item->setText(layer->name);
            }
        });

    auto *delegate =
        qobject_cast<LayerItemDelegate *>(m_layerList->itemDelegate());
    connect(delegate,
        &LayerItemDelegate::visibilityToggled,
        this,
        [this](const QModelIndex &index)
        {
            if (!m_controller)
            {
                return;
            }
            const QUuid id = index.data(LayerItemRoles::LayerId).toUuid();
            const bool visible = index.data(LayerItemRoles::Visible).toBool();
            if (!id.isNull())
            {
                m_controller->setLayerVisible(id, !visible);
            }
        });

    connect(m_layerList,
        &LayerListWidget::reorderRequested,
        this,
        &LayerDock::handleReorder);

    connect(m_addButton,
        &QToolButton::clicked,
        this,
        [this]()
        {
            if (m_controller)
            {
                m_controller->addLayer();
            }
        });
    connect(m_duplicateButton,
        &QToolButton::clicked,
        this,
        [this]()
        {
            const QUuid id = selectedLayerId();
            if (m_controller && !id.isNull())
            {
                m_controller->duplicateLayer(id);
            }
        });
    connect(m_deleteButton,
        &QToolButton::clicked,
        this,
        [this]()
        {
            const QUuid id = selectedLayerId();
            if (m_controller && !id.isNull())
            {
                m_controller->removeLayer(id);
            }
        });
    connect(m_moveUpButton,
        &QToolButton::clicked,
        this,
        [this]()
        {
            const QUuid id = selectedLayerId();
            if (m_controller && !id.isNull())
            {
                m_controller->moveLayer(id, 1);
            }
        });
    connect(m_moveDownButton,
        &QToolButton::clicked,
        this,
        [this]()
        {
            const QUuid id = selectedLayerId();
            if (m_controller && !id.isNull())
            {
                m_controller->moveLayer(id, -1);
            }
        });

    connect(m_opacitySlider,
        &QSlider::sliderPressed,
        this,
        [this]()
        {
            if (m_syncing)
            {
                return;
            }
            m_opacityDragging = true;
            m_opacityLayerId = selectedLayerId();
        });
    connect(m_opacitySlider,
        &QSlider::sliderReleased,
        this,
        [this]()
        {
            if (!m_opacityDragging)
            {
                return;
            }
            const QUuid id = m_opacityLayerId;
            m_opacityDragging = false;
            m_opacityLayerId = {};
            commitOpacity(id, m_opacitySlider->value());
        });
    connect(m_opacitySlider,
        &QSlider::valueChanged,
        this,
        [this](int value)
        {
            m_opacityValue->setText(tr("%1%").arg(value));
            if (!m_syncing && !m_opacityDragging)
            {
                commitOpacity(selectedLayerId(), value);
            }
        });

    connect(m_blendModeCombo,
        &QComboBox::currentIndexChanged,
        this,
        [this](int index)
        {
            if (!m_controller || m_syncing || index < 0)
            {
                return;
            }
            const QUuid id = selectedLayerId();
            if (!id.isNull())
            {
                m_controller->setLayerBlendMode(id,
                    static_cast<LayerBlendMode>(
                        m_blendModeCombo->itemData(index).toInt()));
            }
        });

    if (m_controller)
    {
        connect(m_controller,
            &DocumentController::documentChanged,
            this,
            [this]()
            {
                rebuild();
            });
        connect(m_controller,
            &DocumentController::layerThumbnailChanged,
            this,
            &LayerDock::scheduleLayerThumbnail);
        connect(m_controller,
            &DocumentController::layerThumbnailsReset,
            this,
            &LayerDock::scheduleAllThumbnails);
        connect(m_controller,
            &DocumentController::activeLayerChanged,
            this,
            &LayerDock::syncActiveLayer);
        connect(m_controller,
            &QObject::destroyed,
            this,
            [this]()
            {
                m_controller = nullptr;
                rebuild();
            });
    }
}

void LayerDock::rebuild()
{
    QScopedValueRollback syncing(m_syncing, true);
    QSignalBlocker blocker(m_layerList);
    m_layerList->clear();

    if (m_controller)
    {
        const Document &document = m_controller->document();
        for (int index = document.layers.size() - 1; index >= 0; --index)
        {
            const Layer &layer = document.layers[index];
            auto *item = new QListWidgetItem(layer.name, m_layerList);
            item->setData(
                LayerItemRoles::LayerId, QVariant::fromValue(layer.id));
            item->setData(LayerItemRoles::Visible, layer.visible);
            item->setData(LayerItemRoles::Thumbnail,
                QVariant::fromValue(m_thumbnails.value(layer.id)));
            item->setFlags(
                item->flags() | Qt::ItemIsEditable | Qt::ItemIsDragEnabled);
            if (layer.id == document.activeLayerId)
            {
                m_layerList->setCurrentItem(item);
            }
        }
    }

    m_layerList->setEnabled(m_layerList->count() > 0);
    updateControls();
}

void LayerDock::syncActiveLayer(const QUuid &id)
{
    QScopedValueRollback syncing(m_syncing, true);
    QSignalBlocker blocker(m_layerList);
    QListWidgetItem *match = nullptr;

    for (int row = 0; row < m_layerList->count(); ++row)
    {
        QListWidgetItem *item = m_layerList->item(row);
        if (item->data(LayerItemRoles::LayerId).toUuid() == id)
        {
            match = item;
            break;
        }
    }

    m_layerList->setCurrentItem(match);
    updateControls();
}

void LayerDock::updateControls()
{
    const QUuid id = selectedLayerId();
    const Document *document =
        m_controller ? &m_controller->document() : nullptr;
    const Layer *layer = document ? document->layer(id) : nullptr;
    const int index = document ? document->layerIndex(id) : -1;
    const bool hasLayer = layer != nullptr;
    const bool hasCapacity =
        document && document->layers.size() < DocumentLimits::maximumLayers;

    m_addButton->setEnabled(m_controller && hasCapacity);
    m_duplicateButton->setEnabled(hasLayer && hasCapacity);
    m_deleteButton->setEnabled(hasLayer);
    m_moveUpButton->setEnabled(hasLayer && index < document->layers.size() - 1);
    m_moveDownButton->setEnabled(hasLayer && index > 0);
    m_blendModeCombo->setEnabled(hasLayer);
    m_opacitySlider->setEnabled(hasLayer);

    {
        const QSignalBlocker blocker(m_blendModeCombo);
        const int blendModeIndex =
            layer
                ? m_blendModeCombo->findData(static_cast<int>(layer->blendMode))
                : -1;
        m_blendModeCombo->setCurrentIndex(blendModeIndex);
    }

    if (!m_opacityDragging || m_opacityLayerId != id)
    {
        const int opacity =
            layer ? static_cast<int>(std::lround(layer->opacity * 100.0)) : 0;
        QSignalBlocker blocker(m_opacitySlider);
        m_opacitySlider->setValue(opacity);
        m_opacityValue->setText(tr("%1%").arg(opacity));
    }
}

void LayerDock::regenerateThumbnails()
{
    if (!m_controller)
    {
        m_thumbnails.clear();
        m_pendingThumbnails.clear();
        m_regenerateAllThumbnails = false;
        return;
    }

    const Document &document = m_controller->document();
    QSet<QUuid> existing;
    for (const Layer &layer : document.layers)
    {
        existing.insert(layer.id);
        if (m_regenerateAllThumbnails)
        {
            m_pendingThumbnails.insert(layer.id);
        }
    }
    for (auto iterator = m_thumbnails.begin(); iterator != m_thumbnails.end();)
    {
        if (!existing.contains(iterator.key()))
        {
            iterator = m_thumbnails.erase(iterator);
        }
        else
        {
            ++iterator;
        }
    }
    m_regenerateAllThumbnails = false;

    if (m_pendingThumbnails.isEmpty())
    {
        return;
    }

    const QUuid nextId = *m_pendingThumbnails.constBegin();
    m_pendingThumbnails.remove(nextId);
    if (const Layer *layer = document.layer(nextId))
    {
        m_thumbnails.insert(
            nextId, LayerThumbnailRenderer::render(document, *layer));
    }

    QScopedValueRollback syncing(m_syncing, true);
    QSignalBlocker blocker(m_layerList);
    for (int row = 0; row < m_layerList->count(); ++row)
    {
        QListWidgetItem *item = m_layerList->item(row);
        const QUuid id = item->data(LayerItemRoles::LayerId).toUuid();
        if (id != nextId)
        {
            continue;
        }
        item->setData(LayerItemRoles::Thumbnail,
            QVariant::fromValue(m_thumbnails.value(id)));
        break;
    }
    m_layerList->viewport()->update();

    if (!m_pendingThumbnails.isEmpty())
    {
        // Yield to input and paint events between expensive layer renders.
        m_thumbnailTimer.start(0);
    }
}

void LayerDock::scheduleAllThumbnails()
{
    m_regenerateAllThumbnails = true;
    m_pendingThumbnails.clear();
    m_thumbnailTimer.start(180);
}

void LayerDock::scheduleLayerThumbnail(const QUuid &id)
{
    if (!id.isNull() && !m_regenerateAllThumbnails)
    {
        m_pendingThumbnails.insert(id);
    }
    m_thumbnailTimer.start(180);
}

void LayerDock::commitOpacity(const QUuid &id, int value)
{
    if (!m_controller || id.isNull())
    {
        return;
    }
    m_controller->setLayerOpacity(id, static_cast<qreal>(value) / 100.0);
}

void LayerDock::handleReorder(int sourceRow, int insertRow)
{
    if (!m_controller || sourceRow < 0 || sourceRow >= m_layerList->count())
    {
        return;
    }
    const QUuid id =
        m_layerList->item(sourceRow)->data(LayerItemRoles::LayerId).toUuid();
    const int finalRow = insertRow > sourceRow ? insertRow - 1 : insertRow;
    const int offset = sourceRow - finalRow;
    if (offset != 0 && !id.isNull())
    {
        m_controller->moveLayer(id, offset);
    }
}

QUuid LayerDock::selectedLayerId() const
{
    const QListWidgetItem *item = m_layerList->currentItem();
    return item ? item->data(LayerItemRoles::LayerId).toUuid() : QUuid();
}

}
