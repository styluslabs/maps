// Sentinel-2 imagery - 10m resolution with 5 day revisit period
// Create a free account (30K req/month) at https://dataspace.copernicus.eu/ then follow the directions at
//  https://documentation.dataspace.copernicus.eu/APIs/SentinelHub/OGC.html (choose "Simple WMS Instance") to
//  get an instance id, which should be added as secrets["sentinel2_id"] in _secrets.js
// - note that other layers are available, e.g., vegetation, and custom layers can be created in the dashboard
// More:
// - https://s2maps.eu/ - yearly cloudless tiles
// - https://docs.terrascope.be/#/Developers/WebServices/OGC/WMTS - free, slow, Europe only
// - https://wiki.openstreetmap.org/wiki/Sentinel-2
// - https://github.com/kr-stn/awesome-sentinel
// - https://planetarycomputer.microsoft.com/dataset/sentinel-2-l2a
// - https://developers.google.com/earth-engine/datasets/catalog/COPERNICUS_S2_SR_HARMONIZED

function esaSentinel2()
{
  const maxcc = Math.round(Number(readSceneValue("global.sentinel2_clouds"))) || 100;
  const date = readSceneValue("global.sentinel2_date") || new Date().toISOString().slice(0, 10);  //"2024-01-01";
  const maxage = Number(readSceneValue("global.sentinel2_age")) || 10;
  const startdate = new Date(date);
  startdate.setDate(startdate.getDate() - maxage);

  // MAXCC is maximum allowable cloud cover (in percent)
  // PREVIEW=2 allows for zoomed out view (instead of tiles with error text!)
  // default FORMAT is PNG with missing pixels transparent (vs. black for jpeg)
  // LAYER=NATURAL-COLOR uses L1C data; TRUE-COLOR-S2L2A uses L2A which adds an atmospheric correction
  //const tileUrl = "https://sh.dataspace.copernicus.eu/ogc/wmts/" + secrets.sentinel2_id + "?REQUEST=GetTile&TILEMATRIXSET=PopularWebMercator256&LAYER=TRUE-COLOR-S2L2A&FORMAT=image/jpeg&PREVIEW=2&MAXCC=" + maxcc + "&TILEMATRIX={z}&TILEROW={y}&TILECOL={x}&TIME=" + startdate.toISOString().slice(0, 10) + "/" + date;

  const sentinel2_id = secrets.sentinel2_id || readSceneValue("config.secrets.sentinel2_id");
  // Sentinel Hub WMTS PopularWebMercator512 needs TILEMATRIX={z+1} !  PopularWebMercator256 would work but
  //  has a 4:1 request to processing unit ratio.
  // 512x512 = 1 processing unit credit, so no advantage to requesting larger image
  const tileUrl = "https://sh.dataspace.copernicus.eu/ogc/wms/" + sentinel2_id + "?REQUEST=GetMap&LAYERS=TRUE-COLOR-S2L2A&PREVIEW=2&MAXCC=" + maxcc + "&FORMAT=image/jpeg&VERSION=1.3.0&WIDTH=512&HEIGHT=512&CRS=CRS:84&BBOX={bbox}&TIME=" + startdate.toISOString().slice(0, 10) + "/" + date;

  const updates = {
    "global.sentinel2_date": date,
    "global.sentinel2_age": maxage,
    "global.sentinel2_clouds": maxcc,
    "application.gui_variables.sentinel2_date": { "label": "Date", "type": "date", "onchange": "esaSentinel2" },
    "application.gui_variables.sentinel2_age": { "label": "Max age (days)", "type": "int", "min": "0", "max": "180", "onchange": "esaSentinel2" },
    "application.gui_variables.sentinel2_clouds": { "label": "Max cloud cover (%)", "type": "int", "min": "0", "max": "100", "onchange": "esaSentinel2" },
    "application.dark_base_map": true
  };
  const mapSrc = {
    "title": "Sentinel-2 " + date,
    "description": sentinel2_id ? "Weekly 10m imagery" : "Set secrets.sentinel2_id to use!",
    "url": sentinel2_id ? tileUrl : "",
    "zoom_offset": 1,  //"tile_size": 512,  -- download fewer tiles since usage is limited
    "max_zoom": 14,  // for 10m resolution
    "cache": false,
    "updates": updates,
    "attribution": "Copernicus Sentinel data 2024"
  };
  addMapSource("esa-sentinel2", mapSrc);
}

esaSentinel2();
