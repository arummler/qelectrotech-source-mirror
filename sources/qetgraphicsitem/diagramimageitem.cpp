/*
	Copyright 2006-2026 The QElectroTech Team
	This file is part of QElectroTech.

	QElectroTech is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.

	QElectroTech is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with QElectroTech.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "diagramimageitem.h"

#include "../PropertiesEditor/propertieseditordialog.h"
#include "../QPropertyUndoCommand/qpropertyundocommand.h"
#include "../diagram.h"
#include "../diagramview.h"
#include "../qet.h"
#include "../qetapp.h"
#include "../ui/imagepropertieswidget.h"
#include "../ui/imagecropdialog.h"
#include "../ui/imagetransparentcolordialog.h"

#include <QAction>
#include <QFileDialog>
#include <QGraphicsSceneContextMenuEvent>
#include <QMenu>
#include <QMessageBox>

/**
	@brief DiagramImageItem::DiagramImageItem
	Constructor without pixmap
	@param parent_item the parent graphics item
*/
DiagramImageItem::DiagramImageItem(QetGraphicsItem *parent_item):
	QetGraphicsItem(parent_item)
{
	setFlags(QGraphicsItem::ItemIsSelectable|QGraphicsItem::ItemIsMovable|QGraphicsItem::ItemSendsGeometryChanges);
}

/**
	@brief DiagramImageItem::DiagramImageItem
	Constructor with pixmap
	@param pixmap the pixmap to be draw
	@param parent_item the parent graphic item
*/
DiagramImageItem::DiagramImageItem(const QPixmap &pixmap, QetGraphicsItem *parent_item):
	QetGraphicsItem(parent_item),
	pixmap_(pixmap),
	m_base_pixmap(pixmap),
	m_crop_rect(pixmap.rect())
{
	setTransformOriginPoint(boundingRect().center());
	setFlags(QGraphicsItem::ItemIsSelectable|QGraphicsItem::ItemIsMovable|QGraphicsItem::ItemSendsGeometryChanges);
}

/**
	@brief DiagramImageItem::~DiagramImageItem
	Destructor
*/
DiagramImageItem::~DiagramImageItem()
{
}

/**
	@brief DiagramImageItem::paint
	Draw the pixmap.
	@param painter the Qpainter to use for draw the pixmap
	@param option the style option
	@param widget the QWidget where we draw the pixmap
*/
void DiagramImageItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) {
	painter -> drawPixmap(pixmap_.rect(),pixmap_);

	Q_UNUSED(option); Q_UNUSED(widget);

	if (isSelected()) {
		painter -> save();
		// Annulation des renderhints
		painter -> setRenderHint(QPainter::Antialiasing,          false);
		painter -> setRenderHint(QPainter::TextAntialiasing,      false);
		painter -> setRenderHint(QPainter::SmoothPixmapTransform, false);
		// Dessin du cadre de selection en noir à partir du boundingrect
		QPen t(Qt::black);
		t.setStyle(Qt::DashLine);
		painter -> setPen(t);
		painter -> drawRect(boundingRect());
		painter -> restore();
	}
}

/**
	@brief DiagramImageItem::editProperty
	Open the appropriate dialog to edit this image
*/
void DiagramImageItem::editProperty()
{
	if (diagram() -> isReadOnly()) return;
	PropertiesEditorDialog dialog(new ImagePropertiesWidget(this), QApplication::activeWindow());
	dialog.exec();
}

/**
	@brief DiagramImageItem::setPixmap
	Set the new pixmap to be draw
	@param pixmap the new pixmap
*/
void DiagramImageItem::setPixmap(const QPixmap &pixmap) {
	// Only ever mattered once setPixmap() could be called on an
	// already-placed item with a differently-sized replacement (see
	// replaceImage()) -- previously this only ran from the constructor,
	// before the item existed in any scene, where there was nothing yet
	// to invalidate. Without this, a same-session replace to a
	// different-sized image would leave the scene's bounding-rect cache
	// stale, the exact class of bug already fixed multiple times this
	// session for shapes.
	prepareGeometryChange();
	pixmap_ = pixmap;
	setTransformOriginPoint(boundingRect().center());
	emit pixmapChanged();
}

