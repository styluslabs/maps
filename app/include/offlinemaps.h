#pragma once

#include "mapscomponent.h"

class MapsOffline : public MapsComponent
{
public:
  using MapsComponent::MapsComponent;
  ~MapsOffline();
  void showGUI();
};
