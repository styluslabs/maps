function nominatimSearch(query, bounds, flags)
{
  // &bounded=1 limits results to viewbox; otherwise results outside are possible
  const url = "https://nominatim.openstreetmap.org/search?format=jsonv2&viewbox=" + bounds.join() + "&limit=50&q=" + encodeURIComponent(query);
  httpRequest(url, "", function(_content, _error) {
    if(!_content) { notifyError("search", "Nominatim Search error"); return; }
    const content = JSON.parse(_content);
    if(content.length >= 50) { flags = flags | 0x8000; }  // MapSearch::MORE_RESULTS flag
    for(var ii = 0; ii < content.length; ii++) {
      const r = content[ii];
      const tags = {"name": r.display_name, [r.category]: r.type};
      if(ii == content.length - 1) { flags = flags | 0x4000; } // MapSearch::UPDATE_RESULTS flag
      addSearchResult(r.osm_id, r.lat, r.lon, r.importance, flags, tags);
    }
  });
}

registerFunction("nominatimSearch", "search-unified", "Nominatim Search");
