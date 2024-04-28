// refs:
// - https://documentation.dataspace.copernicus.eu/APIs/SentinelHub/OGC.html
// Create an account at https://dataspace.copernicus.eu/ then follow the directions at the above link to get
//  an instance id, which should be added as sentinel2_id in _secrets.js

function esaSentinel2()
{
  const maxcc = Math.round(Number(readSceneValue("global.sentinel2_clouds"))) || 100;
  const date = readSceneValue("global.sentinel2_date") || new Date().toISOString().slice(0, 10);  //"2024-01-01";
  const maxage = Number(readSceneValue("global.sentinel2_age")) || 7;
  const startdate = new Date(date);
  startdate.setDate(startdate.getDate() - maxage);

  // MAXCC is maximum allowable cloud cover (in percent)
  const tileUrl = "https://sh.dataspace.copernicus.eu/ogc/wmts/" + secrets.sentinel2_id + "?REQUEST=GetTile&TILEMATRIXSET=PopularWebMercator512&LAYER=NATURAL-COLOR&MAXCC=" + maxcc + "&TILEMATRIX={z}&TILEROW={y}&TILECOL={x}&TIME=" + startdate.toISOString().slice(0, 10) + "/" + date;

  const updates = {
    "global.sentinel2_date": date,
    "global.gui_variables.sentinel2_date": { "label": "Date", "type": "date", "onchange": "esaSentinel2" },
    "global.gui_variables.sentinel2_age": { "label": "Max age (days)", "min": "0", "max": "180", "onchange": "esaSentinel2" },
    "global.gui_variables.sentinel2_clouds": { "label": "Max cloud cover", "min": "0", "max": "100", "onchange": "esaSentinel2" },
  };
  const mapSrc = {
    "title": "Sentinel 2 " + date,
    "description": secrets.sentinel2_id ? "10m imagery updated 1-2 times per week" : "Error: plugin requires sentinel2_id!"
    "url": tileUrl,
    "zoom_offset": 1  //"tile_size": 512,  -- download fewer tiles since usage is limited
    "max_zoom": 14,  // for 10m resolution
    "cache": false,
    "updates": updates
  };
  addMapSource("esa-sentinel2", mapSrc);
}

esaSentinel2();
