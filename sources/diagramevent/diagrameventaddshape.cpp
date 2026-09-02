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
#include "diagrameventaddshape.h"

#include "../diagram.h"
#include "../lastusedstyle.h"
#include "../qetapp.h"
#include "../qetdiagrameditor.h"
#include "../undocommand/addgraphicsobjectcommand.h"

#include <QStatusBar>

/**
	@brief DiagramEventAddShape::DiagramEventAddShape
	Default constructor
	@param diagram : the diagram where this event must operate
	@param shape_type : the type of shape to draw
*/
DiagramEventAddShape::DiagramEventAddShape(Diagram *diagram, QetShapeItem::ShapeType shape_type) :
	DiagramEventInterface(diagram),
	m_shape_type (shape_type),
	m_shape_item (nullptr),
	m_help_horiz (nullptr),
	m_help_verti (nullptr)
{
	m_running = true;
	init();
	updateCreationHint();
}

/**
	@brief DiagramEventAddShape::~DiagramEventAddShape
*/
DiagramEventAddShape::~DiagramEventAddShape()
{
	if ((m_running || m_abort) && m_shape_item)
	{
		m_diagram->removeItem(m_shape_item);
		delete m_shape_item;
	}
	delete m_help_horiz;
	delete m_help_verti;

	if (m_diagram && !m_diagram->views().isEmpty())
	{
		if (auto *editor = QETApp::diagramEditorAncestorOf(m_diagram->views().constFirst()))
			editor->statusBar()->clearMessage();
	}

	foreach (QGraphicsView *v, m_diagram->views())
		v->setContextMenuPolicy(Qt::DefaultContextMenu);
}

/**
	@brief DiagramEventAddShape::applyPosition
	Applies a drag/click position to the in-progress shape, honouring two
	modifiers that mirror how the very same shape can already be edited
	afterward, once placed:
	  - Ctrl, for Rectangle/Ellipse only: the first click becomes the
	    shape's *center* rather than a corner, growing symmetrically as
	    the cursor moves away from it -- the same meaning Ctrl already
	    has on a Resize handle (anchor at center). Deliberately not
	    offered for Line: unlike the Rectangle/Ellipse case, there's no
	    established convention for "a line grows symmetrically from its
	    middle" to justify it by, so Ctrl for Line means only free
	    positioning (see the plain grid-snap check above), nothing more.
	  - Shift, for Rectangle/Ellipse only: forces the bounding box square
	    (so Ellipse becomes a true circle), using whichever of the two
	    dragged dimensions is currently larger and mirroring that onto
	    the other, preserving the direction the user is actually
	    dragging in.
	Both can combine (Ctrl+Shift: a centered square/circle). Whether or
	not Ctrl is currently held, the non-anchored branch always rebuilds
	from m_anchor_point rather than nudging the existing rect/line --
	otherwise, if Ctrl had been held earlier in the same drag (moving the
	shape's own first point to a mirrored position), releasing it would
	leave that point stuck there instead of actually restoring it.
*/
void DiagramEventAddShape::applyPosition(const QPointF &pos, Qt::KeyboardModifiers mods)
{
	if (!m_shape_item)
		return;

	const bool centerAnchored = (mods & Qt::ControlModifier)
			&& (m_shape_type == QetShapeItem::Rectangle
			 || m_shape_type == QetShapeItem::Ellipse);

	QPointF target = pos;

	if ((mods & Qt::ShiftModifier)
			&& (m_shape_type == QetShapeItem::Rectangle || m_shape_type == QetShapeItem::Ellipse))
	{
		// m_anchor_point, not the shape's current rect(), on purpose:
		// after a period of Ctrl being held earlier in the same drag,
		// rect()'s corner could be the *displaced* one, not the true
		// original anchor -- see the note below on why the non-anchored
		// branch now always rebuilds from m_anchor_point for the same
		// reason.
		const QPointF ref = m_anchor_point;
		const qreal dx = target.x() - ref.x();
		const qreal dy = target.y() - ref.y();
		const qreal size = qMax(qAbs(dx), qAbs(dy));
		target.setX(ref.x() + (dx < 0 ? -size : size));
		target.setY(ref.y() + (dy < 0 ? -size : size));
	}

	if (centerAnchored)
	{
		const QPointF mirrored = 2 * m_anchor_point - target;
		m_shape_item->setRect(QRectF(mirrored, target));
	}
	else
	{
		// Rebuilt from m_anchor_point explicitly, not just setP2(target)
		// -- if Ctrl was held earlier in this same drag, the shape's own
		// first point was moved to a mirrored position; releasing Ctrl
		// has to actually restore it, not just stop moving it further,
		// or it gets stuck at wherever it last was for the rest of the
		// drag (a real, confirmed bug: releasing Ctrl was a one-way
		// street back to normal).
		if (m_shape_type == QetShapeItem::Line)
			m_shape_item->setLine(QLineF(m_anchor_point, target));
		else
			m_shape_item->setRect(QRectF(m_anchor_point, target));
	}
}

