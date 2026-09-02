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
#include "qetshapeitem.h"

#include "../PropertiesEditor/propertieseditordialog.h"
#include "../QPropertyUndoCommand/qpropertyundocommand.h"
#include "../QetGraphicsItemModeler/qetgraphicshandlerutility.h"
#include "../createdxf.h"
#include "../diagram.h"
#include "../diagramview.h"
#include "../qet.h"
#include "../qeticons.h"
#include "../qetxml.h"
#include "../ui/shapegraphicsitempropertieswidget.h"
#include "../utils/qetutils.h"
#include "../undocommand/promoteshapecommand.h"

#include <QActionGroup>
#include <QMenu>
#include <QtCore/qmath.h>
#include <utility>

/**
	@brief QetShapeItem::QetShapeItem
	Constructor of shape item. point 1 and 2 must be in scene coordinate
	@param p1 first point
	@param p2 second point
	@param type type of item (line, rectangle, ellipse, polygon, path)
	@param parent parent item
*/
QetShapeItem::QetShapeItem(QPointF p1, QPointF p2, ShapeType type, QGraphicsItem *parent) :
	QetGraphicsItem(parent),
	m_shapeType(type),
	m_P1 (p1),
	m_P2 (p2),
	m_hovered(false)
{
	if (type == Polygon) m_polygon << m_P1 << m_P2;
	setFlags(QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemSendsGeometryChanges);
	setAcceptHoverEvents(true);
	m_pen.setStyle(Qt::SolidLine);
	m_transform.pivot = localRect().center();
		//ensure handlers are always above this item
	connect(this, &QetShapeItem::zChanged, [this]()
	{
		for(QetGraphicsHandlerItem *qghi : m_handler_vector)
			qghi->setZValue(this->zValue()+1);
	});

	m_insert_point = new QAction(tr("Ajouter un point"), this);
	m_insert_point->setIcon(QET::Icons::Add);
	connect(m_insert_point, &QAction::triggered, this, &QetShapeItem::insertPoint);
	m_remove_point = new QAction(tr("Supprimer ce point"), this);
	m_remove_point->setIcon(QET::Icons::Remove);
	connect(m_remove_point, &QAction::triggered, this, &QetShapeItem::removePoint);
}

QetShapeItem::~QetShapeItem()
{
	if(!m_handler_vector.isEmpty())
		qDeleteAll(m_handler_vector);
}

/**
	@brief QetShapeItem::setPen
	Set the pen to use for draw the shape
	@param pen
*/
void QetShapeItem::setPen(const QPen &pen)
{
	if (m_pen == pen) return;
	m_pen = pen;
	update();
	emit penChanged();
}

/**
	@brief QetShapeItem::setBrush
	Set the brush to use for the fill the shape
	@param brush
*/
void QetShapeItem::setBrush(const QBrush &brush)
{
	if (m_brush == brush) return;
	m_brush = brush;
	update();
	emit brushChanged();
}

/**
	@brief QetShapeItem::setP2
	Set the second point of this item.
	If this item is a polyline,
	the last point of the polyline is replaced by P2.
	@param P2
*/
void QetShapeItem::setP2(const QPointF &P2)
{
	if (m_shapeType == Polygon && m_polygon.last() != P2)
	{
		prepareGeometryChange();
		m_polygon.replace(m_polygon.size()-1, P2);
	}
	else if (P2 != m_P2)
	{
		prepareGeometryChange();
		m_P2 = P2;
	}
	else
	{
		return; // nothing actually changed
	}
	// setP2() is what drives the live "second point under the cursor"
	// preview while a new shape is being drawn (see
	// diagrameventaddshape.cpp) -- every other geometry setter already
	// keeps the pivot following the center until it's customized; this
	// one was missing it, which is why a freshly click-and-dragged shape
	// could end up with its pivot frozen at the very first click point
	// (a degenerate, zero-size starting rect/line) instead of the
	// shape's actual center once it was drawn out.
	if (!m_pivotIsCustom)
		resetPivotToBoundingRectCenter();
}

/**
	@brief QetShapeItem::setLine
	Set item geometry to line (only available for line shape)
	@param line
	@return  : true when shape is a Line, else false
*/
bool QetShapeItem::setLine(const QLineF &line)
{
	if (Q_UNLIKELY(m_shapeType != Line)) return false;
	prepareGeometryChange();
	m_P1 = line.p1();
	m_P2 = line.p2();
	if (!m_pivotIsCustom)
		resetPivotToBoundingRectCenter();
	repositionHandles();
	return true;
}

/**
	@brief QetShapeItem::setRect
	Set this item geometry to rect (only available if shape is a rectangle or an ellipse)
	@param rect : new rect
	@return  : true when shape is rectangle or ellipse, else false
*/
bool QetShapeItem::setRect(const QRectF &rect)
{
	if (Q_LIKELY(m_shapeType == Rectangle || m_shapeType == Ellipse))
	{
		prepareGeometryChange();
		m_P1 = rect.topLeft();
		m_P2 = rect.bottomRight();
		if (!m_pivotIsCustom)
			resetPivotToBoundingRectCenter();
		repositionHandles();
		return true;
	}

	return false;
}

/**
	@brief QetShapeItem::setPolygon
	Set this item geometry to polygon (only available if shape is a polyline)
	@param polygon : new polygon
	@return true if item is polygon, else false
*/
bool QetShapeItem::setPolygon(const QPolygonF &polygon)
{
	if (Q_UNLIKELY(m_shapeType != Polygon)) {
		return false;
	}
	prepareGeometryChange();
	m_polygon = polygon;
	if (!m_pivotIsCustom)
		resetPivotToBoundingRectCenter();
	repositionHandles();
	return true;
}

/**
	@brief QetShapeItem::setClosed
	Close this item, have effect only if this item is a polygon.
	@param close
*/
void QetShapeItem::setClosed(bool close)
{
	if (m_shapeType == Polygon && close != m_closed)
	{
		prepareGeometryChange();
		m_closed = close;
		emit closeChanged();
	}
}

void QetShapeItem::setXRadius(qreal X)
{
	m_xRadius = X;
	update();
	repositionHandles();
	emit XRadiusChanged();
}

void QetShapeItem::setYRadius(qreal Y)
{
	m_yRadius = Y;
	update();
	repositionHandles();
	emit YRadiusChanged();
}

/**
	@brief QetShapeItem::setRotation
	@param degrees
	One property per ShapeTransform scalar: every handle (and the
	properties panel) touches exactly one of these, never the matrix
	directly. See shapetransform.h for why the matrix itself is only ever
	built from these, never mutated by hand.
*/
void QetShapeItem::setRotation(qreal degrees)
{
	if (qFuzzyCompare(m_transform.rotation, degrees)) return;
	prepareGeometryChange();
	m_transform.rotation = degrees;
	setTransform(m_transform.toMatrix());
	emit transformChanged();
}

void QetShapeItem::setSkewX(qreal degrees)
{
	if (qFuzzyCompare(m_transform.skewX, degrees)) return;
	prepareGeometryChange();
	m_transform.skewX = degrees;
	setTransform(m_transform.toMatrix());
	emit transformChanged();
}

void QetShapeItem::setSkewY(qreal degrees)
{
	if (qFuzzyCompare(m_transform.skewY, degrees)) return;
	prepareGeometryChange();
	m_transform.skewY = degrees;
	setTransform(m_transform.toMatrix());
	emit transformChanged();
}

void QetShapeItem::setScaleFactorX(qreal factor)
{
	if (qFuzzyCompare(m_transform.scaleX, factor)) return;
	prepareGeometryChange();
	m_transform.scaleX = factor;
	setTransform(m_transform.toMatrix());
	emit transformChanged();
}

void QetShapeItem::setScaleFactorY(qreal factor)
{
	if (qFuzzyCompare(m_transform.scaleY, factor)) return;
	prepareGeometryChange();
	m_transform.scaleY = factor;
	setTransform(m_transform.toMatrix());
	emit transformChanged();
}

/**
	@brief QetShapeItem::setPivot
	Move the pivot point. pos() is adjusted at the same time so the shape
	never visibly jumps -- see compensatedPositionForNewPivot() in
	shapetransform.h for why that adjustment is needed at all.
	@param newPivot in local coordinates
*/
void QetShapeItem::setPivot(const QPointF &newPivot)
{
	if (m_transform.pivot == newPivot) return;
	const QPointF newPos = compensatedPositionForNewPivot(
			pos(), m_transform.pivot, newPivot, m_transform.linearPart());
	prepareGeometryChange();
	m_transform.pivot = newPivot;
	// Apply both related updates, then reposition handles exactly once
	// using the fully-updated state. Without this guard, setTransform()
	// alone would trigger a reposition using the new transform but the
	// still-old pos() (or vice versa if the calls were swapped), which is
	// a real, if usually imperceptible, inconsistent intermediate state --
	// removing it outright is cheaper than reasoning about whether it's
	// ever visible.
	m_deferHandleReposition = true;
	setTransform(m_transform.toMatrix());
	// QetGraphicsItem::setPos() (our own base class) silently re-snaps its
	// argument to the grid before applying it -- appropriate for an
	// ordinary user drag of the whole shape, but wrong here: newPos is an
	// exact compensation value derived from rotation trigonometry, so it's
	// essentially never grid-aligned, and snapping it introduces real
	// rounding error on every single frame of a pivot drag. Over a
	// multi-frame drag that accumulates into a clearly visible drift of
	// the whole rectangle -- confirmed by simulation: reproducing the
	// grid-snap here produced tens of units of drift over a normal drag;
	// calling Qt's own, non-snapping setPos() directly produced exactly
	// zero. Grid-snapping the *pivot itself* (done earlier, in
	// handlerMouseMoveEvent, on the mouse position) is still exactly
	// right -- it's only this internal, precision-critical compensation
	// step that must bypass it.
	QGraphicsItem::setPos(newPos);
	m_deferHandleReposition = false;
	repositionHandles();
	emit transformChanged();
}

void QetShapeItem::resetPivotToBoundingRectCenter()
{
	m_pivotIsCustom = false;
	setPivot(localRect().center());
}

/**
	@brief QetShapeItem::setActiveNode
	Makes this Path node the active one and switches into NodeEdit mode,
	so its control handles (and the tangent guide lines drawn in paint())
	become visible -- the same visual feedback normal editing already
	gets via the context menu's node-kind actions, made available to the
	pen tool too so a node's handles are visible *as they're being
	dragged into existence*, not only afterward.
*/
void QetShapeItem::setActiveNode(int index)
{
	if (m_shapeType != Path || index < 0 || index >= m_nodes.size())
		return;
	m_activeNode = index;
	m_handleMode = HandleMode::NodeEdit;
	rebuildHandles();
}