/**
	@brief DiagramImageItem::computeDisplayPixmap
	Re-derives what pixmap_ should be from first principles: crop the
	true original down to the chosen region, then colour-key whichever
	colours have been picked out of it. Used whenever crop() or
	setTransparentColor() changes one of those two independently, so
	the other's effect is correctly re-applied on top rather than lost
	or compounded -- cropping after colours were already picked has to
	still show them keyed out; picking colours after a crop has to only
	ever consider what's still actually part of the image.
	@param base the true, uncropped original
	@param cropRect the region of base to keep, in base's own coordinates
	@param colors colours to key transparent within the cropped region
	@param tolerance how loosely to match those colours, 0-100
*/
QPixmap DiagramImageItem::computeDisplayPixmap(const QPixmap &base, const QRect &cropRect, const QList<QColor> &colors, int tolerance)
{
	const QPixmap cropped = cropRect == base.rect() ? base : base.copy(cropRect);
	if (colors.isEmpty())
		return cropped;
	return QPixmap::fromImage(ImageTransparentColorDialog::applyColorKey(cropped.toImage(), colors, tolerance));
}

/**
	@brief DiagramImageItem::boundingRect
	the outer bounds of the item as a rectangle,
	if no pixmap are set, return a default QRectF
	@return a QRectF represent the bounding rectangle
*/
QRectF DiagramImageItem::boundingRect() const
{
	if (!pixmap_.isNull()) {
		return (QRectF(pixmap_.rect()));
	} else {
		QRectF bound;
		return (bound);
	}
}

/**
	@brief DiagramImageItem::name
	@return the generic name of this item (picture)
*/
QString DiagramImageItem::name() const
{
	return tr("une image");
}

/**
	@brief DiagramImageItem::fromXml
	Load this image from xml element e
	@param e
	@return true if successfully loaded.
*/
bool DiagramImageItem::fromXml(const QDomElement &e)
{
	if (e.tagName() != "image") {
		return (false);
	}
	
	QDomNode image_node = e.firstChild();
	if (!image_node.isText()) {
		return (false);
	}

	//load xml image to QByteArray
	QByteArray array;
	array = QByteArray::fromBase64(e.text().toLatin1());

	//Set QPixmap from the array
	QPixmap pixmap;
	pixmap.loadFromData(array);
	setPixmap(pixmap);

	// Falls back to treating the loaded result as its own base, with no
	// remembered crop or colours -- correct both for a genuinely plain
	// image and for a file saved before these features existed.
	// Overwritten below if the file actually does carry this
	// information.
	m_base_pixmap = pixmap;
	m_crop_rect = pixmap.rect();
	m_transparent_colors.clear();
	m_transparent_tolerance = 10;

	const QDomElement colorsElement = e.firstChildElement("transparent_colors");
	bool hasColors = !colorsElement.isNull();
	if (hasColors)
	{
		m_transparent_tolerance = colorsElement.attribute("tolerance", "10").toInt();
		for (const QDomElement &colorElement : QET::findInDomElement(colorsElement, "color"))
		{
			m_transparent_colors.append(QColor(
					colorElement.attribute("r").toInt(),
					colorElement.attribute("g").toInt(),
					colorElement.attribute("b").toInt()));
		}
	}

	const QDomElement cropElement = e.firstChildElement("crop");
	bool hasCrop = !cropElement.isNull();
	if (hasCrop)
	{
		m_crop_rect = QRect(
				cropElement.attribute("x").toInt(),
				cropElement.attribute("y").toInt(),
				cropElement.attribute("w").toInt(),
				cropElement.attribute("h").toInt());
	}

	// Present, and only meaningful, whenever either of the above is --
	// the base pixmap on its own, with no crop or colours to apply to
	// it, wouldn't mean anything.
	if (hasColors || hasCrop)
	{
		const QDomElement baseElement = e.firstChildElement("image_base");
		if (!baseElement.isNull())
		{
			const QByteArray baseArray = QByteArray::fromBase64(baseElement.text().toLatin1());
			QPixmap basePixmap;
			if (basePixmap.loadFromData(baseArray))
				m_base_pixmap = basePixmap;
		}
		// m_crop_rect may still refer to a saved file's base image, not
		// pixmap (used as a fallback above only when nothing better is
		// available) -- clamp it to whatever base actually ended up
		// loaded, so an inconsistent or hand-edited file can't produce
		// an out-of-bounds crop rect later.
		m_crop_rect = m_crop_rect.intersected(m_base_pixmap.rect());
		if (m_crop_rect.isEmpty())
			m_crop_rect = m_base_pixmap.rect();
	}

	setScale(e.attribute("size").toDouble());
	setRotation(e.attribute("rotation").toDouble());
		//We directly call setPos from QGraphicsObject, because QetGraphicsItem will snap to grid
	QGraphicsObject::setPos(e.attribute("x").toDouble(), e.attribute("y").toDouble());
	setZValue(e.attribute("z", QString::number(this->zValue())).toDouble());
	is_movable_ = (e.attribute("is_movable").toInt());

	return (true);
}

