/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of Natron <http://www.natron.fr/>,
 * Copyright (C) 2016 INRIA and Alexandre Gauthier-Foichat
 *
 * Natron is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Natron is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Natron.  If not, see <http://www.gnu.org/licenses/gpl-2.0.html>
 * ***** END LICENSE BLOCK ***** */

// ***** BEGIN PYTHON BLOCK *****
// from <https://docs.python.org/3/c-api/intro.html#include-files>:
// "Since Python may define some pre-processor definitions which affect the standard headers on some systems, you must include Python.h before any standard headers are included."
#include <Python.h>
// ***** END PYTHON BLOCK *****

#include "RotoLayer.h"

#include <algorithm> // min, max
#include <sstream>
#include <locale>
#include <limits>
#include <cassert>
#include <stdexcept>

#include <QtCore/QLineF>
#include <QtCore/QDebug>

GCC_DIAG_UNUSED_LOCAL_TYPEDEFS_OFF
// /usr/local/include/boost/bind/arg.hpp:37:9: warning: unused typedef 'boost_static_assert_typedef_37' [-Wunused-local-typedef]
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/math/special_functions/fpclassify.hpp>
GCC_DIAG_UNUSED_LOCAL_TYPEDEFS_ON

#include "Global/MemoryInfo.h"
#include "Engine/RotoContextPrivate.h"

#include "Engine/AppInstance.h"
#include "Engine/Bezier.h"
#include "Engine/BezierSerialization.h"
#include "Engine/BezierCP.h"
#include "Engine/CoonsRegularization.h"
#include "Engine/FeatherPoint.h"
#include "Engine/Format.h"
#include "Engine/Hash64.h"
#include "Engine/Image.h"
#include "Engine/ImageParams.h"
#include "Engine/Interpolation.h"
#include "Engine/RenderStats.h"
#include "Engine/RotoLayerSerialization.h"
#include "Engine/RotoStrokeItem.h"
#include "Engine/Settings.h"
#include "Engine/TimeLine.h"
#include "Engine/Transform.h"
#include "Engine/ViewerInstance.h"

#define kMergeOFXParamOperation "operation"
#define kBlurCImgParamSize "size"
#define kTimeOffsetParamOffset "timeOffset"
#define kFrameHoldParamFirstFrame "firstFrame"

#define kTransformParamTranslate "translate"
#define kTransformParamRotate "rotate"
#define kTransformParamScale "scale"
#define kTransformParamUniform "uniform"
#define kTransformParamSkewX "skewX"
#define kTransformParamSkewY "skewY"
#define kTransformParamSkewOrder "skewOrder"
#define kTransformParamCenter "center"
#define kTransformParamFilter "filter"
#define kTransformParamResetCenter "resetCenter"
#define kTransformParamBlackOutside "black_outside"


#ifndef M_PI
#define M_PI        3.14159265358979323846264338327950288   /* pi             */
#endif

NATRON_NAMESPACE_ENTER;

////////////////////////////////////Layer////////////////////////////////////

RotoLayer::RotoLayer(const RotoContextPtr& context,
                     const std::string & n,
                     const RotoLayerPtr& parent)
    : RotoItem(context, n, parent)
    , _imp( new RotoLayerPrivate() )
{
}

RotoLayer::RotoLayer(const RotoLayer & other)
    : RotoItem( other.getContext(), other.getScriptName(), other.getParentLayer() )
    , _imp( new RotoLayerPrivate() )
{
    clone(&other);
}

RotoLayer::~RotoLayer()
{
}

