#include "touchhandler.h"
#include "mapsapp.h"
#include "ugui/svggui.h"


//static constexpr float flingInvTau = 1/0.05f;  // time constant for smoothing of pan speed
static constexpr float pinchThreshold = 30;  // pixels/xyScale (change in distance between fingers)
static constexpr float rotateThreshold = 0.25f;  // radians
static constexpr float shoveThreshold = 30;  // pixels/xyScale (translation of centroid of fingers)

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
  else if(sdltype == SVGGUI_FINGERCANCEL) return -1;
  return 0;
}

bool TouchHandler::sdlEvent(SvgGui* gui, SDL_Event* event)
{
  if(isLongPressOrRightClick(event)) {
    app->longPressEvent(event->tfinger.x*xyScale, event->tfinger.y*xyScale);
  }
  else if(event->type == SDL_FINGERDOWN || event->type == SDL_FINGERUP ||
      (event->type == SDL_FINGERMOTION && event->tfinger.fingerId == SDL_BUTTON_LMASK)) {
    if(event->tfinger.touchId == SDL_TOUCH_MOUSEID && event->tfinger.fingerId != SDL_BUTTON_LMASK)
      return false;
    if(event->type == SDL_FINGERUP) {
      if(gui->fingerClicks > 0) {
        app->map->handlePanGesture(prevCOM.x, prevCOM.y, initCOM.x, initCOM.y);  // undo any panning
        if(gui->fingerClicks == 1)
          app->tapEvent(initCOM.x, initCOM.y);
        else if(gui->fingerClicks == 2)  //%2 == 0)
          app->doubleTapEvent(initCOM.x, initCOM.y);
      }
      else if(gui->flingV != Point(0,0)) {
        Point v = Point(gui->flingV).clamp(-2000, 2000)*xyScale;
        // handleFlingGesture will ignore if velocity is too low
        app->map->handleFlingGesture(prevCOM.x, prevCOM.y, v.x, v.y);
      }
    }
    touchEvent(0, actionFromSDLFinger(event->type), event->tfinger.timestamp/1000.0,
        event->tfinger.x*xyScale, event->tfinger.y*xyScale, 1.0f);
  }
  else if(event->type == SDL_MOUSEWHEEL) {
    Point p = gui->prevFingerPos;
    uint32_t mods = (PLATFORM_WIN || PLATFORM_LINUX) ? (event->wheel.direction >> 16) : SDL_GetModState();
    app->onMouseWheel(p.x*xyScale, p.y*xyScale,
        event->wheel.x/120.0, event->wheel.y/120.0, mods & KMOD_ALT, mods & KMOD_CTRL);
  }
  else if(event->type == SvgGui::MULTITOUCH) {
    SDL_Event* fevent = static_cast<SDL_Event*>(event->user.data1);
    touchEvent(fevent->tfinger.fingerId, actionFromSDLFinger(fevent->type), fevent->tfinger.timestamp/1000.0,
        fevent->tfinger.x*xyScale, fevent->tfinger.y*xyScale, 1.0f);
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
  auto it = touchPoints.begin();
  while(it < touchPoints.end() && it->id != ptrId) { ++it; }
  if(it != touchPoints.end()) {
    if(action == -1) {
      touchPoints.erase(it);
    }
    else {
      if(action == 1)
        LOGE("Duplicate touch press event received!");
      *it = TouchPt{ptrId, x, y, p};
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
    multiTouchState = TOUCH_NONE;
    return;
  }

  const TouchPt& pt = touchPoints.front();
  if(touchPoints.size() > 1) {
    const TouchPt& pt2 = touchPoints.back();
    TouchPt com = {0, 0.5f*(pt.x + pt2.x), 0.5f*(pt.y + pt2.y), 0};
    float dx = pt2.x - pt.x, dy = pt2.y - pt.y;
    float dist = std::sqrt(dx*dx + dy*dy);
    float angle = std::atan2(dy, dx);
    if(touchPoints.size() > prevpoints) {
      multiTouchState = TOUCH_NONE;
    }
    else {
      if(multiTouchState == TOUCH_NONE) {
        if(std::abs(dist - prevDist) > pinchThreshold*xyScale)
          multiTouchState = TOUCH_PINCH;
        else if(std::abs(angle - prevAngle) > rotateThreshold)
          multiTouchState = TOUCH_ROTATE;
        else if(std::abs(com.y - prevCOM.y) > shoveThreshold*xyScale)
          multiTouchState = TOUCH_SHOVE;
      }
      if(multiTouchState == TOUCH_PINCH) {
        map->handlePanGesture(prevCOM.x, prevCOM.y, com.x, com.y);
        map->handlePinchGesture(com.x, com.y, dist/prevDist, 0.f);
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
    }
  }
  else if(prevpoints > 0) {
    if(prevpoints == 1) {
      map->handlePanGesture(prevCOM.x, prevCOM.y, pt.x, pt.y);
    }
    prevCOM = pt;
  }
  else if(prevpoints == 0) {
    map->handlePanGesture(0.0f, 0.0f, 0.0f, 0.0f);  // cancel any previous motion
    prevCOM = initCOM = pt;
  }
}
