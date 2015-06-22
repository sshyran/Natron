//  Natron
//
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
/*
 * Created by Alexandre GAUTHIER-FOICHAT on 6/1/2012.
 * contact: immarespond at gmail dot com
 *
 */

// from <https://docs.python.org/3/c-api/intro.html#include-files>:
// "Since Python may define some pre-processor definitions which affect the standard headers on some systems, you must include Python.h before any standard headers are included."
#include <Python.h>

#include "CurveEditorUndoRedo.h"

#include <cmath>
#include <QDebug>

#include "Global/GlobalDefines.h"

#include "Gui/CurveEditor.h"
#include "Gui/KnobGui.h"
#include "Gui/CurveWidget.h"

#include "Engine/Knob.h"
#include "Engine/Curve.h"
#include "Engine/RotoContext.h"
#include "Engine/KnobTypes.h"
#include "Engine/Transform.h"


//////////////////////////////ADD MULTIPLE KEYS COMMAND//////////////////////////////////////////////
AddKeysCommand::AddKeysCommand(CurveWidget *editor,
                               const KeysToAddList & keys,
                               QUndoCommand *parent)
    : QUndoCommand(parent)
      , _keys(keys)
      , _curveWidget(editor)
{
}

AddKeysCommand::AddKeysCommand(CurveWidget *editor,
                               const boost::shared_ptr<CurveGui>& curve,
                               const std::vector<KeyFrame> & keys,
                               QUndoCommand *parent)
    : QUndoCommand(parent)
      , _keys()
      , _curveWidget(editor)
{
    boost::shared_ptr<KeysForCurve> k(new KeysForCurve);

    k->curve = curve;
    k->keys = keys;
    _keys.push_back(k);
}

void
AddKeysCommand::addOrRemoveKeyframe(bool add)
{
    KeysToAddList::iterator next = _keys.begin();
    if (next != _keys.end()) {
        ++next;
    }
    for (KeysToAddList::iterator it = _keys.begin(); it != _keys.end(); ++it) {
        boost::shared_ptr<CurveGui> curve = (*it)->curve.lock();
        if (!curve) {
            continue;
        }
        KnobCurveGui* isKnobCurve = dynamic_cast<KnobCurveGui*>(curve.get());
        BezierCPCurveGui* isBezierCurve = dynamic_cast<BezierCPCurveGui*>(curve.get());
        if (isKnobCurve) {
            boost::shared_ptr<KnobI> knob = isKnobCurve->getInternalKnob();
            boost::shared_ptr<Parametric_Knob> isParametric = boost::dynamic_pointer_cast<Parametric_Knob>(knob);
            
            knob->beginChanges();
            assert( !(*it)->keys.empty() );
            for (U32 i = 0; i < (*it)->keys.size(); ++i) {
                
                double time = (*it)->keys[i].getTime();
                
                if (add) {
                    if (isParametric) {
                        Natron::StatusEnum st = isParametric->addControlPoint( isKnobCurve->getDimension(),
                                                                              time,
                                                                              (*it)->keys[i].getValue() );
                        assert(st == Natron::eStatusOK);
                        (void)st;
                    } else {
                        KnobGui* isgui = isKnobCurve->getKnobGui();
                        if (isgui) {
                            isgui->setKeyframe(time, isKnobCurve->getDimension() );
                        } else {
                            
                            Knob<double>* isDouble = dynamic_cast<Knob<double>*>(knob.get());
                            Knob<bool>* isBool = dynamic_cast<Knob<bool>*>(knob.get());
                            Knob<int>* isInt = dynamic_cast<Knob<int>*>(knob.get());
                            Knob<std::string>* isString = dynamic_cast<Knob<std::string>*>(knob.get());
                            if (isDouble) {
                                isDouble->setValueAtTime(time, isDouble->getValueAtTime(time), isKnobCurve->getDimension());
                            } else if (isBool) {
                                isBool->setValueAtTime(time, isBool->getValueAtTime(time), isKnobCurve->getDimension());
                            } else if (isInt) {
                                isInt->setValueAtTime(time, isInt->getValueAtTime(time), isKnobCurve->getDimension());
                            } else if (isString) {
                                isString->setValueAtTime(time, isString->getValueAtTime(time), isKnobCurve->getDimension());
                            }

                        }
                        
                    }
                } else {
                    if (isParametric) {
                        Natron::StatusEnum st = isParametric->deleteControlPoint( isKnobCurve->getDimension(),
                                                                                 curve->getInternalCurve()->keyFrameIndex(time) );
                        assert(st == Natron::eStatusOK);
                        (void)st;
                    } else {
                        KnobGui* isgui = isKnobCurve->getKnobGui();

                        if (isgui) {
                            isgui->removeKeyFrame(time, isKnobCurve->getDimension() );
                        } else {
                            knob->deleteValueAtTime(time, isKnobCurve->getDimension());
                        }
                        
                    }
                }
            }
            knob->endChanges();
    
        } else if (isBezierCurve) {
            for (U32 i = 0; i < (*it)->keys.size(); ++i) {
                if (add) {
                    isBezierCurve->getBezier()->setKeyframe((*it)->keys[i].getTime());
                } else {
                    isBezierCurve->getBezier()->removeKeyframe((*it)->keys[i].getTime());
                }
            }
        }

        // increment for next iteration
        if (next != _keys.end()) {
            ++next;
        }
    } // for(it)

    _curveWidget->update();

    setText( QObject::tr("Add multiple keyframes") );
} // addOrRemoveKeyframe

