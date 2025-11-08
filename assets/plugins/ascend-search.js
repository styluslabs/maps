function ascendSearch(query, bounds, flags)
{
  // &bounded=1 limits results to viewbox; otherwise results outside are possible
  const url = "http://localhost:8080/search?bounds=" + bounds.join() + "&limit=50&q=" + encodeURIComponent(query);
  httpRequest(url, "", function(_content, _error) {
    if(!_content) { notifyError("search", "Ascend online search error"); return; }
    const content = JSON.parse(_content);
    if(content.length >= 50) { flags = flags | 0x8000; }  // MapSearch::MORE_RESULTS flag
    for(var ii = 0; ii < content.length; ii++) {
      const r = content[ii];
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