void
RotoLayer::clone(const RotoItem* other)
{
    RotoItem::clone(other);
    const RotoLayer* isOtherLayer = dynamic_cast<const RotoLayer*>(other);

    if (!isOtherLayer) {
        return;
    }
    RotoLayerPtr this_shared = toRotoLayer( shared_from_this() );
    assert(this_shared);

    QMutexLocker l(&itemMutex);

    _imp->items.clear();
    for (std::list<RotoItemPtr >::const_iterator it = isOtherLayer->_imp->items.begin();
         it != isOtherLayer->_imp->items.end(); ++it) {
        RotoLayerPtr isLayer = toRotoLayer(*it);
        BezierPtr isBezier = toBezier(*it);
        RotoStrokeItemPtr isStroke = toRotoStrokeItem(*it);
        if (isBezier) {
            BezierPtr copy( new Bezier(*isBezier, this_shared) );
            copy->createNodes();
            _imp->items.push_back(copy);
        } else if (isStroke) {
            RotoStrokeItemPtr copy( new RotoStrokeItem( isStroke->getBrushType(),
                                                                        isStroke->getContext(),
                                                                        isStroke->getScriptName() + "copy",
                                                                        RotoLayerPtr() ) );
            copy->createNodes();
            _imp->items.push_back(copy);
            copy->setParentLayer(this_shared);
        } else {
            assert(isLayer);
            if (isLayer) {
                RotoLayerPtr copy( new RotoLayer(*isLayer) );
                copy->setParentLayer(this_shared);
                _imp->items.push_back(copy);
                getContext()->addLayer(copy);
            }
        }
    }
}

void
RotoLayer::save(const RotoItemSerializationPtr& obj) const
{
    RotoLayerSerializationPtr s = boost::dynamic_pointer_cast<RotoLayerSerialization>(obj);

    assert(s);
    if (!s) {
        throw std::logic_error("RotoLayer::save");
    }
    RotoItems items;
    {
        QMutexLocker l(&itemMutex);
        items = _imp->items;
    }

    for (RotoItems::const_iterator it = items.begin(); it != items.end(); ++it) {
        BezierPtr isBezier = toBezier(*it);
        RotoStrokeItemPtr isStroke = toRotoStrokeItem(*it);
        RotoLayerPtr layer = toRotoLayer(*it);
        RotoItemSerializationPtr childSerialization;
        if (isBezier && !isStroke) {
            childSerialization.reset(new BezierSerialization);
            isBezier->save( childSerialization );
        } else if (isStroke) {
            childSerialization.reset( new RotoStrokeItemSerialization() );
            isStroke->save( childSerialization );
        } else {
            assert(layer);
            if (layer) {
                childSerialization.reset(new RotoLayerSerialization);
                layer->save( childSerialization );
            }
        }
        assert(childSerialization);
        s->children.push_back(childSerialization);
    }


    RotoItem::save(obj);
}

void
RotoLayer::load(const RotoItemSerialization &obj)
{
    const RotoLayerSerialization & s = dynamic_cast<const RotoLayerSerialization &>(obj);
    RotoLayerPtr this_layer = toRotoLayer( shared_from_this() );

    assert(this_layer);
    RotoItem::load(obj);
    {
        for (std::list<RotoItemSerializationPtr >::const_iterator it = s.children.begin(); it != s.children.end(); ++it) {
            boost::shared_ptr<BezierSerialization> b = boost::dynamic_pointer_cast<BezierSerialization>(*it);
            boost::shared_ptr<RotoStrokeItemSerialization> s = boost::dynamic_pointer_cast<RotoStrokeItemSerialization>(*it);
            boost::shared_ptr<RotoLayerSerialization> l = boost::dynamic_pointer_cast<RotoLayerSerialization>(*it);
            if (b && !s) {
                BezierPtr bezier( new Bezier(getContext(), kRotoBezierBaseName, RotoLayerPtr(), false) );
                bezier->createNodes(false);
                bezier->load(*b);
                if ( !bezier->getParentLayer() ) {
                    bezier->setParentLayer(this_layer);
                }
                QMutexLocker l(&itemMutex);
                _imp->items.push_back(bezier);
            } else if (s) {
                RotoStrokeItemPtr stroke( new RotoStrokeItem( (RotoStrokeType)s->getType(), getContext(), kRotoPaintBrushBaseName, RotoLayerPtr() ) );
                stroke->createNodes(false);
                stroke->load(*s);
                if ( !stroke->getParentLayer() ) {
                    stroke->setParentLayer(this_layer);
                }


                QMutexLocker l(&itemMutex);
                _imp->items.push_back(stroke);
            } else if (l) {
                RotoLayerPtr layer( new RotoLayer(getContext(), kRotoLayerBaseName, this_layer) );
                _imp->items.push_back(layer);
                getContext()->addLayer(layer);
                layer->load(*l);
                if ( !layer->getParentLayer() ) {
                    layer->setParentLayer(this_layer);
                }
            }
            //Rotopaint tree nodes use the roto context age for their script-name, make sure they have a different one
            getContext()->incrementAge();
        }
    }
}

