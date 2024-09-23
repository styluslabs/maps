function escapeRegex(s)
{
  return s.replace(/[/\-\\^$*+?.()|[\]{}]/g, '\\$&');
}

function overpassSearch(query, bounds, flags)
{
  const url = "https://overpass-api.de/api/interpreter";
  // bounds provided as left,bottom,right,top
  const q = escapeRegex(query).replace(/[ ]+/g, ".+");
  const body = '[out:json][timeout:5][bbox:' + [bounds[1],bounds[0],bounds[3],bounds[2]].join(",") +
      ']; ( node[~".+"~"' + q + '"]; ); out body 100;'

  console.log(body);
  httpRequest(url, "", body, function(_content, _error) {
    if(!_content) { notifyError("search", "Overpass Search error"); return; }
    const content = JSON.parse(_content);
    const elements = content.elements;
    if(elements.length >= 100) { flags = flags | 0x8000; }  // MapSearch::MORE_RESULTS flag
    for(var ii = 0; ii < elements.length; ii++) {
      const r = elements[ii];
      addSearchResult(r.id, r.lat, r.lon, elements.length-ii, flags, r.tags);
    }
    addSearchResult(0, 0, 0, 0, flags | 0x4000, {});  // MapSearch::UPDATE_RESULTS flag
  });
}

registerFunction("overpassSearch", "search-unified", "Overpass Search");