/**
	@brief QetShapeItem::setStartAngle / setEndAngle
	Only meaningful for Ellipse. Dragging one endpoint onto the other
	(span within 5 degrees of a full turn) snaps back to a full ellipse --
	this is the whole answer to "how do I turn an arc back into a closed
	ellipse": there is no separate Arc type to convert out of.
*/
// Dragging one endpoint until it nearly touches the other closes the
// ellipse back up -- checked on the *geometric* (mod 360) proximity of
// the two angles, not the raw stored span, since dragArcEndpoint()
// deliberately keeps the raw span continuous/unwrapped (it can legally
// exceed +-360 during a drag) rather than snapping it into a fixed
// range on every frame.
static bool anglesGeometricallyAdjacent(qreal span)
{
	qreal wrapped = std::fmod(span, 360.0);
	if (wrapped < 0) wrapped += 360.0;
	return wrapped < 5.0 || wrapped > 355.0;
}

void QetShapeItem::setStartAngle(qreal degrees)
{
	if (qFuzzyCompare(m_startAngle, degrees)) return;
	prepareGeometryChange();
	m_startAngle = degrees;
	if (anglesGeometricallyAdjacent(spanAngle())) { m_startAngle = 0; m_endAngle = 360; }
	repositionHandles();
	emit arcChanged();
}

void QetShapeItem::setEndAngle(qreal degrees)
{
	if (qFuzzyCompare(m_endAngle, degrees)) return;
	prepareGeometryChange();
	m_endAngle = degrees;
	if (anglesGeometricallyAdjacent(spanAngle())) { m_startAngle = 0; m_endAngle = 360; }
	repositionHandles();
	emit arcChanged();
}

void QetShapeItem::setArcClosure(ArcClosure closure)
{
	if (m_arcClosure == closure) return;
	prepareGeometryChange();
	m_arcClosure = closure;
	emit arcChanged();
}

/**
	@brief QetShapeItem::setPathNodes
	Replace the node list of a Path shape. Interactive editing of anchors
	and control handles both go through the handle roles built by
	rebuildHandles() (PathAnchor always visible; PathControlIn/Out for
	the active node once double-click enters node-edit mode) -- this
	setter itself is just the plain data replacement underneath that.
*/
void QetShapeItem::setPathNodes(const QVector<PathNode> &nodes)
{
	prepareGeometryChange();
	m_nodes = nodes;
	if (!m_pivotIsCustom)
		resetPivotToBoundingRectCenter();
	// Always a full rebuild, not just a reposition: unlike the drag
	// handlers (dragPathAnchor, dragPathControlHandle), which mutate
	// m_nodes in place and are called from a handle's own mouse-move
	// event -- where destroying that handle mid-gesture would be a real
	// problem -- this setter is only ever called from outside that
	// pipeline (currently: the pen tool, adding a new node with every
	// click). The node *count* routinely changes here, and
	// repositionHandles() has no way to notice that on its own; it only
	// moves whatever handles already exist.
	rebuildHandles();
}

/**
	@brief QetShapeItem::pointCount
	@return the number of point in the polygon
*/
int QetShapeItem::pointsCount() const
{
	return m_polygon.size();
}

/**
	@brief QetShapeItem::setNextPoint
	Add a new point to the current polygon
	@param P the new point.
*/
void QetShapeItem::setNextPoint(QPointF P)
{
	prepareGeometryChange();
	m_polygon.append(Diagram::snapToGrid(P));
}

/**
	@brief QetShapeItem::removePoints
	Number of point to remove on the polygon
	If number is superior to number of polygon points-2,
	all points of polygon will be removed except
	the first two (minimum point for the polygon);
*/
void QetShapeItem::removePoints(int number)
{
	if (pointsCount() == 2 || number < 1) return;
	if ((pointsCount()-2) < number)
		number = pointsCount() - 2;

	int i = 0;
	do
	{
		i++;
		prepareGeometryChange();
		m_polygon.pop_back();

	} while (i < number);
}

/**
	@brief QetShapeItem::boundingRect
	@return the bounding rect of this item
*/
QRectF QetShapeItem::boundingRect() const
{
	QRectF rect = shape().boundingRect().adjusted(-6, -6, 6, 6);

	if (m_shapeType == Path && m_handleMode == HandleMode::NodeEdit && m_activeNode < m_nodes.size())
	{
		const PathNode &active = m_nodes.at(m_activeNode);
		if (active.inHandle)
			rect |= QRectF(active.anchor, active.anchor + *active.inHandle).normalized().adjusted(-6, -6, 6, 6);
		if (active.outHandle)
			rect |= QRectF(active.anchor, active.anchor + *active.outHandle).normalized().adjusted(-6, -6, 6, 6);
	}

	return rect;
}

/**
	@brief QetShapeItem::localRect
	The rect used for corner/edge handle placement and for arc/radius
	math. For Polygon and Path this is the vertex bounding box; for
	everything else it is simply the P1/P2 rect, as before.
*/
QRectF QetShapeItem::localRect() const
{
	if (m_shapeType == Polygon)
		return m_polygon.boundingRect();
	if (m_shapeType == Path)
	{
		QPolygonF anchors;
		for (const PathNode &n : m_nodes) anchors << n.anchor;
		return anchors.boundingRect();
	}
	return QRectF(m_P1, m_P2).normalized();
}

/**
	@brief QetShapeItem::outline
	The raw, unstroked path for the current type, in local coordinates.
	Shared by shape() (which strokes it for hit-testing/selection) and
	paint() (which draws it directly) so the two can never disagree about
	what the shape actually looks like.
*/
QPainterPath QetShapeItem::outline() const
{
	QPainterPath path;

	switch (m_shapeType)
	{
		case Line:
			path.moveTo(m_P1);
			path.lineTo(m_P2);
			break;

		case Rectangle:
			path.addRoundedRect(QRectF(m_P1, m_P2), m_xRadius, m_yRadius);
			break;

		case Ellipse:
		{
			const QRectF r(QRectF(m_P1, m_P2));
			if (isFullEllipse())
			{
				path.addEllipse(r);
			}
			else if (m_arcClosure == Pie)
			{
				path.moveTo(r.center());
				path.arcTo(r, m_startAngle, spanAngle());
				path.closeSubpath();
			}
			else
			{
				path.arcMoveTo(r, m_startAngle);
				path.arcTo(r, m_startAngle, spanAngle());
				if (m_arcClosure == Chord)
					path.closeSubpath();
			}
			break;
		}

		case Polygon:
			path.addPolygon(m_polygon);
			if (m_closed) {
				path.closeSubpath();
			}
			break;

		case Path:
			if (!m_nodes.isEmpty())
			{
				path.moveTo(m_nodes.first().anchor);
				for (int i = 1; i < m_nodes.size(); ++i)
				{
					const PathNode &prev = m_nodes.at(i - 1);
					const PathNode &cur  = m_nodes.at(i);
					if (prev.outHandle || cur.inHandle)
						path.cubicTo(
								prev.anchor + prev.outHandle.value_or(QPointF()),
								cur.anchor  + cur.inHandle.value_or(QPointF()),
								cur.anchor);
					else
						path.lineTo(cur.anchor);
				}
				if (m_closed && m_nodes.size() > 1)
				{
					const PathNode &last  = m_nodes.last();
					const PathNode &first = m_nodes.first();
					if (last.outHandle || first.inHandle)
						path.cubicTo(
								last.anchor + last.outHandle.value_or(QPointF()),
								first.anchor + first.inHandle.value_or(QPointF()),
								first.anchor);
					path.closeSubpath();
				}
			}
			break;
	}

	return path;
}

/**
	@brief QetShapeItem::shape
	@return the shape of this item
*/
QPainterPath QetShapeItem::shape() const
{
	QPainterPathStroker pps;
	pps.setWidth(m_hovered? m_pen.widthF()+10 : m_pen.widthF());
	pps.setJoinStyle(Qt::RoundJoin);
	return pps.createStroke(outline());
}

/**
	@brief QetShapeItem::paint
	Paint this item
	@param painter
	@param option
	@param widget
*/
void QetShapeItem::paint(
		QPainter *painter,
		const QStyleOptionGraphicsItem *option,
		QWidget *widget)
{
	Q_UNUSED(option)
	Q_UNUSED(widget)

	painter->save();
	painter->setRenderHint(QPainter::Antialiasing, true);
	painter->setPen(m_pen);
	painter->setBrush(m_brush);

		//Draw hovered shadow
	if (m_hovered)
	{
		painter->save();
		QColor color(Qt::darkBlue);
		color.setAlpha(50);
		painter -> setBrush (QBrush (color));
		painter -> setPen   (Qt::NoPen);
		painter -> drawPath (shape());
		painter -> restore  ();
	}

	painter->drawPath(outline());

	// Tangent guide lines: connects each visible control handle back to
	// its anchor, for the active node only (matching why only the active
	// node's handles are shown at all -- drawing every node's tangents on
	// a complex path would be as cluttered as every node's handles).
	// Cosmetic on purpose, unlike the shape's own stroke: this is a UI
	// aid, not artwork, so it should stay a constant screen width
	// regardless of zoom or the shape's own skew -- the same reasoning
	// that already makes the handle dots themselves ItemIgnoresTransformations.
	if (m_shapeType == Path && m_handleMode == HandleMode::NodeEdit && m_activeNode < m_nodes.size())
	{
		const PathNode &active = m_nodes.at(m_activeNode);
		if (active.inHandle || active.outHandle)
		{
			painter->save();
			QPen guidePen(QColor(120, 120, 120));
			guidePen.setStyle(Qt::DashLine);
			guidePen.setWidthF(1.0);
			guidePen.setCosmetic(true);
			painter->setPen(guidePen);
			if (active.inHandle)
				painter->drawLine(active.anchor, active.anchor + *active.inHandle);
			if (active.outHandle)
				painter->drawLine(active.anchor, active.anchor + *active.outHandle);
			painter->restore();
		}
	}

	painter->restore();
}

/**
	@brief QetShapeItem::hoverEnterEvent
	Handle hover enter event
	@param event
*/
void QetShapeItem::hoverEnterEvent(QGraphicsSceneHoverEvent *event)
{
	m_hovered = true;
	QetGraphicsItem::hoverEnterEvent(event);
}

/**
	@brief QetShapeItem::hoverLeaveEvent
	Handle hover leave event
	@param event
*/
void QetShapeItem::hoverLeaveEvent(QGraphicsSceneHoverEvent *event)
{
	m_hovered = false;
	QetGraphicsItem::hoverLeaveEvent(event);
}

