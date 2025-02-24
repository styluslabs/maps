// Converted from Lua Tilemaker processing script for Ascend Maps OSM schema

#include "tilebuilder.h"

#include <string>
#include <set>
#include <map>
#include <vector>

using geodesk::Relation;

// tilebuilder.cpp/.h + ascendtiles.cpp/.h ?
class AscendTileBuilder : public TileBuilder {
public:
  AscendTileBuilder(TileID _id);
  void processFeature() override;

  void ProcessNode();
  void ProcessWay();
  void ProcessRelation();

  void WriteBoundary(Feature& feat);
  void SetBuildingHeightAttributes();
  bool SetMinZoomByArea(double area = 0);
  void SetBrunnelAttributes();
  void SetEleAttributes();
  void SetNameAttributes(Feature& feat, int minz = 0);
  void SetNameAttributes(int minz = 0) { SetNameAttributes(feature(), minz); }
  bool NewWritePOI(double area = 0, bool force = false);
};

std::string buildTile(Features& world, TileID id)
{
  AscendTileBuilder tileBuilder(id);
  try {
    return tileBuilder.build(world);
  }
  catch(std::exception &e) {
    LOG("Exception building tile %s (feature id %lld): %s", id.toString().c_str(), tileBuilder.feature().id(), e.what());
    return "";
  }
}

#ifndef NDEBUG
int main(int argc, char* argv[])
{
  if(argc < 2) {
    LOG("No gol file specified!");
    return -1;
  }

  Features world(argv[1]);
  LOG("Loaded %s", argv[1]);

  // for(int x = 2616; x <= 2621; ++x) {
  //   for(int y = 6331; y <= 6336; ++y) {
  //     TileID id(x, y, 14);
  //     std::string mvt = buildTile(world, id);
  //   }
  // }

  TileID id(2617, 6332, 14);  // Alamo square!
  while(id.z > 10) {
    id = id.getParent();
    std::string mvt = buildTile(world, id);
  }

  return 0;
}
#endif

// AscendTileBuilder impl

struct Set {
  std::set<std::string> m_items;
  Set(std::initializer_list<std::string> items) : m_items(items) {}

  bool operator[](const std::string& key) const { return !key.empty() && m_items.find(key) != m_items.end(); }
};

struct ZMap {
  using map_t = std::map<std::string, int>;
  std::string m_tag;
  map_t m_items;
  const int m_dflt = 100;
  ZMap(std::string _tag, int _dflt=100) : m_tag(_tag), m_dflt(_dflt) {}
  ZMap(std::initializer_list<map_t::value_type> items) : m_items(items) {}
  ZMap& add(int z, std::initializer_list<std::string> items) {
    for(auto& s : items)
      m_items.emplace(s, z);
    return *this;
  }

  const std::string& tag() const { return m_tag; }

  int operator[](const std::string& key) const {
    if (key.empty()) { return m_dflt; }
    auto it = m_items.find(key);
    return it != m_items.end() ? it->second : m_dflt;
  }
};


static const std::vector<std::string> ascendLayers =
    { "place", "boundary", "poi", "transportation", "transit", "building", "water", "landuse" };

AscendTileBuilder::AscendTileBuilder(TileID _id) : TileBuilder(_id, ascendLayers) {}

void AscendTileBuilder::processFeature()
{
  if (feature().isWay()) { ProcessWay(); }
  else if (feature().isNode()) { ProcessNode(); }
  else if (feature()["type"] == "multipolygon") { ProcessWay(); }
  else { ProcessRelation(); }  //if (feature().isRelation())
  //else { LOG("Unknown feature type!"); }
}


static const auto aerodromeValues = Set { "international", "public", "regional", "military", "private" };

