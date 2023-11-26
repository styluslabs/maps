// fn to modify query for offline search, e.g, for catagorical search
// - see e.g., https://github.com/organicmaps/organicmaps/blob/master/data/categories.txt

const catagories = {
  "restaurant": ["fast + food", "food + court"],
  "food": ["restaurant"],
  "coffee": ["cafe"],
  "bar": ["pub", "biergarten"],
  "pub": ["bar"],
  "college": ["university"],
  "school": ["college", "university"],
  "gas": ["fuel"],
  "movie": ["cinema"], "theater": ["cinema"],
  "liquor": ["alcohol"],
  "grocery": ["supermarket", "greengrocer"],
  "groceries": ["supermarket", "greengrocer"],
  "barber": ["hairdresser"],
  "diy": ["doityourself", "hardware"],
  "hardware": "doityourself",
  "electronics": ["computer", "hifi"],
  "auto": ["car"],
  "hotel": ["motel", "hostel", "guest + house"],
  "motel": ["hotel", "hostel", "guest + house"],
  "accomodation": ["hotel", "motel", "hostel", "guest + house", "apartment", "chalet"],
  "lodging": ["hotel", "motel", "hostel", "guest + house", "apartment", "chalet"]
};

function transformQuery(query)
{
  const q = query.toLowerCase();
  const cat = catagories[q] || catagories[q.slice(0, -1)];
  return cat ? query + " OR " + cat.join(" OR ") : "";
}

// for now, transformQuery() name is hardcoded
//registerFunction("transformQuery", "offline-search", "Offline Search");
