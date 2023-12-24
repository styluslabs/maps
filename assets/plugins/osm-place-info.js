//<html> <script type="text/javascript">

// Populate place info from OSM tags from API

//function addPlaceInfo(icon, title, value) { console.log(title + ": " + value); }
//function httpRequest(url, hdrs, callback) { if(!callback) callback = hdrs; fetch(url).then(res => res.text()).then(j => callback(j)); }

// from https://github.com/osmlab/jsopeninghours
// - see https://wiki.openstreetmap.org/wiki/Key:opening_hours for more sophisticated (and far more complex) parsers
function parseOpeningHours(text)
{
  const days = ['mo', 'tu', 'we', 'th', 'fr', 'sa', 'su'];
  var result = [[], [], [], [], [], [], []];
  var dayregex = /^(mo|tu|we|th|fr|sa|su)\-?(mo|tu|we|th|fr|sa|su)?$/,
      timeregex = /^\s*(\d\d:\d\d)\-(\d\d:\d\d)\s*$/,
      dtranges = text.toLowerCase().split(/\s*;\s*/),
      dtrange;
  if(dtranges.length == 0) { return null; }
  while((dtrange = dtranges.shift())) {
    var dtclean = dtrange.trim().replace(/\s*,\s*/, ",");
    var daytimes = dtclean.split(/\s+/),
        opendays = [null, null, null, null, null, null, null],
        dayrange,
        timerange;

    if(daytimes.length == 0) { return null; }
    else if(daytimes.length == 1) { daytimes = ["mo-su", daytimes[0]]; }
    var dayranges = daytimes[0].split(",");
    while((dayrange = dayranges.shift())) {
      if(dayrange == "ph") { break; }
      var daymatches = dayrange.match(dayregex);
      if (daymatches && daymatches.length === 3) {
        var startday = days.indexOf(daymatches[1]);
        var endday = daymatches[2] ? days.indexOf(daymatches[2]) : startday;
        if(endday < startday) { endday += 7; }
        for (var j = startday; j <= endday; j++) {
          opendays[j%7] = true;
        }
      } else {
        return null;
      }
    }

    var timeranges = daytimes[1].split(",");
    while((timerange = timeranges.shift())) {
      if(timerange == "off") { break; }
      var timematches = timerange.match(timeregex);
      if (timematches && timematches.length === 3) {
        var starttime = timematches[1];
        var endtime = timematches[2];
        for (var j = 0; j < opendays.length; j++) {
          if(opendays[j]) {
            result[j].push([starttime, endtime]);
          }
        }
      } else {
        return null;
      }
    }
  }
  return result;
}

function to12H(hm)
{
  var h = parseInt(hm);
  if(h == 0 || h == 24) { return "12" + hm.slice(-3) + " AM"; }
  if(h > 24) { h -= 24; }
  return (h > 12 ? h - 12 : h) + hm.slice(-3) + (h >= 12 ? " PM" : " AM");
}

const popIcon = '<svg xmlns="http://www.w3.org/2000/svg" width="1em" height="1em" viewBox="0 0 24 24"><path fill="none" stroke="currentColor" stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M5 7a4 4 0 1 0 8 0a4 4 0 1 0-8 0M3 21v-2a4 4 0 0 1 4-4h4a4 4 0 0 1 4 4v2m1-17.87a4 4 0 0 1 0 7.75M21 21v-2a4 4 0 0 0-3-3.85"/></svg>';

function wikiDataCb(_content, _error)
{
  if(!_content) {
    if(_error.substr(-3) != "410") {
      notifyError("place", "Wikidata place info error");
    }
    return;
  }
  const content = JSON.parse(_content);
  const entities = content["entities"] || {};
  const data = entities[Object.keys(entities)[0]];
  if(!data) return;

  try {
    const url = data["sitelinks"]["enwiki"]["url"];
    const title = data["sitelinks"]["enwiki"]["title"];
    addPlaceInfo("wikipedia", "Wikipedia", "<a href='" + url + "'><text>" + title + "</text></a>");
  } catch(e) {}

  try {
    const addr = data["claims"]["P6375"][0]["mainsnak"]["datavalue"]["value"]["text"];
    addPlaceInfo("road", "Address", addr);
  } catch(e) {}

  try {
    const pop = data["claims"]["P1082"];
    for(var ii = 0; ii < pop.length; ii++) {
      if(pop[ii]["rank"] == "preferred") {
        const popval = Number(pop[ii]["mainsnak"]["datavalue"]["value"]["amount"]);
        addPlaceInfo(popIcon, "Population", popval.toLocaleString());
        break;
      }
    }
  } catch(e) {}
}

