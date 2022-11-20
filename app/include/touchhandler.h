#pragma once

#include <vector>
#include "glm/vec2.hpp"

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

  static constexpr float flingInvTau = 1/0.05f;  // time constant for smoothing of pan speed
  static constexpr float pinchThreshold = 60;  // pixels (change in distance between fingers)
  static constexpr float rotateThreshold = 0.25f;  // radians
  static constexpr float shoveThreshold = 60;  // pixels (translation of centroid of fingers)

  static constexpr float maxTapDist = 20;  // max pixels between start and end points for a tap
  static constexpr float minFlingDist = 150;
  static constexpr double maxTapTime = 0.25;  // seconds
  static constexpr double minDblTapTime = 0.04;  // min time between end of first tap and start of second
  static constexpr double maxDblTapTime = 0.25;  // max time between end of first tap and start of second
  static constexpr double minFlingTime = 0.25;
  static constexpr double minLongPressTime = 0.7;  // 0.5s is typical on Android
};
