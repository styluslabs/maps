// For now, have to set filename and list name by editing plugin code; depending on requirements for other plugins,
//  we could consider adding ability for plugin to create custom UI, e.g.,
//addUIElement("file_picker", "Input file:", "file");  addUIElement("text_input", "List:", "list");  ...
//createPluginUI('<g class="horz_layout"><g id="file" class="file_picker"/><g id="list" class="text_input"/></g>');

// importGooglePlaces("file:///home/mwhite/maps/Reviews.json", "Reviews")

function importGooglePlaces(url, list)
{
  httpRequest(url, function(_places) {
    const places = JSON.parse(_places);
    for(var ii = 0; ii < places.features.length; ii++) {
      const r = places.features[ii];
      const loc = r.properties["Location"];
      if(!loc) { continue; }  // some places are missing Location (and have lat,lng = 0,0)
      const lnglat = r.geometry.coordinates;
      const note = r.properties["Star Rating"] + "* " + r.properties["Review Comment"];
      //const props = { "name": loc["Business Name"] };
      const name = loc["Business Name"];
      const date = Date.parse(r.properties["Published"])/1000;
      addBookmark(list, "none", name, {}, note, lnglat[0], lnglat[1], date);
    }
  });
}

registerFunction("importGooglePlaces", "command", "importGooglePlaces(<path to json file>, <name of bookmark list>");