//node_keys = { "addr:housenumber","aerialway","aeroway","amenity","barrier","highway","historic","leisure","natural","office","place","railway","shop","sport","tourism","waterway" }
void AscendTileBuilder::ProcessNode()
{
  // Write 'place'
  // note that OpenMapTiles has a rank for countries (1-3);, states (1-6) and cities (1-10+);
  //   we could potentially approximate it for cities based on the population tag
  auto place = Find("place");
  if (place != "") {
    int mz = 13, rank = 0;
    double pop = atof(Find("population").c_str());

    if (place == "continent"   ) { mz = 0; }
    else if (place == "country") { rank = mz = 3 - (pop > 50E6) - (pop > 20E6); }
    else if (place == "state"  ) { mz = 4; }
    else if (place == "city"   ) { mz = 5; }
    else if (place == "town"   ) { mz = pop > 8000 ? 7 : 8; }
    else if (place == "village") { mz = pop > 2000 ? 9 : 10; }
    else if (place == "suburb" ) { mz = 11; }
    else if (place == "hamlet" ) { mz = 12; }
    else if (place == "quarter") { mz = 12; }
    //else if (place == "neighbourhood") { mz = 13; }  -- 13 is the default
    //else if (place == "locality"     ) { mz = 13; }

    if (!MinZoom(mz)) { return; }

    Layer("place", false);
    Attribute("class", place);
    Attribute("place", place);
    if (rank > 0) { AttributeNumeric("rank", rank); }
    if (pop > 0) { AttributeNumeric("population", pop); }
    double sqkm = atof(Find("sqkm").c_str());
    if (sqkm > 0) { AttributeNumeric("sqkm", sqkm); }
    if (place == "country") { Attribute("iso_a2", Find("ISO3166-1:alpha2")); }
    Attribute("place_CN");  //, Find("place:CN"));
    SetNameAttributes();
    return;
  }

  // many smaller airports only have aerodrome node instead of way
  auto aeroway = Find("aeroway");
  if (aeroway == "aerodrome") {
    if (!MinZoom(11)) { return; }
    Layer("transportation", false);  //"aeroway"
    Attribute("aeroway", aeroway);
    Attribute("ref");  //, Find("ref"));
    SetNameAttributes();
    SetEleAttributes();
    Attribute("iata");  //, Find("iata"));
    Attribute("icao");  //, Find("icao"));
    auto aerodrome = Find("aerodrome");
    Attribute("aerodrome", aerodromeValues[aerodrome] ? aerodrome : "other");
    return;
  }

  // Write 'poi'
  NewWritePOI();

  // Write 'mountain_peak' and 'water_name'
  auto natural = Find("natural");
  if (natural == "peak" || natural == "volcano") {
    if (!MinZoom(11)) { return; }
    Layer("poi", false);
    SetNameAttributes();
    SetEleAttributes();
    Attribute("natural", natural);
    return;
  }

  if (natural == "bay") {
    if (!MinZoom(8)) { return; }
    Layer("water", false);
    SetNameAttributes();  //14);
    return;
  }
}

//static const auto majorRoadValues = Set { "motorway", "trunk", "primary" };
//static const auto mainRoadValues  = Set { "secondary", "motorway_link", "trunk_link", "primary_link", "secondary_link" };
//static const auto midRoadValues   = Set { "tertiary", "tertiary_link" };
static const auto minorRoadValues = Set { "unclassified", "residential", "road", "living_street" };
static const auto trackValues     = Set { "cycleway", "byway", "bridleway", "track" };
static const auto pathValues      = Set { "footway", "path", "steps", "pedestrian" };
static const auto linkValues      = Set { "motorway_link", "trunk_link", "primary_link", "secondary_link", "tertiary_link" };
//static const auto constructionValues = Set { "primary", "secondary", "tertiary", "motorway", "service", "trunk", "track" };

static const auto pavedValues = Set { "paved", "asphalt", "cobblestone", "concrete", "concrete:lanes", "concrete:plates", "metal", "paving_stones", "sett", "unhewn_cobblestone", "wood" };
static const auto unpavedValues = Set { "unpaved", "compacted", "dirt", "earth", "fine_gravel", "grass", "grass_paver", "gravel", "gravel_turf", "ground", "ice", "mud", "pebblestone", "salt", "sand", "snow", "woodchips" };

static const auto boundaryValues = Set { "administrative", "disputed" };
static const auto parkValues = Set { "protected_area", "national_park" };
static const auto landuseAreas = Set { "retail", "military", "residential", "commercial", "industrial", "railway", "cemetery", "forest", "grass", "allotments", "meadow", "recreation_ground", "village_green", "landfill", "farmland", "farmyard", "orchard", "vineyard", "plant_nursery", "greenhouse_horticulture", "farm" };
static const auto naturalAreas = Set { "wood", "grassland", "grass", "scrub", "fell", "heath", "wetland", "glacier", "beach", "sand", "bare_rock", "scree" };
static const auto leisureAreas = Set { "pitch", "park", "garden", "playground", "golf_course", "stadium" };
static const auto amenityAreas = Set { "school", "university", "kindergarten", "college", "library", "hospital", "bus_station", "marketplace" };
static const auto tourismAreas = Set { "zoo", "theme_park", "aquarium" };

