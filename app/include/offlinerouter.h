#pragma once

#include "mapscomponent.h"

// offline routing from z14 vector tiles (offlinerouter.cpp); result is passed to MapsTracks::addRoute() and
//  addRouteStep() on the main thread, or routePluginError() on failure
void offlineRoute(const std::string& routeMode, const std::vector<LngLat>& waypts);
// discard result of any pending offline route request
void cancelOfflineRoute();