/**
	@brief QetShapeItem::mousePressEvent
	A left click on an *already selected* shape (without having dragged)
	toggles between the Size and RotateSkew handle sets -- the same
	convention LibreOffice Draw and PowerPoint use. A click that performs
	the selection itself does not toggle, so selecting a shape always
	starts in Size mode.
*/
void QetShapeItem::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
	const bool wasAlreadySelected = isSelected();
	event->ignore();
	QetGraphicsItem::mousePressEvent(event);

	if (event->button() == Qt::LeftButton)
	{
		if (wasAlreadySelected && isSelected())
			toggleHandleMode();
		event->accept();
	}
}

/**
	@brief QetShapeItem::itemChange
	@param change
	@param value
	@return
*/
QVariant QetShapeItem::itemChange(QGraphicsItem::GraphicsItemChange change,
				  const QVariant &value)
{
	if (change == ItemSelectedHasChanged)
	{
		if (value.toBool() == true) {
			//If this is selected, we add handlers.
			rebuildHandles();
		}
		else //Else this is deselected, we remove handlers
		{
			if(!m_handler_vector.isEmpty())
			{
				qDeleteAll(m_handler_vector);
				m_handler_vector.clear();
				m_handleRoles.clear();
				m_handleSlot.clear();
			}
			// Same reasoning as toggleHandleMode(): resetting the mode
			// changes boundingRect()'s coverage, so Qt needs to be told
			// before it happens, not after -- this is exactly the path
			// that left a stale guide line behind when deselecting a
			// Path with a far-dragged handle still active.
			prepareGeometryChange();
			m_handleMode = HandleMode::Size;
		}
	}
	else if (change == ItemPositionHasChanged || change == ItemTransformHasChanged) {
		if (!m_deferHandleReposition)
			repositionHandles();
	}
	else if (change == ItemSceneHasChanged)
	{
		if (!scene()) //This is removed from scene, then we deselect this, and so, the handlers is also removed.
		{
			setSelected(false);
		}
	}

	return QGraphicsItem::itemChange(change, value);
}

/**
	@brief QetShapeItem::sceneEventFilter
	@param watched
	@param event
	@return
*/
bool QetShapeItem::sceneEventFilter(QGraphicsItem *watched, QEvent *event)
{
		//Watched must be an handler
	if(watched->type() == QetGraphicsHandlerItem::Type)
	{
		QetGraphicsHandlerItem *qghi = qgraphicsitem_cast<QetGraphicsHandlerItem *>(watched);

		if(m_handler_vector.contains(qghi)) //Handler must be in m_vector_index, then we can start resize
		{
			m_vector_index = m_handler_vector.indexOf(qghi);
			if (m_vector_index != -1)
			{
				if(event->type() == QEvent::GraphicsSceneMousePress) //Click
				{
					handlerMousePressEvent(m_vector_index);
					return true;
				}
				else if(event->type() == QEvent::GraphicsSceneMouseMove) //Move
				{
					handlerMouseMoveEvent(m_vector_index, static_cast<QGraphicsSceneMouseEvent *>(event));
					return true;
				}
				else if (event->type() == QEvent::GraphicsSceneMouseRelease) //Release
				{
					handlerMouseReleaseEvent(m_vector_index);
					return true;
				}
			}
		}
	}

	return false;
}

/**
	@brief QetShapeItem::contextMenuEvent
	@param event
*/
void QetShapeItem::contextMenuEvent(QGraphicsSceneContextMenuEvent *event)
{
	m_context_menu_pos = event->pos();

	const bool canConvertToPath = (m_shapeType == Rectangle || m_shapeType == Ellipse);

	if (m_shapeType == QetShapeItem::Polygon || m_shapeType == QetShapeItem::Path || canConvertToPath)
	{
		if (diagram()->selectedItems().isEmpty()) {
			this->setSelected(true);
		}

		if (isSelected() && scene()->selectedItems().size() == 1)
		{
			if (diagram())
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

					if (m_shapeType == QetShapeItem::Polygon)
					{
						menu.data()->addAction(m_insert_point);

						if (m_handler_vector.count() > 2)
						{
							for (QetGraphicsHandlerItem *qghi : m_handler_vector)
							{
								if (qghi->contains(qghi->mapFromScene(event->scenePos())))
								{
									menu.data()->addAction(m_remove_point);
									break;
								}
							}
						}
					}

					if (m_shapeType == QetShapeItem::Path && !m_nodes.isEmpty())
					{
						// Nearest node to the click, not "whichever tiny
						// handle you managed to hit exactly" -- the latter
						// is finicky for a right-click (no drag feedback
						// to correct your aim), and silently found nothing
						// once NodeEdit mode added PathControlIn/Out
						// handles right next to the anchor, which this
						// menu never checked for.
						const QPointF localClick = mapFromScene(event->scenePos());
						int nearest = 0;
						qreal bestDist = -1;
						for (int i = 0; i < m_nodes.size(); ++i)
						{
							const QPointF d = m_nodes.at(i).anchor - localClick;
							const qreal dist = d.x() * d.x() + d.y() * d.y();
							if (bestDist < 0 || dist < bestDist) { bestDist = dist; nearest = i; }
						}

						const std::pair<int, qreal> segmentHit = nearestPathSegment(localClick);
						if (segmentHit.first >= 0)
						{
							const int seg = segmentHit.first;
							const qreal t = segmentHit.second;
							QAction *insertAct = menu.data()->addAction(tr("Ajouter un point"));
							connect(insertAct, &QAction::triggered, this, [this, seg, t]() { insertPathPoint(seg, t); });
						}

						QMenu *nodeMenu = menu.data()->addMenu(tr("Nœud le plus proche"));
						QAction *toSmooth    = nodeMenu->addAction(tr("Lisse"));
						QAction *toSymmetric = nodeMenu->addAction(tr("Symétrique"));
						QAction *toCorner    = nodeMenu->addAction(tr("Anguleux"));

						auto *group = new QActionGroup(nodeMenu);
						for (QAction *a : {toSmooth, toSymmetric, toCorner})
						{
							a->setCheckable(true);
							a->setActionGroup(group);
						}
						const NodeKind currentKind = m_nodes.at(nearest).kind;
						toSmooth->setChecked(currentKind == NodeKind::Smooth);
						toSymmetric->setChecked(currentKind == NodeKind::Symmetric);
						toCorner->setChecked(currentKind == NodeKind::Corner);

						connect(toSmooth,    &QAction::triggered, this, [this, nearest]() { setNodeKind(nearest, NodeKind::Smooth); });
						connect(toSymmetric, &QAction::triggered, this, [this, nearest]() { setNodeKind(nearest, NodeKind::Symmetric); });
						connect(toCorner,    &QAction::triggered, this, [this, nearest]() { setNodeKind(nearest, NodeKind::Corner); });

						if (m_nodes.size() > 2)
						{
							QAction *removeAct = menu.data()->addAction(tr("Supprimer le nœud le plus proche"));
							connect(removeAct, &QAction::triggered, this, [this, nearest]() { removePathPoint(nearest); });
						}
					}

					if (canConvertToPath)
					{
						QAction *convert = menu.data()->addAction(tr("Convertir en polyligne"));
						connect(convert, &QAction::triggered, this, &QetShapeItem::convertToPathExplicitly);
					}

					menu.data()->addSeparator();
					menu.data()->addActions(d_view->contextMenuActions());
					menu.data()->exec(event->screenPos());
					event->accept();
					return;
				}
			}
		}
	}

	QetGraphicsItem::contextMenuEvent(event);
}

/**
	@brief QetShapeItem::toggleHandleMode
	Cycled by clicking an already-selected shape: Size -> Corner ->
	RotateSkew -> Size for Rectangle (the only type with a corner-radius
	concept); Size -> RotateSkew -> Size for everything else, skipping the
	Corner state entirely rather than showing an empty/meaningless one.
*/
void QetShapeItem::toggleHandleMode()
{
	// Changing m_handleMode changes what boundingRect() covers (it only
	// includes the active node's guide-line extent while in NodeEdit
	// mode) -- without this, leaving NodeEdit with a far-flung handle
	// left exactly the same kind of ghost fixed for m_activeNode changes
	// a moment ago, just triggered by a different state change.
	prepareGeometryChange();

	if (m_shapeType == Rectangle)
	{
		if (m_handleMode == HandleMode::Size) m_handleMode = HandleMode::Corner;
		else if (m_handleMode == HandleMode::Corner) m_handleMode = HandleMode::RotateSkew;
		else m_handleMode = HandleMode::Size;
	}
	else if (m_shapeType == Path)
	{
		if (m_handleMode == HandleMode::Size) { m_handleMode = HandleMode::NodeEdit; m_activeNode = 0; }
		else if (m_handleMode == HandleMode::NodeEdit) m_handleMode = HandleMode::RotateSkew;
		else m_handleMode = HandleMode::Size;
	}
	else
	{
		m_handleMode = (m_handleMode == HandleMode::Size) ? HandleMode::RotateSkew : HandleMode::Size;
	}

	rebuildHandles();
}

/**
	@brief QetShapeItem::colorForHandleRole
	Purely cosmetic, but consistent with the existing convention of
	color-coding what a handle currently does. Mirror-resize used to be
	its own colored click-cycled mode (green); it is now a Ctrl modifier
	available on every Resize handle, so only genuinely distinct
	behaviours get their own color here.
*/
QColor QetShapeItem::colorForHandleRole(HandleRole role)
{
	switch (role)
	{
		case HandleRole::Resize:        return Qt::blue;
		case HandleRole::Rotate:        return Qt::darkGreen;
		case HandleRole::SkewEdge:      return Qt::darkYellow;
		case HandleRole::Pivot:         return Qt::red;
		case HandleRole::CornerRadius:  return Qt::magenta;
		case HandleRole::ArcEndpoint:   return Qt::darkCyan;
		case HandleRole::PathAnchor:    return Qt::blue;
		case HandleRole::PathControlIn:
		case HandleRole::PathControlOut:return Qt::gray;
	}
	return Qt::blue;
}

QPointF QetShapeItem::cornerPoint(const QRectF &rect, int cornerIndex)
{
	switch (cornerIndex & 3)
	{
		case 0: return rect.topLeft();
		case 1: return rect.topRight();
		case 2: return rect.bottomRight();
		default: return rect.bottomLeft();
	}
}

QPointF QetShapeItem::rotateHandleReference(int slot) const
{
	if (m_shapeType == Line)
		return (slot == 0) ? m_P1 : m_P2;
	if (m_shapeType == Polygon)
		return m_polygon.value(slot);
	if (m_shapeType == Path)
		return (slot < m_nodes.size()) ? m_nodes.at(slot).anchor : QPointF();
	return cornerPoint(localRect(), slot);
}

