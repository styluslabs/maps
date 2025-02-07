#include "touchhandler.h"
#include "mapsapp.h"
#include "gaml/src/yaml.h"
#include "ugui/svggui.h"


static constexpr float pinchThreshold = 30;  // pixels/xyScale (change in distance between fingers)
static constexpr float rotateThreshold = 0.25f;  // radians
static constexpr float shoveThreshold = 40;  // pixels/xyScale (translation of centroid of fingers)
static constexpr float longPressThreshold = 8;  // pixels/xyScale
static constexpr float mouseRotateScale = 0.001f;  // radians/pixel
static constexpr float tiltThresholdRad = 75.0*M_PI/180;
TouchHandler::TouchPt TouchHandler::TOUCHPT_NAN = {0, NAN, NAN, NAN};

static int actionFromSDLFinger(unsigned int sdltype)
{
  if(sdltype == SDL_FINGERMOTION) return 0;
  else if(sdltype == SDL_FINGERDOWN) return 1;
  else if(sdltype == SDL_FINGERUP) return -1;
  else if(sdltype == SVGGUI_FINGERCANCEL) return -2;
  return 0;
}

bool TouchHandler::sdlEvent(SvgGui* gui, SDL_Event* event)
{
  if(event->type == SDL_FINGERDOWN || event->type == SDL_FINGERUP || event->type == SVGGUI_FINGERCANCEL ||
      (event->type == SDL_FINGERMOTION && (event->tfinger.fingerId == SDL_BUTTON_LMASK || altDragMode))) {
    if(app->drawOnMap) {
      int action = actionFromSDLFinger(event->type);
      app->fingerEvent(action, event->tfinger.x*xyScale, event->tfinger.y*xyScale);
      if(action == 0)  // need to forward finger down so multitouch works properly
        return true;
    }
    else if(event->type == SDL_FINGERDOWN) {
      uint32_t mods = SDL_GetModState();
      bool ismouse = event->tfinger.touchId == SDL_TOUCH_MOUSEID;
      altDragMode = ismouse && event->tfinger.fingerId != SDL_BUTTON_LMASK;
      tapState = ((!ismouse && gui->fingerClicks == 2) || (mods & KMOD_CTRL)) ? DBL_TAP_DRAG_PENDING : TAP_NONE;
      if(altDragMode)
        rotOrigin = (mods & KMOD_SHIFT) ? TOUCHPT_NAN : initCOM;  //app->map->getTilt() > tiltThresholdRad
    }
    else if(event->type == SDL_FINGERMOTION) {
      if(tapState == DBL_TAP_DRAG_PENDING && gui->fingerClicks == 0)
        tapState = DBL_TAP_DRAG_ACTIVE;
    }
    else if(event->type == SDL_FINGERUP || event->type == SVGGUI_FINGERCANCEL) {
      if(gui->fingerClicks > 0) {
        app->map->handlePanGesture(prevCOM.x, prevCOM.y, initCOM.x, initCOM.y);  // undo any panning
        if(gui->fingerClicks == 1) {
          if(event->tfinger.touchId == SDL_TOUCH_MOUSEID) {
            if(altDragMode)
              app->longPressEvent(initCOM.x, initCOM.y);
            else
              app->tapEvent(initCOM.x, initCOM.y);
          }
          else {
            int delay = MapsApp::cfg()["ui"]["tap_delay"].as<int>(150);
            if(delay > 0) {
              tapTimer = app->gui->setTimer(delay, app->win.get(), tapTimer, [this]() {
                app->tapEvent(initCOM.x, initCOM.y);
                tapTimer = NULL;
                return 0;
              });
            }
            else
              app->tapEvent(initCOM.x, initCOM.y);
          }
        }
        else if(gui->fingerClicks == 2) { //%2 == 0)
          gui->removeTimer(tapTimer);
          app->doubleTapEvent(initCOM.x, initCOM.y);
        }
      }
      else if(gui->flingV != Point(0,0) && !altDragMode) {
        Point v = Point(gui->flingV).clamp(-2000, 2000)*xyScale;  // pixels per second
        // Tangram will ignore gestures if velocity is too low (as desired)
        if(tapState == DBL_TAP_DRAG_ACTIVE) {
          float h = app->getMapViewport().height();
          app->map->handlePinchGesture(initCOM.x, initCOM.y, 1.0, 4.0f*dblTapDragScale*v.y/h);
        }
        else
          app->map->handleFlingGesture(prevCOM.x, prevCOM.y, v.x, v.y);
      }
      altDragMode = false;
    }
    // hack because fingerId gets replaced for single touch events
    int fingerId = !gui->touchPoints.empty() ? gui->touchPoints[0].id : (!touchPoints.empty() ? touchPoints[0].id : 0);
    //int fingerId = gui->pressEvent.tfinger.fingerId;  -- this doesn't work
    touchEvent(fingerId, actionFromSDLFinger(event->type), event->tfinger.timestamp/1000.0,
        event->tfinger.x*xyScale, event->tfinger.y*xyScale, 1.0f);
  }
  else if(event->type == SvgGui::MULTITOUCH) {
    SDL_Event* fevent = static_cast<SDL_Event*>(event->user.data1);
    touchEvent(fevent->tfinger.fingerId, actionFromSDLFinger(fevent->type), fevent->tfinger.timestamp/1000.0,
        fevent->tfinger.x*xyScale, fevent->tfinger.y*xyScale, 1.0f);
  }
  else if(event->type == SDL_MOUSEWHEEL) {
    Point p = gui->prevFingerPos;
    uint32_t mods = (PLATFORM_WIN || PLATFORM_LINUX) ? (event->wheel.direction >> 16) : SDL_GetModState();
    app->onMouseWheel(p.x*xyScale, p.y*xyScale,
        event->wheel.x/120.0, event->wheel.y/120.0, mods & KMOD_ALT, mods & KMOD_CTRL);
  }
  else if(event->type == SvgGui::LONG_PRESS && event->tfinger.touchId == SvgGui::LONGPRESSID) {
    // GUI threshold for long press isn't strict enough for map interaction
    if(gui->totalFingerDist < longPressThreshold)
      app->longPressEvent(event->tfinger.x*xyScale, event->tfinger.y*xyScale);
  }
  else if(event->type == SvgGui::OUTSIDE_PRESSED) {
    sdlEvent(gui, (SDL_Event*)event->user.data1);  // just treat as a normal fingerup event
  }
  else if(event->type == SDL_KEYDOWN) {
    if(event->key.keysym.mod & KMOD_CTRL) {
      if(event->key.keysym.sym == SDLK_EQUALS)
        app->map->setZoom(app->map->getZoom() + 0.2f);
      else if(event->key.keysym.sym == SDLK_MINUS)
        app->map->setZoom(app->map->getZoom() - 0.2f);
      else
        return false;
    }
    else
      return false;
  }
  else
    return false;
  return true;
}

