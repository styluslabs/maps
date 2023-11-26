// simple demo of adding map source from plugin

function waymarkedTrails()
{
  const tileUrl = "http://tile.waymarkedtrails.org/hiking/{z}/{x}/{y}.png";
  const yaml = "{ type: Raster, title: Waymarked Trails, url: \"" + tileUrl + "\" }";
  addMapSource("waymarked-hiking", yaml)
}

waymarkedTrails();