void
AddKeysCommand::undo()
{
    addOrRemoveKeyframe(false);
}

void
AddKeysCommand::redo()
{
    addOrRemoveKeyframe(true);
}

//////////////////////////////REMOVE  MULTIPLE KEYS COMMAND//////////////////////////////////////////////
RemoveKeysCommand::RemoveKeysCommand(CurveWidget* editor,
                                     const std::vector<std::pair<boost::shared_ptr<CurveGui>,KeyFrame> > & keys,
                                     QUndoCommand *parent )
    : QUndoCommand(parent)
      , _keys(keys)
      , _curveWidget(editor)
{
}

void
RemoveKeysCommand::addOrRemoveKeyframe(bool add)
{
    for (U32 i = 0; i < _keys.size(); ++i) {
        bool hasBlocked = false;
        
        KnobCurveGui* isKnobCurve = dynamic_cast<KnobCurveGui*>(_keys[i].first.get());
        BezierCPCurveGui* isBezierCurve = dynamic_cast<BezierCPCurveGui*>(_keys[i].first.get());
        
        if (isKnobCurve) {
            if (i != _keys.size() - 1) {
                isKnobCurve->getInternalKnob()->beginChanges();
                hasBlocked = true;
            }
            
            if (add) {
                
                int time = _keys[i].second.getTime();
                boost::shared_ptr<KnobI> knob = isKnobCurve->getInternalKnob();
                boost::shared_ptr<Parametric_Knob> isParametric = boost::dynamic_pointer_cast<Parametric_Knob>(knob);

                if (isParametric) {
                    
                    Natron::StatusEnum st = isParametric->addControlPoint( isKnobCurve->getDimension(), _keys[i].second.getTime(),_keys[i].second.getValue() );
                    assert(st == Natron::eStatusOK);
                    (void) st;
                } else {
                    KnobGui* guiKnob = isKnobCurve->getKnobGui();
                    if (guiKnob) {
                        guiKnob->setKeyframe(time,_keys[i].second, isKnobCurve->getDimension() );
                    } else {
                        
                        Knob<double>* isDouble = dynamic_cast<Knob<double>*>(knob.get());
                        Knob<bool>* isBool = dynamic_cast<Knob<bool>*>(knob.get());
                        Knob<int>* isInt = dynamic_cast<Knob<int>*>(knob.get());
                        Knob<std::string>* isString = dynamic_cast<Knob<std::string>*>(knob.get());
                        if (isDouble) {
                            isDouble->setValueAtTime(time, isDouble->getValueAtTime(time), isKnobCurve->getDimension());
                        } else if (isBool) {
                            isBool->setValueAtTime(time, isBool->getValueAtTime(time), isKnobCurve->getDimension());
                        } else if (isInt) {
                            isInt->setValueAtTime(time, isInt->getValueAtTime(time), isKnobCurve->getDimension());
                        } else if (isString) {
                            isString->setValueAtTime(time, isString->getValueAtTime(time), isKnobCurve->getDimension());
                        }

                    }
                    
                }
            } else {
                
                boost::shared_ptr<Parametric_Knob> knob = boost::dynamic_pointer_cast<Parametric_Knob>( isKnobCurve->getInternalKnob() );

                if (knob) {
                    Natron::StatusEnum st = knob->deleteControlPoint( isKnobCurve->getDimension(),
                                                                     _keys[i].first->getInternalCurve()->keyFrameIndex( _keys[i].second.getTime() ) );
                    assert(st == Natron::eStatusOK);
                    (void)st;
                } else {
                    KnobGui* guiKnob = isKnobCurve->getKnobGui();
                    if (guiKnob) {
                        guiKnob->removeKeyFrame( _keys[i].second.getTime(), isKnobCurve->getDimension() );
                    } else {
                        isKnobCurve->getInternalKnob()->deleteValueAtTime(_keys[i].second.getTime(), isKnobCurve->getDimension() );
                    }
                }
            }
            
            if (hasBlocked) {
                isKnobCurve->getInternalKnob()->endChanges();
            }
        } else if (isBezierCurve) {
            if (add) {
                isBezierCurve->getBezier()->setKeyframe(_keys[i].second.getTime());
            } else {
                isBezierCurve->getBezier()->removeKeyframe(_keys[i].second.getTime());
            }

        }
    }

    _curveWidget->update();
    setText( QObject::tr("Remove multiple keyframes") );
}