QPointF QetShapeItem::edgeMidpoint(const QRectF &rect, int edgeIndex)
{
	switch (edgeIndex & 3)
	{
		case 0: return QPointF(rect.center().x(), rect.top());
		case 1: return QPointF(rect.right(), rect.center().y());
		case 2: return QPointF(rect.center().x(), rect.bottom());
		default: return QPointF(rect.left(), rect.center().y());
	}
}

/**
	@brief QetShapeItem::handlePositionFor
	Local-coordinate position for one (role, slot) pair, given the
	shape's *current* geometry. This is the single source of truth used
	both to build handles from scratch (rebuildHandles()) and to move
	existing ones during a live drag (repositionHandles()) -- the two
	can never disagree about where a handle belongs.
*/
QPointF QetShapeItem::handlePositionFor(HandleRole role, int slot) const
{
	const QRectF r = localRect();

	switch (role)
	{
		case HandleRole::Resize:
			if (m_shapeType == Line)
				return (slot == 0) ? m_P1 : m_P2;
			return QetGraphicsHandlerUtility::pointsForRect(r).value(slot);

		case HandleRole::Rotate:
			return rotateHandleReference(slot);

		case HandleRole::SkewEdge:
			return edgeMidpoint(r, slot);

		case HandleRole::Pivot:
			return m_transform.pivot;

		case HandleRole::CornerRadius:
			return QetGraphicsHandlerUtility::pointForRadiusRect(r, m_xRadius, m_yRadius).value(slot);

		case HandleRole::ArcEndpoint:
			return QetGraphicsHandlerUtility::pointsForArc(r, m_startAngle, spanAngle()).value(slot);

		case HandleRole::PathAnchor:
			if (m_shapeType == Polygon)
				return m_polygon.value(slot);
			return (slot < m_nodes.size()) ? m_nodes.at(slot).anchor : QPointF();

		case HandleRole::PathControlIn:
			return (slot < m_nodes.size() && m_nodes.at(slot).inHandle)
					? m_nodes.at(slot).anchor + *m_nodes.at(slot).inHandle : QPointF();
		case HandleRole::PathControlOut:
			return (slot < m_nodes.size() && m_nodes.at(slot).outHandle)
					? m_nodes.at(slot).anchor + *m_nodes.at(slot).outHandle : QPointF();
	}
	return QPointF();
}

QVector<QPointF> QetShapeItem::currentHandlePositions() const
{
	QVector<QPointF> positions;
	positions.reserve(m_handleRoles.size());
	for (int i = 0; i < m_handleRoles.size(); ++i)
		positions << handlePositionFor(m_handleRoles.at(i), m_handleSlot.at(i));
	return positions;
}

/**
	@brief QetShapeItem::scaleOnlyOffset / scaleAndShearOffset
	A local point's offset from the pivot, run through *only* the parts of
	the linear transform that are not currently being edited by a drag --
	i.e. the parts that stay fixed while the user is dragging one specific
	handle. These are the fixed reference values dragRotateHandle() and
	dragSkewHandle() solve against, instead of round-tripping through
	mapFromScene() (which would divide out the very parameter being
	solved for and turn the drag into a feedback loop -- see
	dragRotateHandle()'s comment for the concrete failure mode this
	replaces).
*/
QPointF QetShapeItem::scaleOnlyOffset(const QPointF &localPoint) const
{
	const QPointF offset = localPoint - m_transform.pivot;
	return QPointF(offset.x() * m_transform.scaleX, offset.y() * m_transform.scaleY);
}

QPointF QetShapeItem::scaleAndShearOffset(const QPointF &localPoint) const
{
	const QPointF scaled = scaleOnlyOffset(localPoint);
	const qreal kx = qTan(qDegreesToRadians(m_transform.skewX));
	const qreal ky = qTan(qDegreesToRadians(m_transform.skewY));
	return QPointF(scaled.x() + kx * scaled.y(), scaled.y() + ky * scaled.x());
}

/**
	@brief QetShapeItem::rebuildHandles
	(Re)creates the handler items from scratch, for the current
	shapeType()/handleMode(). Only called when the *set* of handles
	changes -- selection, mode toggle, node count change -- never during
	a live drag, since that would delete the very QetGraphicsHandlerItem
	currently receiving the mouse-move events. See repositionHandles()
	for the drag-safe alternative.
*/
void QetShapeItem::rebuildHandles()
{
	if (!m_handler_vector.isEmpty())
	{
		qDeleteAll(m_handler_vector);
		m_handler_vector.clear();
	}
	m_handleRoles.clear();
	m_handleSlot.clear();

	auto addRole = [this](HandleRole role, int slot) {
		m_handleRoles << role;
		m_handleSlot << slot;
	};

	switch (m_shapeType)
	{
		case Line:
			if (m_handleMode == HandleMode::Size)
			{
				addRole(HandleRole::Resize, 0);
				addRole(HandleRole::Resize, 1);
			}
			else // RotateSkew: rotate around the pivot; skewing a
			     // zero-height line isn't a meaningful operation
			{
				addRole(HandleRole::Rotate, 0);
				addRole(HandleRole::Rotate, 1);
				addRole(HandleRole::Pivot, 0);
			}
			break;

		case Rectangle:
		case Ellipse:
			if (m_handleMode == HandleMode::Size)
			{
				for (int i = 0; i < 8; ++i) addRole(HandleRole::Resize, i);
			}
			else if (m_handleMode == HandleMode::Corner)
			{
				// Rectangle only -- toggleHandleMode() never selects this
				// state for Ellipse, so this branch is a no-op for it.
				for (int i = 0; i < 2; ++i) addRole(HandleRole::CornerRadius, i);
			}
			else // RotateSkew
			{
				for (int i = 0; i < 4; ++i) addRole(HandleRole::Rotate, i);
				for (int i = 0; i < 4; ++i) addRole(HandleRole::SkewEdge, i);
				addRole(HandleRole::Pivot, 0);
			}
			if (m_shapeType == Ellipse)
				for (int i = 0; i < 2; ++i) addRole(HandleRole::ArcEndpoint, i);
			break;

		case Polygon:
			if (m_handleMode == HandleMode::Size)
			{
				for (int i = 0; i < m_polygon.size(); ++i)
					addRole(HandleRole::PathAnchor, i);
			}
			else // RotateSkew: any vertex can be dragged to rotate the
			     // whole polygon around the pivot; skewing doesn't have
			     // an obvious "which edge" convention for an arbitrary
			     // vertex count the way it does for a rectangle's 4
			     // fixed edges, so it's left out here too.
			{
				for (int i = 0; i < m_polygon.size(); ++i)
					addRole(HandleRole::Rotate, i);
				addRole(HandleRole::Pivot, 0);
			}
			break;

		case Path:
			if (m_handleMode == HandleMode::Size)
			{
				for (int i = 0; i < m_nodes.size(); ++i)
					addRole(HandleRole::PathAnchor, i);
			}
			else if (m_handleMode == HandleMode::NodeEdit)
			{
				for (int i = 0; i < m_nodes.size(); ++i)
					addRole(HandleRole::PathAnchor, i);
				if (m_activeNode < m_nodes.size())
				{
					const PathNode &active = m_nodes.at(m_activeNode);
					if (active.inHandle)  addRole(HandleRole::PathControlIn, m_activeNode);
					if (active.outHandle) addRole(HandleRole::PathControlOut, m_activeNode);
				}
			}
			else // RotateSkew
			{
				for (int i = 0; i < m_nodes.size(); ++i)
					addRole(HandleRole::Rotate, i);
				addRole(HandleRole::Pivot, 0);
			}
			break;
	}

	if (m_handleRoles.isEmpty() || !scene())
		return;

	const QVector<QPointF> positions = currentHandlePositions();
	m_handler_vector = QetGraphicsHandlerItem::handlerForPoint(mapToScene(positions), QETUtils::graphicsHandlerSize(this));

	for (int i = 0; i < m_handler_vector.size(); ++i)
	{
		QetGraphicsHandlerItem *h = m_handler_vector.at(i);
		h->setZValue(zValue() + 1);
		h->setColor(colorForHandleRole(m_handleRoles.at(i)));
		scene()->addItem(h);
		h->installSceneEventFilter(this);
	}
}

/**
	@brief QetShapeItem::repositionHandles
	Moves the *existing* handler items to match current geometry, without
	touching their identity or which one is mid-drag. Safe (and expected)
	to be called on every frame of a live drag. Falls back to a full
	rebuild only if the handle count has somehow drifted out of sync --
	this should not normally happen, since only rebuildHandles() ever
	changes m_handleRoles/m_handleSlot.
*/
void QetShapeItem::repositionHandles()
{
	if (m_handler_vector.isEmpty())
		return;

	const QVector<QPointF> positions = currentHandlePositions();
	if (positions.size() != m_handler_vector.size())
	{
		rebuildHandles();
		return;
	}

	const QVector<QPointF> scenePositions = mapToScene(positions);
	for (int i = 0; i < scenePositions.size(); ++i)
		m_handler_vector.at(i)->setPos(scenePositions.at(i));
}

void QetShapeItem::insertPoint()
{
	if (m_shapeType == QetShapeItem::Polygon)
	{
		QPolygonF new_polygon = QetGraphicsHandlerUtility::polygonForInsertPoint(this->polygon(), m_closed, Diagram::snapToGrid(m_context_menu_pos));

		if(new_polygon != m_polygon)
		{
				//Wrap the undo for avoid to merge the undo commands when user add several points.
			QUndoCommand *undo = new QUndoCommand(tr("Ajouter un point à un polygone"));
			new QPropertyUndoCommand(this, "polygon", m_polygon, new_polygon, undo);
			diagram()->undoStack().push(undo);
		}
	}
}

void QetShapeItem::removePoint()
{
	if (m_shapeType != QetShapeItem::Polygon) {
		return;
	}

	if (m_handler_vector.size() == 2) {
		return;
	}

	QPointF point = mapToScene(m_context_menu_pos);
	int index = -1;
	for (int i=0 ; i<m_handler_vector.size() ; i++)
	{
		QetGraphicsHandlerItem *qghi = m_handler_vector.at(i);
		if (qghi->contains(qghi->mapFromScene(point)))
		{
			index = i;
			break;
		}
	}
	if (index > -1 && index<m_handler_vector.count())
	{
		QPolygonF polygon = this->polygon();
		polygon.removeAt(index);

			//Wrap the undo for avoid to merge the undo commands when user add several points.
		QUndoCommand *undo = new QUndoCommand(tr("Supprimer un point d'un polygone"));
		new QPropertyUndoCommand(this, "polygon", this->polygon(), polygon, undo);
		diagram()->undoStack().push(undo);
	}
}

