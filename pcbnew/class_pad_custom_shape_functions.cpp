/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2016 Jean-Pierre Charras, jp.charras at wanadoo.fr
 * Copyright (C) 1992-2016 KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

/**
 * @file class_pad_custom_shape_functions.cpp
 * class D_PAD functions specific to custom shaped pads.
 */

#include <fctsys.h>
#include <PolyLine.h>
#include <trigo.h>
#include <wxstruct.h>

#include <pcbnew.h>

#include <class_pad.h>
#include <class_drawsegment.h>
#include <convert_basic_shapes_to_polygon.h>
#include <geometry/shape_rect.h>
#include <geometry/convex_hull.h>


void PAD_CS_PRIMITIVE::ExportTo( DRAWSEGMENT* aTarget )
{
    aTarget->SetShape( m_Shape );
    aTarget->SetWidth( m_Thickness );
    aTarget->SetStart( m_Start );
    aTarget->SetEnd( m_End );

    // in a DRAWSEGMENT the radius of a circle is calculated from the
    // center and one point on the circle outline (stored in m_End)
    if( m_Shape == S_CIRCLE )
    {
        wxPoint end = m_Start;
        end.x += m_Radius;
        aTarget->SetEnd( end );
    }

    aTarget->SetAngle( m_ArcAngle );
    aTarget->SetPolyPoints( m_Poly );
}

/*
 * Has meaning only for free shape pads.
 * add a free shape to the shape list.
 * the shape is a polygon (can be with thick outline), segment, circle or arc
 */
void D_PAD::AddPrimitive( std::vector<wxPoint>& aPoly, int aThickness )
{
    PAD_CS_PRIMITIVE shape( S_POLYGON );
    shape.m_Poly = aPoly;
    shape.m_Thickness = aThickness;
    m_basicShapes.push_back( shape );

    MergePrimitivesAsPolygon();
}


void D_PAD::AddPrimitive( wxPoint aStart, wxPoint aEnd, int aThickness )
{
    PAD_CS_PRIMITIVE shape( S_SEGMENT );
    shape.m_Start = aStart;
    shape.m_End = aEnd;
    shape.m_Thickness = aThickness;
    m_basicShapes.push_back( shape );

    MergePrimitivesAsPolygon();
}


void D_PAD::AddPrimitive( wxPoint aCenter, wxPoint aStart, int aArcAngle, int aThickness )
{
    PAD_CS_PRIMITIVE shape( S_ARC );
    shape.m_Start = aCenter;
    shape.m_End = aStart;
    shape.m_ArcAngle = aArcAngle;
    shape.m_Thickness = aThickness;
    m_basicShapes.push_back( shape );

    MergePrimitivesAsPolygon();
}


void D_PAD::AddPrimitive( wxPoint aCenter, int aRadius, int aThickness )
{
    PAD_CS_PRIMITIVE shape( S_CIRCLE );
    shape.m_Start = aCenter;
    shape.m_Radius = aRadius;
    shape.m_Thickness = aThickness;
    m_basicShapes.push_back( shape );

    MergePrimitivesAsPolygon();
}


bool D_PAD::SetPrimitives( const std::vector<PAD_CS_PRIMITIVE>& aPrimitivesList )
{
    // clear old list
    m_basicShapes.clear();

    // Import to the basic shape list
    if( aPrimitivesList.size() )
        m_basicShapes = aPrimitivesList;

    // Only one polygon is expected (pad area = only one copper area)
    return MergePrimitivesAsPolygon();
}

// clear the basic shapes list and associated data
void D_PAD::DeletePrimitivesList()
{
    m_basicShapes.clear();
    m_customShapeAsPolygon.RemoveAllContours();
}


/* Merge all basic shapes, converted to a polygon in one polygon,
 * return true if OK, false in there is more than one polygon
 * in aMergedPolygon
 */
