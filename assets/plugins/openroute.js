// Create an account at https://openrouteservice.org/dev/#/signup to obtain API key, then assign to
// secrets['openroute_auth'] in another js file loaded before this one (e.g. _secrets.js)

function openRouteService(mode, waypoints)
{
  const auth = secrets.openroute_auth || readSceneValue("config.secrets.openroute_auth");
  if(!auth) {
    notifyError("route", "Open Route Service API key missing - set config.secrets.openroute_auth");
    return;
  }

  const mode0 = mode.split("-")[0];
  const profile = mode0 == "walk" ? "foot-hiking" : mode0 == "bike" ? "cycling-road" : "driving-car";  //cycling-regular, cycling-mountain
  const url = "https://api.openrouteservice.org/v2/directions/" + profile + "/gpx";
  const hdrs = "Content-Type: application/json\r\nAuthorization: " + auth;
  const body = {"coordinates": waypoints, "elevation": "true"};

  httpRequest(url, hdrs, JSON.stringify(body), function(content, error) {
    if(!content)
      notifyError("route", "Open Route Service error");
    else
      addRouteGPX(content);
  });
}

const auth = secrets.openroute_auth || readSceneValue("config.secrets.openroute_auth");
if(auth) { registerFunction("openRouteService", "route", "Open Route Service"); }