void
RemoveKeysCommand::undo()
{
    addOrRemoveKeyframe(true);
}

void
RemoveKeysCommand::redo()
{
    addOrRemoveKeyframe(false);
}

namespace  {
static bool
selectedKeyLessFunctor(const KeyPtr & lhs,
                       const KeyPtr & rhs)
{
    return lhs->key.getTime() < rhs->key.getTime();
}
}

//////////////////////////////MOVE MULTIPLE KEYS COMMAND//////////////////////////////////////////////
MoveKeysCommand::MoveKeysCommand(CurveWidget* widget,
                                 const SelectedKeys &keys,
                                 double dt,
                                 double dv,
                                 bool updateOnFirstRedo,
                                 QUndoCommand *parent )
    : QUndoCommand(parent)
      , _firstRedoCalled(false)
      , _updateOnFirstRedo(updateOnFirstRedo)
      , _dt(dt)
      , _dv(dv)
      , _keys(keys)
      , _widget(widget)
{
    ///sort keys by increasing time
    _keys.sort(selectedKeyLessFunctor);
}

static void
moveKey(KeyPtr &k,
        double dt,
        double dv)
{
    
    
    KnobCurveGui* isKnobCurve = dynamic_cast<KnobCurveGui*>(k->curve.get());
    BezierCPCurveGui* isBezierCurve = dynamic_cast<BezierCPCurveGui*>(k->curve.get());
    if (isKnobCurve) {
        boost::shared_ptr<KnobI> knob = isKnobCurve->getInternalKnob();
        Parametric_Knob* isParametric = dynamic_cast<Parametric_Knob*>(knob.get());
        
        if (isParametric) {
           // std::pair<double,double> curveYRange = k->curve->getInternalCurve()->getCurveYRange();
            double newX = k->key.getTime() + dt;
            double newY = k->key.getValue() + dv;
            boost::shared_ptr<Curve> curve = k->curve->getInternalCurve();
            
            if ( curve->areKeyFramesValuesClampedToIntegers() ) {
                newY = std::floor(newY + 0.5);
            } else if ( curve->areKeyFramesValuesClampedToBooleans() ) {
                newY = newY < 0.5 ? 0 : 1;
            }        
            double oldTime = k->key.getTime();
            int keyframeIndex = curve->keyFrameIndex(oldTime);
            int newIndex;
            
            k->key = curve->setKeyFrameValueAndTime(newX,newY, keyframeIndex, &newIndex);
            isParametric->evaluateValueChange(isKnobCurve->getDimension(), Natron::eValueChangedReasonUserEdited);
        } else {
            knob->moveValueAtTime(k->key.getTime(), isKnobCurve->getDimension(), dt, dv,&k->key);
        }
    } else if (isBezierCurve) {
        int oldTime = k->key.getTime();
        k->key.setTime(k->key.getTime() + dt);
        k->key.setValue(k->key.getValue() + dv);
        isBezierCurve->getBezier()->moveKeyframe(oldTime, k->key.getTime());

    }
}

void
MoveKeysCommand::move(double dt,
                      double dv)
{
    std::list<KnobHolder*> differentKnobs;

    std::list<boost::shared_ptr<RotoContext> > rotoToEvaluate;
    
    for (SelectedKeys::iterator it = _keys.begin(); it != _keys.end(); ++it) {
        KnobCurveGui* isKnobCurve = dynamic_cast<KnobCurveGui*>((*it)->curve.get());
        if (isKnobCurve) {
            
            if (!isKnobCurve->getKnobGui()) {
                boost::shared_ptr<RotoContext> roto = isKnobCurve->getRotoContext();
                assert(roto);
                if (std::find(rotoToEvaluate.begin(),rotoToEvaluate.end(),roto) == rotoToEvaluate.end()) {
                    rotoToEvaluate.push_back(roto);
                }
            } else {
                KnobI* k = isKnobCurve->getInternalKnob().get();
                if (k->getHolder()) {
                    if ( std::find(differentKnobs.begin(), differentKnobs.end(), k->getHolder()) == differentKnobs.end() ) {
                        differentKnobs.push_back(k->getHolder());
                        k->getHolder()->beginChanges();
                    }
                }
            }
        }
    }
    
    if (dt <= 0) {
        for (SelectedKeys::iterator it = _keys.begin(); it != _keys.end(); ++it) {
            moveKey(*it, dt, dv);
        }
    } else {
        for (SelectedKeys::reverse_iterator it = _keys.rbegin(); it != _keys.rend(); ++it) {
            moveKey(*it, dt, dv);
        }
    }
    
    if (_firstRedoCalled || _updateOnFirstRedo) {
        for (std::list<KnobHolder*>::iterator it = differentKnobs.begin(); it != differentKnobs.end(); ++it) {
            (*it)->endChanges();
        }
    }
    
    for (std::list<boost::shared_ptr<RotoContext> >::iterator it = rotoToEvaluate.begin(); it != rotoToEvaluate.end(); ++it) {
        (*it)->evaluateChange();
    }

    _widget->refreshSelectedKeys();
}