static const auto waterwayClasses = Set { "stream", "river", "canal", "drain", "ditch" };
static const auto waterwayAreas   = Set { "river", "riverbank", "stream", "canal", "drain", "ditch", "dock" };
static const auto waterLanduse    = Set { "reservoir", "basin", "salt_pond" };
static const auto noNameWater     = Set { "river", "basin", "wastewater", "canal", "stream", "ditch", "drain" };
static const auto manMadeClasses  = Set { "pier", "breakwater", "groyne" };  // "storage_tank", "water_tap", "dyke", "lighthouse"
static const auto aerowayClasses  = Set { "taxiway", "hangar", "runway", "helipad", "aerodrome", "airstrip", "tower" };
static const auto aerowayBuildings = Set { "terminal", "gate", "tower" };

static const ZMap transitRoutes =
    { {"train", 8}, {"subway", 10}, {"tram", 12}, {"share_taxi", 12}, {"light_rail", 12}, {"bus", 14}, {"trolleybus", 14} };
static const ZMap otherRoutes =
    { {"road", 8}, {"ferry", 9}, {"bicycle", 10}, {"hiking", 10}, {"foot", 12}, {"mtb", 10}, {"ski", 12} };  //piste = 12;,
//ignoredRoutes = Set { "power", "railway", "detour", "tracks", "horse", "emergency_access", "snowmobile", "historic", "running", "fitness_trail" }

void AscendTileBuilder::ProcessRelation()
{
  auto reltype = Find("type");
  if (reltype == "route") {
    auto route = Find("route");
    if (route == "ferry") {
      if(!MinZoom(9)) { return; }
      Layer("transportation", false);
      Attribute("route", "ferry");
      SetNameAttributes(12);
      return;
    }
    if (MinZoom(transitRoutes[route])) {
      Layer("transit", false);
    } else if (MinZoom(otherRoutes[route])) {
      Layer("transportation", false);
    } else {
      return;
    }
    Attribute("class", "route");
    Attribute("route", route);
    Attribute("name");  //, Find("name"));
    Attribute("ref");  //, Find("ref"));
    Attribute("network");  //, Find("network"));
    Attribute("color", Find("colour"));  // note spelling
    Attribute("osm_id", Id());
    Attribute("osm_type", "relation");
    return;
  }
  if (reltype == "boundary") {
    auto boundary = Find("boundary");
    if (!parkValues[boundary] || !MinZoom(8)) { return; }   //SetMinZoomByArea(rel, area);
    if (Find("maritime") == "yes") { return; }  // marine sanctuaries not really useful for typical use
    auto leisure = Find("leisure");
    auto protect_class = Find("protect_class");
    // tilemaker doesn't calculate area for relations
    auto area = Area();
    Layer("landuse", true);
    Attribute("class", boundary);
    Attribute("boundary", boundary);
    Attribute("leisure", leisure);
    Attribute("protect_class", protect_class);
    SetNameAttributes();
    AttributeNumeric("area", area);
    // write POI at centroid
    LayerAsCentroid("poi");
    //MinZoom(8);  //SetMinZoomByArea(rel, area);
    Attribute("class", boundary);
    Attribute("boundary", boundary);
    Attribute("leisure", leisure);
    Attribute("protect_class", protect_class);
    SetNameAttributes();
    AttributeNumeric("area", area);
  }
}

