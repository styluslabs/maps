// Create an account at https://openrouteservice.org/dev/#/signup to obtain API key, then assign to
// secrets['openroute_auth'] in another js file loaded before this one (e.g. _secrets.js)

function openRouteService(mode, waypoints)
{
  if(!secrets.openroute_auth) {
    notifyError("route", "Open Route Service API key missing - set secrets['openroute_auth'] in plugins/_secrets.js");
    return;
  }

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
