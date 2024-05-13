#pragma once

#include <vector>
#include <list>

class MapsApp;
class SvgGui;
struct Timer;
union SDL_Event;

class TouchHandler
{
public:
  TouchHandler(MapsApp* _app) : app(_app) {}
  void touchEvent(int ptrId, int action, double t, float x, float y, float p);
  bool sdlEvent(SvgGui* gui, SDL_Event* event);

  MapsApp* app;

  struct TouchPt {
    int id;
    float x, y, p;
  };
  std::vector<TouchPt> touchPoints;

  TouchPt prevCOM = {0, 0, 0, 0};
  TouchPt initCOM = {0, 0, 0, 0};
  float prevDist = 0;
  float prevAngle = 0;
  double prevTime = 0;
  float xyScale = 1.0f;
  float dblTapDragScale = 1.0f;
  struct PrevPinchPt { float dist; double t; };
  std::list<PrevPinchPt> prevDists;
  Timer* tapTimer = nullptr;

  enum { TOUCH_NONE, TOUCH_PINCH, TOUCH_ROTATE, TOUCH_SHOVE } multiTouchState;
  enum { TAP_NONE, DBL_TAP_DRAG_PENDING, DBL_TAP_DRAG_ACTIVE } tapState;
};