void
MoveKeysCommand::undo()
{
    move(-_dt,-_dv);
    setText( QObject::tr("Move multiple keys") );
}

void
MoveKeysCommand::redo()
{
    move(_dt,_dv);
    _firstRedoCalled = true;
    setText( QObject::tr("Move multiple keys") );
}

bool
MoveKeysCommand::mergeWith(const QUndoCommand * command)
{
    const MoveKeysCommand* cmd = dynamic_cast<const MoveKeysCommand*>(command);

    if ( cmd && ( cmd->id() == id() ) ) {
        if ( cmd->_keys.size() != _keys.size() ) {
            return false;
        }

        SelectedKeys::const_iterator itother = cmd->_keys.begin();
        for (SelectedKeys::const_iterator it = _keys.begin(); it != _keys.end(); ++it, ++itother) {
            if (*itother != *it) {
                return false;
            }
        }

        _dt += cmd->_dt;
        _dv += cmd->_dv;

        return true;
    } else {
        return false;
    }
}

int
MoveKeysCommand::id() const
{
    return kCurveEditorMoveMultipleKeysCommandCompressionID;
}

//////////////////////////////SET MULTIPLE KEYS INTERPOLATION COMMAND//////////////////////////////////////////////
SetKeysInterpolationCommand::SetKeysInterpolationCommand(CurveWidget* widget,
                                                         const std::list< KeyInterpolationChange > & keys,
                                                         QUndoCommand *parent)
    : QUndoCommand(parent)
      , _keys(keys)
      , _widget(widget)
{
}

void
SetKeysInterpolationCommand::setNewInterpolation(bool undo)
{
    std::list<KnobI*> differentKnobs;

    std::list<boost::shared_ptr<RotoContext> > rotoToEvaluate;

    for (std::list< KeyInterpolationChange >::iterator it = _keys.begin(); it != _keys.end(); ++it) {
        KnobCurveGui* isKnobCurve = dynamic_cast<KnobCurveGui*>(it->key->curve.get());
        if (isKnobCurve) {
            
            if (!isKnobCurve->getKnobGui()) {
                boost::shared_ptr<RotoContext> roto = isKnobCurve->getRotoContext();
                assert(roto);
                if (std::find(rotoToEvaluate.begin(),rotoToEvaluate.end(),roto) == rotoToEvaluate.end()) {
                    rotoToEvaluate.push_back(roto);
                }
            } else {
                KnobI* k = isKnobCurve->getInternalKnob().get();
                if ( std::find(differentKnobs.begin(), differentKnobs.end(), k) == differentKnobs.end() ) {
                    differentKnobs.push_back(k);
                    k->beginChanges();
                }
            }
        } else {
            BezierCPCurveGui* bezierCurve = dynamic_cast<BezierCPCurveGui*>(it->key->curve.get());
            assert(bezierCurve);
            rotoToEvaluate.push_back(bezierCurve->getBezier()->getContext());
        }
    }

    for (std::list< KeyInterpolationChange >::iterator it = _keys.begin(); it != _keys.end(); ++it) {
        
        Natron::KeyframeTypeEnum interp = undo ? it->oldInterp : it->newInterp;
        
        KnobCurveGui* isKnobCurve = dynamic_cast<KnobCurveGui*>(it->key->curve.get());
        if (isKnobCurve) {
            boost::shared_ptr<KnobI> knob = isKnobCurve->getInternalKnob();
            Parametric_Knob* isParametric = dynamic_cast<Parametric_Knob*>(knob.get());
            
            if (isParametric) {
                
                int keyframeIndex = it->key->curve->getKeyFrameIndex( it->key->key.getTime() );
                if (keyframeIndex != -1) {
                    it->key->curve->setKeyFrameInterpolation(interp, keyframeIndex);
                }
                
            } else {
                knob->setInterpolationAtTime(isKnobCurve->getDimension(), it->key->key.getTime(), interp, &it->key->key);
            }
        } else {
            ///interpolation for bezier curve is either linear or constant
            interp = interp == Natron::eKeyframeTypeConstant ? Natron::eKeyframeTypeConstant :
            Natron::eKeyframeTypeLinear;
            int keyframeIndex = it->key->curve->getKeyFrameIndex( it->key->key.getTime() );
            if (keyframeIndex != -1) {
                it->key->curve->setKeyFrameInterpolation(interp, keyframeIndex);
            }
        }
        
    }
    
    for (std::list<KnobI*>::iterator it = differentKnobs.begin(); it != differentKnobs.end(); ++it) {
        (*it)->endChanges();
    }
    for (std::list<boost::shared_ptr<RotoContext> >::iterator it = rotoToEvaluate.begin(); it != rotoToEvaluate.end(); ++it) {
        (*it)->evaluateChange();
    }

    _widget->refreshSelectedKeys();
    setText( QObject::tr("Set multiple keys interpolation") );
}