function osmPlaceInfoCb(_content, _error)
{
  if(!_content) {
    if(_error.substr(-3) != "410") {
      notifyError("place", "OpenStreetMap place info error");
    }
    return;
  }
  const content = JSON.parse(_content);
  //console.log(content);
  //try {
  const tags = content["elements"][0]["tags"];
  //} catch (err) { return; }

  // start a wikidata request if needed
  const wikidata = tags["wikidata"];
  if(wikidata) {
    const wdurl = "https://www.wikidata.org/w/api.php?action=wbgetentities&format=json&props=claims|sitelinks/urls&sitefilter=enwiki&ids=" + wikidata;
    httpRequest(wdurl, wikiDataCb);
  }

  if(tags["population"] && !wikidata) {
    // " (" + tags["population:date"] + ")"
    addPlaceInfo(popIcon, "Population", Number(tags["population"]).toLocaleString());
  }

  if(tags["cuisine"]) {
    const s = tags["cuisine"].replace(/_/g, " ").replace(/;/g, ", ");
    addPlaceInfo("food", "Cuisine", s[0].toUpperCase() + s.slice(1));
  }
  //tags["takeaway"] (yes, no, only)
  //tags["outdoor_seating"] (yes, no)

  // we'll assume wikidata has address if present
  if(tags["addr:street"] && !wikidata) {
    const hnum = tags["addr:housenumber"];
    const city = tags["addr:city"];
    const state = tags["addr:state"] || tags["addr:province"];
    const zip = tags["addr:postcode"];
    const addr = (hnum ? (hnum + " ") : "") + tags["addr:street"] + (city ? "\n" + city : "")
        + (state ? ", " + state : "") + (zip ? " " + zip : "");
    addPlaceInfo("road", "Address", addr);
  }

  const url = tags["website"];
  if(url) {
    const shorturl = url.split("://").slice(-1)[0];
    addPlaceInfo("globe", "Website", "<a href='" + url + "'><text>" + shorturl + "</text></a>");
  }

  if(tags["wikipedia"] && !wikidata) {
    const wikiurl = "https://wikipedia.org/wiki/" + encodeURI(tags["wikipedia"]);
    const title = tags["wikipedia"].substr(3);
    addPlaceInfo("wikipedia", "Wikipedia", "<a href='" + wikiurl + "'><text>" + title + "</text></a>");
  }

  if(tags["phone"]) {
    addPlaceInfo("phone", "Phone", "<a href='tel:" + tags["phone"] + "'><text>" + tags["phone"] + "</text></a>");
  }

  const hourstag = tags["opening_hours"];
  if(hourstag) {
    //console.log(tags["opening_hours"]);
    if(hourstag == "24/7") {
      addPlaceInfo("clock", "Hours", "Open 24 hours");
    } else if(hourstag == "sunrise-sunset") {
      addPlaceInfo("clock", "Hours", "Open sunrise to sunset");
    } else {
      var hours = parseOpeningHours(hourstag);
      // a common error is using , instead of ;, so try working around that
      if(!hours && hourstag.indexOf(";") < 0 && hourstag.indexOf(",") > 0) {
        hours = parseOpeningHours(hourstag.replace(/,/g, ";"));
      }
      if(!hours) {
        addPlaceInfo("clock", "Hours", hourstag);
      } else {
        //console.log(hours);
        const days = ["Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"];
        const now = new Date();
        // change from Sunday = 0 to Monday = 0
        const today = (now.getDay() + 6)%7;
        const nowhm = ("0" + now.getHours()).slice(-2) + ":" + now.getMinutes();

        var state = "";
        const yd = hours[(today+6)%7];
        if(yd.length) {
          const ydopen = yd.slice(-1)[0][0];
          const ydclose = yd.slice(-1)[0][1];
          const nowhm24 = (24 + now.getHours()) + ":" + now.getMinutes();
          if((ydclose < ydopen && nowhm < ydclose) || nowhm24 < ydclose) {
            state = "Open until " + to12H(ydclose);
          }
        }
        if(!state) {
          for(var jj = 0; jj < hours[today].length; jj++) {
            const h = hours[today][jj];
            if(nowhm < h[0]) {
              state = "Closed until " + to12H(h[0]);
              break;
            } else if (nowhm < h[1]) {
              state = "Open until " + to12H(h[1]);
              break;
            }
          }
        }
        if(!state) {
          for (var ii = 1; ii < 7; ii++) {
            if(hours[(today+ii)%7].length) {
              const day = ii == 1 ? "tomorrow" : days[(today+ii)%7];
              state = "Closed until " + day + " " + to12H(hours[(today+ii)%7][0][0]);
              break;
            }
          }
        }

        var daily = [];
        // TODO: use \t for alignment!
        for (var ii = 0; ii < hours.length; ii++) {
          var t = [];
          for (var jj = 0; jj < hours[ii].length; jj++) {
            t.push( to12H(hours[ii][jj][0]) + " - " + to12H(hours[ii][jj][1]) );
          }
          daily.push( days[ii] + "  " + (t.length ? t.join(", ") : "Closed") );
        }

        addPlaceInfo("clock", "Hours", state + "\r" + daily.join("\n"));
      }
    }
  }
}

function wikiExtractCb(_content, _error)
{
  if(!_content) {
    if(_error) { notifyError("place", "Wikipedia extract error: " + _error); }
    return;
  }
  const content = JSON.parse(_content);
  const res = content["query"]["pages"];
  const extract = res[Object.keys(res)[0]]["extract"];  // key is page id even if querying by title
  addPlaceInfo("", "Summary", "<text class='wrap-text' font-size='12'>" + extract + "</text>");
}

function osmPlaceInfo(osmid)
{
  if(osmid.startsWith("wiki:")) {
    const url = "https://en.wikipedia.org/w/api.php?format=json&action=query&prop=extracts&exintro&explaintext&redirects=1&titles=" + osmid.substr(5);
    httpRequest(url, wikiExtractCb);
  } else {
    const url = "https://www.openstreetmap.org/api/0.6/" + osmid.replace(":", "/") + ".json";
    httpRequest(url, osmPlaceInfoCb);
  }
}

registerFunction("osmPlaceInfo", "place", "OpenStreetMap");

/* Maybe something like this to format opening hours?
<g layout="flex" flex-direction="row" box-anchor="hfill"/>
  <g layout="flex" flex-direction="column" box-anchor="left"/>
    <text box-anchor="left" margin="0 10"></text>
    ...
  </g>
  <g layout="flex" flex-direction="column" box-anchor="left"/>
    <text box-anchor="left" margin="0 10"></text>
    ...
  </g>
</g>
*/

//osmPlaceInfo("node:9203665041");
//</script> <head/> <body/> </html>