void AscendTileBuilder::ProcessWay()
{
  //auto tags = feature().tags();  if(tags.begin() == tags.end()) { return; }  // skip if no tags
  auto building = Find("building");  // over 50% of ways are buildings, so process first
  if (building != "") {
    if (!MinZoom(13) || !SetMinZoomByArea()) { return; }
    Layer("building", true);
    SetBuildingHeightAttributes();
    if (MinZoom(14)) {
      // housenumber is also commonly set on poi nodes, but not very useful w/o at least street name too
      Attribute("housenumber", Find("addr:housenumber"));  //, 14);
      NewWritePOI(0, true);
    }
    return;
  }

  if (Find("disused") == "yes") { return; }

  // Roads/paths/trails - 2nd most common way type
  auto highway = Find("highway");
  if (highway != "") {
    if (highway == "proposed" || highway == "construction") { return; }
    auto access = Find("access");
    if (access == "private" || access == "no") { return; }
    // most footways are sidewalks or crossings, which are mapped inconsistently so just add clutter and
    //  confusion the map; we could consider keeping footway == "alley"
    if (highway == "footway" && Find("footway") != "") { return; }

    // Construction -- not used currently
    //auto construction = Find("construction");
    //if (highway == "construction" && constructionValues[construction]) {
    //  highway = construction;
    //  construction = "yes";
    //}

    int minzoom = 99, lblzoom = 99;
    bool ramp = false;
    //if (majorRoadValues[highway]) { minzoom = 4; }
    if (highway == "motorway"        ) { minzoom = 4;  lblzoom = 8; }
    else if (highway == "trunk"      ) { minzoom = 5;  lblzoom = 8; }
    else if (highway == "primary"    ) { minzoom = 7;  lblzoom = 12; }
    else if (highway == "secondary"  ) { minzoom = 9;  lblzoom = 12; }  //mainRoadValues[highway]
    else if (highway == "tertiary"   ) { minzoom = 11; lblzoom = 12; }  //midRoadValues[highway]
    else if (minorRoadValues[highway]) { minzoom = 12; lblzoom = 14; }
    else if (trackValues[highway]    ) { minzoom = 10; lblzoom = 14; }  // was 14
    else if (pathValues[highway]     ) { minzoom = 10; lblzoom = 14; }  // was 14
    else if (highway == "service"    ) { minzoom = 12; lblzoom = 14; }
    else if (linkValues[highway]     ) {
      highway = highway.substr(0, highway.find("_"));
      ramp = true; minzoom = 11; lblzoom = 14;
    }

    if(!MinZoom(minzoom)) { return; }

    Layer("transportation", false);
    //Attribute("class", h);
    Attribute("highway", highway);
    SetBrunnelAttributes();
    if (ramp) { AttributeNumeric("ramp", 1); }

    // Service
    if (highway == "service") { Attribute("service"); } //, Find("service")); }

    auto oneway = Find("oneway");
    if (oneway == "yes" || oneway == "1") {
      AttributeNumeric("oneway", 1);
    }
    //if (oneway == "-1") {}

    // cycling
    auto cycleway = Find("cycleway");
    if (cycleway == "") {
      cycleway = Find("cycleway:both");
    }
    if (cycleway != "" && cycleway != "no") {
      Attribute("cycleway", cycleway);
    }

    auto cycleway_left = Find("cycleway:left");
    if (cycleway_left != "" && cycleway_left != "no") {
      Attribute("cycleway_left", cycleway_left);
    }

    auto cycleway_right = Find("cycleway:right");
    if (cycleway_right != "" && cycleway_right != "no") {
      Attribute("cycleway_right", cycleway_right);
    }

    auto bicycle = Find("bicycle");
    if (bicycle != "" && bicycle != "no") {
      Attribute("bicycle", bicycle);
    }

    // surface
    auto surface = Find("surface");
    if (pavedValues[surface]) {
      Attribute("surface", "paved");
    } else if (unpavedValues[surface]) {
      Attribute("surface", "unpaved");
    }

    // trail/path info
    auto trailvis = Find("trail_visibility");
    if (trailvis != "" && trailvis != "good" && trailvis != "excellent") {
      Attribute("trail_visibility", trailvis);
    }
    Attribute("mtb_scale", Find("mtb:scale"));  // mountain biking difficulty rating
    if (highway == "path") { Attribute("golf"); }  //, Find("golf"));

    // name, roadway info
    SetNameAttributes(lblzoom);
    //Attribute("network","road"); // **** could also be us-interstate, us-highway, us-state
    Attribute("maxspeed");  //, Find("maxspeed"));
    Attribute("lanes");  //, Find("lanes"));
    Attribute("ref");  //, Find("ref"));  //AttributeNumeric("ref_length",ref:len());
    return;
  }

  // Railways ('transportation' and 'transportation_name', plus 'transportation_name_detail');
  auto railway = Find("railway");
  if (railway != "") {
    auto service  = Find("service");
    if (!MinZoom(service != "" ? 12 : 9)) { return; }
    Layer("transportation", false);
    Attribute("class", "rail");
    Attribute("railway", railway);
    SetBrunnelAttributes();
    SetNameAttributes(14);
    Attribute("service", service);
    return;
  }

  bool isClosed = IsClosed();

  // Pier, breakwater, etc.
  auto man_made = Find("man_made");
  if (manMadeClasses[man_made]) {
    if(!SetMinZoomByArea()) { return; }
    Layer("landuse", isClosed);
    //SetZOrder(way);
    Attribute("class", man_made);
    Attribute("man_made", man_made);
    return;
  }

  // 'Ferry'
  auto route = Find("route");
  if (route == "ferry") {
    if (!MinZoom(9)) { return; }
    // parents() not implemented! ... we'll assume a parent has route=ferry if any parents
    if (feature().belongsToRelation()) { return; }  // avoid duplication
    //for (Relation rel : feature().parents()) { if (rel["route"] == "ferry") { return; }  }
    Layer("transportation", false);
    Attribute("route", route);
    SetBrunnelAttributes();
    SetNameAttributes(12);
    return;
  }

  auto piste_diff = Find("piste:difficulty");
  if (piste_diff != "") {
    if (!MinZoom(10)) { return; }
    Layer("transportation", isClosed);
    Attribute("class", "piste");
    Attribute("route", "piste");
    Attribute("difficulty", piste_diff);
    Attribute("piste_type", Find("piste:type"));
    Attribute("piste_grooming", Find("piste:grooming"));  // so we can ignore backcountry "pistes"
    SetNameAttributes(14);
    return;
  }

  auto aerialway = Find("aerialway");
  if (aerialway != "") {
    if (!MinZoom(10)) { return; }
    Layer("transportation", false);
    Attribute("class", "aerialway");
    Attribute("aerialway", aerialway);
    SetNameAttributes(14);
    return;
  }

  // 'Aeroway'
  auto aeroway = Find("aeroway");
  if (aerowayBuildings[aeroway]) {
    Layer("building", true);
    Attribute("aeroway", aeroway);
    SetBuildingHeightAttributes();
    if (MinZoom(14)) { NewWritePOI(0, true); }
    return;
  }
  if (aerowayClasses[aeroway]) {
    if (!MinZoom(10)) { return; }
    Layer("transportation", isClosed);  //"aeroway"
    Attribute("aeroway", aeroway);
    Attribute("ref");  //, Find("ref"));
    //write_name = true
    if (aeroway == "aerodrome") {
      //LayerAsCentroid("aerodrome_label");
      SetNameAttributes();
      SetEleAttributes();
      Attribute("iata");  //, Find("iata"));
      Attribute("icao");  //, Find("icao"));
      auto aerodrome = Find("aerodrome");
      Attribute("aerodrome", aerodromeValues[aerodrome] ? aerodrome : "other");
      AttributeNumeric("area", Area());
    }
    return;
  }

  // Water areas (closed ways)
  auto natural  = Find("natural");
  auto landuse  = Find("landuse");
  auto leisure  = Find("leisure");
  auto amenity  = Find("amenity");
  auto tourism  = Find("tourism");
  auto waterway = Find("waterway");
  auto water = Find("water");

  // waterway is single way indicating course of a waterway - wide rivers, etc. have additional polygons to map area
  if (waterwayClasses[waterway] && !isClosed) {
    bool namedriver = waterway == "river" && Holds("name");
    if (!MinZoom(namedriver ? 8 : 12)) { return; }
    Layer("water", false);  //waterway , waterway_detail
    if (Find("intermittent") == "yes") { AttributeNumeric("intermittent", 1); }
    Attribute("class", waterway);
    Attribute("waterway", waterway);
    SetNameAttributes();
    SetBrunnelAttributes();
    return;
  } else if (waterway == "dam") {
    if (!MinZoom(12)) { return; }  // was 13
    Layer("building", isClosed);
    Attribute("waterway", waterway);
    return;
  } else if (waterway == "boatyard" || waterway == "fuel") {
    landuse = "industrial";
  }

  std::string waterbody = "";
  if (waterLanduse[landuse]) { waterbody = landuse; }
  else if (waterwayAreas[waterway]) { waterbody = waterway; }
  else if (leisure == "swimming_pool") { waterbody = leisure; }
  else if (natural == "water" || natural == "bay") { waterbody = natural; }

  if (waterbody != "") {
    if (!isClosed || !SetMinZoomByArea() || Find("covered") == "yes") { return; }
    std::string cls = "lake";
    if (natural == "bay") { cls = "ocean"; } else if (waterway != "") { cls = "river"; }
    //if (class == "lake" and Find("wikidata") == "Q192770") { return; }  // crazy lake in Finland
    //if (cls == "ocean" && isClosed && (AreaIntersecting("ocean")/Area() > 0.98)) { return; }
    Layer("water", true);
    Attribute("class", cls);
    Attribute("water", water != "" ? std::string(water) : waterbody);

    if (Find("intermittent") == "yes") { AttributeNumeric("intermittent", 1); }
    // don't include names for minor man-made basins (e.g. way 25958687) or rivers, which have name on waterway way
    if (Holds("name") && natural == "water" && !noNameWater[water]) {
      //LayerAsCentroid("water_name_detail");
      SetNameAttributes(14);
      //SetMinZoomByArea(way);
      //Attribute("class", class);
      AttributeNumeric("area", Area());
    }
    return;  // in case we get any landuse processing
  }

  // special case since valleys are mapped as ways
  if (natural == "valley") {
    auto len = Length();
    Layer("landuse", false);
    SetMinZoomByArea(len*len);
    Attribute("natural", natural);
    SetNameAttributes();
    return;
  }

  // landuse/landcover
  if (landuse == "field") { landuse = "farmland"; }
  else if (landuse == "meadow" && Find("meadow") == "agricultural") { landuse = "farmland"; }

  if (landuseAreas[landuse] || naturalAreas[natural] || leisureAreas[leisure] || amenityAreas[amenity] || tourismAreas[tourism]) {
    if (!SetMinZoomByArea()) { return; }
    Layer("landuse", true);
    //Attribute("class", landuseKeys[l]);
    //if (landuse == "residential" and Area()<ZRES8) { MinZoom(8); } else { SetMinZoomByArea(way); }
    Attribute("landuse", landuse);
    Attribute("natural", natural);
    Attribute("leisure", leisure);
    Attribute("amenity", amenity);
    Attribute("tourism", tourism);
    if (natural == "wetland") { Attribute("wetland"); }  //, Find("wetland")
    NewWritePOI(Area(), MinZoom(14));
  }

  auto boundary = Find("boundary");
  if (boundary != "" && Find("protection_title") == "National Forest"
      && Find("operator") == "United States Forest Service") { return; }  // too many

  // Parks ... possible for way to be both park boundary and landuse?
  bool park_boundary = parkValues[boundary];
  if (park_boundary || leisure == "nature_reserve") {
    if (!SetMinZoomByArea()) { return; }
    Layer("landuse", true);
    Attribute("class", park_boundary ? boundary : leisure);
    if (park_boundary) { Attribute("boundary", boundary); }
    Attribute("leisure", leisure);
    Attribute("protect_class");  //, Find("protect_class"));
    SetNameAttributes();
    NewWritePOI(Area(), MinZoom(14));
  }

  // Boundaries ... possible for way to be shared with park boundary or landuse?
  // parents() not implemented
  /*for (Feature rel : feature().parents()) {
    auto bndry = rel["boundary"];
    if (bndry == "administrative" || bndry == "disputed") {
      WriteBoundary(rel);
      return;
    }
  }*/

  if (boundary == "administrative" || boundary == "disputed") {
    WriteBoundary(feature());
    return;
  }
}

