#pragma once

#include <vector>
#include "glm/vec2.hpp"

#ifndef GLM_FORCE_CTOR_INIT
#error "GLM_FORCE_CTOR_INIT is not defined!"
#endif

class MapsApp;

class TouchHandler
{
public:
  TouchHandler(MapsApp* _app) : app(_app) {}
  void touchEvent(int ptrId, int action, double t, float x, float y, float p);

  MapsApp* app;

  struct TouchPoint
  {
    int id;
    float x, y, p;
  };
  std::vector<TouchPoint> touchPoints;

  glm::vec2 prevCOM = {0, 0};
  glm::vec2 initCOM = {0, 0};
  float prevDist = 0;
  float prevAngle = 0;
  glm::vec2 flingV = {0, 0};
  double prevTime = 0;
  double initTime = 0;
  double prevTapTime = 0;
  bool canBeLongPress = false;

  enum {TOUCH_NONE, TOUCH_PINCH, TOUCH_ROTATE, TOUCH_SHOVE} multiTouchState;
};