/**
	@param document Le document XML a utiliser
	@return L'element XML representant l'image
*/
QDomElement DiagramImageItem::toXml(QDomDocument &document) const
{
	QDomElement result = document.createElement("image");
	//write some attribute
	result.setAttribute("x", QString::number(pos().x()));
	result.setAttribute("y", QString::number(pos().y()));
	result.setAttribute("z", QString::number(this->zValue()));
	result.setAttribute("rotation", QString::number(QET::correctAngle(rotation())));
	result.setAttribute("size", QString::number(scale()));
	result.setAttribute("is_movable", bool(is_movable_));

	//write the pixmap in the xml element after he was been transformed to base64
	QByteArray array;
	QBuffer buffer(&array);
	buffer.open(QIODevice::ReadWrite);
	pixmap_.save(&buffer, "PNG");
	QDomText base64 = document.createTextNode(array.toBase64());
	result.appendChild(base64);

	// Only written when there's actually something to remember -- a
	// plain image that's never been cropped or had transparency applied
	// shouldn't carry this extra weight at all. pixmap_ above already
	// reflects the current result on its own, so older code (or a file
	// that never used either feature) reads back exactly what it
	// always did; this is purely additional context for the dialogs'
	// own memory, letting them be reopened non-destructively.
	const bool hasCrop = (m_crop_rect != m_base_pixmap.rect());
	const bool hasColors = !m_transparent_colors.isEmpty();

	if (hasColors)
	{
		QDomElement colorsElement = document.createElement("transparent_colors");
		colorsElement.setAttribute("tolerance", m_transparent_tolerance);
		for (const QColor &color : m_transparent_colors)
		{
			QDomElement colorElement = document.createElement("color");
			colorElement.setAttribute("r", color.red());
			colorElement.setAttribute("g", color.green());
			colorElement.setAttribute("b", color.blue());
			colorsElement.appendChild(colorElement);
		}
		result.appendChild(colorsElement);
	}

	if (hasCrop)
	{
		QDomElement cropElement = document.createElement("crop");
		cropElement.setAttribute("x", m_crop_rect.x());
		cropElement.setAttribute("y", m_crop_rect.y());
		cropElement.setAttribute("w", m_crop_rect.width());
		cropElement.setAttribute("h", m_crop_rect.height());
		result.appendChild(cropElement);
	}

	if (hasCrop || hasColors)
	{
		QByteArray baseArray;
		QBuffer baseBuffer(&baseArray);
		baseBuffer.open(QIODevice::ReadWrite);
		m_base_pixmap.save(&baseBuffer, "PNG");
		QDomElement baseElement = document.createElement("image_base");
		baseElement.appendChild(document.createTextNode(baseArray.toBase64()));
		result.appendChild(baseElement);
	}

	return(result);
}