/**
	@brief QetShapeItem::snapshotXml
	toXml() into a private, throwaway document -- used only to hand a
	self-contained QDomElement to PromoteShapeCommand, which deep-clones
	it again into its own document anyway (see promoteshapecommand.cpp).
*/
QDomElement QetShapeItem::snapshotXml() const
{
	QDomDocument doc;
	QDomElement e = toXml(doc);
	doc.appendChild(e);
	return e;
}

/**
	@brief QetShapeItem::promoteRectangleOrEllipseToPolygon
	Alt+drag on a Resize corner detaches that one vertex, at the moment
	the shape stops being a Rectangle/Ellipse and becomes a Polygon. The
	prior state (type, geometry, transform, style) is captured by
	PromoteShapeCommand so a single Undo restores it exactly -- this fact
	never touches the saved .qet file, only the session's undo stack.
	@param detachedResizeIndex the Resize-role slot (0,2,5,7 = corners in
	       QetGraphicsHandlerUtility::pointsForRect's own ordering) being dragged
	@param newLocalPos where that corner is being dragged to
*/
void QetShapeItem::promoteRectangleOrEllipseToPolygon(int detachedResizeIndex, const QPointF &newLocalPos)
{
	const QDomElement before = snapshotXml();

	QPolygonF corners;
	const QRectF r = localRect();
	corners << r.topLeft() << r.topRight() << r.bottomRight() << r.bottomLeft();

	// pointsForRect corner slots (0,2,5,7) map onto our 0..3 corner order.
	static const QHash<int,int> resizeSlotToCorner = {{0,0}, {2,1}, {5,3}, {7,2}};
	const int cornerIndex = resizeSlotToCorner.value(detachedResizeIndex, 0);
	corners[cornerIndex] = newLocalPos;

	prepareGeometryChange();
	m_shapeType = Polygon;
	m_polygon = corners;
	m_closed = true;

	const QDomElement after = snapshotXml();

	if (diagram())
	{
		auto *undo = new PromoteShapeCommand(this, before, after);
		diagram()->undoStack().push(undo);
	}

	rebuildHandles();
}

/**
	@brief QetShapeItem::nearestPathSegment
	Nearest point on the whole curve to localPos, found by coarse sampling
	each segment's cubic Bezier (24 samples is plenty for a context-menu
	pick -- this only has to be close enough to feel right, not exact).
	Returns {-1, 0} if there are fewer than two nodes to form a segment.
*/
std::pair<int, qreal> QetShapeItem::nearestPathSegment(const QPointF &localPos) const
{
	const int count = m_nodes.size();
	if (count < 2)
		return std::make_pair(-1, 0.0);

	const int segments = m_closed ? count : count - 1;
	int bestSegment = -1;
	qreal bestT = 0.0;
	qreal bestDistSq = -1;

	for (int i = 0; i < segments; ++i)
	{
		const PathNode &a = m_nodes.at(i);
		const PathNode &b = m_nodes.at((i + 1) % count);
		const QPointF p0 = a.anchor;
		const QPointF p1 = a.anchor + a.outHandle.value_or(QPointF());
		const QPointF p2 = b.anchor + b.inHandle.value_or(QPointF());
		const QPointF p3 = b.anchor;

		const int samples = 24;
		for (int s = 0; s <= samples; ++s)
		{
			const qreal t = qreal(s) / samples;
			const qreal u = 1 - t;
			const QPointF pt = u*u*u*p0 + 3*u*u*t*p1 + 3*u*t*t*p2 + t*t*t*p3;
			const QPointF d = pt - localPos;
			const qreal distSq = d.x() * d.x() + d.y() * d.y();
			if (bestDistSq < 0 || distSq < bestDistSq)
			{
				bestDistSq = distSq;
				bestSegment = i;
				bestT = t;
			}
		}
	}
	return {bestSegment, bestT};
}

/**
	@brief QetShapeItem::insertPathPoint
	Splits the cubic Bezier between node segmentIndex and its successor at
	parameter t, via De Casteljau's algorithm -- the two resulting halves
	are guaranteed to retrace the original curve exactly (no kink at the
	seam), which a naive "just add a point at this position and guess new
	handles" approach cannot promise. When neither side of the segment
	actually has a handle (a plain straight run between two Corner-ish
	points), this degrades to a plain linear split with no handles at all
	on the new node, rather than introducing phantom zero-effect handles
	on what the user sees as a straight line.
*/
void QetShapeItem::insertPathPoint(int segmentIndex, qreal t)
{
	const QDomElement before = snapshotXml();

	prepareGeometryChange();
	const int count = m_nodes.size();
	const int nextIndex = (segmentIndex + 1) % count;
	PathNode &a = m_nodes[segmentIndex];
	PathNode &b = m_nodes[nextIndex];

	const QPointF p0 = a.anchor;
	const QPointF p1 = a.anchor + a.outHandle.value_or(QPointF());
	const QPointF p2 = b.anchor + b.inHandle.value_or(QPointF());
	const QPointF p3 = b.anchor;

	PathNode mid;
	if (!a.outHandle && !b.inHandle)
	{
		mid.anchor = p0 + (p3 - p0) * t;
		mid.kind = NodeKind::Corner;
	}
	else
	{
		const QPointF p01   = p0   + (p1   - p0)   * t;
		const QPointF p12   = p1   + (p2   - p1)   * t;
		const QPointF p23   = p2   + (p3   - p2)   * t;
		const QPointF p012  = p01  + (p12  - p01)  * t;
		const QPointF p123  = p12  + (p23  - p12)  * t;
		const QPointF p0123 = p012 + (p123 - p012) * t;

		mid.anchor = p0123;
		mid.kind = NodeKind::Smooth;   // De Casteljau guarantees tangent continuity through the split point
		mid.inHandle  = p012 - p0123;
		mid.outHandle = p123 - p0123;

		a.outHandle = p01 - p0;
		b.inHandle  = p23 - p3;
	}

	// Insert right before b's current position -- except when the split
	// segment is the closed path's wrap-around (last node back to
	// first): inserting there at the *end* of the array places the new
	// node correctly between old-last and old-first without shifting
	// every other node's index.
	m_nodes.insert(nextIndex == 0 ? m_nodes.size() : nextIndex, mid);

	const QDomElement after = snapshotXml();
	if (diagram())
	{
		auto *undo = new PromoteShapeCommand(this, before, after);
		undo->setText(tr("Ajouter un point à une courbe"));
		diagram()->undoStack().push(undo);
	}

	rebuildHandles();
}

/**
	@brief QetShapeItem::removePathPoint
	Deletes a node outright and lets its two former neighbours connect
	directly using their own existing handles -- no attempt to re-fit a
	single curve that approximates the old shape through where the point
	used to be. That's a much harder (and inherently lossy) problem; this
	is the same plain "just delete it" convention most editors default to.
*/
void QetShapeItem::removePathPoint(int nodeIndex)
{
	if (nodeIndex < 0 || nodeIndex >= m_nodes.size() || m_nodes.size() <= 2)
		return;

	const QDomElement before = snapshotXml();

	prepareGeometryChange();
	m_nodes.removeAt(nodeIndex);
	if (m_activeNode >= m_nodes.size())
		m_activeNode = qMax(0, m_nodes.size() - 1);

	const QDomElement after = snapshotXml();
	if (diagram())
	{
		auto *undo = new PromoteShapeCommand(this, before, after);
		undo->setText(tr("Supprimer un point d'une courbe"));
		diagram()->undoStack().push(undo);
	}

	rebuildHandles();
}

void QetShapeItem::convertToPathExplicitly()
{
	if (m_shapeType != Rectangle && m_shapeType != Ellipse)
		return;

	const QDomElement before = snapshotXml();

	QPolygonF corners;
	const QRectF r = localRect();
	corners << r.topLeft() << r.topRight() << r.bottomRight() << r.bottomLeft();

	prepareGeometryChange();
	m_shapeType = Polygon;
	m_polygon = corners;
	m_closed = true;

	const QDomElement after = snapshotXml();

	if (diagram())
	{
		auto *undo = new PromoteShapeCommand(this, before, after);
		undo->setText(tr("Convertir %1 en polyligne").arg(name()));
		diagram()->undoStack().push(undo);
	}

	rebuildHandles();
}

/**
	@brief QetShapeItem::setNodeKind
	Change a Path node's kind, via the context menu rather than a drag.
	Promoting a Corner node to Smooth or Symmetric synthesizes whichever
	handles it doesn't already have, from its neighbours -- direction
	toward the far neighbour, length a third of the distance to the near
	one, the same simple heuristic most vector editors use for a "make
	smooth" action. Demoting to Corner leaves any existing handles
	untouched (a Corner node can still have handles -- see
	dragPathControlHandle() -- it just stops forcing them to stay
	linked). The affected node also becomes the active one, in NodeEdit
	mode, so the result is immediately visible rather than a change to
	data you'd otherwise have to click into node-edit mode again to see.
*/
void QetShapeItem::setNodeKind(int nodeIndex, NodeKind kind)
{
	if (nodeIndex < 0 || nodeIndex >= m_nodes.size())
		return;

	const QDomElement before = snapshotXml();

	prepareGeometryChange();
	PathNode &node = m_nodes[nodeIndex];
	node.kind = kind;

	if (kind != NodeKind::Corner)
	{
		const int count = m_nodes.size();
		const bool hasPrev = (nodeIndex > 0) || (m_closed && count > 1);
		const bool hasNext = (nodeIndex < count - 1) || (m_closed && count > 1);
		const QPointF prevAnchor = hasPrev ? m_nodes.at((nodeIndex - 1 + count) % count).anchor : node.anchor;
		const QPointF nextAnchor = hasNext ? m_nodes.at((nodeIndex + 1) % count).anchor : node.anchor;

		QPointF tangent = nextAnchor - prevAnchor;
		const qreal tangentLength = qSqrt(tangent.x() * tangent.x() + tangent.y() * tangent.y());
		if (tangentLength > 1e-6)
			tangent /= tangentLength;

		if (!node.outHandle && hasNext)
		{
			const QPointF toNext = nextAnchor - node.anchor;
			const qreal len = qSqrt(toNext.x() * toNext.x() + toNext.y() * toNext.y()) / 3.0;
			node.outHandle = tangent * len;
		}
		if (!node.inHandle && hasPrev)
		{
			const QPointF toPrev = prevAnchor - node.anchor;
			const qreal len = qSqrt(toPrev.x() * toPrev.x() + toPrev.y() * toPrev.y()) / 3.0;
			node.inHandle = -tangent * len;
		}

		if (kind == NodeKind::Symmetric && node.inHandle && node.outHandle)
		{
			// Equalize lengths so the two handles are true mirrors from
			// the start, not just collinear -- otherwise a node promoted
			// straight to Symmetric would look identical to Smooth until
			// the next drag "fixed" it.
			const qreal inLen  = qSqrt(node.inHandle->x()  * node.inHandle->x()  + node.inHandle->y()  * node.inHandle->y());
			const qreal outLen = qSqrt(node.outHandle->x() * node.outHandle->x() + node.outHandle->y() * node.outHandle->y());
			const qreal avg = (inLen + outLen) / 2.0;
			if (inLen  > 1e-6) node.inHandle  = *node.inHandle  * (avg / inLen);
			if (outLen > 1e-6) node.outHandle = *node.outHandle * (avg / outLen);
		}
	}

	const QDomElement after = snapshotXml();
	if (diagram())
	{
		auto *undo = new PromoteShapeCommand(this, before, after);
		undo->setText(tr("Modifier le type d'un nœud"));
		diagram()->undoStack().push(undo);
	}

	m_activeNode = nodeIndex;
	m_handleMode = HandleMode::NodeEdit;
	rebuildHandles();
}

