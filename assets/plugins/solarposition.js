// from https://github.com/mourner/suncalc/blob/master/suncalc.js
// - single fn version: https://observablehq.com/@mourner/sun-position-in-900-bytes

var PI   = Math.PI,
    sin  = Math.sin,
    cos  = Math.cos,
    tan  = Math.tan,
    asin = Math.asin,
    atan = Math.atan2,
    acos = Math.acos,
    rad  = PI / 180;

// sun calculations are based on http://aa.quae.nl/en/reken/zonpositie.html formulas

// date/time constants and conversions

var dayMs = 1000 * 60 * 60 * 24,
    J1970 = 2440588,
    J2000 = 2451545;

function toJulian(date) { return date.valueOf() / dayMs - 0.5 + J1970; }
function fromJulian(j)  { return new Date((j + 0.5 - J1970) * dayMs); }
function toDays(date)   { return toJulian(date) - J2000; }

// general calculations for position

var e = rad * 23.4397; // obliquity of the Earth

function rightAscension(l, b) { return atan(sin(l) * cos(e) - tan(b) * sin(e), cos(l)); }
function declination(l, b)    { return asin(sin(b) * cos(e) + cos(b) * sin(e) * sin(l)); }

function azimuth(H, phi, dec)  { return atan(sin(H), cos(H) * sin(phi) - tan(dec) * cos(phi)); }
function altitude(H, phi, dec) { return asin(sin(phi) * sin(dec) + cos(phi) * cos(dec) * cos(H)); }

function siderealTime(d, lw) { return rad * (280.16 + 360.9856235 * d) - lw; }

// general sun calculations

function solarMeanAnomaly(d) { return rad * (357.5291 + 0.98560028 * d); }

function eclipticLongitude(M) {

    var C = rad * (1.9148 * sin(M) + 0.02 * sin(2 * M) + 0.0003 * sin(3 * M)), // equation of center
        P = rad * 102.9372; // perihelion of the Earth

    return M + C + P + PI;
}

function sunCoords(d) {

    var M = solarMeanAnomaly(d),
        L = eclipticLongitude(M);

    return {
        dec: declination(L, 0),
        ra: rightAscension(L, 0)
    };
}

// calculates sun position for a given date and latitude/longitude
function solarPosition(date, lat, lng)
{
    var lw  = rad * -lng,
        phi = rad * lat,
        d   = toDays(date),

        c  = sunCoords(d),
        H  = siderealTime(d, lw) - c.ra;

    return {
        azimuth: azimuth(H, phi, c.dec),
        altitude: altitude(H, phi, c.dec)
    };
};

// plugin fns
// supported time formats: HH:MM[:SS][Z|+/-hhmm]

function updateSun()
{
  const colorstr = readSceneValue("global.solar_color");
  const timestr = readSceneValue("global.solar_time");
  const datestr = readSceneValue("global.solar_date");
  const date = Date.parse(datestr + "T" + timestr);

  const cam = getCameraPosition();
  const sun = solarPosition(date, cam.latitude, cam.longitude);
  const az = sun.azimuth, el = sun.altitude;
  if(cam.zoom < 10 || el < 0) { colorstr = "#000000"; }  // below horizon?
  const lgt = { "direction": [ sin(az)*cos(el), cos(az)*cos(el), -sin(el) ], "diffuse": colorstr };
  updateSceneLight("light2", lgt);
}

function onMapEvent_Solar(event) { if(event == 1) { updateSun(); } }

function createSolarSource()
{
  const nowstr = new Date().toISOString();
  const updates = {
    "global.solar_color": "#C00000",  //"#333333",
    "global.solar_date": nowstr.slice(0, 10),  //"2024-01-01"
    "global.solar_time": nowstr.slice(11),
    "application.gui_variables.solar_color": { label: "Sun color", type: "color", onchange: "updateSun", reload: false },
    "application.gui_variables.solar_date": { label: "Date", type: "date", onchange: "updateSun", reload: false },
    "application.gui_variables.solar_time": { label: "Time (HH:MM[+/-hhmm])", type: "time", onchange: "updateSun", reload: false },
    "application.on_map_event": "onMapEvent_Solar",
    "lights.light2.origin": "camera"  // explicitly set origin to make light dynamic
  };
  const mapSrc = {
    "title": "Solar position",
    "description": "Does not show shadows",
    "archived": true,
    "layer": true,
    "layers": [ "hillshade" ],
    "updates": updates
  };
  addMapSource("solar-pos", mapSrc);
}

createSolarSource();
