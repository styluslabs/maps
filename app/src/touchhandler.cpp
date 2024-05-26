#include "touchhandler.h"
#include "mapsapp.h"
#include "ugui/svggui.h"


//static constexpr float flingInvTau = 1/0.05f;  // time constant for smoothing of pan speed
static constexpr float pinchThreshold = 30;  // pixels/xyScale (change in distance between fingers)
static constexpr float rotateThreshold = 0.25f;  // radians
static constexpr float shoveThreshold = 100;  // pixels/xyScale (translation of centroid of fingers)
static constexpr float longPressThreshold = 8;  // pixels/xyScale
//static constexpr float maxTapDist = 20;  // max pixels between start and end points for a tap
//static constexpr float minFlingDist = 150;
//static constexpr double maxTapTime = 0.25;  // seconds
//static constexpr double minDblTapTime = 0.04;  // min time between end of first tap and start of second
//static constexpr double maxDblTapTime = 0.25;  // max time between end of first tap and start of second
//static constexpr double minFlingTime = 0.03;
//static constexpr double minLongPressTime = 0.7;  // 0.5s is typical on Android

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
  if(event->type == SDL_FINGERDOWN && event->tfinger.fingerId != SDL_BUTTON_LMASK) {
    app->longPressEvent(event->tfinger.x*xyScale, event->tfinger.y*xyScale);
  }
  else if(event->type == SDL_FINGERDOWN || event->type == SDL_FINGERUP || event->type == SVGGUI_FINGERCANCEL ||
      (event->type == SDL_FINGERMOTION && event->tfinger.fingerId == SDL_BUTTON_LMASK)) {
    if(event->tfinger.touchId == SDL_TOUCH_MOUSEID && event->tfinger.fingerId != SDL_BUTTON_LMASK)
      return false;
    if(app->drawOnMap) {
      app->fingerEvent(actionFromSDLFinger(event->type), event->tfinger.x*xyScale, event->tfinger.y*xyScale);
      return true;
    }
    if(event->type == SDL_FINGERDOWN) {
      tapState = gui->fingerClicks == 2 ? DBL_TAP_DRAG_PENDING : TAP_NONE;
    }
    else if(event->type == SDL_FINGERMOTION) {
      if(tapState == DBL_TAP_DRAG_PENDING && gui->fingerClicks == 0)
        tapState = DBL_TAP_DRAG_ACTIVE;
    }
    else if(event->type == SDL_FINGERUP || event->type == SVGGUI_FINGERCANCEL) {
      if(gui->fingerClicks > 0) {
        app->map->handlePanGesture(prevCOM.x, prevCOM.y, initCOM.x, initCOM.y);  // undo any panning
        if(gui->fingerClicks == 1) {
          if(event->tfinger.touchId == SDL_TOUCH_MOUSEID)
            app->tapEvent(initCOM.x, initCOM.y);
          else {
            // note delay is less than max double tap delay (400ms)
            tapTimer = app->gui->setTimer(250, app->win.get(), tapTimer, [this]() {
              app->tapEvent(initCOM.x, initCOM.y);
              tapTimer = NULL;
              return 0;
            });
          }
        }
        else if(gui->fingerClicks == 2) { //%2 == 0)
          gui->removeTimer(tapTimer);
          app->doubleTapEvent(initCOM.x, initCOM.y);
        }
      }
      else if(gui->flingV != Point(0,0)) {
        Point v = Point(gui->flingV).clamp(-2000, 2000)*xyScale;  // pixels per second
        // Tangram will ignore gestures if velocity is too low (as desired)
        if(tapState == DBL_TAP_DRAG_ACTIVE) {
          float h = app->map->getViewportHeight();
          app->map->handlePinchGesture(initCOM.x, initCOM.y, 1.0, 4.0f*dblTapDragScale*v.y/h);
        }
        else
          app->map->handleFlingGesture(prevCOM.x, prevCOM.y, v.x, v.y);
      }
    }
    touchEvent(0, actionFromSDLFinger(event->type), event->tfinger.timestamp/1000.0,
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
        else if(prevDist > 150*xyScale && std::abs(angle - prevAngle) > threshscale*rotateThreshold)
          multiTouchState = TOUCH_ROTATE;
        else if(prevDist < 250*xyScale && std::abs(com.y - prevCOM.y) > threshscale*shoveThreshold*xyScale)
          multiTouchState = TOUCH_SHOVE;
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
        map->handleRotateGesture(com.x, com.y, angle - prevAngle);
      else if(multiTouchState == TOUCH_SHOVE)
        map->handleShoveGesture(com.y - prevCOM.y);
    }
    if(multiTouchState != TOUCH_NONE || touchPoints.size() > prevpoints) {
      prevCOM = com;
      prevDist = dist;
      prevAngle = angle;
      prevTime = t;
    }
  }
  else if(prevpoints > 0) {
    if(tapState == DBL_TAP_DRAG_ACTIVE) {
      float h = app->map->getViewportHeight();
      // alternative is to zoom from center of map instead of tap point - float cx = w/2, cy = h/2;
      map->handlePinchGesture(initCOM.x, initCOM.y, std::pow(2.0f, 4.0f*dblTapDragScale*(pt.y - prevCOM.y)/h), 0.f);
    }
    else if(prevpoints == 1 && tapState == TAP_NONE) {
      map->handlePanGesture(prevCOM.x, prevCOM.y, pt.x, pt.y);
    }
    // we'll update prevCOM even in DBL_TAP_DRAG_PENDING so there isn't a jump in zoom when gesture becomes active
    prevCOM = pt;
  }
  else if(prevpoints == 0) {
    map->handlePanGesture(0.0f, 0.0f, 0.0f, 0.0f);  // cancel any previous motion
    prevCOM = initCOM = pt;
  }
}