// POIs: moving toward including all values for key except common unwanted values

static constexpr int EXCLUDE = 100;
static const std::vector<ZMap> poiTags = {
  // all amenity values with count > 1000 (as of Jan 2024) that we wish to exclude
  ZMap("amenity", 14).add(12, { "bus_station", "ferry_terminal" }).add(EXCLUDE, { "parking_space", "bench",
      "shelter", "waste_basket", "bicycle_parking", "recycling", "hunting_stand", "vending_machine",
      "post_box", "parking_entrance", "telephone", "bbq", "motorcycle_parking", "grit_bin", "clock",
      "letter_box", "watering_place", "loading_dock", "payment_terminal", "mobile_money_agent", "trolley_bay",
      "ticket_validator", "lounger", "feeding_place", "vacuum_cleaner", "game_feeding", "smoking_area",
      "photo_booth", "kneipp_water_cure", "table", "fixme", "office", "chair" }),
  ZMap("tourism", 14).add(12, { "attraction", "viewpoint", "museum" }).add(EXCLUDE, { "yes" }),
  ZMap("leisure", 14).add(EXCLUDE, { "fitness_station", "picnic_table",
      "slipway", "outdoor_seating", "firepit", "bleachers", "common", "yes" }),
  ZMap("shop", 14),
  ZMap("sport", 14),
  ZMap("landuse").add(14, { "basin", "brownfield", "cemetery", "reservoir", "winter_sports" }),
  ZMap("historic").add(14, { "monument", "castle", "ruins", "fort", "mine", "archaeological_site" }),
  //archaeological_site = Set { "__EXCLUDE", "tumulus", "fortification", "megalith", "mineral_extraction", "petroglyph", "cairn" },
  ZMap("highway").add(12, { "bus_stop", "trailhead" }).add(14, { "traffic_signals" }),
  ZMap("railway").add(12, { "halt", "station", "tram_stop" }).add(14, { "subway_entrance", "train_station_entrance" }),
  ZMap("natural").add(13, { "spring", "hot_spring", "fumarole", "geyser", "sinkhole", "arch", "cave_entrance", "saddle" }),
  ZMap("barrier").add(14, { "bollard", "border_control",
      "cycle_barrier", "gate", "lift_gate", "sally_port", "stile", "toll_booth" }),
  ZMap("building").add(14, { "dormitory" }),
  ZMap("aerialway").add(14, { "station" }),
  ZMap("waterway").add(14, { "dock" })
};

