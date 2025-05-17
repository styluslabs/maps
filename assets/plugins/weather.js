// weather plugin for place info

function weatherPlaceInfo(props, lng, lat)
{
  const alpurl = "https://www.alpineconditions.com/location/" + lat + "/" + lng + "/wx-forecast";
  addPlaceInfo("weather", "", "<a href='" + alpurl + "'><text>alpineconditions.com</text></a>");

  //tzCallback = function(_content, _error) {
  //  if(!_content) { return; }
  //  const content = JSON.parse(_content);
  //  const tzid = content["timezoneId"];
  //  const url = "https://spotwx.com/products/grib_index.php?model=gfs_pgrb2_0p25_f&lat=" + lat + "&lon=" + lng + "&tz=" + tzid;
  //  addPlaceInfo("weather", "", "<a href='" + url + "'><text>spotwx.com</text></a>");
  //}
  //const tzurl = "http://api.geonames.org/timezoneJSON?lat=" + lat + "&lng=" + lng + "&username=tangram";
  //httpRequest(tzurl, tzCallback);
}

registerFunction("weatherPlaceInfo", "place", "Weather");