bool D_PAD::MergePrimitivesAsPolygon(  SHAPE_POLY_SET* aMergedPolygon,
                                        int aCircleToSegmentsCount )
{
    // if aMergedPolygon == NULL, use m_customShapeAsPolygon as target

    if( !aMergedPolygon )
        aMergedPolygon = &m_customShapeAsPolygon;

    aMergedPolygon->RemoveAllContours();

    // Add the anchor pad shape in aMergedPolygon, others in aux_polyset:
    // The anchor pad is always at 0,0
    switch( GetAnchorPadShape() )
    {
    default:
    case PAD_SHAPE_CIRCLE:
        TransformCircleToPolygon( *aMergedPolygon, wxPoint( 0,0 ), GetSize().x/2,
                              aCircleToSegmentsCount );
        break;

    case PAD_SHAPE_RECT:
        {
        SHAPE_RECT rect( -GetSize().x/2, -GetSize().y/2, GetSize().x, GetSize().y );
        aMergedPolygon->AddOutline( rect.Outline() );
        }
        break;
    }

    SHAPE_POLY_SET aux_polyset;

    for( unsigned cnt = 0; cnt < m_basicShapes.size(); ++cnt )
    {
        const PAD_CS_PRIMITIVE& bshape = m_basicShapes[cnt];

        switch( bshape.m_Shape )
        {
        case S_SEGMENT:         // usual segment : line with rounded ends
            TransformRoundedEndsSegmentToPolygon( aux_polyset,
                bshape.m_Start, bshape.m_End, aCircleToSegmentsCount, bshape.m_Thickness );
            break;

        case S_ARC:             // Arc with rounded ends
            TransformArcToPolygon( aux_polyset,
                                   bshape.m_Start, bshape.m_End, bshape.m_ArcAngle,
                                   aCircleToSegmentsCount, bshape.m_Thickness );
            break;

        case S_CIRCLE:          //  ring or circle
            if( bshape.m_Thickness )    // ring
                TransformRingToPolygon( aux_polyset,
                                    bshape.m_Start, bshape.m_Radius,
                                    aCircleToSegmentsCount, bshape.m_Thickness ) ;
            else                // Filled circle
                TransformCircleToPolygon( aux_polyset,
                                    bshape.m_Start, bshape.m_Radius,
                                    aCircleToSegmentsCount ) ;
            break;

        case S_POLYGON:         // polygon
            if( bshape.m_Poly.size() < 2 )
                break;      // Malformed polygon.

            {
            // Insert the polygon:
            const std::vector< wxPoint>& poly = bshape.m_Poly;
            aux_polyset.NewOutline();

            if( bshape.m_Thickness )
            {
                SHAPE_POLY_SET polyset;
                polyset.NewOutline();

                for( unsigned ii = 0; ii < poly.size(); ii++ )
                    polyset.Append( poly[ii].x, poly[ii].y );

                polyset.Inflate( bshape.m_Thickness/2, 32 );

                aux_polyset.Append( polyset );
            }

            else
                for( unsigned ii = 0; ii < poly.size(); ii++ )
                    aux_polyset.Append( poly[ii].x, poly[ii].y );
            }
            break;

        default:
            break;
        }
    }

    // Merge all polygons:
    if( aux_polyset.OutlineCount() )
    {
        aMergedPolygon->BooleanAdd( aux_polyset, SHAPE_POLY_SET::PM_FAST );
        aMergedPolygon->Fracture( SHAPE_POLY_SET::PM_FAST );
    }

    m_boundingRadius = -1;  // The current bouding radius is no more valid.

    return aMergedPolygon->OutlineCount() <= 1;
}

void D_PAD::CustomShapeAsPolygonToBoardPosition( SHAPE_POLY_SET * aMergedPolygon,
                        wxPoint aPosition, double aRotation ) const
{
    if( aMergedPolygon->OutlineCount() == 0 )
        return;

    // Move, rotate, ... coordinates in aMergedPolygon according to the
    // pad position and orientation
    for( int cnt = 0; cnt < aMergedPolygon->OutlineCount(); ++cnt )
    {
        SHAPE_LINE_CHAIN& poly = aMergedPolygon->Outline( cnt );

        for( int ii = 0; ii < poly.PointCount(); ++ii )
        {
            wxPoint corner( poly.Point( ii ).x, poly.Point( ii ).y );
            RotatePoint( &corner, aRotation );
            corner += aPosition;

            poly.Point( ii ).x = corner.x;
            poly.Point( ii ).y = corner.y;
        }
    }
}