static const std::vector<std::string> extraPoiTags =
    { "cuisine", "station", "religion", "operator", "archaeological_site" };  // atm:operator

bool AscendTileBuilder::NewWritePOI(double area, bool force)
{
  if(!MinZoom(12)) { return false; }  // no POIs below z12

  bool force12 = area > 0 || Holds("wikipedia");
  for (const ZMap& z : poiTags) {
    auto val = Find(z.tag());
    if (val != "" && (force12 || MinZoom(z[val]))) {
      LayerAsCentroid("poi");
      SetNameAttributes();
      if (area > 0) { AttributeNumeric("area", area); }
      // write value for all tags in poiTags (if present)
      for(const ZMap& y : poiTags) { Attribute(y.tag()); }
      for(auto& s : extraPoiTags) { Attribute(s); }
      return true;
    }
  }
  if (force && Holds("name")) {
    LayerAsCentroid("poi");
    SetNameAttributes();
    if (area > 0) { AttributeNumeric("area", area); }
  }
  return false;
}

// Common functions

void AscendTileBuilder::SetNameAttributes(Feature& feat, int minz)
{
  if (!MinZoom(minz)) { return; }
  std::string name = feat["name"];
  Attribute("name", name);
  std::string name_en = feat["name:en"];  // force std::string because == not impl yet for TagValue
  if(name_en != "" && name_en != name) {
    Attribute("name_en", name_en);
  }
  // add OSM id
  std::string osm_type = feat.isWay() ? "way" : feat.isNode() ? "node" : "relation";
  Attribute("osm_id", std::to_string(feat.id()));
  Attribute("osm_type", osm_type);
}