// action: -1 = release, 0 = move, 1 = press
void TouchHandler::touchEvent(int ptrId, int action, double t, float x, float y, float p)
{
  Map* map = app->map.get();
  size_t prevpoints = touchPoints.size();
  //LOGW("touchEvent: %d for %d, t: %f, x: %f, y: %f; current npts %d", action, ptrId, t, x, y, prevpoints);
  auto tpiter = touchPoints.begin();
  while(tpiter < touchPoints.end() && tpiter->id != ptrId) { ++tpiter; }
  if(tpiter != touchPoints.end()) {
    if(action < 0) {
      touchPoints.erase(tpiter);
    }
    else {
      if(action == 1)
        LOGE("Duplicate touch press event received!");
      *tpiter = TouchPt{ptrId, x, y, p};
    }
  }
  else {
    if(action > 0)
      touchPoints.push_back({ptrId, x, y, p});
    else if(action == 0)
      app->hoverEvent(x, y);
    else
      LOGE("Release event received for unknown touch point!");
  }

  if(touchPoints.empty()) {
    if(multiTouchState == TOUCH_PINCH && t - prevTime < 0.1) {
      auto it = --prevDists.end();
      while(it != prevDists.begin() && prevTime - it->t < 0.05) --it;  //FLING_AVG_SECS
      float pinchSpeed = (prevDist/it->dist - 1)/float(prevTime - it->t);
      map->handlePinchGesture(prevCOM.x, prevCOM.y, 1.0, pinchSpeed);
    }
    multiTouchState = TOUCH_NONE;
    return;
  }

  const TouchPt& pt = touchPoints.front();
  if(touchPoints.size() > 1) {
    const TouchPt& pt2 = touchPoints[1];  //.back();  -- ignore touch points beyond first two
    TouchPt com = {0, 0.5f*(pt.x + pt2.x), 0.5f*(pt.y + pt2.y), 0};
    float dx = pt2.x - pt.x, dy = pt2.y - pt.y;
    float dist = std::sqrt(dx*dx + dy*dy);
    float angle = std::atan2(dy, dx);
    if(touchPoints.size() > prevpoints) {
      if(app->drawOnMap)
        app->fingerEvent(-2, pt.x, pt.y);
      multiTouchState = TOUCH_NONE;
    }
    else {
      if(multiTouchState == TOUCH_NONE) {
         float threshscale = (map->getRotation() != 0 || map->getTilt() != 0) ? 0.5f : 1.0f;
        if(std::abs(dist - prevDist) > pinchThreshold*xyScale) {
          multiTouchState = TOUCH_PINCH;
          prevDists.clear();
        }
        else if(prevDist > 150*xyScale && std::abs(angle - prevAngle) > threshscale*rotateThreshold) {
          multiTouchState = TOUCH_ROTATE;
          rotOrigin = com;  //app->map->getTilt() > tiltThresholdRad ? TOUCHPT_NAN :
        }
        else if(prevDist < 250*xyScale && std::abs(com.y - prevCOM.y) > threshscale*shoveThreshold*xyScale) {
          multiTouchState = TOUCH_SHOVE;
        }
        else if(prevDist < 250*xyScale && std::abs(com.x - prevCOM.x) > threshscale*shoveThreshold*xyScale) {
          multiTouchState = TOUCH_ROTATE2;
          auto pos = app->getMapViewport().center();
          rotOrigin = app->map->getTilt() > tiltThresholdRad ? TOUCHPT_NAN : TouchPt{0, float(pos.x), float(pos.y), 0};
        }
      }
      if(multiTouchState == TOUCH_PINCH) {
        map->handlePanGesture(prevCOM.x, prevCOM.y, com.x, com.y);
        map->handlePinchGesture(com.x, com.y, dist/prevDist, 0.f);
        // once again FIR ends up being simpler than IIR when we need to handle, e.g, input events with dt = 0
        while(prevDists.size() > 1 && prevTime - prevDists.back().t < 0.005) prevDists.pop_back();  //MIN_INPUT_DT
        while(prevDists.size() >= 12) prevDists.pop_front();  //MAX_PREV_INPUT
        prevDists.push_back({prevDist, prevTime});
      }
      else if(multiTouchState == TOUCH_ROTATE)
        map->handleRotateGesture(rotOrigin.x, rotOrigin.y, angle - prevAngle);
      else if(multiTouchState == TOUCH_SHOVE)
        map->handleShoveGesture(com.y - prevCOM.y);
      else if(multiTouchState == TOUCH_ROTATE2) {
        //auto pos = app->getMapViewport().center();
        map->handleRotateGesture(rotOrigin.x, rotOrigin.y, -(com.x - prevCOM.x)*mouseRotateScale);
      }
    }
    if(multiTouchState != TOUCH_NONE || touchPoints.size() > prevpoints) {
      prevCOM = com;
      prevDist = dist;
      prevAngle = angle;
      prevTime = t;
    }
  }
  else if(prevpoints > 0) {
    if(multiTouchState != TOUCH_NONE && multiTouchState != TOUCH_PINCH) {}  // only allow panning after pinch
    else if(altDragMode && prevpoints == 1) {
      map->handleRotateGesture(rotOrigin.x, rotOrigin.y, -(pt.x - prevCOM.x)*mouseRotateScale);
      map->handleShoveGesture(pt.y - prevCOM.y);
    }
    else if(tapState == DBL_TAP_DRAG_ACTIVE) {
      float h = app->getMapViewport().height();
      // alternative is to zoom from center of map instead of tap point - float cx = w/2, cy = h/2;
      map->handlePinchGesture(initCOM.x, initCOM.y, std::pow(2.0f, 4.0f*dblTapDragScale*(pt.y - prevCOM.y)/h), 0.f);
    }
    else if(prevpoints == 1 && tapState == TAP_NONE) {
      map->handlePanGesture(prevCOM.x, prevCOM.y, pt.x, pt.y);
      // disable follow if sufficent panning (to avoid enraging user)
      if(app->followState == MapsApp::FOLLOW_ACTIVE && app->gui->totalFingerDist > 50)
        app->toggleFollow();
    }
    // we'll update prevCOM even in DBL_TAP_DRAG_PENDING so there isn't a jump in zoom when gesture becomes active
    prevCOM = pt;
  }
  else if(prevpoints == 0) {
    map->cancelCameraAnimation();  //handlePanGesture(0.0f, 0.0f, 0.0f, 0.0f);  // cancel any previous motion
    prevCOM = initCOM = pt;
  }
}
