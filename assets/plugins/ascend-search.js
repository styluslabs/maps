function ascendSearch(query, bounds, flags)
{
  // &bounded=1 limits results to viewbox; otherwise results outside are possible
  const bndstr = bounds.map(function(v) { return v.toFixed(6); }).join(',');
  const sortstr = (flags & 0x4) ? "dist" : "auto";
  //tiles-b.styluslabs.com / localhost
  const url = "http://tiles-b.styluslabs.com:8080/search?bounds=" + bndstr + "&sort=" + sortstr + "&limit=50&q=" + encodeURIComponent(query);
  httpRequest(url, "", function(_content, _error) {
    if(!_content) { notifyError("search", "Ascend online search error"); return; }
    const content = JSON.parse(_content);
    results = content.results ? content.results : content;
    if(results >= 50) { flags = flags | 0x8000; }  // MapSearch::MORE_RESULTS flag
    for(var ii = 0; ii < results.length; ii++) {
      const r = results[ii];
      addSearchResult(r.props.osm_id, r.lat, r.lng, r.score, flags, r.props);
      //if(ii == 0 && r.boundingbox) {
      //  const b = r.boundingbox;
      //  flags = flags & ~0x08;  // clear FLY_TO
      //  lookAt(b[2], b[0], b[3], b[1], 15);
      //}
    }
    addSearchResult(0, 0, 0, 0, flags | 0x4000, {});  // MapSearch::UPDATE_RESULTS flag
  });
}

registerFunction("ascendSearch", "search-unified-autocomplete", "Online Search");