void AscendTileBuilder::SetEleAttributes()
{
  double ele = atof(Find("ele").c_str());
  if (ele != 0) {
    AttributeNumeric("ele", ele);
    //AttributeNumeric("ele_ft", ele * 3.2808399);
  }
}

void AscendTileBuilder::SetBrunnelAttributes()
{
  if (Find("bridge") == "yes") { Attribute("brunnel", "bridge"); }
  else if (Find("tunnel") == "yes") { Attribute("brunnel", "tunnel"); }
  else if (Find("ford") == "yes") { Attribute("brunnel", "ford"); }
}

// Meters per pixel if tile is 256x256
constexpr double SQ(double x) { return x*x; }
static constexpr double ZRES5  = SQ(4891.97);
static constexpr double ZRES6  = SQ(2445.98);
static constexpr double ZRES7  = SQ(1222.99);
static constexpr double ZRES8  = SQ(611.5);
static constexpr double ZRES9  = SQ(305.7);
static constexpr double ZRES10 = SQ(152.9);
static constexpr double ZRES11 = SQ(76.4);
static constexpr double ZRES12 = SQ(38.2);
static constexpr double ZRES13 = SQ(19.1);

// Set minimum zoom level by area
bool AscendTileBuilder::SetMinZoomByArea(double area)
{
  if (area <= 0) { area = Area(); }
  if      (area > ZRES5 ) { return MinZoom(6);  }
  else if (area > ZRES6 ) { return MinZoom(7);  }
  else if (area > ZRES7 ) { return MinZoom(8);  }
  else if (area > ZRES8 ) { return MinZoom(9);  }
  else if (area > ZRES9 ) { return MinZoom(10); }
  else if (area > ZRES10) { return MinZoom(11); }
  else if (area > ZRES11) { return MinZoom(12); }
  else if (area > ZRES12) { return MinZoom(13); }
  else                    { return MinZoom(14); }
}

