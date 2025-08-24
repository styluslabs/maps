// refs:
// - https://nasa-gibs.github.io/gibs-api-docs/
// - https://gibs.earthdata.nasa.gov/wmts/epsg3857/best/wmts.cgi?SERVICE=WMTS&request=GetCapabilities (to get TileMatrixSet values)

function nasaWorldView()
{
  const date = readSceneValue("global.worldview_date") || "2024-01-01";
  const tileUrl = "https://gitc-{s}.earthdata.nasa.gov/wmts/epsg3857/best/MODIS_Terra_CorrectedReflectance_TrueColor/default/" + date + "/GoogleMapsCompatible_Level9/{z}/{y}/{x}.jpg"

  const updates = {
    "global.worldview_date": date,
    "application.gui_variables.worldview_date":
        { "label": "Date", "type": "date", "onchange": "nasaWorldView", "reload": "false" },  // addMapSource will trigger reload
    "application.dark_base_map": true
  };
  const mapSrc = {
    "title": "NASA Worldview " + date,
    "description": "Daily 250m imagery",
    "url": tileUrl,
    "url_subdomains": ["a", "b", "c"],
    //"tile_size": 512,
    //"headers": headers,
    "max_zoom": 9,
    "cache": false,
    "updates": updates
  };
  addMapSource("nasa-worldview", mapSrc);
}

nasaWorldView();