void
RotoLayer::addItem(const RotoItemPtr & item,
                   bool declareToPython )
{
    ///only called on the main-thread
    assert( QThread::currentThread() == qApp->thread() );
    RotoLayerPtr parentLayer = item->getParentLayer();
    if (parentLayer) {
        parentLayer->removeItem(item);
    }

    item->setParentLayer( toRotoLayer( shared_from_this() ) );
    {
        QMutexLocker l(&itemMutex);
        _imp->items.push_back(item);
    }
    if (declareToPython) {
        getContext()->declareItemAsPythonField(item);
    }
    getContext()->refreshRotoPaintTree();
}

void
RotoLayer::insertItem(const RotoItemPtr & item,
                      int index)
{
    ///only called on the main-thread
    assert( QThread::currentThread() == qApp->thread() );
    assert(index >= 0);

    RotoLayerPtr parentLayer = item->getParentLayer();
    if ( parentLayer && (parentLayer.get() != this) ) {
        parentLayer->removeItem(item);
    }


    {
        QMutexLocker l(&itemMutex);
        if (parentLayer.get() != this) {
            item->setParentLayer( toRotoLayer( shared_from_this() ) );
        } else {
            RotoItems::iterator found = std::find(_imp->items.begin(), _imp->items.end(), item);
            if ( found != _imp->items.end() ) {
                _imp->items.erase(found);
            }
        }
        RotoItems::iterator it = _imp->items.begin();
        if ( index >= (int)_imp->items.size() ) {
            it = _imp->items.end();
        } else {
            std::advance(it, index);
        }
        ///insert before the iterator
        _imp->items.insert(it, item);
    }
    getContext()->declareItemAsPythonField(item);
    getContext()->refreshRotoPaintTree();
}

void
RotoLayer::removeItem(const RotoItemPtr& item)
{
    ///only called on the main-thread
    assert( QThread::currentThread() == qApp->thread() );
    {
        QMutexLocker l(&itemMutex);
        for (RotoItems::iterator it = _imp->items.begin(); it != _imp->items.end(); ++it) {
            if (*it == item) {
                l.unlock();
                getContext()->removeItemAsPythonField(item);
                l.relock();
                _imp->items.erase(it);
                break;
            }
        }
    }
    item->setParentLayer( RotoLayerPtr() );
    RotoStrokeItemPtr isStroke = toRotoStrokeItem( item );
    if (isStroke) {
        isStroke->disconnectNodes();
    }
    getContext()->refreshRotoPaintTree();
}

int
RotoLayer::getChildIndex(const RotoItemPtr & item) const
{
    QMutexLocker l(&itemMutex);
    int i = 0;

    for (RotoItems::iterator it = _imp->items.begin(); it != _imp->items.end(); ++it, ++i) {
        if (*it == item) {
            return i;
        }
    }

    return -1;
}

const RotoItems &
RotoLayer::getItems() const
{
    ///only called on the main-thread
    assert( QThread::currentThread() == qApp->thread() );

    return _imp->items;
}

RotoItems
RotoLayer::getItems_mt_safe() const
{
    QMutexLocker l(&itemMutex);

    return _imp->items;
}

NATRON_NAMESPACE_EXIT;