/**
	@brief QetShapeItem::lockAspectRatio
	Shift-constrained resize: keep the pre-drag width:height ratio,
	driven by whichever dimension moved proportionally more, re-anchored
	the same way the unconstrained result already was.
*/
QRectF QetShapeItem::lockAspectRatio(const QRectF &oldRect, QRectF newRect, int resizeIndex, bool mirrored)
{
	Q_UNUSED(resizeIndex)
	if (qFuzzyIsNull(oldRect.width()) || qFuzzyIsNull(oldRect.height()))
		return newRect;

	const qreal ratio = oldRect.width() / oldRect.height();
	qreal w = newRect.width(), h = newRect.height();
	const qreal wChange = qAbs(w / oldRect.width() - 1.0);
	const qreal hChange = qAbs(h / oldRect.height() - 1.0);
	if (wChange > hChange) h = w / ratio; else w = h * ratio;

	const QPointF anchor = mirrored ? oldRect.center() : newRect.topLeft();
	if (mirrored)
		return QRectF(anchor.x() - w / 2.0, anchor.y() - h / 2.0, w, h);

	// Re-anchor on whichever corner of newRect did not move -- simplest
	// robust way to express "keep the fixed corner fixed" without having
	// to re-derive which corner that is from resizeIndex.
	const bool keepLeft = qFuzzyCompare(newRect.left(), oldRect.left()) || newRect.left() >= oldRect.right();
	const bool keepTop  = qFuzzyCompare(newRect.top(),  oldRect.top())  || newRect.top()  >= oldRect.bottom();
	const qreal x = keepLeft ? newRect.left() : newRect.right() - w;
	const qreal y = keepTop  ? newRect.top()  : newRect.bottom() - h;
	return QRectF(x, y, w, h);
}

void QetShapeItem::dragResize(int index, const QPointF &localPos, Qt::KeyboardModifiers mods)
{
	if (m_shapeType == Line)
	{
		prepareGeometryChange();
		(index == 0 ? m_P1 : m_P2) = localPos;
		repositionHandles();
		return;
	}

	// Alt on a corner detaches that single vertex instead of resizing --
	// only corners (0,2,5,7 in pointsForRect's own ordering) carry that
	// meaning; dragging an edge midpoint with Alt has no special effect.
	static const QVector<int> cornerSlots = {0, 2, 5, 7};
	if ((mods & Qt::AltModifier) && cornerSlots.contains(index))
	{
		promoteRectangleOrEllipseToPolygon(index, localPos);
		return;
	}

	const bool mirrored = mods & Qt::ControlModifier;
	QRectF newRect = mirrored
			? QetGraphicsHandlerUtility::mirrorRectForPosAtIndex(localRect(), localPos, index)
			: QetGraphicsHandlerUtility::rectForPosAtIndex(localRect(), localPos, index);

	if (mods & Qt::ShiftModifier)
		newRect = lockAspectRatio(localRect(), newRect, index, mirrored);

	setRect(newRect.normalized());
}

void QetShapeItem::dragRotateHandle(int cornerIndex, const QPointF &scenePos, Qt::KeyboardModifiers mods)
{
	// Both angles below are computed *without* ever un-rotating by the
	// shape's current rotation: scenePivot is exactly pos()+pivot
	// regardless of rotation/skew/scale (a linear map always fixes its
	// own origin -- see shapetransform.h), and scaleAndShearOffset() is
	// the corner's position with everything *except* rotation already
	// applied. Solving "angleMouse == angleReference + rotation" this way
	// is a plain assignment, not a recurrence -- unlike computing the
	// angle via mapFromScene(), which divides out the *current* rotation
	// and makes each frame's result depend on the previous frame's
	// result: with the mouse held perfectly still that recurrence
	// alternates between two values forever instead of settling, which is
	// exactly the "jumps back and forth, lags behind" symptom.
	const QPointF scenePivot = pos() + m_transform.pivot;
	const qreal angleMouse = qRadiansToDegrees(qAtan2(scenePos.y() - scenePivot.y(), scenePos.x() - scenePivot.x()));

	const QPointF reference = scaleAndShearOffset(rotateHandleReference(cornerIndex));
	const qreal angleReference = qRadiansToDegrees(qAtan2(reference.y(), reference.x()));

	qreal angle = angleMouse - angleReference;
	if (mods & Qt::ShiftModifier)
		angle = qRound(angle / 15.0) * 15.0;
	setRotation(angle);
}

void QetShapeItem::dragSkewHandle(int edgeIndex, const QPointF &scenePos, Qt::KeyboardModifiers mods)
{
	// Solved in closed form for the one skew component being dragged,
	// holding rotation, scale and the *other* skew axis at their current
	// values -- the same reasoning as dragRotateHandle() above applies
	// here: computing this via mapFromScene() would divide out the skew
	// value this very function is trying to determine.
	//
	// Q  = the edge midpoint's offset from pivot, after scale only (fixed
	//      during this drag).
	// M  = the target offset (mouse position, relative to pos()+pivot),
	//      with rotation undone (fixed rotation during this drag).
	// Shear(kx,ky) maps Q to (Qx + kx*Qy, Qy + ky*Qx) -- see
	// shapetransform.cpp's decomposeLinear() derivation for this same
	// convention -- so each unknown drops out with a single division.
	const QPointF pivot = m_transform.pivot;
	const QPointF Q = scaleOnlyOffset(edgeMidpoint(localRect(), edgeIndex));

	const qreal rad = qDegreesToRadians(m_transform.rotation);
	const qreal c = qCos(rad), s = qSin(rad);
	const QPointF targetRel = scenePos - pos() - pivot;
	const QPointF M(targetRel.x() * c + targetRel.y() * s,
	               -targetRel.x() * s + targetRel.y() * c);

	qreal degrees;
	if (edgeIndex == 0 || edgeIndex == 2)   // N/S edge -> skewX ("sh"): M.x = Q.x + kx*Q.y
	{
		if (qFuzzyIsNull(Q.y())) return;
		degrees = qRadiansToDegrees(qAtan((M.x() - Q.x()) / Q.y()));
		if (mods & Qt::ShiftModifier) degrees = qRound(degrees / 15.0) * 15.0;
		setSkewX(degrees);
	}
	else                                     // E/W edge -> skewY ("sv"): M.y = Q.y + ky*Q.x
	{
		if (qFuzzyIsNull(Q.x())) return;
		degrees = qRadiansToDegrees(qAtan((M.y() - Q.y()) / Q.x()));
		if (mods & Qt::ShiftModifier) degrees = qRound(degrees / 15.0) * 15.0;
		setSkewY(degrees);
	}
}

void QetShapeItem::dragPivotHandle(const QPointF &localPos)
{
	// Snapping the pivot to the shape's own corners/center/edge-midpoints
	// (Shift) is a straightforward nearest-of-9-candidates check; left as
	// plain movement for now so the handle math above stays the focus.
	m_pivotIsCustom = true;
	setPivot(localPos);
}

void QetShapeItem::dragArcEndpoint(int which, const QPointF &localPos, Qt::KeyboardModifiers mods)
{
	const QRectF r = localRect();
	const QPointF center = r.center();
	const qreal rawAngle = -qRadiansToDegrees(qAtan2(localPos.y() - center.y(), localPos.x() - center.x()));

	// atan2's principal value jumps by 360 degrees at the +-180 degree
	// crossing even though the mouse only moved a hair -- unwrap it
	// relative to this endpoint's own previous value (the smallest
	// equivalent delta) so the stored angle, and therefore the rendered
	// arc, changes continuously through that crossing instead of
	// flipping to its complementary half.
	const qreal previous = (which == 0) ? m_startAngle : m_endAngle;
	qreal delta = std::fmod(rawAngle - previous + 540.0, 360.0) - 180.0;
	qreal angle = previous + delta;

	if (mods & Qt::ShiftModifier)
		angle = qRound(angle / 15.0) * 15.0;
	which == 0 ? setStartAngle(angle) : setEndAngle(angle);
}

void QetShapeItem::dragCornerRadius(int which, const QPointF &localPos)
{
	const qreal radius = QetGraphicsHandlerUtility::radiusForPosAtIndex(localRect(), localPos, which);
	if (m_modifie_radius_equaly) { setXRadius(radius); setYRadius(radius); }
	else if (which == 0) setXRadius(radius);
	else setYRadius(radius);
}

void QetShapeItem::dragPathAnchor(int which, const QPointF &localPos)
{
	prepareGeometryChange();
	if (m_shapeType == Polygon)
		m_polygon.replace(which, localPos);
	else if (which < m_nodes.size())
		m_nodes[which].anchor = localPos;   // in/out handles are relative offsets: they follow for free
	repositionHandles();
}

/**
	@brief QetShapeItem::dragPathControlHandle
	Dragging a Bezier control handle. Corner nodes have no linked
	opposite handle to update. Smooth nodes keep the two handles
	collinear through the anchor but let each keep its own length
	(tangent-continuous, magnitude-independent). Symmetric nodes mirror
	both direction and length exactly. Alt always means "break a normally
	-linked relationship" in this design -- here, detaching this handle
	from its mirror, permanently downgrading the node to Corner, exactly
	the same convention Alt already has on a rectangle's resize corner
	(see dragResize()).
*/
void QetShapeItem::dragPathControlHandle(bool isOutHandle, int nodeIndex, const QPointF &localPos, Qt::KeyboardModifiers mods)
{
	if (nodeIndex >= m_nodes.size())
		return;

	prepareGeometryChange();
	PathNode &node = m_nodes[nodeIndex];
	const QPointF newOffset = localPos - node.anchor;
	auto &dragged  = isOutHandle ? node.outHandle : node.inHandle;
	auto &mirrored = isOutHandle ? node.inHandle  : node.outHandle;
	dragged = newOffset;

	if (mods & Qt::AltModifier)
	{
		node.kind = NodeKind::Corner;
	}
	else if (mirrored && node.kind != NodeKind::Corner)
	{
		const qreal len = qSqrt(newOffset.x() * newOffset.x() + newOffset.y() * newOffset.y());
		if (len > 1e-6)
		{
			const QPointF direction(-newOffset.x() / len, -newOffset.y() / len);
			const QPointF oldMirrored = *mirrored;
			const qreal keptLength = (node.kind == NodeKind::Symmetric)
					? len
					: qSqrt(oldMirrored.x() * oldMirrored.x() + oldMirrored.y() * oldMirrored.y());
			mirrored = direction * keptLength;
		}
	}
	repositionHandles();
}