/**
	@brief DiagramImageItem::contextMenuEvent
	@param event
*/
void DiagramImageItem::contextMenuEvent(QGraphicsSceneContextMenuEvent *event)
{
	if (!diagram())
	{
		QetGraphicsItem::contextMenuEvent(event);
		return;
	}

	if (diagram()->selectedItems().isEmpty())
		this->setSelected(true);

	if (isSelected() && scene()->selectedItems().size() == 1)
	{
		DiagramView *d_view = nullptr;
		for (QGraphicsView *view : diagram()->views())
		{
			if (view->isActiveWindow())
			{
				d_view = dynamic_cast<DiagramView *>(view);
				if (d_view)
					continue;
			}
		}

		if (d_view)
		{
			QScopedPointer<QMenu> menu(new QMenu());

			QAction *replace = menu.data()->addAction(tr("Remplacer l'image..."));
			connect(replace, &QAction::triggered, this, &DiagramImageItem::replaceImage);

			QAction *transparentColor = menu.data()->addAction(tr("Couleur transparente..."));
			connect(transparentColor, &QAction::triggered, this, &DiagramImageItem::setTransparentColor);

			QAction *cropAction = menu.data()->addAction(tr("Rogner..."));
			connect(cropAction, &QAction::triggered, this, &DiagramImageItem::crop);

			QAction *mirrorH = menu.data()->addAction(tr("Miroir horizontal"));
			QAction *mirrorV = menu.data()->addAction(tr("Miroir vertical"));
			connect(mirrorH, &QAction::triggered, this, [this]() { mirror(true); });
			connect(mirrorV, &QAction::triggered, this, [this]() { mirror(false); });

			menu.data()->addSeparator();
			QAction *properties = menu.data()->addAction(tr("Propriétés..."));
			connect(properties, &QAction::triggered, this, &DiagramImageItem::editProperty);

			menu.data()->addSeparator();
			menu.data()->addActions(d_view->contextMenuActions());
			menu.data()->exec(event->screenPos());
			event->accept();
			return;
		}
	}

	QetGraphicsItem::contextMenuEvent(event);
}

/**
	@brief DiagramImageItem::replaceImage
	Context-menu action: swaps the underlying pixmap for one loaded from
	a new file, keeping position, rotation and scale untouched -- only
	pixmap_ changes, everything else about how this item sits on the
	diagram is left exactly as it was. Reuses the same file dialog
	filter and error handling as DiagramEventAddImage::openDialog(), so
	picking a replacement looks and behaves like picking a new image did
	when this item was first inserted.
*/
void DiagramImageItem::replaceImage()
{
	if (!diagram() || diagram()->isReadOnly())
		return;

	QWidget *parentWidget = diagram()->views().isEmpty() ? nullptr : diagram()->views().first();
	const QString fileName = QFileDialog::getOpenFileName(
			parentWidget, tr("Selectionner une image..."),
			QETApp::pictureDir(), tr("Image Files (*.png *.jpg *.jpeg *.bmp *.svg)"));
	if (fileName.isEmpty())
		return;

	QImage image(fileName);
	if (image.isNull())
	{
		QMessageBox::critical(parentWidget, tr("Erreur"), tr("Impossible de charger l'image."));
		return;
	}

	const QPixmap oldPixmap = pixmap_;
	const QPixmap newPixmap = QPixmap::fromImage(image);

	// A wholesale replacement, not an edit of the existing image -- the
	// new picture has nothing to do with whatever colours were picked
	// or region was cropped for the old one, so this starts that
	// memory fresh rather than carrying over choices that would no
	// longer make sense.
	m_base_pixmap = newPixmap;
	m_crop_rect = newPixmap.rect();
	m_transparent_colors.clear();

	auto *undo = new QPropertyUndoCommand(this, "pixmap", oldPixmap, newPixmap);
	undo->setText(tr("Remplacer une image"));
	diagram()->undoStack().push(undo);
}