/**
	@brief DiagramEventAddShape::mousePressEvent
	Action when mouse is pressed
	@param event : event of mouse press
*/
void DiagramEventAddShape::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
	if (Q_UNLIKELY(m_diagram->isReadOnly())) {
		return;
	}

	QPointF pos = event->scenePos();
	if (event->modifiers() != Qt::ControlModifier) {
		pos = Diagram::snapToGrid(pos);
	}

		//Action for left mouse click
	if (event->button() == Qt::LeftButton)
	{
			//Create shape item
		if (!m_shape_item)
		{
			m_shape_item = new QetShapeItem(pos, pos, m_shape_type);
			m_anchor_point = pos;
				//Start from whatever pen/brush was last applied this
				//session, rather than always the hardcoded default.
			if (LastUsedStyle::hasShapePen()) {
				m_shape_item->setPen(LastUsedStyle::shapePen());
			}
			if (LastUsedStyle::hasShapeBrush()) {
				m_shape_item->setBrush(LastUsedStyle::shapeBrush());
			}
			m_diagram->addItem (m_shape_item);
			updateCreationHint();
			event->setAccepted(true);
			return;
		}

			//If current item isn't a polyline, add it with an undo command
		if (m_shape_type != QetShapeItem::Polygon)
		{
			applyPosition(pos, event->modifiers());
			if (m_shape_item->shapeType() == QetShapeItem::Rectangle || m_shape_item->shapeType() == QetShapeItem::Ellipse) {
				m_shape_item->setRect(m_shape_item->rect().normalized());
			}
			m_diagram->undoStack().push (new AddGraphicsObjectCommand(m_shape_item, m_diagram));
			m_shape_item = nullptr; //< set to nullptr for create new shape at next left clic
			updateCreationHint();
		}
			//Else add a new point to polyline
		else
		{
			m_shape_item->setNextPoint (pos);
		}

		event->setAccepted(true);
		return;
	}

	if (event->button() == Qt::RightButton) {
		event->setAccepted(true);
	}
}

/**
	@brief DiagramEventAddShape::mouseMoveEvent
	Action when mouse move
	@param event : event of mouse move
*/
void DiagramEventAddShape::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
	updateHelpCross(event->scenePos());

	if (m_shape_item && event->buttons() == Qt::NoButton)
	{
		QPointF pos = event->scenePos();
		if (event->modifiers() != Qt::ControlModifier) {
			pos = Diagram::snapToGrid(pos);
		}

		applyPosition(pos, event->modifiers());
		event->setAccepted(true);
	}
}

/**
	@brief DiagramEventAddShape::mouseReleaseEvent
	Action when mouse button is released
	@param event : event of mouse release
*/
void DiagramEventAddShape::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
	if (event->button() == Qt::RightButton)
	{
			//If shape is created, we manage right click
		if (m_shape_item)
		{
				//Shape is a polyline and have three points or more we just remove the last point
			if (m_shape_type == QetShapeItem::Polygon && (m_shape_item->pointsCount() >= 3) )
			{
				m_shape_item->removePoints();

				QPointF pos = event->scenePos();
				if (event->modifiers() != Qt::ControlModifier)
					pos = Diagram::snapToGrid(pos);

				m_shape_item->setP2(pos); //Set the new last point under the cursor
				event->setAccepted(true);
				return;
			}

				//For other case, we remove item from scene
			m_diagram->removeItem(m_shape_item);
			delete m_shape_item;
			m_shape_item = nullptr;
			updateCreationHint();
			event->setAccepted(true);
			return;
		}

			//Else (no shape), we set to false the running status
			//for indicate to the owner of this event that everything is done
		m_running = false;
		emit finish();
		event->setAccepted(true);
	}
}