/**
	@brief QetShapeItem::handlerMousePressEvent
	@param handlerIndex
*/
void QetShapeItem::handlerMousePressEvent(int handlerIndex)
{
	Q_UNUSED(handlerIndex)
	m_old_P1 = m_P1;
	m_old_P2 = m_P2;
	m_old_polygon = m_polygon;
	m_old_xRadius = m_xRadius;
	m_old_yRadius = m_yRadius;
	m_old_transform = m_transform;
	m_old_pos = pos();
	m_old_nodes = m_nodes;
	if(m_xRadius == 0 && m_yRadius == 0) {
		m_modifie_radius_equaly = true;
	}
}

/**
	@brief QetShapeItem::handlerMouseMoveEvent
	@param handlerIndex
	@param event
*/
void QetShapeItem::handlerMouseMoveEvent(int handlerIndex, QGraphicsSceneMouseEvent *event)
{
	QPointF scenePos = event->scenePos();
	if (event->modifiers() != Qt::ControlModifier)
		scenePos = Diagram::snapToGrid(scenePos);

	const HandleRole role = m_handleRoles.value(handlerIndex, HandleRole::Resize);
	const int slot = m_handleSlot.value(handlerIndex, 0);
	const Qt::KeyboardModifiers mods = event->modifiers();

	// Rotate and SkewEdge are deliberately handled in scene space -- see
	// their comments for why mapFromScene() (used for every other role
	// below) is exactly the wrong tool for them.
	if (role == HandleRole::Rotate) { dragRotateHandle(slot, scenePos, mods); return; }
	if (role == HandleRole::SkewEdge) { dragSkewHandle(slot, scenePos, mods); return; }

	const QPointF new_pos = mapFromScene(scenePos);

	switch (role)
	{
		case HandleRole::Resize:        dragResize(slot, new_pos, mods); break;
		case HandleRole::Pivot:         dragPivotHandle(new_pos); break;
		case HandleRole::CornerRadius:  dragCornerRadius(slot, new_pos); break;
		case HandleRole::ArcEndpoint:   dragArcEndpoint(slot, new_pos, mods); break;
		case HandleRole::PathAnchor:    dragPathAnchor(slot, new_pos); break;
		case HandleRole::PathControlIn:  dragPathControlHandle(false, slot, new_pos, mods); break;
		case HandleRole::PathControlOut: dragPathControlHandle(true,  slot, new_pos, mods); break;
		case HandleRole::Rotate:
		case HandleRole::SkewEdge:
			break; // handled above
	}
}

/**
	@brief QetShapeItem::handlerMouseReleaseEvent
	@param handlerIndex
*/
void QetShapeItem::handlerMouseReleaseEvent(int handlerIndex)
{
	m_modifie_radius_equaly = false;
	const HandleRole role = m_handleRoles.value(handlerIndex, HandleRole::Resize);
	const int slot = m_handleSlot.value(handlerIndex, 0);

	if (!diagram())
		return;

	QUndoCommand *undo = nullptr;

	switch (role)
	{
		case HandleRole::Resize:
			if (m_shapeType == Line)
			{
				if (m_P1 != m_old_P1 || m_P2 != m_old_P2)
					undo = new QPropertyUndoCommand(this, "line", QLineF(m_old_P1, m_old_P2), QLineF(m_P1, m_P2));
			}
			else if (m_P1 != m_old_P1 || m_P2 != m_old_P2)
			{
				undo = new QPropertyUndoCommand(this, "rect", QRectF(m_old_P1, m_old_P2), QRectF(m_P1, m_P2).normalized());
			}
			break;

		case HandleRole::Rotate:
			if (!qFuzzyCompare(m_transform.rotation, m_old_transform.rotation))
				undo = new QPropertyUndoCommand(this, "rotation", m_old_transform.rotation, m_transform.rotation);
			break;

		case HandleRole::SkewEdge:
			if (!qFuzzyCompare(m_transform.skewX, m_old_transform.skewX))
				undo = new QPropertyUndoCommand(this, "skewX", m_old_transform.skewX, m_transform.skewX);
			else if (!qFuzzyCompare(m_transform.skewY, m_old_transform.skewY))
				undo = new QPropertyUndoCommand(this, "skewY", m_old_transform.skewY, m_transform.skewY);
			break;

		case HandleRole::Pivot:
			if (m_transform.pivot != m_old_transform.pivot)
			{
				undo = new QUndoCommand(tr("Deplacer le centre de rotation"));
				new QPropertyUndoCommand(this, "pos", m_old_pos, pos(), undo);
				new QPropertyUndoCommand(this, "pivot", m_old_transform.pivot, m_transform.pivot, undo);
			}
			break;

		case HandleRole::CornerRadius:
			if (m_old_xRadius != m_xRadius || m_old_yRadius != m_yRadius)
			{
				undo = new QPropertyUndoCommand(this, "xRadius", m_old_xRadius, m_xRadius);
				new QPropertyUndoCommand(this, "yRadius", m_old_yRadius, m_yRadius, undo);
			}
			break;

		case HandleRole::ArcEndpoint:
			// startAngle/endAngle changes are cosmetic-cost enough (and
			// re-derived from each other on snap-to-full-ellipse) that
			// they are intentionally not wrapped in undo here yet -- flag
			// for a follow-up once ArcEndpoint dragging ships in the UI.
			break;

		case HandleRole::PathAnchor:
			if (m_shapeType == Polygon && m_polygon != m_old_polygon)
			{
				undo = new QPropertyUndoCommand(this, "polygon", m_old_polygon, m_polygon);
			}
			else if (m_shapeType == Path && m_nodes != m_old_nodes)
			{
				// PathNode/QVector<PathNode> isn't a Q_PROPERTY-friendly
				// type, so this reuses PromoteShapeCommand's generic
				// before/after XML snapshot mechanism instead of a
				// dedicated undo class -- swap in the old nodes just
				// long enough to snapshot them, then restore.
				const QVector<PathNode> after = m_nodes;
				m_nodes = m_old_nodes;
				const QDomElement before = snapshotXml();
				m_nodes = after;
				const QDomElement afterXml = snapshotXml();
				undo = new PromoteShapeCommand(this, before, afterXml);
			}
			// Selecting a different node's anchor while in node-edit mode
			// reveals *its* control handles -- done here, at release, not
			// at press: rebuildHandles() destroys and recreates every
			// handler item, which would pull the rug out from under the
			// very drag gesture currently in progress if done any earlier.
			if (m_shapeType == Path && m_handleMode == HandleMode::NodeEdit && slot != m_activeNode)
			{
				prepareGeometryChange();
				m_activeNode = slot;
				rebuildHandles();
			}
			break;

		case HandleRole::PathControlIn:
		case HandleRole::PathControlOut:
			if (m_nodes != m_old_nodes)
			{
				const QVector<PathNode> after = m_nodes;
				m_nodes = m_old_nodes;
				const QDomElement before = snapshotXml();
				m_nodes = after;
				const QDomElement afterXml = snapshotXml();
				undo = new PromoteShapeCommand(this, before, afterXml);
			}
			break;
	}

	if (undo)
	{
		if (undo->text().isEmpty())
			undo->setText(tr("Modifier %1").arg(name()));
		diagram()->undoStack().push(undo);
	}
}