void AscendTileBuilder::SetBuildingHeightAttributes()
{
  // The height of one floor, in meters
  static constexpr double BUILDING_FLOOR_HEIGHT = 3.66;

  double height = atof(Find("height").c_str());
  double minHeight = atof(Find("min_height").c_str());
  double levels = atof(Find("building:levels").c_str());
  double minLevel = atof(Find("building:min_level").c_str());

  double renderHeight = height > 0 ? height : (levels > 0 ? levels : 1) * BUILDING_FLOOR_HEIGHT;
  double renderMinHeight = minHeight > 0 ? minHeight : (minLevel > 0 ? minLevel : 0) * BUILDING_FLOOR_HEIGHT;

  // Fix upside-down buildings
  if (renderHeight < renderMinHeight) {
    renderHeight = renderHeight + renderMinHeight;
  }

  AttributeNumeric("height", renderHeight);
  AttributeNumeric("min_height", renderMinHeight);
  // TODO: remove these
  AttributeNumeric("render_height", renderHeight);
  AttributeNumeric("render_min_height", renderMinHeight);
}

void AscendTileBuilder::WriteBoundary(Feature& feat)
{
  double admin_level = feat["admin_level"];
  if (!(admin_level < 12)) { admin_level = 11; }  // handle NaN
  int mz = 0;
  if (admin_level >= 3 && admin_level < 5) { mz=4; }
  else if (admin_level >= 5 && admin_level < 7) { mz=8; }
  else if (admin_level == 7) { mz=10; }
  else if (admin_level >= 8) { mz=12; }

  if (!MinZoom(mz)) { return; }
  Layer("boundary", false);
  AttributeNumeric("admin_level", admin_level);
  SetNameAttributes(feat);
  // to allow hiding coastal boundaries (natural=coastline)
  Attribute("natural", Find("natural"));   // always get from way (not parent relation)
  if (feat["maritime"] == "yes") { Attribute("maritime", "yes"); }
  if (feat["boundary"] == "disputed" || feat["disputed"] == "yes") {
    Attribute("disputed", "yes");
  }
}


// attributes for features from shapefiles (set in config.json)
// we set featurecla so scene YAML can distinguish features from Natural Earth
// an alternative approach to using shapefiles is to convert shapefiles to OSM PBF using ogr2osm script and
//  pass multiple PBFs to Tilemaker
/*void attribute_function(attr, layer) {;
  auto featurecla = attr["featurecla"]

  if (featurecla == "Glaciated areas") {
    return { class="ice", natural="glacier" }
  } else if (featurecla == "Antarctic Ice Shelf") {
    return { class="ice", natural="glacier", glacier_type="shelf" }
  } else if (featurecla == "Urban area") {
    return { class="residential" }
  } else if (layer == "ocean") {
    return { class="ocean" }

  // ne_10m_lakes
  } else if (layer == "ne_lakes") {
    return { class="lake", water="lake", name=attr["name"], wikidata=attr["wikidataid"], rank=attr["scalerank"], featurecla=featurecla }

  // ne_10m_admin_0_boundary_lines_land; can't use ne_10m_admin_0_countries because it includes shoreline
  } else if (layer == "ne_boundaries") {
    auto res = { admin_level=2;, adm0_l=attr["adm0_left"], adm0_r=attr["adm0_right"], featurecla=featurecla }
    if (featurecla != "International boundary (verify);") { res["disputed"] = 1 }
    return res

  // ne_10m_populated_places
  } else if (layer == "ne_populated_places") {
    auto rank = attr["scalerank"]
    auto z = rank < 6 and 3 or 6
    return { _minzoom=z, class="city", place="city", name=attr["name"], population=attr["pop_max"], rank=rank, wikidata=attr["wikidataid"], featurecla=featurecla }

  // ne_10m_roads
  } else if (layer == "ne_roads") {
    if (featurecla == "Ferry") {
      return { _minzoom=3;, class="ferry", route="ferry", ref=attr["name"], rank=attr["scalerank"], featurecla=featurecla }
    } else if (attr["expressway"] == 1) {
      return { _minzoom=3;, class="motorway", highway="motorway", ref=attr["name"], rank=attr["scalerank"], featurecla=featurecla }
    }
    return { _minzoom=6;, class="trunk", highway="trunk", ref=attr["name"], rank=attr["scalerank"], featurecla=featurecla }

  } else {
    return attr
  }
}*/
