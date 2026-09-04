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
#ifndef DIAGRAM_IMAGE_ITEM_H
#define DIAGRAM_IMAGE_ITEM_H

#include "qetgraphicsitem.h"

#include <QColor>
#include <QList>

class QDomElement;
class QDomDocument;
class QGraphicsSceneContextMenuEvent;

/**
	This class represents a selectable, movable and editable image on a
	diagram.
	@see QGraphicsItem::GraphicsItemFlags
*/
class DiagramImageItem : public QetGraphicsItem {
	Q_OBJECT
	Q_PROPERTY(QPixmap pixmap READ pixmap WRITE setPixmap NOTIFY pixmapChanged)

	// constructors, destructor
	public:
	DiagramImageItem(QetGraphicsItem * = nullptr);
	DiagramImageItem(const QPixmap &pixmap, QetGraphicsItem * = nullptr);
	~DiagramImageItem() override;
	
	// attributes
	public:
	enum { Type = UserType + 1007 };
	
	// methods
	public:
	/**
		Enable the use of qgraphicsitem_cast to safely cast a QGraphicsItem into a
		DiagramImageItem
		@return the QGraphicsItem type
	*/
	int type() const override { return Type; }
	
	virtual bool fromXml(const QDomElement &);
	virtual QDomElement toXml(QDomDocument &) const;
	void editProperty() override;
	void setPixmap(const QPixmap &pixmap);
	QPixmap pixmap() const { return pixmap_; }
	QRectF boundingRect() const override;
	QString name() const override;

	signals:
	void pixmapChanged();

	protected:
	void paint(QPainter *, const QStyleOptionGraphicsItem *, QWidget *) override;
	void contextMenuEvent(QGraphicsSceneContextMenuEvent *event) override;

	private:
	void replaceImage();
	void mirror(bool horizontal);
	void setTransparentColor();
	void crop();
	static QPixmap computeDisplayPixmap(const QPixmap &base, const QRect &cropRect, const QList<QColor> &colors, int tolerance);

	protected:
	QPixmap pixmap_;
	// The true, pristine original -- never itself cropped or colour-
	// keyed. pixmap_ (the displayed result) is always re-derived from
	// this plus m_crop_rect and m_transparent_colors/tolerance, via
	// computeDisplayPixmap(). Without keeping this separate, re-opening
	// either the crop or transparency dialog after using the other
	// would show an already-modified image as if it were the source --
	// areas already cropped away or coloured out would be gone for
	// good, with no way to recover or adjust them, only start over.
	// Updated by whatever genuinely replaces or reorients the image's
	// actual content (construction, replaceImage(), and mirror(), which
	// also mirrors m_crop_rect to keep referring to the same region of
	// the now-flipped base) -- never by crop() or setTransparentColor()
	// themselves, which only ever change which subset of this base is
	// shown.
	QPixmap m_base_pixmap;
	QRect m_crop_rect;   // relative to m_base_pixmap; equals m_base_pixmap.rect() when nothing has been cropped
	QList<QColor> m_transparent_colors;
	int m_transparent_tolerance = 10;
};
#endif