/**
	@brief QetShapeItem::fromXml
	Build this item from the xml description
	@param e element where is stored this item
	@return true if load success
*/
bool QetShapeItem::fromXml(const QDomElement &e)
{
	if (e.tagName() != "shape") return (false);

	// fromXml() is also used to *restore* an already-displayed item's
	// state (PromoteShapeCommand's undo/redo), not just to populate a
	// freshly constructed one -- without this, Qt has no way to know the
	// item's *previous* on-screen bounding rect needs repainting once the
	// geometry underneath it changes, so the old rendering stays stuck
	// until something unrelated forces a repaint of that area.
	prepareGeometryChange();

	is_movable_ = (e.attribute("is_movable").toInt());
	m_closed = e.attribute("closed", "0").toInt();
	m_pen = QETXML::penFromXml(e.firstChildElement("pen"));
	m_brush = QETXML::brushFromXml(e.firstChildElement("brush"));

	QString type = e.attribute("type");
	QMetaEnum me = metaObject()->enumerator(metaObject()->indexOfEnumerator("ShapeType"));
	m_shapeType = QetShapeItem::ShapeType(me.keysToValue(type.toStdString().data()));

	if (m_shapeType != Polygon && m_shapeType != Path)
	{
		m_P1.setX(e.attribute("x1", nullptr).toDouble());
		m_P1.setY(e.attribute("y1", nullptr).toDouble());
		m_P2.setX(e.attribute("x2", nullptr).toDouble());
		m_P2.setY(e.attribute("y2", nullptr).toDouble());

		if (m_shapeType == Rectangle)
		{
			setXRadius(e.attribute("rx", "0").toDouble());
			setYRadius(e.attribute("ry", "0").toDouble());
		}
	}
	if (m_shapeType == Polygon)
	{
		// fromXml() must be safe to call on an already-populated item, not
		// just a freshly constructed one: PromoteShapeCommand's undo/redo
		// (and the automatic redo() that QUndoStack::push() performs the
		// instant a command is pushed) both call it to *restore* a prior
		// state, on an object that already has geometry in it. Appending
		// onto whatever is already there -- rather than replacing it --
		// silently duplicated every point on the very first undo-worthy
		// edit.
		m_polygon.clear();
		for(const QDomElement& de : QET::findInDomElement(e, "points", "point")) {
			m_polygon << QPointF(de.attribute("x", nullptr).toDouble(), de.attribute("y", nullptr).toDouble());
		}
	}
	else if (m_shapeType == Path)
	{
		m_nodes.clear();
		for (const QDomElement &nodeElement : QET::findInDomElement(e, "nodes", "node"))
		{
			PathNode node;
			node.anchor = QPointF(nodeElement.attribute("x").toDouble(), nodeElement.attribute("y").toDouble());
			const QString kind = nodeElement.attribute("kind", "corner");
			node.kind = (kind == "smooth") ? NodeKind::Smooth
			          : (kind == "symmetric") ? NodeKind::Symmetric
			          : NodeKind::Corner;
			QDomElement in = nodeElement.firstChildElement("in");
			if (!in.isNull())
				node.inHandle = QPointF(in.attribute("dx").toDouble(), in.attribute("dy").toDouble());
			QDomElement out = nodeElement.firstChildElement("out");
			if (!out.isNull())
				node.outHandle = QPointF(out.attribute("dx").toDouble(), out.attribute("dy").toDouble());
			m_nodes << node;
		}
	}

	QDomElement transformElement = e.firstChildElement("transform");
	if (!transformElement.isNull())
	{
		m_transform.rotation = transformElement.attribute("rotation", "0").toDouble();
		m_transform.skewX    = transformElement.attribute("skewX", "0").toDouble();
		m_transform.skewY    = transformElement.attribute("skewY", "0").toDouble();
		m_transform.scaleX   = transformElement.attribute("scaleX", "1").toDouble();
		m_transform.scaleY   = transformElement.attribute("scaleY", "1").toDouble();
		m_transform.pivot    = QPointF(transformElement.attribute("pivotX", "0").toDouble(),
		                                transformElement.attribute("pivotY", "0").toDouble());
		m_pivotIsCustom = true;
	}
	else
	{
		m_transform = ShapeTransform();
		m_transform.pivot = localRect().center();
		m_pivotIsCustom = false;
	}
	setTransform(m_transform.toMatrix());

	QDomElement arcElement = e.firstChildElement("arc");
	if (!arcElement.isNull() && m_shapeType == Ellipse)
	{
		m_startAngle = arcElement.attribute("startAngle", "0").toDouble();
		m_endAngle   = m_startAngle + arcElement.attribute("spanAngle", "360").toDouble();
		const QString closure = arcElement.attribute("closure", "none");
		m_arcClosure = (closure == "chord") ? Chord : (closure == "pie") ? Pie : NoClosure;
	}

	if (e.hasAttribute("posX") || e.hasAttribute("posY"))
	{
		QGraphicsItem::setPos(e.attribute("posX", "0").toDouble(),
		                      e.attribute("posY", "0").toDouble());
	}

	setZValue(e.attribute("z", QString::number(this->zValue())).toDouble());

	// fromXml() can change anything about the shape -- geometry, node
	// count, even shapeType() itself (undoing a Rectangle->Polygon
	// promotion) -- so a full rebuild, not just a reposition, is the only
	// choice that's guaranteed consistent with whatever state was just
	// restored. Only when selected: an unselected item should have no
	// handles at all, and this can run on any item in the diagram, not
	// just the one currently being interacted with.
	if (isSelected())
		rebuildHandles();

	return (true);
}

/**
	@brief QetShapeItem::toXml
	Save this item to xml element
	@param document parent document xml
	@return element xml where is write this item
*/
QDomElement QetShapeItem::toXml(QDomDocument &document) const
{
	QDomElement result = document.createElement("shape");

		//write some attribute
	QMetaEnum me = metaObject()->enumerator(metaObject()->indexOfEnumerator("ShapeType"));
	result.setAttribute("type", me.valueToKey(m_shapeType));
	result.appendChild(QETXML::penToXml(document, m_pen));
	result.appendChild(QETXML::brushToXml(document, m_brush));
	result.setAttribute("is_movable", bool(is_movable_));
	result.setAttribute("closed", bool(m_closed));

	if (m_shapeType != Polygon && m_shapeType != Path)
	{
		result.setAttribute("x1", QString::number(m_P1.x()));
		result.setAttribute("y1", QString::number(m_P1.y()));
		result.setAttribute("x2", QString::number(m_P2.x()));
		result.setAttribute("y2", QString::number(m_P2.y()));

		if (m_shapeType == Rectangle)
		{
			result.setAttribute("rx", QString::number(m_xRadius));
			result.setAttribute("ry", QString::number(m_yRadius));
		}
	}
	if (m_shapeType == Polygon)
	{
		QDomElement points = document.createElement("points");
		for (QPointF p : m_polygon)
		{
			QDomElement point = document.createElement("point");
			point.setAttribute("x", QString::number(p.x()));
			point.setAttribute("y", QString::number(p.y()));
			points.appendChild(point);
		}
		result.appendChild(points);
	}
	else if (m_shapeType == Path)
	{
		QDomElement nodes = document.createElement("nodes");
		for (const PathNode &n : m_nodes)
		{
			QDomElement node = document.createElement("node");
			node.setAttribute("x", QString::number(n.anchor.x()));
			node.setAttribute("y", QString::number(n.anchor.y()));
			if (n.kind != NodeKind::Corner)
				node.setAttribute("kind", n.kind == NodeKind::Smooth ? "smooth" : "symmetric");
			if (n.inHandle)
			{
				QDomElement in = document.createElement("in");
				in.setAttribute("dx", QString::number(n.inHandle->x()));
				in.setAttribute("dy", QString::number(n.inHandle->y()));
				node.appendChild(in);
			}
			if (n.outHandle)
			{
				QDomElement out = document.createElement("out");
				out.setAttribute("dx", QString::number(n.outHandle->x()));
				out.setAttribute("dy", QString::number(n.outHandle->y()));
				node.appendChild(out);
			}
			nodes.appendChild(node);
		}
		result.appendChild(nodes);
	}

	// Omitted entirely at identity, exactly like an absent radius today --
	// this is what keeps old files, and files that never touch rotation,
	// byte-for-byte unchanged.
	if (!m_transform.isIdentity() || m_pivotIsCustom)
	{
		QDomElement transformElement = document.createElement("transform");
		transformElement.setAttribute("rotation", QString::number(m_transform.rotation));
		transformElement.setAttribute("skewX", QString::number(m_transform.skewX));
		transformElement.setAttribute("skewY", QString::number(m_transform.skewY));
		transformElement.setAttribute("scaleX", QString::number(m_transform.scaleX));
		transformElement.setAttribute("scaleY", QString::number(m_transform.scaleY));
		transformElement.setAttribute("pivotX", QString::number(m_transform.pivot.x()));
		transformElement.setAttribute("pivotY", QString::number(m_transform.pivot.y()));
		result.appendChild(transformElement);
	}

	if (m_shapeType == Ellipse && (!isFullEllipse() || m_arcClosure != NoClosure))
	{
		QDomElement arcElement = document.createElement("arc");
		arcElement.setAttribute("startAngle", QString::number(m_startAngle));
		arcElement.setAttribute("spanAngle", QString::number(spanAngle()));
		if (m_arcClosure != NoClosure)
			arcElement.setAttribute("closure", m_arcClosure == Chord ? "chord" : "pie");
		result.appendChild(arcElement);
	}

	// pos() is meaningful now that pivot moves (and, transparently, every
	// ordinary resize via the auto-recentring pivot -- see setRect())
	// compensate through it rather than through m_P1/m_P2. It was never
	// written here before; omitted at every reload it silently reset to
	// (0,0), which is exactly the "position shifts slightly on reload"
	// bug -- geometry and transform round-tripped correctly, but the
	// placement component of the two together did not.
	if (!pos().isNull())
	{
		result.setAttribute("posX", QString::number(pos().x()));
		result.setAttribute("posY", QString::number(pos().y()));
	}

	result.setAttribute("z", QString::number(this->zValue()));

	return(result);
}

/**
	@brief QetShapeItem::toDXF
	Draw this element to the dxf document
	@param filepath file path of the the dxf document
	@param pen
	@return true if draw success
*/
bool QetShapeItem::toDXF(const QString &filepath,const QPen &pen)
{
	// A non-identity transform means the shape is no longer axis-aligned
	// in scene space: mapping just two opposite corners and building a
	// new axis-aligned QRectF from them (the old Rectangle/Ellipse path
	// below) would silently produce the wrong quadrilateral. Fall back to
	// exporting the mapped outline as a polygon/polyline instead, exactly
	// the path already used for Polygon today.
	if (!m_transform.isIdentity() && (m_shapeType == Rectangle || m_shapeType == Ellipse))
	{
		const QPolygonF mappedOutline = mapToScene(outline().toFillPolygon());
		if (m_shapeType == Rectangle || isFullEllipse())
			Createdxf::drawPolygon(filepath, mappedOutline, Createdxf::dxfColor(pen));
		else
			Createdxf::drawPolyline(filepath, mappedOutline, Createdxf::dxfColor(pen));
		return true;
	}

	switch (m_shapeType)
	{
		case Line:
			Createdxf::drawLine(filepath,
			QLineF( mapToScene(m_P1),
				mapToScene(m_P2)),
				Createdxf::dxfColor(pen));
			return true;
		case Rectangle:
			Createdxf::drawRectangle(filepath,
			QRectF(mapToScene(m_P1),
				mapToScene(m_P2)).normalized(),
				Createdxf::dxfColor(pen));
			return true;
		case Ellipse:
			Createdxf::drawEllipse(filepath,
			QRectF(mapToScene(m_P1),
				mapToScene(m_P2)).normalized(),
				Createdxf::dxfColor(pen));
			return true;
		case Polygon:
			if(m_polygon.isClosed())
				Createdxf::drawPolygon(filepath,m_polygon,Createdxf::dxfColor(pen));
			else
				Createdxf::drawPolyline(filepath,m_polygon,Createdxf::dxfColor(pen));
			return true;
		case Path:
		{
			const QPolygonF flattened = mapToScene(outline().toFillPolygon());
			if (m_closed)
				Createdxf::drawPolygon(filepath, flattened, Createdxf::dxfColor(pen));
			else
				Createdxf::drawPolyline(filepath, flattened, Createdxf::dxfColor(pen));
			return true;
		}
		default:
			return false;
	}
}

/**
	@brief QetShapeItem::editProperty
	Edit the property of this item
*/
void QetShapeItem::editProperty()
{
	if (diagram() -> isReadOnly()) return;

	PropertiesEditorDialog ped(new ShapeGraphicsItemPropertiesWidget(this), diagram()->views().at(0));
	ped.exec();
}

/**
	@brief QetShapeItem::name
	@return the name of the current shape.
*/
QString QetShapeItem::name() const
{
	switch (m_shapeType) {
		case Line:	    return tr("une ligne");
		case Rectangle:	return tr("un rectangle");
		case Ellipse:	return isFullEllipse() ? tr("une éllipse") : tr("un arc");
		case Polygon:	return tr("une polyligne");
		case Path:	    return tr("une courbe");
		default:	    return tr("une shape");
	}
}
