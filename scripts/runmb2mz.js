// Mapbox-GL style to Tangram scene converter - based on https://gitlab.com/stevage/mapbox2tangram
// edit this file as needed for style being converted

var mb2mz = require('./mb2mz');
var toyaml = require('./toyaml');

function processStyle(filename, fixup, outname='out')
{
  function copy(o) { return JSON.parse(JSON.stringify(o)); }
  //let style =schemaCross.convertSchema('mapbox', 'mapzen', require(filename));
  let style = require(filename);
  // first copy avoids messing up source object. second strips out undefined values.
  return copy(mb2mz.toTangram(copy(style), { globalColors: true }));
}

function saveYaml(obj, outname)
{
  let yamlout = toyaml.dump(obj, {extraLines: 2, flowLevel: 8,
      alwaysFlow: ['size', 'width', 'dash', 'buffer', 'offset', 'placement', 'alpha', 'data']});
  //require('js-yaml').safeDump(output, {"flowLevel": 5})
  require('fs').writeFileSync(outname, yamlout);
  console.log(`Wrote ${outname}`);
  //require('fs').writeFileSync('./compare/in.json', JSON.stringify(style,undefined,4));
}


// OMT schema style from https://github.com/openmaptiles/osm-bright-gl-style
// - also look into https://github.com/elastic/osm-bright-desaturated-gl-style
function fix_osm_bright(obj)
{
  for (var key in obj) {
    if(key.startsWith("landuse-") || key.startsWith("landcover-")) {
      if(obj[key].draw && obj[key].draw.polygons) {
        obj[key].enabled = "global.show_polygons";
        //if(obj[key].draw.polygons.style == "polygons-inlay")
        //  obj[key].draw.polygons.style = "global.earth_inlay_style";
        //else
          obj[key].draw.polygons.style = "global.earth_style";
      }
    } else if(key.startsWith("poi-level-")) {
      if(obj[key].draw && obj[key].draw.points && obj[key].draw.points.text) {
        obj[key].draw.points.interactive = true;
        obj[key].draw.points.text.interactive = true;
      }
    } else if(key == "text_source") {
      if(obj[key] == "name:latin")
        obj[key] = "global.latin_name";
      else if(obj[key] == "name:latin\nname:nonlatin")
        obj[key] = "global.names_two_lines";
      else if(obj[key] == "name:latin name:nonlatin")
        obj[key] = "global.names_one_line";
    } else if(key == "family" && obj[key].length > 0) {
      if(obj[key][0].endsWith("Italic"))
        obj["style"] = "italic";
      if(obj[key][0].endsWith("Bold"))
        obj["weight"] = "bold";
      if(obj[key][0].startsWith("Noto Sans"))
        obj[key] = "global.font_sans";
    } else if(key == "sprite") {
      obj["texture"] = "osm-bright";
      var s = obj["sprite"];
      // currently only used for sprite, but we could check all values if needed
      if(s.match(/\{\w+\}/)) {
        obj["sprite"] = "function() { return '" + s.replace(/\{(\w+)\}/g, "' + feature.$1 + '") + "'; }";
      }
    }
    if(obj[key] !== null && typeof(obj[key])=="object") {
      fix_osm_bright(obj[key]);
    }
  }
}

let obj = processStyle('../../osm-bright-gl-style/style.json');
fix_osm_bright(obj);
// osm-bright.svg generated with `svgconcat icons/*.svg`
obj.lights = { light1: { type: "directional", origin: "world", direction: [1, 1, -1], diffuse: 0.5, ambient: 0.7 } };
obj.textures = { "osm-bright": { url: "img/osm-bright.svg", density: 2 } };
obj.fonts = { "Noto Sans": [
  { url: "fonts/NotoSans-Regular.ttf" },
  { style: "italic", url: "fonts/NotoSans-Italic.ttf" },
  { weight: 600, url: "fonts/NotoSans-SemiBold.ttf" }
]}
obj.layers.building.draw.extrusion = {
  filter: { "$zoom": { min: 15 } },
  draw: { polygons: { extrude: ["render_min_height", "render_height"] } }
};
obj.global.earth_style = "polygons"
obj.global.elevation_sources = [];
obj.global.show_polygons = "true";
obj.global.font_sans = "Noto Sans";
obj.global.latin_name = "function() { return feature['name:latin'] || feature.name_en || feature.name; }";
obj.global.names_two_lines =
    "function() { const nl = feature['name:nonlatin']; return global.latin_name() + (nl ? '\n' + nl : ''); }";
obj.global.names_one_line =
    "function() { const nl = feature['name:nonlatin']; return global.latin_name() + (nl ? ' ' + nl : ''); }";
saveYaml(obj, '../scenes/osm-bright.yaml');


// Shortbread schema style from https://github.com/versatiles-org/versatiles-style
//function fix_colorful(obj) { }
//
//let obj = processStyle('./colorful.json');
//fix_colorful(obj);
//obj.sources["versatiles-shortbread"] = {type: "MVT", max_zoom: 14,
//    url: "https://tiles.versatiles.org/tiles/osm/{z}/{x}/{y}"};
//
//saveYaml(obj, 'sb-colorful');
