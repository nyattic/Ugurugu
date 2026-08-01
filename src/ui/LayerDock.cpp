#include "ui/LayerDock.hpp"

#include "document/DocumentController.hpp"
#include "document/DocumentLimits.hpp"
#include "ui/Icons.hpp"
#include "ui/LayerItemDelegate.hpp"
#include "ui/LayerListWidget.hpp"
#include "ui/LayerThumbnailRenderer.hpp"

#include <QAbstractItemView>
#include <QCheckBox>
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
#include <functional>

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
    m_layerList->setAccessibleName(tr("Layers"));
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

    m_addGroupButton = makeLayerButton(content,
        IconGlyph::Add,
        QStringLiteral("layerAddGroupButton"),
        tr("Add group containing the selected layer"));
    m_addGroupButton->setText(QStringLiteral("G"));
    m_addGroupButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    buttonLayout->addWidget(m_addGroupButton);

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

    auto *groupLayout = new QHBoxLayout;
    groupLayout->setContentsMargins(10, 0, 10, 0);
    groupLayout->setSpacing(6);

    auto *groupLabel = new QLabel(tr("GROUP"), content);
    groupLabel->setProperty("fieldLabel", true);
    groupLayout->addWidget(groupLabel);

    m_parentGroupCombo = new QComboBox(content);
    m_parentGroupCombo->setObjectName(QStringLiteral("layerParentGroupCombo"));
    m_parentGroupCombo->setAccessibleName(tr("Parent layer group"));
    groupLabel->setBuddy(m_parentGroupCombo);
    groupLayout->addWidget(m_parentGroupCombo, 1);
    layout->addLayout(groupLayout);

    m_clipCheck = new QCheckBox(tr("Clip to layer below"), content);
    m_clipCheck->setObjectName(QStringLiteral("layerClipCheck"));
    m_clipCheck->setToolTip(
        tr("Limit this layer to the opacity of the base layer below it"));
    layout->addWidget(m_clipCheck, 0, Qt::AlignLeft);

    m_referenceCheck = new QCheckBox(tr("Reference layer"), content);
    m_referenceCheck->setObjectName(QStringLiteral("layerReferenceCheck"));
    m_referenceCheck->setToolTip(
        tr("Use this layer when a selection tool references marked layers"));
    layout->addWidget(m_referenceCheck, 0, Qt::AlignLeft);

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
            m_selectedLayerId = id;
            const Layer *layer = m_controller->document().layer(id);
            if (layer && layer->kind == LayerKind::Paint)
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
                const DocumentController::RenameLayerResult result =
                    m_controller->renameLayer(id, name);
                if (result != DocumentController::RenameLayerResult::Renamed)
                {
                    const Layer *currentLayer =
                        m_controller->document().layer(id);
                    if (currentLayer)
                    {
                        QSignalBlocker blocker(m_layerList);
                        item->setText(currentLayer->name);
                    }
                    else
                    {
                        rebuild();
                    }
                }
            }
            else if (item->text() != layer->name)
            {
                QSignalBlocker blocker(m_layerList);
                item->setText(layer->name);
            }
        });

    auto *delegate =
        qobject_cast<LayerItemDelegate *>(m_layerList->itemDelegate());
    const auto toggleVisibility = [this](const QModelIndex &index)
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
    };
    connect(delegate,
        &LayerItemDelegate::visibilityToggled,
        this,
        toggleVisibility);
    connect(m_layerList,
        &LayerListWidget::visibilityToggleRequested,
        this,
        toggleVisibility);

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
                const Layer *selected =
                    m_controller->document().layer(selectedLayerId());
                m_controller->addLayer(
                    selected && selected->kind == LayerKind::Group
                        ? selected->id
                        : QUuid());
            }
        });
    connect(m_addGroupButton,
        &QToolButton::clicked,
        this,
        [this]()
        {
            if (m_controller)
            {
                m_controller->addLayerGroup(selectedLayerId());
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

    connect(m_parentGroupCombo,
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
                m_controller->setLayerParentGroup(
                    id, m_parentGroupCombo->itemData(index).toUuid());
            }
        });

    connect(m_clipCheck,
        &QCheckBox::toggled,
        this,
        [this](bool clipped)
        {
            if (!m_controller || m_syncing)
            {
                return;
            }
            const QUuid id = selectedLayerId();
            if (!id.isNull())
            {
                m_controller->setLayerClipToBelow(id, clipped);
            }
        });

    connect(m_referenceCheck,
        &QCheckBox::toggled,
        this,
        [this](bool reference)
        {
            if (!m_controller || m_syncing)
            {
                return;
            }
            const QUuid id = selectedLayerId();
            if (!id.isNull())
            {
                m_controller->setLayerReference(id, reference);
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
        if (!document.layer(m_selectedLayerId))
        {
            m_selectedLayerId = document.activeLayerId;
        }
        std::function<void(const QUuid &, int)> appendChildren;
        appendChildren = [&](const QUuid &parentId, int depth)
        {
            for (int index = document.layers.size() - 1; index >= 0; --index)
            {
                const Layer &layer = document.layers[index];
                if (layer.parentGroupId != parentId)
                {
                    continue;
                }
                auto *item = new QListWidgetItem(layer.name, m_layerList);
                item->setData(
                    LayerItemRoles::LayerId, QVariant::fromValue(layer.id));
                item->setData(LayerItemRoles::Visible, layer.visible);
                item->setData(Qt::AccessibleDescriptionRole,
                    layer.visible ? tr("Layer is visible")
                                  : tr("Layer is hidden"));
                item->setData(LayerItemRoles::Thumbnail,
                    QVariant::fromValue(m_thumbnails.value(layer.id)));
                item->setData(
                    LayerItemRoles::Kind, static_cast<int>(layer.kind));
                item->setData(LayerItemRoles::Depth, depth);
                item->setData(LayerItemRoles::Clipped, layer.clipToLayerBelow);
                item->setData(LayerItemRoles::Reference, layer.reference);
                item->setFlags(
                    item->flags() | Qt::ItemIsEditable | Qt::ItemIsDragEnabled);
                if (layer.id == m_selectedLayerId)
                {
                    m_layerList->setCurrentItem(item);
                }
                if (layer.kind == LayerKind::Group)
                {
                    appendChildren(layer.id, depth + 1);
                }
            }
        };
        appendChildren({}, 0);
    }

    m_layerList->setEnabled(m_layerList->count() > 0);
    updateControls();
}

void LayerDock::syncActiveLayer(const QUuid &id)
{
    m_selectedLayerId = id;
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
    const bool hasLayer = layer != nullptr;
    const bool paintLayer = layer && layer->kind == LayerKind::Paint;
    const bool hasCapacity =
        document && document->layers.size() < DocumentLimits::maximumLayers;

    QVector<const Layer *> siblings;
    if (document && layer)
    {
        for (const Layer &candidate : document->layers)
        {
            if (candidate.parentGroupId == layer->parentGroupId)
            {
                siblings.append(&candidate);
            }
        }
    }
    const int siblingPosition = layer
                                    ? std::find_if(siblings.cbegin(),
                                          siblings.cend(),
                                          [layer](const Layer *candidate)
                                          {
                                              return candidate->id == layer->id;
                                          })
                                          - siblings.cbegin()
                                    : -1;

    m_addButton->setEnabled(m_controller && hasCapacity);
    m_addGroupButton->setEnabled(m_controller && hasCapacity);
    m_duplicateButton->setEnabled(paintLayer && hasCapacity);
    m_deleteButton->setEnabled(hasLayer);
    m_moveUpButton->setEnabled(hasLayer && siblingPosition >= 0
                               && siblingPosition < siblings.size() - 1);
    m_moveDownButton->setEnabled(hasLayer && siblingPosition > 0);
    m_blendModeCombo->setEnabled(hasLayer);
    m_opacitySlider->setEnabled(hasLayer);
    m_clipCheck->setEnabled(paintLayer);
    m_referenceCheck->setEnabled(paintLayer);

    {
        const QSignalBlocker blocker(m_parentGroupCombo);
        m_parentGroupCombo->clear();
        m_parentGroupCombo->addItem(
            tr("No group"), QVariant::fromValue(QUuid()));
        if (document && layer)
        {
            for (const Layer &candidate : document->layers)
            {
                if (candidate.kind != LayerKind::Group
                    || candidate.id == layer->id
                    || document->isLayerDescendantOf(candidate.id, layer->id))
                {
                    continue;
                }
                m_parentGroupCombo->addItem(
                    candidate.name, QVariant::fromValue(candidate.id));
            }
        }
        m_parentGroupCombo->setCurrentIndex(
            layer ? m_parentGroupCombo->findData(
                        QVariant::fromValue(layer->parentGroupId))
                  : -1);
    }
    m_parentGroupCombo->setEnabled(hasLayer);
    {
        const QSignalBlocker blocker(m_clipCheck);
        m_clipCheck->setChecked(layer && layer->clipToLayerBelow);
    }
    {
        const QSignalBlocker blocker(m_referenceCheck);
        m_referenceCheck->setChecked(layer && layer->reference);
    }

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
        if (m_controller)
        {
            const Document &document = m_controller->document();
            const Layer *layer = document.layer(id);
            while (layer && !layer->parentGroupId.isNull())
            {
                m_pendingThumbnails.insert(layer->parentGroupId);
                layer = document.layer(layer->parentGroupId);
            }
        }
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
    if (finalRow < 0 || finalRow >= m_layerList->count())
    {
        return;
    }
    const Document &document = m_controller->document();
    const Layer *source = document.layer(id);
    const QUuid targetId =
        m_layerList->item(finalRow)->data(LayerItemRoles::LayerId).toUuid();
    const Layer *target = document.layer(targetId);
    if (!source || !target || source->parentGroupId != target->parentGroupId)
    {
        return;
    }
    QVector<QUuid> siblings;
    for (int row = 0; row < m_layerList->count(); ++row)
    {
        const QUuid siblingId =
            m_layerList->item(row)->data(LayerItemRoles::LayerId).toUuid();
        const Layer *sibling = document.layer(siblingId);
        if (sibling && sibling->parentGroupId == source->parentGroupId)
        {
            siblings.append(siblingId);
        }
    }
    const int offset = siblings.indexOf(id) - siblings.indexOf(targetId);
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