void
SetKeysInterpolationCommand::undo()
{
    setNewInterpolation(true);
}

void
SetKeysInterpolationCommand::redo()
{
    setNewInterpolation(false);
}

/////////////////////////// MoveTangentCommand

MoveTangentCommand::MoveTangentCommand(CurveWidget* widget,
                                       SelectedTangentEnum deriv,
                                       const KeyPtr& key,
                                       double dx,double dy, //< dx dy relative to the center of the keyframe
                                       bool updateOnFirstRedo,
                                       QUndoCommand *parent)
: QUndoCommand(parent)
, _widget(widget)
, _key(key)
, _deriv(deriv)
, _oldInterp(key->key.getInterpolation())
, _oldLeft(key->key.getLeftDerivative())
, _oldRight(key->key.getRightDerivative())
, _setBoth(false)
, _updateOnFirstRedo(updateOnFirstRedo)
, _firstRedoCalled(false)
{
    KeyFrameSet keys = key->curve->getInternalCurve()->getKeyFrames_mt_safe();
    KeyFrameSet::const_iterator cur = keys.find(key->key);
    assert( cur != keys.end() );

    //find next and previous keyframes
    KeyFrameSet::const_iterator prev = cur;
    if ( prev != keys.begin() ) {
        --prev;
    } else {
        prev = keys.end();
    }
    KeyFrameSet::const_iterator next = cur;
    if (next != keys.end()) {
        ++next;
    }

    // handle first and last keyframe correctly:
    // - if their interpolation was eKeyframeTypeCatmullRom or eKeyframeTypeCubic, then it becomes eKeyframeTypeFree
    // - in all other cases it becomes eKeyframeTypeBroken
    Natron::KeyframeTypeEnum interp = key->key.getInterpolation();
    bool keyframeIsFirstOrLast = ( prev == keys.end() || next == keys.end() );
    bool interpIsNotBroken = (interp != Natron::eKeyframeTypeBroken);
    bool interpIsCatmullRomOrCubicOrFree = (interp == Natron::eKeyframeTypeCatmullRom ||
                                            interp == Natron::eKeyframeTypeCubic ||
                                            interp == Natron::eKeyframeTypeFree);
    _setBoth = keyframeIsFirstOrLast ? interpIsCatmullRomOrCubicOrFree : interpIsNotBroken;

    bool isLeft;
    if (deriv == eSelectedTangentLeft) {
        //if dx is not of the good sign it would make the curve uncontrollable
        if (dx <= 0) {
            dx = 0.0001;
        }
        isLeft = true;
    } else {
        //if dx is not of the good sign it would make the curve uncontrollable
        if (dx >= 0) {
            dx = -0.0001;
        }
        isLeft = false;
    }
    double derivative = dy / dx;
    
    if (_setBoth) {
        _newInterp = Natron::eKeyframeTypeFree;
        _newLeft = derivative;
        _newRight = derivative;
    } else {
        if (isLeft) {
            _newLeft = derivative;
            _newRight = _oldRight;
        } else {
            _newLeft = _oldLeft;
            _newRight = derivative;
        }
        _newInterp = Natron::eKeyframeTypeBroken;
    }
}

MoveTangentCommand::MoveTangentCommand(CurveWidget* widget,
                   SelectedTangentEnum deriv,
                   const KeyPtr& key,
                   double derivative,
                   QUndoCommand *parent)