/**
	@brief DiagramEventAddShape::mouseDoubleClickEvent
	Action when mouse button is double clicked
	@param event : event of mouse double click
*/
void DiagramEventAddShape::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event)
{
		//If current item is a polyline, add it with an undo command
	if (m_shape_item && m_shape_type == QetShapeItem::Polygon && event->button() == Qt::LeftButton)
	{
			//<double clic is used to finish polyline, but they also add two points at the same pos
			//<(double clic is a double press event), so we remove the last point of polyline
		m_shape_item->removePoints();

			//If the last is at the same pos of the first point
			//that mean user want a closed polygon, so we remove the last point and close polygon
		QPolygonF polygon = m_shape_item->polygon();
		if (polygon.first() == polygon.last())
		{
			m_shape_item->removePoints();
			m_shape_item->setClosed(true);
		}
		m_diagram->undoStack().push (new AddGraphicsObjectCommand(m_shape_item, m_diagram));
		m_shape_item = nullptr; //< set to nullptr for create new shape at next left clic
		updateCreationHint();
		event->setAccepted(true);
	}
}

void DiagramEventAddShape::init()
{
	foreach (QGraphicsView *v, m_diagram->views())
		v->setContextMenuPolicy(Qt::NoContextMenu);
}

/**
	@brief DiagramEventAddShape::updateCreationHint
	Shows whichever of beforeClickHint()/afterClickHint() matches the
	current phase -- there was previously either no message at all
	(Line/Rectangle/Ellipse) or a single static one that never changed
	regardless of progress (Polygon, set externally in
	QETDiagramEditor::addItemGroupTriggered()); this replaces both with
	one phase-aware message per shape type, managed by the tool itself.
*/
void DiagramEventAddShape::updateCreationHint() const
{
	if (!m_diagram || m_diagram->views().isEmpty())
		return;
	if (auto *editor = QETApp::diagramEditorAncestorOf(m_diagram->views().constFirst()))
		editor->statusBar()->showMessage(m_shape_item ? afterClickHint() : beforeClickHint());
}

QString DiagramEventAddShape::beforeClickHint() const
{
	switch (m_shape_type)
	{
		case QetShapeItem::Line:
			return tr("Clic gauche : positionner le point de départ (Ctrl = position libre)");
		case QetShapeItem::Rectangle:
		case QetShapeItem::Ellipse:
			return tr("Clic gauche : positionner le premier coin (Ctrl = point central, position libre)");
		case QetShapeItem::Polygon:
			return tr("Clic gauche : positionner le premier point (Ctrl = position libre)");
		default:
			return QString();
	}
}

QString DiagramEventAddShape::afterClickHint() const
{
	switch (m_shape_type)
	{
		case QetShapeItem::Line:
			return tr("Clic gauche : positionner le point final (Ctrl = position libre) ; clic droit : annuler");
		case QetShapeItem::Rectangle:
			return tr("Clic gauche : positionner le coin opposé (Maj = carré, "
					"Ctrl = depuis le centre + position libre, Ctrl+Maj = carré centré) ; clic droit : annuler");
		case QetShapeItem::Ellipse:
			return tr("Clic gauche : positionner le coin opposé (Maj = cercle, "
					"Ctrl = depuis le centre + position libre, Ctrl+Maj = cercle centré) ; clic droit : annuler");
		case QetShapeItem::Polygon:
			return tr("Clic gauche : point suivant ; double-clic ou Entrée : terminer ; "
					"clic droit : annuler le dernier point");
		default:
			return QString();
	}
}

/**
	@brief DiagramEventAddShape::updateHelpCross
	Create and update the position of the cross to help user for draw new shape
	@param p : the center of the cross
*/
void DiagramEventAddShape::updateHelpCross(const QPointF &p)
{
		//If line isn't created yet, we create it.
	if (!m_help_horiz || !m_help_verti)
	{
		QPen pen;
		pen.setWidthF(0.4);
		pen.setCosmetic(true);
		pen.setColor(Diagram::background_color == Qt::darkGray ? Qt::lightGray : Qt::darkGray);

		QRectF rect = m_diagram->border_and_titleblock.insideBorderRect();

		if (!m_help_horiz)
		{
			m_help_horiz = new QGraphicsLineItem(rect.topLeft().x(), 0, rect.topRight().x(), 0);
			m_help_horiz->setPen(pen);
			m_diagram->addItem(m_help_horiz);
		}

		if (!m_help_verti)
		{
			m_help_verti = new QGraphicsLineItem(0, rect.topLeft().y(), 0, rect.bottomLeft().y());
			m_help_verti->setPen(pen);
			m_diagram->addItem(m_help_verti);
		}
	}

		//Update the position of the cross
	QPointF point = Diagram::snapToGrid(p);

	m_help_horiz->setY(point.y());
	m_help_verti->setX(point.x());
}
