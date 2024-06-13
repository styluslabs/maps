//<html> <script type="text/javascript">

// Populate place info from OSM tags from API

//function addPlaceInfo(icon, title, value) { console.log(title + ": " + value); }
//function httpRequest(url, hdrs, callback) { if(!callback) callback = hdrs; fetch(url).then(res => res.text()).then(j => callback(j)); }

// get POI name and type (fn name is hardcoded in app)
function getPlaceType(_props)
{
  if(!_props) return "";
  const props = JSON.parse(_props);
  var type = props["tourism"] || props["leisure"] || props["amenity"] || props["historic"] || props["shop"] || props["place"] || props["railway"] || props["natural"];
  if(!type) return "";
  type = type.replace(/_/g, " ");
  return type.charAt(0).toUpperCase() + type.slice(1);
}

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

const osmIcon = '<svg width="1em" height="1em" viewBox="0 0 32 32" xmlns="http://www.w3.org/2000/svg"><path fill="currentColor" d="M3.563 31.959c-0.469-0.12-0.713-0.313-1.964-1.557-1.484-1.484-1.577-1.631-1.572-2.401 0-0.86-0.267-0.552 4.489-5.317 4.796-4.808 4.552-4.6 5.416-4.6 0.407 0 0.505 0.021 0.828 0.188l0.371 0.188 2.323-2.323-0.292-0.459c-0.563-0.885-1.099-2.172-1.339-3.24-0.677-3.016 0.052-6.172 1.975-8.588 3.317-4.156 9.276-5.084 13.676-2.115 2.417 1.625 3.991 4.129 4.439 7.083 0.093 0.625 0.093 2.104 0 2.767-0.297 2.228-1.333 4.296-2.939 5.869-0.744 0.771-1.624 1.407-2.593 1.869-1.276 0.641-2.364 0.943-3.839 1.068-2.099 0.177-4.427-0.407-6.208-1.547l-0.459-0.292-2.328 2.323 0.188 0.376c0.167 0.323 0.187 0.421 0.187 0.823 0.005 0.864 0.209 0.62-4.567 5.396-3.683 3.676-4.251 4.219-4.532 4.348-0.359 0.167-0.916 0.229-1.26 0.141zM23.312 18.599c2.672-0.495 4.953-2.235 6.141-4.677 1.853-3.869 0.604-8.411-2.975-10.812-0.848-0.573-2.036-1.057-3.156-1.281-0.771-0.156-2.411-0.14-3.219 0.032-1.172 0.244-2.287 0.728-3.265 1.421-0.593 0.416-1.573 1.396-1.984 1.979-2.292 3.209-2.084 7.573 0.505 10.543 1.337 1.541 3.187 2.552 5.203 2.848 0.677 0.104 2.057 0.079 2.755-0.047zM18.328 28.432c-0.979-0.271-1.953-0.563-2.927-0.864-0.032-0.037 1.609-5.865 1.677-5.932 0.031-0.037 5.656 1.536 5.885 1.645 0.083 0.036-0.005 0.385-0.704 2.801-0.281 0.996-0.563 1.991-0.844 2.985-0.036 0.115-0.093 0.208-0.129 0.208-0.036-0.005-1.369-0.385-2.959-0.844zM22.213 29.136c0-0.073 1.636-5.781 1.661-5.797 0.063-0.047 5.765-1.641 5.787-1.615 0.027 0.025-1.599 5.781-1.64 5.812-0.027 0.027-5.5 1.589-5.677 1.625-0.073 0.016-0.131 0-0.131-0.025zM12.74 26.584c1.124-1.136 2.129-2.168 2.229-2.303 0.228-0.292 0.541-0.975 0.629-1.355 0.037-0.156 0.073-0.468 0.079-0.692l0.016-0.412 0.239-0.068c0.141-0.036 0.261-0.057 0.271-0.047 0.032 0.037-1.609 5.761-1.661 5.813-0.025 0.020-0.901 0.285-1.953 0.583l-1.895 0.541 2.047-2.057zM23.792 22.505c-0.032-0.084-0.057-0.167-0.079-0.251l-0.063-0.208 0.209-0.036c1.785-0.303 3.703-1.141 5.213-2.281 0.213-0.167 0.333-0.229 0.353-0.183 0.068 0.219 0.36 1.265 0.355 1.281-0.047 0.041-5.975 1.708-5.989 1.683zM3.683 20.771c-0.068-0.052-1.573-5.151-1.688-5.719-0.021-0.115 0.193-0.057 2.901 0.713 2.823 0.808 2.921 0.839 2.959 0.989 0.041 0.131 0.015 0.172-0.167 0.297-0.109 0.079-1.027 0.964-2.032 1.969-0.604 0.619-1.224 1.224-1.849 1.823-0.020 0-0.077-0.032-0.124-0.073zM4.776 14.932c-1.583-0.453-2.885-0.828-2.891-0.833-0.021-0.016 1.683-5.911 1.713-5.948 0.032-0.025 5.896 1.615 5.959 1.672 0.011 0.011-0.36 1.355-0.823 2.984-0.771 2.693-0.86 2.964-0.964 2.959-0.068 0-1.411-0.38-3-0.833zM8.719 15.651c0.016-0.041 0.287-0.963 0.593-2.047l0.557-1.964 0.079 0.428c0.125 0.739 0.355 1.583 0.62 2.317 0.14 0.396 0.244 0.719 0.239 0.729-0.009 0.011-0.443 0.136-0.957 0.281-0.521 0.145-0.989 0.281-1.047 0.301-0.079 0.021-0.105 0.005-0.084-0.047zM6.541 8.151c-1.583-0.453-2.891-0.848-2.905-0.875-0.057-0.083-1.663-5.76-1.641-5.781 0.032-0.036 5.745 1.604 5.803 1.661 0.063 0.068 1.703 5.839 1.661 5.833l-2.912-0.839zM9.407 5.864l-0.693-2.405-0.104-0.376 2.255-0.645c1.245-0.349 2.297-0.656 2.349-0.677 0.041-0.020-0.115 0.188-0.349 0.448-1.233 1.36-2.129 2.995-2.609 4.765-0.077 0.245-0.135 0.496-0.176 0.745 0 0.396-0.157-0.031-0.672-1.849z"/></svg>'

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
  const ele0 = content["elements"][0];
  const tags = ele0["tags"];
  //} catch (err) { return; }

  // start a wikidata request if needed
  const wikidata = tags["wikidata"];
  if(wikidata) {
    // '/' -> %2F - iOS NSURLSession doesn't like the slash
    const wdurl = "https://www.wikidata.org/w/api.php?action=wbgetentities&format=json&props=claims%7Csitelinks%2Furls&sitefilter=enwiki&ids=" + wikidata;
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

  const osmUrl = "openstreetmap.org/" + ele0["type"] + "/" + ele0["id"];
  addPlaceInfo(osmIcon, "OpenStreetMap", "<a href='https://www." + osmUrl + "'><text>" + osmUrl + "</text></a>");
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