: QUndoCommand(parent)
, _widget(widget)
, _key(key)
, _deriv(deriv)
, _oldInterp(key->key.getInterpolation())
, _oldLeft(key->key.getLeftDerivative())
, _oldRight(key->key.getRightDerivative())
, _setBoth(true)
, _updateOnFirstRedo(true)
, _firstRedoCalled(false)
{
    _newInterp = _oldInterp == Natron::eKeyframeTypeBroken ? Natron::eKeyframeTypeBroken : Natron::eKeyframeTypeFree;
    _setBoth = _newInterp == Natron::eKeyframeTypeFree;
    
    switch (deriv) {
        case eSelectedTangentLeft:
            _newLeft = derivative;
            if (_newInterp == Natron::eKeyframeTypeBroken) {
                _newRight = _oldRight;
            } else {
                _newRight = derivative;
            }
            break;
        case eSelectedTangentRight:
            _newRight = derivative;
            if (_newInterp == Natron::eKeyframeTypeBroken) {
                _newLeft = _oldLeft;
            } else {
                _newLeft = derivative;
            }
        default:
            break;
    }
}


void
MoveTangentCommand::setNewDerivatives(bool undo)
{
    KnobCurveGui* isKnobCurve = dynamic_cast<KnobCurveGui*>(_key->curve.get());
    if (isKnobCurve) {
        boost::shared_ptr<KnobI> attachedKnob = isKnobCurve->getInternalKnob();
        assert(attachedKnob);
        Parametric_Knob* isParametric = dynamic_cast<Parametric_Knob*>(attachedKnob.get());
        
        
        double left = undo ? _oldLeft : _newLeft;
        double right = undo ? _oldRight : _newRight;
        Natron::KeyframeTypeEnum interp = undo ? _oldInterp : _newInterp;
        
        if (!isParametric) {
            attachedKnob->beginChanges();
            if (_setBoth) {
                attachedKnob->moveDerivativesAtTime(isKnobCurve->getDimension(), _key->key.getTime(), left, right);
            } else {
                attachedKnob->moveDerivativeAtTime(isKnobCurve->getDimension(), _key->key.getTime(),
                                                   _deriv == eSelectedTangentLeft ? left : right,
                                                   _deriv == eSelectedTangentLeft);
                
            }
            attachedKnob->setInterpolationAtTime(isKnobCurve->getDimension(), _key->key.getTime(), interp, &_key->key);
            if (_firstRedoCalled || _updateOnFirstRedo) {
                attachedKnob->endChanges();
            }
        } else {
            int keyframeIndexInCurve = _key->curve->getInternalCurve()->keyFrameIndex( _key->key.getTime() );
            _key->key = _key->curve->getInternalCurve()->setKeyFrameInterpolation(interp, keyframeIndexInCurve);
            _key->key = _key->curve->getInternalCurve()->setKeyFrameDerivatives(left, right,keyframeIndexInCurve);
            attachedKnob->evaluateValueChange(isKnobCurve->getDimension(), Natron::eValueChangedReasonUserEdited);
        }
        
        _widget->refreshDisplayedTangents();
    }
}


void
MoveTangentCommand::undo()
{
    setNewDerivatives(true);
    setText( QObject::tr("Move keyframe slope") );
}


void
MoveTangentCommand::redo()
{
    setNewDerivatives(false);
    _firstRedoCalled = true;
    setText( QObject::tr("Move keyframe slope") );
}

int
MoveTangentCommand::id() const
{
    return kCurveEditorMoveTangentsCommandCompressionID;
}

bool
MoveTangentCommand::mergeWith(const QUndoCommand * command)
{
    const MoveTangentCommand* cmd = dynamic_cast<const MoveTangentCommand*>(command);
    
    if ( cmd && ( cmd->id() == id() ) ) {
        if ( cmd->_key != _key ) {
            return false;
        }
        
        
        _newInterp = cmd->_newInterp;
        _newLeft = cmd->_newLeft;
        _newRight = cmd->_newRight;
        
        return true;
    } else {
        return false;
    }
}



TransformKeysCommand::TransformKeysCommand(CurveWidget* widget,
                     const SelectedKeys & keys,
                     double centerX,
                     double centerY,
                     double tx,
                     double ty,
                     double sx,
                     double sy,
                     bool updateOnFirstRedo,
                     QUndoCommand *parent)
: QUndoCommand(parent)
, _firstRedoCalled(false)
, _updateOnFirstRedo(updateOnFirstRedo)
, _keys(keys)
, _widget(widget)
, _matrix(new Transform::Matrix3x3)
{
    *_matrix = Transform::matTransformCanonical(tx, ty, sx, sy, 0, 0, true, 0, centerX, centerY);
}

TransformKeysCommand::~TransformKeysCommand()
{
}


