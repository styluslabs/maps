// web interface: https://valhalla.openstreetmap.de
// refs: https://github.com/valhalla/valhalla/blob/master/docs/docs/api/turn-by-turn/api-reference.md
// Valhalla format=gpx doesn't seem to support elevation

const valhalla_options = {
  "pedestrian":{
    "pedestrian":{
      "use_ferry":1,
      "use_living_streets":0.5,
      "use_tracks":0,
      "service_penalty":15,
      "service_factor":1,
      "shortest":false,
      "use_hills":0.5,
      "walking_speed":5.1,
      "walkway_factor":1,
      "sidewalk_factor":1,
      "alley_factor":2,
      "driveway_factor":5,
      "step_penalty":0,
      "max_hiking_difficulty":1,
      "use_lit":0,
      "transit_start_end_max_distance":2145,
      "transit_transfer_max_distance":800
    }
  },
  "bicycle":{
    "bicycle":{
      "maneuver_penalty":5,
      "country_crossing_penalty":0,
      "country_crossing_cost":600,
      "use_ferry":1,
      "use_living_streets":0.5,
      "service_penalty":15,
      "service_factor":1,
      "shortest":false,
      "bicycle_type":"Hybrid",
      "cycling_speed":20,
      "use_roads":0.5,
      "use_hills":0.5,
      "avoid_bad_surfaces":0.25,
      "gate_penalty":300,
      "gate_cost":30
    }
  },
  "auto":{
    "auto":{
      "maneuver_penalty":5,
      "country_crossing_penalty":0,
      "country_crossing_cost":600,
      "width":1.6,
      "height":1.9,
      "use_highways":1,
      "use_tolls":1,
      "use_ferry":1,
      "ferry_cost":300,
      "use_living_streets":0.5,
      "use_tracks":0,
      "private_access_penalty":450,
      "ignore_closures":false,
      "closure_factor":9,
      "service_penalty":15,
      "service_factor":1,
      "exclude_unpaved":1,
      "shortest":false,
      "exclude_cash_only_tolls":false,
      "top_speed":140,
      "fixed_speed":0,
      "toll_booth_penalty":0,
      "toll_booth_cost":15,
      "gate_penalty":300,
      "gate_cost":30,
      "include_hov2":false,
      "include_hov3":false,
      "include_hot":false,
      "disable_hierarchy_pruning":false
    }
  }
};

// maneuver type (valhalla repo docs/docs/api/route/api-reference.md, "kNone = 0") -> route step
//  maneuver keyword (see MapsTracks::addRouteStep); null: no step (continue along same road, roundabout exit, transit)
const valhalla_turns = [
  null, "start", "start", "start", "arrive", "arrive", "arrive", "continue", null,  // 0 - 8
  "slight_right", "right", "sharp_right", "uturn", "uturn", "sharp_left", "left", "slight_left",  // 9 - 16
  "continue", "slight_right", "slight_left", "slight_right", "slight_left",  // 17 - 21: ramps, exits
  "continue", "keep_right", "keep_left", "continue", "roundabout", null, "continue", "continue",  // 22 - 29
  null, null, null, null, null, null, null,  // 30 - 36: transit
  "keep_right", "keep_left", "continue", "continue", "continue", "continue", "continue"  // 37 - 43
];

function valhallaStep(steps, ii)
{
  const step = steps[ii];
  const turn = step.type < valhalla_turns.length ? valhalla_turns[step.type] : "continue";
  if(!turn) { return; }
  // for roundabout, use street of the following roundabout exit maneuver
  const names = turn == "roundabout" && ii + 1 < steps.length && steps[ii+1].type == 27 ?
      steps[ii+1].street_names : step.street_names;
  var street = names && names.length ? names[0] : "";
  if(turn == "roundabout" && step.roundabout_exit_count)
    street = "Exit " + step.roundabout_exit_count + (street ? ": " + street : "");
  else if(turn == "arrive" && !street)
    street = "destination";
  addRouteStep(step.instruction || "", step.begin_shape_index, turn, street);
}

function valhallaOsmde(mode, waypoints)
{
  const mode0 = mode.split("-")[0];
  const profile = mode0 == "walk" ? "pedestrian" : mode0 == "bike" ? "bicycle" : "auto";
  const url = "https://valhalla1.openstreetmap.de/route?json=";
  const hdrs = "";

  var locations = [];
  const nwpts = waypoints.length;
  for(var ii = 0; ii < nwpts; ++ii) {
    locations.push({"lon": waypoints[ii][0], "lat": waypoints[ii][1], "type": ii == 0 || ii == nwpts - 1 ? "break" : "via"});
  }

  const req = {
    "costing": profile,
    "costing_options": valhalla_options[profile],
    "locations": locations,
    "exclude_polygons":[],
    "directions_options":{"units":"kilometers"},
    "elevation_interval": 30,
    "id":"valhalla_directions"
  };

  httpRequest(url + encodeURIComponent(JSON.stringify(req)), hdrs, function(_content, _error) {
    if(!_content)
      notifyError("route", "Valhalla osm.de error");
    else {
      const content = JSON.parse(_content);
      const leg = content.trip.legs[0];
      addRoutePolyline(leg.shape, leg.elevation, leg.summary.time);
      const steps = leg.maneuvers;
      for(var ii = 0; ii < steps.length; ii++)
        valhallaStep(steps, ii);
    }
  });
}

registerFunction("valhallaOsmde", "route", "Valhalla osm.de");
