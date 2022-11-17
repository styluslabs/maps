#pragma once

#include "tangram.h"

using namespace Tangram;

class MapsApp;

class MapsComponent
{
public:
  MapsComponent(MapsApp* _app) : app(_app) {}
  MapsApp* app;
};
