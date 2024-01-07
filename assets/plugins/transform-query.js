// fn to modify query for offline search, e.g, for categorical search
// - see e.g., https://github.com/organicmaps/organicmaps/blob/master/data/categories.txt

const categories = {
  "restaurant": ["fast + food", "food + court"],
  "food": ["restaurant"],
  "coffee": ["cafe"],
  "bar": ["pub", "biergarten"],
  "pub": ["bar"],
  "college": ["university"],
  "school": ["college", "university"],
  "gas": ["fuel"],
  "gas station": ["fuel"],
  "movie": ["cinema"], "theater": ["cinema"],
  "liquor": ["alcohol"],
  "grocery": ["supermarket", "greengrocer"],
  "groceries": ["supermarket", "greengrocer"],
  "barber": ["hairdresser"],
  "diy": ["doityourself", "hardware"],
  "hardware": ["doityourself"],
  "electronics": ["computer", "hifi"],
  "charity" : ["second + hand"],
  "second hand" : ["charity"],
  "auto": ["car"],
  "bike": "(bike OR bicycle) NOT (rental OR parking)",
  "bicycle": "bicycle NOT (rental OR parking)",
  "hotel": ["motel", "hostel", "guest + house"],
  "motel": ["hotel", "hostel", "guest + house"],
  "accomodation": ["hotel", "motel", "hostel", "guest + house", "apartment", "chalet"],
  "lodging": ["hotel", "motel", "hostel", "guest + house", "apartment", "chalet"]
};

const replacements = {
  "bike": "(bike OR bicycle)",
  "restaurant": "(restaurant OR food)",
  "restaurants": "(restaurant OR food)",
  "food": "(restaurant OR food)"
};

const extrawords = [ "me", "near", "nearby", "store", "shop" ];


function transformQuery(query)
{
  var replaced = false;
  var q = query.toLowerCase();
  // remove extraneous words
  for(var ii = 0; ii < extrawords.length; ii++) {
    if(q.endsWith(" " + extrawords[ii])) {
      replaced = true;
      q = q.slice(0, -extrawords[ii].length - 1);
    }
  }
  var cat = categories[q];
  if(!cat) {
    const q1 = q.slice(0, -1);
    cat = categories[q1];
    if(cat) q = q1;
  }
  if(cat) {
    // string instead of array indicates replacement instead of addition
    if(typeof cat === 'string') return cat;
    return q + " OR " + cat.join(" OR ");
  }

  // if no category match, try replacements
  var qwords = q.split(' ');
  for(var k in replacements) {
    if(!replacements.hasOwnProperty(k)) continue;
    for(var ii = 0; ii < qwords.length; ii++) {
      if(qwords[ii] == k) {
        replaced = true;
        qwords[ii] = replacements[k];
      }
    }
  }
  return replaced ? qwords.join(" AND ") : "";
}

// for now, transformQuery() name is hardcoded
//registerFunction("transformQuery", "offline-search", "Offline Search");