/**
	@brief DiagramImageItem::mirror
	Context-menu action: flips the pixmap itself, not a transform --
	unlike QetShapeItem::mirror(), which has rotation/skew to contend
	with, there's no equivalent linear-transform decomposition needed
	here: the bitmap is flipped once, directly, and stays flipped
	regardless of whatever rotation is applied on top afterward.
*/
void DiagramImageItem::mirror(bool horizontal)
{
	if (!diagram() || diagram()->isReadOnly())
		return;

	const QPixmap oldPixmap = pixmap_;
	const QTransform flip = horizontal ? QTransform(-1, 0, 0, 1, 0, 0) : QTransform(1, 0, 0, -1, 0, 0);
	const QPixmap newPixmap = pixmap_.transformed(flip);

	// The base is flipped the same way, to stay in sync with pixmap_ --
	// but the picked-colours list itself is left untouched: the actual
	// colour values don't change when the image is mirrored, only their
	// positions, so whatever was already keyed transparent should stay
	// remembered and still apply correctly to the flipped version.
	m_base_pixmap = m_base_pixmap.transformed(flip);

	// m_crop_rect, unlike the colour list, DOES need to change: it's
	// defined in terms of positions within the base, and those
	// positions just moved. Width/height and the other axis are
	// untouched -- only the axis being flipped needs its edge mirrored
	// (dimensions are unaffected by the flip, so it doesn't matter
	// whether this reads m_base_pixmap's size from before or after the
	// assignment above).
	if (horizontal)
		m_crop_rect = QRect(m_base_pixmap.width() - m_crop_rect.left() - m_crop_rect.width(),
				m_crop_rect.top(), m_crop_rect.width(), m_crop_rect.height());
	else
		m_crop_rect = QRect(m_crop_rect.left(), m_base_pixmap.height() - m_crop_rect.top() - m_crop_rect.height(),
				m_crop_rect.width(), m_crop_rect.height());

	auto *undo = new QPropertyUndoCommand(this, "pixmap", oldPixmap, newPixmap);
	undo->setText(horizontal ? tr("Miroir horizontal d'une image") : tr("Miroir vertical d'une image"));
	diagram()->undoStack().push(undo);
}

/**
	@brief DiagramImageItem::setTransparentColor
	Context-menu action: opens ImageTransparentColorDialog against
	m_base_pixmap (the pristine source), pre-populated with whatever
	colours and tolerance were remembered from a previous session --
	both problems fixed together, since they had the same root cause:
	passing pixmap_ (the already colour-keyed result) as if it were the
	source, with nowhere to remember which colours produced it. Applies
	the result the same way replaceImage() and mirror() do, through the
	"pixmap" property, so undo/redo stays consistent across all three;
	m_base_pixmap itself is deliberately left untouched here, since this
	action only ever changes which colours are keyed out of it, not the
	source those colours are keyed out of.
*/
/**
	@brief DiagramImageItem::setTransparentColor
	Context-menu action: opens ImageTransparentColorDialog against the
	CROPPED base (m_base_pixmap.copy(m_crop_rect)), not the full,
	uncropped original -- picking a colour from a region that's already
	been permanently cropped away would be picking a colour that isn't
	even part of the image anymore. Pre-populated with whatever colours
	and tolerance were remembered from a previous session. Applies the
	result the same way replaceImage() and mirror() do, through the
	"pixmap" property, so undo/redo stays consistent across all three;
	m_base_pixmap and m_crop_rect are deliberately left untouched here,
	since this action only ever changes which colours are keyed out,
	never the source region they're keyed out of.
*/
void DiagramImageItem::setTransparentColor()
{
	if (!diagram() || diagram()->isReadOnly())
		return;

	QWidget *parentWidget = diagram()->views().isEmpty() ? nullptr : diagram()->views().first();
	const QPixmap croppedBase = m_base_pixmap.copy(m_crop_rect);
	ImageTransparentColorDialog dialog(croppedBase, m_transparent_colors, m_transparent_tolerance, parentWidget);
	if (dialog.exec() != QDialog::Accepted)
		return;

	m_transparent_colors = dialog.pickedColors();
	m_transparent_tolerance = dialog.tolerance();

	const QPixmap oldPixmap = pixmap_;
	const QPixmap newPixmap = dialog.resultPixmap();

	auto *undo = new QPropertyUndoCommand(this, "pixmap", oldPixmap, newPixmap);
	undo->setText(tr("Définir une couleur transparente"));
	diagram()->undoStack().push(undo);
}

