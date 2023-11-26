// secrets.openroute_auth should be set in another js file loaded as plugin (and not committed to git)

function openRouteService(mode, waypoints)
{
  const mode0 = mode.split("-")[0];
  const profile = mode0 == "walk" ? "foot-hiking" : mode0 == "bike" ? "cycling-regular" : "driving-car";
  const url = "https://api.openrouteservice.org/v2/directions/" + profile + "/gpx";
  const hdrs = "Content-Type: application/json\r\nAuthorization: " + secrets.openroute_auth;
  const body = {"coordinates": waypoints, "elevation": "true"};

  httpRequest(url, hdrs, JSON.stringify(body), function(content, error) {
    if(!content)
      notifyError("route", "Open Route Service error");
    else
      addRouteGPX(content);
  });
}

registerFunction("openRouteService", "route", "Open Route Service");
