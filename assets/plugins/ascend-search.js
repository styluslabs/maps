var g_AscendSearchOffset = 0;

function ascendSearch(query, bounds, flags)
{
  // &bounded=1 limits results to viewbox; otherwise results outside are possible
  const listsearch = (flags & 0x2);
  const bndstr = bounds.map(function(v) { return v.toFixed(6); }).join(',');
  const sortstr = (flags & 0x4) ? "dist" : "auto";  // MapsSearch::SORT_BY_DIST
  const acstr = (flags & 0x20) ? 1 : 0;  // MapsSearch::AUTOCOMPLETE
  const limit = listsearch ? 50 : 100;
  if(listsearch && !(flags & 0x10)) { g_AscendSearchOffset = 0; }  // MapsSearch::NEXTPAGE
  //tiles-b.styluslabs.com / localhost
  const url = "http://tiles-b.styluslabs.com:8080/search?bounds=" + bndstr + "&autocomplete=" + acstr +
      "&bounded=" + (listsearch ? 0 : 1) + "&sort=" + sortstr + "&offset=" +
      (listsearch ? g_AscendSearchOffset : 0) + "&limit=" + limit + "&q=" + encodeURIComponent(query);
  httpRequest(url, "", function(_content, _error) {
    if(!_content) { notifyError("search", "Ascend online search error"); return; }
    const content = JSON.parse(_content);
    results = content.results ? content.results : content;
    if(results.length >= limit) { flags = flags | 0x8000; }  // MapsSearch::MORE_RESULTS flag
    if(listsearch) { g_AscendSearchOffset += results.length; }
    for(var ii = 0; ii < results.length; ii++) {
      const r = results[ii];
      addSearchResult(r.props.osm_id, r.lat, r.lng, r.score, flags, r.props);
    }
    addSearchResult(0, 0, 0, 0, flags | 0x4000, {});  // MapsSearch::UPDATE_RESULTS flag
  });
}

registerFunction("ascendSearch", "search-unified-autocomplete-more", "Online Search");
