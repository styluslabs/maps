function routeBRouter(mode, waypoints)
{
  const mode0 = mode.split("-")[0];
  const profile = mode0 == "walk" ? "hiking-mountain" : mode0 == "bike" ? "trekking" : "car-fast";
  var locations = [];
  for(var ii = 0; ii < waypoints.length; ++ii) {
    locations.push(waypoints[ii][0] + "," + waypoints[ii][1]);
  }

  const url = "https://brouter.de/brouter?lonlats=" + locations.join("|") + "&profile=" + profile + "&alternativeidx=0&format=gpx";

  httpRequest(url, "", function(content, error) {
    if(!content)
      notifyError("route", "bRouter error");
    else
      addRouteGPX(content);
  });
}

registerFunction("routeBRouter", "route", "BRouter");
