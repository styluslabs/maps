// BRouter voice hint command (brouter-core VoiceHint.java; JSON index from Formatter.getJsonCommandIndex()) ->
//  route step maneuver keyword (see MapsTracks::addRouteStep); null: no step (continue straight - BRouter has no
//  street names, so these would just be icons at every junction; off route; beeline)
const brouter_turns = [
  null, null, "left", "slight_left", "sharp_left", "right", "slight_right", "sharp_right",  // 0 - 7
  "keep_left", "keep_right", "uturn", "uturn", null, "roundabout", "roundabout", "uturn", null,  // 8 - 16
  "slight_left", "slight_right"  // 17 - 18: exit left, exit right (turnInstructionMode 2 or 9 only)
];

function routeBRouter(mode, waypoints)
{
  const mode0 = mode.split("-")[0];
  const profile = mode0 == "walk" ? "hiking-mountain" : mode0 == "bike" ? "trekking" : "car-fast";
  var locations = [];
  for(var ii = 0; ii < waypoints.length; ++ii) {
    locations.push(waypoints[ii][0] + "," + waypoints[ii][1]);
  }

  // turnInstructionMode=2 (locus style) is needed for voicehints and gives exit left/right commands
  const url = "https://brouter.de/brouter?lonlats=" + locations.join("|") + "&profile=" + profile
      + "&alternativeidx=0&format=geojson&turnInstructionMode=2";

  httpRequest(url, "", function(content, error) {
    var feat = null;
    try { feat = JSON.parse(content).features[0]; } catch(e) {}
    if(!feat || !feat.geometry || !feat.geometry.coordinates) {
      // BRouter returns plain text error message, e.g. "target island detected for section 0"
      notifyError("route", "BRouter error" + (content && content.length < 200 ? ": " + content : ""));
      return;
    }
    const coords = feat.geometry.coordinates;
    const props = feat.properties || {};
    addRoutePoints(coords, props.times);
    addRouteStep("", 0, "start", "");
    const hints = props.voicehints || [];
    for(var ii = 0; ii < hints.length; ++ii) {
      // [index in track, command, roundabout exit number, distance to next, angle]
      const hint = hints[ii];
      const turn = hint[1] < brouter_turns.length ? brouter_turns[hint[1]] : null;
      if(turn)
        addRouteStep("", hint[0], turn, turn == "roundabout" && hint[2] ? "Exit " + Math.abs(hint[2]) : "");
    }
    addRouteStep("", coords.length - 1, "arrive", "destination");
  });
}

registerFunction("routeBRouter", "route", "BRouter");