void
TransformKeysCommand::undo()
{
    std::list<KnobHolder*> differentKnobs;
    
    std::list<boost::shared_ptr<RotoContext> > rotoToEvaluate;
    
    std::list<CurveGui*> processedCurves;
    for (SelectedKeys::iterator it = _keys.begin(); it != _keys.end(); ++it) {
        KnobCurveGui* isKnobCurve = dynamic_cast<KnobCurveGui*>((*it)->curve.get());
        if (isKnobCurve) {
            
            if (!isKnobCurve->getKnobGui()) {
                boost::shared_ptr<RotoContext> roto = isKnobCurve->getRotoContext();
                assert(roto);
                if (std::find(rotoToEvaluate.begin(),rotoToEvaluate.end(),roto) == rotoToEvaluate.end()) {
                    rotoToEvaluate.push_back(roto);
                }
            } else {
                KnobI* k = isKnobCurve->getInternalKnob().get();
                if (k->getHolder()) {
                    if ( std::find(differentKnobs.begin(), differentKnobs.end(), k->getHolder()) == differentKnobs.end() ) {
                        differentKnobs.push_back(k->getHolder());
                        k->getHolder()->beginChanges();
                    }
                }
            }
        }
        if (std::find(processedCurves.begin(), processedCurves.end(), (*it)->curve.get()) == processedCurves.end()) {
            processedCurves.push_back((*it)->curve.get());
        }
        
    }

    assert(_curves.size() == processedCurves.size());
    std::list<std::pair<boost::shared_ptr<Curve>,boost::shared_ptr<Curve> > >::iterator itCurve = _curves.begin();
    for (std::list<CurveGui*>::iterator it = processedCurves.begin(); it != processedCurves.end(); ++it, ++itCurve) {
        KnobCurveGui* isKnobCurve = dynamic_cast<KnobCurveGui*>(*it);
        if (isKnobCurve && !dynamic_cast<Parametric_Knob*>(isKnobCurve->getInternalKnob().get())) {
            isKnobCurve->getInternalKnob()->cloneCurve(isKnobCurve->getDimension(),*itCurve->first);
        } else {
            (*it)->getInternalCurve()->clone(*(itCurve->first));
        }
        
    }
    
    for (std::list<KnobHolder*>::iterator it = differentKnobs.begin(); it != differentKnobs.end(); ++it) {
        (*it)->endChanges();
    }
    
    for (std::list<boost::shared_ptr<RotoContext> >::iterator it = rotoToEvaluate.begin(); it != rotoToEvaluate.end(); ++it) {
        (*it)->evaluateChange();
    }
    
    _widget->refreshSelectedKeys();
}

void
TransformKeysCommand::redo()
{
    
    std::list<KnobHolder*> differentKnobs;
    
    std::list<boost::shared_ptr<RotoContext> > rotoToEvaluate;
    
    std::list<CurveGui*> processedCurves;
    for (SelectedKeys::iterator it = _keys.begin(); it != _keys.end(); ++it) {
        KnobCurveGui* isKnobCurve = dynamic_cast<KnobCurveGui*>((*it)->curve.get());
        if (isKnobCurve) {
            
            if (!isKnobCurve->getKnobGui()) {
                boost::shared_ptr<RotoContext> roto = isKnobCurve->getRotoContext();
                assert(roto);
                if (std::find(rotoToEvaluate.begin(),rotoToEvaluate.end(),roto) == rotoToEvaluate.end()) {
                    rotoToEvaluate.push_back(roto);
                }
            } else {
                KnobI* k = isKnobCurve->getInternalKnob().get();
                if (k->getHolder()) {
                    if ( std::find(differentKnobs.begin(), differentKnobs.end(), k->getHolder()) == differentKnobs.end() ) {
                        differentKnobs.push_back(k->getHolder());
                        k->getHolder()->beginChanges();
                    }
                }
            }
        }
        if (std::find(processedCurves.begin(), processedCurves.end(), (*it)->curve.get()) == processedCurves.end()) {
            processedCurves.push_back((*it)->curve.get());
        }
        
    }
    
    if (!_firstRedoCalled) {
        for (std::list<CurveGui*>::iterator it = processedCurves.begin(); it != processedCurves.end(); ++it) {
            boost::shared_ptr<Curve> oldCurve(new Curve(*(*it)->getInternalCurve()));
            _curves.push_back(std::make_pair(oldCurve, boost::shared_ptr<Curve>()));
        }
        for (SelectedKeys::reverse_iterator it = _keys.rbegin(); it != _keys.rend(); ++it) {
            transform(*it);
        }
        assert(_curves.size() == processedCurves.size());
        std::list<std::pair<boost::shared_ptr<Curve>,boost::shared_ptr<Curve> > >::iterator itCurve = _curves.begin();
        for (std::list<CurveGui*>::iterator it = processedCurves.begin(); it != processedCurves.end(); ++it, ++itCurve) {
            boost::shared_ptr<Curve> newCurve(new Curve(*(*it)->getInternalCurve()));
            itCurve->second = newCurve;
        }
    } else {
        assert(_curves.size() == processedCurves.size());
        std::list<std::pair<boost::shared_ptr<Curve>,boost::shared_ptr<Curve> > >::iterator itCurve = _curves.begin();
        for (std::list<CurveGui*>::iterator it = processedCurves.begin(); it != processedCurves.end(); ++it, ++itCurve) {
            KnobCurveGui* isKnobCurve = dynamic_cast<KnobCurveGui*>(*it);
            if (isKnobCurve && !dynamic_cast<Parametric_Knob*>(isKnobCurve->getInternalKnob().get())) {
                isKnobCurve->getInternalKnob()->cloneCurve(isKnobCurve->getDimension(),*itCurve->second);
            } else {
                (*it)->getInternalCurve()->clone(*(itCurve->second));
            }
            
        }
    }
    


    
    if (_firstRedoCalled || _updateOnFirstRedo) {
        for (std::list<KnobHolder*>::iterator it = differentKnobs.begin(); it != differentKnobs.end(); ++it) {
            (*it)->endChanges();
        }
    }
    
    for (std::list<boost::shared_ptr<RotoContext> >::iterator it = rotoToEvaluate.begin(); it != rotoToEvaluate.end(); ++it) {
        (*it)->evaluateChange();
    }
    
    _widget->refreshSelectedKeys();
    _firstRedoCalled = true;
    
}