/**
	@brief DiagramImageItem::crop
	Context-menu action: opens ImageCropDialog against pixmap_ (the
	current, already colour-keyed display, so cropping is WYSIWYG
	against whatever is actually visible), then applies the chosen
	rectangle to both pixmap_ and m_base_pixmap together -- kept in
	sync the same way mirror() keeps them in sync, since cropping is a
	permanent, geometric change to the image's own content, unlike
	setTransparentColor()'s non-destructive colour keying.

	pos() also needs adjusting, not just pixmap_: setPixmap() (called
	via the "pixmap" undo command below) recomputes
	transformOriginPoint() from the new, smaller boundingRect(), but
	pos() itself is untouched by that -- without fixing it up here too,
	the surviving content would visually jump to wherever local (0,0)
	happens to land after shrinking, rather than staying exactly where
	it already was. Chained into one undo step together with the pixmap
	change, since undoing a crop has to restore both, or the restored
	(larger) image ends up in the wrong place.
*/
/**
	@brief DiagramImageItem::crop
	Context-menu action: opens ImageCropDialog against m_base_pixmap
	(the true, uncropped original), pre-populated with whatever crop
	rectangle was chosen in a previous session -- re-editable, not
	destructive: nothing about the original content is ever discarded,
	only which region of it is currently being shown, exactly the same
	principle setTransparentColor() already follows for its own choices.
	Recomputes pixmap_ via computeDisplayPixmap() so any already-picked
	transparent colours are correctly re-applied to the newly-cropped
	region, rather than lost (the crop dialog itself knows nothing
	about them).

	pos() also needs adjusting, not just pixmap_: setPixmap() (called
	via the "pixmap" undo command below) recomputes
	transformOriginPoint() from the new boundingRect(), but pos() itself
	is untouched by that -- without fixing it up here too, the
	surviving content would visually jump to wherever local (0,0) ends
	up after the crop rect changes, rather than staying exactly where
	it already was. This has to work whether this is the first crop
	ever applied or an adjustment of an existing one, so the position
	math is always done relative to the CURRENT crop rect (m_crop_rect,
	before it's updated below) -- when there's no previous crop, that's
	simply the whole base, which is what the very first version of this
	method assumed unconditionally.

	Chained into one undo step together with the pixmap change, since
	undoing a crop has to restore both, or the restored (larger) image
	ends up in the wrong place.
*/
void DiagramImageItem::crop()
{
	if (!diagram() || diagram()->isReadOnly())
		return;

	QWidget *parentWidget = diagram()->views().isEmpty() ? nullptr : diagram()->views().first();
	ImageCropDialog dialog(m_base_pixmap, m_crop_rect, parentWidget);
	if (dialog.exec() != QDialog::Accepted)
		return;

	const QRect newCropRect = dialog.cropRect();
	if (newCropRect.isEmpty() || newCropRect == m_crop_rect)
		return;   // nothing actually changed

	// newCropRect is in m_base_pixmap's own coordinates; converting its
	// center into the CURRENT local space (pixmap_'s own coordinates,
	// i.e. relative to the OLD m_crop_rect's own top-left) before
	// mapping to the scene through the transform that's still active
	// right now, prior to anything below changing it.
	const QPointF newCropCenterInCurrentLocal = QRectF(newCropRect).center() - QPointF(m_crop_rect.topLeft());
	const QPointF cropCenterScene = mapToScene(newCropCenterInCurrentLocal);
	const QPointF oldPos = pos();

	const QPixmap oldPixmap = pixmap_;
	const QPixmap newPixmap = computeDisplayPixmap(m_base_pixmap, newCropRect, m_transparent_colors, m_transparent_tolerance);
	m_crop_rect = newCropRect;

	// boundingRect() is exactly QRectF(pixmap_.rect()) (confirmed by
	// reading the actual implementation, not assumed) -- so the new
	// origin point setPixmap() will compute can be derived directly
	// from newPixmap here, without needing to mutate pixmap_ early or
	// duplicate setPixmap()'s own logic.
	const QPointF newOriginPoint = QRectF(newPixmap.rect()).center();
	const QPointF newPos = cropCenterScene - newOriginPoint;

	auto *undo = new QPropertyUndoCommand(this, "pixmap", oldPixmap, newPixmap);
	undo->setText(tr("Rogner une image"));
	new QPropertyUndoCommand(this, "pos", oldPos, newPos, undo);
	diagram()->undoStack().push(undo);
}