int
TransformKeysCommand::id() const
{
    return kCurveEditorTransformKeysCommandCompressionID;
}

bool
TransformKeysCommand::mergeWith(const QUndoCommand * command)
{
    const TransformKeysCommand* cmd = dynamic_cast<const TransformKeysCommand*>(command);
    
    if ( cmd && ( cmd->id() == id() ) ) {
        if ( cmd->_keys.size() != _keys.size() ) {
            return false;
        }
        
        SelectedKeys::const_iterator itother = cmd->_keys.begin();
        for (SelectedKeys::const_iterator it = _keys.begin(); it != _keys.end(); ++it, ++itother) {
            if (*itother != *it) {
                return false;
            }
        }
        
        *_matrix = matMul(*_matrix, *cmd->_matrix);
        return true;
    } else {
        return false;
    }
}

void
TransformKeysCommand::transform(const KeyPtr& k)
{
    KnobCurveGui* isKnobCurve = dynamic_cast<KnobCurveGui*>(k->curve.get());
    BezierCPCurveGui* isBezierCurve = dynamic_cast<BezierCPCurveGui*>(k->curve.get());
    
    if (isKnobCurve) {
        boost::shared_ptr<KnobI> knob = isKnobCurve->getInternalKnob();
        Parametric_Knob* isParametric = dynamic_cast<Parametric_Knob*>(knob.get());
        
        if (isParametric) {
            // std::pair<double,double> curveYRange = k->curve->getInternalCurve()->getCurveYRange();
            Transform::Point3D p;
            p.x = k->key.getTime();
            p.y = k->key.getValue();
            p.z = 1;
            p = Transform::matApply(*_matrix, p);
            
            boost::shared_ptr<Curve> curve = k->curve->getInternalCurve();
            
            if ( curve->areKeyFramesValuesClampedToIntegers() ) {
                p.y = std::floor(p.y + 0.5);
            } else if ( curve->areKeyFramesValuesClampedToBooleans() ) {
                p.y = p.y < 0.5 ? 0 : 1;
            }
            double oldTime = k->key.getTime();
            int keyframeIndex = curve->keyFrameIndex(oldTime);
            int newIndex;
            
            k->key = curve->setKeyFrameValueAndTime(p.x,p.y, keyframeIndex, &newIndex);
            isParametric->evaluateValueChange(isKnobCurve->getDimension(), Natron::eValueChangedReasonUserEdited);
        } else {
            knob->transformValueAtTime(k->key.getTime(), isKnobCurve->getDimension(), *_matrix,&k->key);
        }
    } else if (isBezierCurve) {
        int oldTime = k->key.getTime();
        Transform::Point3D p;
        p.x = k->key.getTime();
        p.y = k->key.getValue();
        p.z = 1;
    
        p = Transform::matApply(*_matrix, p);
        
        //clamp time to integers
        p.x = std::floor(p.x + 0.5);
        
        if ( k->curve->areKeyFramesValuesClampedToIntegers() ) {
            p.y = std::floor(p.y + 0.5);
        } else if ( k->curve->areKeyFramesValuesClampedToBooleans() ) {
            p.y = p.y < 0.5 ? 0 : 1;
        }
        
        isBezierCurve->getBezier()->moveKeyframe(oldTime, p.x);
        
    }

}

