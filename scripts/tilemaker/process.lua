-- Tilemaker processing script for Stylus Labs / Ascend Maps OSM schema

-- we cannot update to Tilemaker 3.0 yet because the 3.0 release creates mbtiles w/ UTF-16 encoding, which
--  cannot be attached to Ascend's UTF-8 mbtiles; this was later fixed on master, so next release should be OK

-- extracting OSM data w/ Overpass
-- 1. get all country labels: [out:xml][timeout:25]; node["place"="country"]({{bbox}}); out geom;
-- 1. cut and paste xml
-- 1. convert xml to pbf: `osmium cat overpass_countries.xml -o overpass_countries.pbf`
-- 1. pass pbf as input to tilemaker

-- tilemaker world map
-- .\build\tilemaker.exe .\cultural\overpass_countries.pbf --output basemap7.mbtiles --bbox -180,-85,180,85 --process .\process.lua --config .\config-basemap.json

-- tilemaker extract
-- 1. get bounds: `osmium fileinfo andorra-latest.osm.pbf`
-- 1. expand to zoom = 6 tile bounds: ascend --bbox minlng minlat maxlng maxlat zoom
-- 1. use expanded bounds for tilemaker bbox:
-- .\build\tilemaker.exe .\Sydney.osm.pbf --output sydney2.mbtiles --bbox 146.2500008983152497,-36.5978884118672738,151.8749989941716194,-31.9521630914605268 --process .\process.lua --config .\config.json

-- Dumping a mbtiles layer to GeoJSON (for debugging):
-- e.g.: ogr2ogr -oo CLIP=NO shasta.geojson shasta2.mbtiles poi

--------
-- Alter these lines to control which languages are written for place/streetnames
--
-- Preferred language can be (for example) "en" for English, "de" for German, or nil to use OSM's name tag:
preferred_language = nil
-- This is written into the following vector tile attribute (usually "name:latin"):
preferred_language_attribute = "name"  --"name:latin"
-- If OSM's name tag differs, then write it into this attribute (usually "name_int"):
default_language_attribute = nil  --"name_int"
-- Also write these languages if they differ - for example, { "de", "fr" }
additional_languages = { "en" }
--------

-- Enter/exit Tilemaker
function init_function()
end
function exit_function()
end

-- Implement Sets in tables
function Set(list)
  local set = {}
  for _, l in ipairs(list) do set[l] = true end
  return set
end

-- Meters per pixel if tile is 256x256
ZRES5  = 4891.97
ZRES6  = 2445.98
ZRES7  = 1222.99
ZRES8  = 611.5
ZRES9  = 305.7
ZRES10 = 152.9
ZRES11 = 76.4
ZRES12 = 38.2
ZRES13 = 19.1

-- The height of one floor, in meters
BUILDING_FLOOR_HEIGHT = 3.66

-- Process node/way tags
aerodromeValues = Set { "international", "public", "regional", "military", "private" }

-- Process node tags

node_keys = { "addr:housenumber","aerialway","aeroway","amenity","barrier","highway","historic","leisure","natural","office","place","railway","shop","sport","tourism","waterway" }
function node_function(node)
  -- many smaller airports only have aerodrome node instead of way
  local aeroway = Find("aeroway")
  if aeroway=="aerodrome" then
    Layer("transportation", false)  --"aeroway"
    MinZoom(11)
    Attribute("aeroway", aeroway)
    Attribute("ref", Find("ref"))
    SetNameAttributes(node, 0, "node")
    SetEleAttributes(node)
    Attribute("iata", Find("iata"))
    Attribute("icao", Find("icao"))
    local aerodrome = Find("aerodrome")
    Attribute("aerodrome", aerodromeValues[aerodrome] and aerodrome or "other")
  end

  -- Write 'housenumber'
  --local housenumber = Find("addr:housenumber")
  --if housenumber~="" then
  --  Layer("housenumber", false)
  --  Attribute("housenumber", housenumber)
  --end

  -- Write 'place'
  -- note that OpenMapTiles has a rank for countries (1-3), states (1-6) and cities (1-10+);
  --   we could potentially approximate it for cities based on the population tag
  local place = Find("place")
  if place ~= "" then
    local rank = nil
    local mz = 13
    local pop = tonumber(Find("population")) or 0
    local sqkm = tonumber(Find("sqkm")) or 0
    local placeCN = Find("place:CN")

    if     place == "continent"     then mz=0
    elseif place == "country"       then
      if     pop>50000000 then rank=1; mz=1
      elseif pop>20000000 then rank=2; mz=2
      else                     rank=3; mz=3 end
    elseif place == "state"         then mz=4
    elseif place == "city"          then mz=5
    elseif place == "town" and pop>8000 then mz=7
    elseif place == "town"          then mz=8
    elseif place == "village" and pop>2000 then mz=9
    elseif place == "village"       then mz=10
    elseif place == "suburb"        then mz=11
    elseif place == "hamlet"        then mz=12
    elseif place == "quarter"       then mz=12
    elseif place == "neighbourhood" then mz=13
    elseif place == "locality"      then mz=13
    end

    Layer("place", false)
    Attribute("class", place)
    Attribute("place", place)
    MinZoom(mz)
    if rank then AttributeNumeric("rank", rank) end
    if pop then AttributeNumeric("population", pop) end
    if sqkm then AttributeNumeric("sqkm", sqkm) end
    if place=="country" then Attribute("iso_a2", Find("ISO3166-1:alpha2")) end
    if placeCN ~= "" then Attribute("place_CN", placeCN) end
    SetNameAttributes(node, 0, "node")
    return
  end

  -- Write 'poi'
  NewWritePOI(node, 0, "node")

  -- Write 'mountain_peak' and 'water_name'
  local natural = Find("natural")
  if natural == "peak" or natural == "volcano" then
    Layer("mountain_peak", false)
    SetEleAttributes(node)
    Attribute("natural", natural)
    SetNameAttributes(node, 0, "node")
    return
  end
  if natural == "bay" then
    Layer("water", false)  --Layer("water_name", false)
    SetNameAttributes(node, 14, "node")
    return
  end
end

-- Process way tags

majorRoadValues = Set { "motorway", "trunk", "primary" }
mainRoadValues  = Set { "secondary", "motorway_link", "trunk_link", "primary_link", "secondary_link" }
midRoadValues   = Set { "tertiary", "tertiary_link" }
minorRoadValues = Set { "unclassified", "residential", "road", "living_street" }
trackValues     = Set { "cycleway", "byway", "bridleway", "track" }
pathValues      = Set { "footway", "path", "steps", "pedestrian" }
linkValues      = Set { "motorway_link", "trunk_link", "primary_link", "secondary_link", "tertiary_link" }
constructionValues = Set { "primary", "secondary", "tertiary", "motorway", "service", "trunk", "track" }

pavedValues = Set { "paved", "asphalt", "cobblestone", "concrete", "concrete:lanes", "concrete:plates", "metal", "paving_stones", "sett", "unhewn_cobblestone", "wood" }
unpavedValues = Set { "unpaved", "compacted", "dirt", "earth", "fine_gravel", "grass", "grass_paver", "gravel", "gravel_turf", "ground", "ice", "mud", "pebblestone", "salt", "sand", "snow", "woodchips" }

boundaryValues = Set { "administrative", "disputed" }
parkValues = Set { "protected_area", "national_park" }
landuseAreas = Set { "retail", "military", "residential", "commercial", "industrial", "railway", "cemetery", "forest", "grass", "allotments", "meadow", "recreation_ground", "village_green", "landfill", "farmland", "farmyard", "orchard", "vineyard", "plant_nursery", "greenhouse_horticulture", "farm" }
naturalAreas = Set { "wood", "grassland", "grass", "scrub", "fell", "heath", "wetland", "glacier", "beach", "sand", "bare_rock", "scree" }
leisureAreas = Set { "pitch", "park", "garden", "playground", "golf_course", "stadium" }
amenityAreas = Set { "school", "university", "kindergarten", "college", "library", "hospital", "bus_station", "marketplace" }
tourismAreas = Set { "zoo", "theme_park", "aquarium" }

-- POIs: moving toward including all values for key except common unwanted values
poiMinZoom = 14
poiTags = { aerialway = Set { "station" },
          -- all amenity values with count > 1000 (as of Jan 2024) we wish to exclude
          amenity = { [12] = Set { "bus_station", "ferry_terminal" }, [poiMinZoom] = Set { "__EXCLUDE", "bus_station", "ferry_terminal", "parking_space", "bench", "shelter", "waste_basket", "bicycle_parking", "recycling", "hunting_stand", "vending_machine", "post_box", "parking_entrance", "telephone", "bbq", "motorcycle_parking", "grit_bin", "clock", "letter_box", "watering_place", "loading_dock", "payment_terminal", "mobile_money_agent", "trolley_bay", "ticket_validator", "lounger", "feeding_place", "vacuum_cleaner", "game_feeding", "smoking_area", "photo_booth", "kneipp_water_cure", "table", "fixme", "office", "chair" } },
          barrier = Set { "bollard", "border_control", "cycle_barrier", "gate", "lift_gate", "sally_port", "stile", "toll_booth" },
          building = Set { "dormitory" },
          highway = { [12] = Set { "bus_stop", "trailhead" }, [poiMinZoom] = Set { "traffic_signals" } },
          historic = Set { "monument", "castle", "ruins", "fort", "mine" },
          archaeological_site = Set { "__EXCLUDE", "tumulus", "fortification", "megalith", "mineral_extraction", "petroglyph", "cairn" },
          landuse = Set { "basin", "brownfield", "cemetery", "reservoir", "winter_sports" },
          leisure = Set { "__EXCLUDE", "fitness_station", "picnic_table", "slipway", "outdoor_seating", "firepit", "bleachers", "common", "yes" },
          natural = { [13] = Set { "spring", "hot_spring", "fumarole", "geyser", "sinkhole", "arch", "cave_entrance", "saddle" } },
          railway = { [12] = Set { "halt", "station", "tram_stop" }, [poiMinZoom] = Set { "subway_entrance", "train_station_entrance" } },
          shop = {},
          sport = {},
          tourism = { [12] = Set { "attraction", "viewpoint", "museum" }, [poiMinZoom] = Set { "__EXCLUDE", "attraction", "viewpoint", "museum", "yes"} },
          waterway = Set { "dock" } }

waterwayClasses = Set { "stream", "river", "canal", "drain", "ditch" }
waterwayAreas   = Set { "river", "riverbank", "stream", "canal", "drain", "ditch", "dock" }
waterLanduse    = Set { "reservoir", "basin", "salt_pond" }
noNameWater     = Set { "river", "basin", "wastewater", "canal", "stream", "ditch", "drain" }
manMadeClasses  = Set { "pier", "breakwater", "groyne" }  -- "storage_tank", "water_tap", "dyke", "lighthouse"
aerowayClasses  = Set { "taxiway", "hangar", "runway", "helipad", "aerodrome", "airstrip", "tower" }
aerowayBuildings = Set { "terminal", "gate", "tower" }

transitRoutes = { train = 8, subway = 10, tram = 12, share_taxi = 12, light_rail = 12, bus = 14, trolleybus = 14 }
otherRoutes = { road = 8, ferry = 9, bicycle = 10, hiking = 10, foot = 12, mtb = 10, ski = 12 }  --piste = 12,
--ignoredRoutes = Set { "power", "railway", "detour", "tracks", "horse", "emergency_access", "snowmobile", "historic", "running", "fitness_trail" }

-- Scan relations for use in ways

function relation_scan_function(rel)
  local reltype = Find("type");
  if reltype == "boundary" then
    local bndtype = Find("boundary")
    if boundaryValues[bndtype] or parkValues[bndtype] then
      Accept()
    end
  elseif reltype == "route" then
    Accept()
  end
end

-- process relations for public transport routes

function relation_function(rel)
  local reltype = Find("type");
  if reltype=="route" then
    local route = Find("route")
    if route == "ferry" then
      Layer("transportation", false)
      Attribute("route", "ferry")
      MinZoom(9)
      SetNameAttributes(rel, 12, "relation")
      return
    elseif transitRoutes[route] then
      Layer("transit", false)
      MinZoom(transitRoutes[route])
    elseif otherRoutes[route] then
      Layer("transportation", false)
      MinZoom(otherRoutes[route])
    else
      return
    end
    Attribute("class", "route")
    Attribute("route", route)
    Attribute("name", Find("name"))
    Attribute("ref", Find("ref"))
    Attribute("network", Find("network"))
    Attribute("color", Find("colour"))
    Attribute("osm_id", Id())
    Attribute("osm_type", "relation")
  elseif reltype=="boundary" then
    local boundary = Find("boundary")
    if parkValues[boundary] then
      local leisure = Find("leisure")
      local protect_class = Find("protect_class")
      -- tilemaker doesn't calculate area for relations
      local area = Area();
      Layer("landuse", true)
      MinZoom(8)  --SetMinZoomByArea(rel, area)
      Attribute("class", boundary)
      Attribute("boundary", boundary)
      if leisure~="" then Attribute("leisure", leisure) end
      if protect_class~="" then Attribute("protect_class", protect_class) end
      SetNameAttributes(rel, 0, "relation")
      AttributeNumeric("area", area)
      -- write POI at centroid
      LayerAsCentroid("poi")
      MinZoom(8)  --SetMinZoomByArea(rel, area)
      Attribute("class", boundary)
      Attribute("boundary", boundary)
      if leisure~="" then Attribute("leisure", leisure) end
      if protect_class~="" then Attribute("protect_class", protect_class) end
      SetNameAttributes(rel, 0, "relation")
      AttributeNumeric("area", area)
    end
  end
end

-- Process way tags

function way_function(way)
  local route    = Find("route")
  local highway  = Find("highway")
  local waterway = Find("waterway")
  local water    = Find("water")
  local building = Find("building")
  local natural  = Find("natural")
  --local historic = Find("historic")
  local landuse  = Find("landuse")
  local leisure  = Find("leisure")
  local amenity  = Find("amenity")
  local aeroway  = Find("aeroway")
  local railway  = Find("railway")
  local service  = Find("service")
  --local sport    = Find("sport")
  --local shop     = Find("shop")
  local tourism  = Find("tourism")
  local man_made = Find("man_made")
  local boundary = Find("boundary")
  local housenumber = Find("addr:housenumber")
  local construction = Find("construction")
  local piste_diff = Find("piste:difficulty")
  local aerialway = Find("aerialway")
  local isClosed = IsClosed()

  -- Miscellaneous preprocessing
  if Find("disused") == "yes" then return end
  if boundary~="" and Find("protection_title")=="National Forest" and Find("operator")=="United States Forest Service" then return end
  if highway == "proposed" then return end
  if aerowayBuildings[aeroway] then building="yes"; aeroway="" end
  if landuse == "field" then landuse = "farmland" end
  if landuse == "meadow" and Find("meadow")=="agricultural" then landuse="farmland" end

  -- Boundaries
  while true do
    local rel = NextRelation()
    if not rel then break end
    local bndry = FindInRelation("boundary")
    if bndry=="administrative" or bndry=="disputed" then
      WriteBoundary(bndry, way, rel)
      boundary = ""  -- if way is part of boundary relation, do not write as standalone boundary
    end
  end

  if boundary=="administrative" or boundary=="disputed" then
    WriteBoundary(boundary, way, nil)
  end

  -- Roads ('transportation' and 'transportation_name', plus 'transportation_name_detail')
  if highway~="" then
    local access = Find("access")
    if access=="private" or access=="no" then return end
    -- most footways are sidewalks or crossings, which are mapped inconsistently so just add clutter and
    --  confusion the map; we could consider keeping footway=="alley"
    if highway == "footway" and Find("footway") ~= "" then return end

    local h = highway
    local minzoom = 99
    local layer = "transportation"
    if majorRoadValues[highway] then              minzoom = 4 end
    if highway == "trunk"       then              minzoom = 5
    elseif highway == "primary" then              minzoom = 7 end
    if mainRoadValues[highway]  then              minzoom = 9 end
    if midRoadValues[highway]   then              minzoom = 11 end
    if minorRoadValues[highway] then h = "minor"; minzoom = 12 end
    if trackValues[highway]     then h = "track"; minzoom = 10 end  -- was 14 ... include paths at lower zoom for hiking map
    if pathValues[highway]      then h = "path" ; minzoom = 10 end  -- was 14
    if h=="service"             then              minzoom = 12 end

    -- Links (ramp)
    local ramp=false
    if linkValues[highway] then
      splitHighway = split(highway, "_")
      highway = splitHighway[1]; h = highway
      ramp = true
      minzoom = 11
    end

    -- Construction
    if highway == "construction" then
      if constructionValues[construction] then
        h = construction .. "_construction"
        if construction ~= "service" and construction ~= "track" then
          minzoom = 11
        else
          minzoom = 12
        end
      else
        h = "minor_construction"
        minzoom = 14
      end
    end

    -- Write to layer
    if minzoom <= 14 then
      Layer(layer, false)
      MinZoom(minzoom)
      SetZOrder(way)
      Attribute("class", h)
      Attribute("highway", highway)  -- we want to start using OSM tags instead of class ... if h~=highway then
      SetBrunnelAttributes(way)
      if ramp then AttributeNumeric("ramp",1) end

      -- Service
      if highway == "service" and service ~="" then Attribute("service", service) end

      local oneway = Find("oneway")
      if oneway == "yes" or oneway == "1" then
        AttributeNumeric("oneway",1)
      end
      if oneway == "-1" then
        -- **** TODO
      end

      -- cycling
      local cycleway = Find("cycleway")
      if cycleway == "" then
        cycleway = Find("cycleway:both")
      end
      if cycleway ~= "" and cycleway ~= "no" then
        Attribute("cycleway", cycleway)
      end

      local cycleway_left = Find("cycleway:left")
      if cycleway_left ~= "" and cycleway_left ~= "no" then
        Attribute("cycleway_left", cycleway_left)
      end

      local cycleway_right = Find("cycleway:right")
      if cycleway_right ~= "" and cycleway_right ~= "no" then
        Attribute("cycleway_right", cycleway_right)
      end

      local bicycle = Find("bicycle")
      if bicycle ~= "" and bicycle ~= "no" then
        Attribute("bicycle", bicycle)
      end

      -- surface
      local surface = Find("surface")
      if pavedValues[surface] then
        Attribute("surface", "paved")
      elseif unpavedValues[surface] then
        Attribute("surface", "unpaved")
      end

      local trailvis = Find("trail_visibility")
      if trailvis ~= "" and trailvis ~= "good" and trailvis ~= "excellent" then
        Attribute("trail_visibility", trailvis)
      end

      local mtbscale = Find("mtb:scale")  -- mountain biking difficulty rating
      if mtbscale ~= "" then
        Attribute("mtb_scale", mtbscale)
      end

      if highway == "path" and Find("golf") ~= "" then
        Attribute("subclass", "golf")
      end

      -- Write names
      --"transportation_name":        { "minzoom": 8,  "maxzoom": 14 },
      --"transportation_name_mid":    { "minzoom": 12, "maxzoom": 14, "write_to": "transportation_name" },
      --"transportation_name_detail": { "minzoom": 14, "maxzoom": 14, "write_to": "transportation_name" },
      --if minzoom < 8 then
      --  minzoom = 8
      --end
      if highway == "motorway" or highway == "trunk" then
        --Layer("transportation_name", false)
        minzoom = math.max(minzoom, 8)  --MinZoom(minzoom)
      elseif h == "minor" or h == "track" or h == "path" or h == "service" then
        --Layer("transportation_name_detail", false)
        minzoom = math.max(minzoom, 14)  --MinZoom(minzoom)
      else
        --Layer("transportation_name_mid", false)
        minzoom = math.max(minzoom, 12)  --MinZoom(minzoom)
      end
      SetNameAttributes(way, minzoom)
      --Attribute("class",h)
      --if h~=highway then Attribute("subclass",highway) end
      --Attribute("network","road") -- **** could also be us-interstate, us-highway, us-state
      local maxspeed = Find("maxspeed")
      if maxspeed~="" then
        Attribute("maxspeed",maxspeed)
      end
      local lanes = Find("lanes")
      if lanes~="" then
        Attribute("lanes",lanes)
      end
      local ref = Find("ref")
      if ref~="" then
        Attribute("ref",ref)
        --AttributeNumeric("ref_length",ref:len())
      end
    end
  end

  -- Railways ('transportation' and 'transportation_name', plus 'transportation_name_detail')
  if railway~="" then
    Layer("transportation", false)
    Attribute("class", "rail")
    Attribute("railway", railway)
    SetZOrder(way)
    SetBrunnelAttributes(way)
    SetNameAttributes(way, 14)
    if service~="" then
      Attribute("service", service)
      MinZoom(12)
    else
      MinZoom(9)
    end
  end

  -- Pier, breakwater, etc.
  if manMadeClasses[man_made] then  --man_made=="pier" then
    Layer("landuse", isClosed)
    SetZOrder(way)
    Attribute("class", man_made)
    Attribute("man_made", man_made)
    SetMinZoomByArea(way)
  end

  -- 'Ferry'
  if route=="ferry" then
    Layer("transportation", false)
    --Attribute("class", "ferry")
    Attribute("route", route)
    --SetZOrder(way)
    MinZoom(9)
    SetBrunnelAttributes(way)
    SetNameAttributes(way, 12)
  end

  if piste_diff~="" then
    local piste_type = Find("piste:type")
    local grooming = Find("piste:grooming")
    Layer("transportation", isClosed)
    Attribute("class", "piste")
    Attribute("route", "piste")
    Attribute("difficulty", piste_diff)
    if piste_type~="" then Attribute("piste_type", piste_type) end
    if grooming~="" then Attribute("piste_grooming", grooming) end  -- so we can ignore backcountry "pistes"
    MinZoom(10)
    SetNameAttributes(way, 14)
  end

  if aerialway~="" then
    Layer("transportation", false)
    Attribute("class", "aerialway")
    Attribute("aerialway", aerialway)
    MinZoom(10)
    SetNameAttributes(way, 14)
  end

  -- 'Aeroway'
  if aerowayClasses[aeroway] then
    Layer("transportation", isClosed)  --"aeroway"
    MinZoom(10)
    Attribute("aeroway", aeroway)
    Attribute("ref", Find("ref"))
    --write_name = true
    if aeroway=="aerodrome" then
      --LayerAsCentroid("aerodrome_label")
      SetNameAttributes(way)
      SetEleAttributes(way)
      Attribute("iata", Find("iata"))
      Attribute("icao", Find("icao"))
      local aerodrome = Find("aerodrome")
      Attribute("aerodrome", aerodromeValues[aerodrome] and aerodrome or "other")
      AttributeNumeric("area", Area())
    end
  end

  -- Set 'building' and associated
  if building~="" then
    Layer("building", true)
    SetBuildingHeightAttributes(way)
    SetMinZoomByArea(way)

    -- housenumber is also commonly set on poi nodes, but not very useful w/o at least street name too
    --"housenumber":      { "minzoom": 14, "maxzoom": 14 },
    if housenumber~="" then
      --LayerAsCentroid("housenumber", false)
      Attribute("housenumber", housenumber, 14)
    end
  end

  -- waterway is single way indicating course of a waterway - wide rivers, etc. have additional polygons to map area
  if waterwayClasses[waterway] and not isClosed then
    if waterway == "river" and Holds("name") then
      Layer("waterway", false)
    else
      Layer("waterway_detail", false)
    end
    if Find("intermittent")=="yes" then AttributeNumeric("intermittent", 1) end
    Attribute("class", waterway)
    Attribute("waterway", waterway)
    SetNameAttributes(way)
    SetBrunnelAttributes(way)
  elseif waterway == "boatyard"  then Layer("landuse", isClosed); Attribute("class", "industrial"); MinZoom(12)
  elseif waterway == "dam"       then Layer("building", isClosed)
  elseif waterway == "fuel"      then Layer("landuse", isClosed); Attribute("class", "industrial"); MinZoom(14)
  end

  -- Water areas (closed ways)
  local waterbody = ""
  if waterLanduse[landuse] then waterbody = landuse
  elseif waterwayAreas[waterway] then waterbody = waterway
  elseif leisure=="swimming_pool" then waterbody = leisure
  elseif natural=="water" or natural=="bay" then waterbody = natural
  end

  if waterbody~="" then
    if Find("covered")=="yes" or not isClosed then return end
    local class="lake"; if natural=="bay" then class="ocean" elseif waterway~="" then class="river" end
    if class=="lake" and Find("wikidata")=="Q192770" then return end  -- crazy lake in Finland
    if class=="ocean" and isClosed and (AreaIntersecting("ocean")/Area() > 0.98) then return end
    Layer("water", true)
    SetMinZoomByArea(way)
    Attribute("class", class)
    Attribute("water", water~="" and water or waterbody)

    if Find("intermittent")=="yes" then Attribute("intermittent",1) end
    -- don't include names for minor man-made basins (e.g. way 25958687) or rivers, which have name on waterway way
    if Holds("name") and natural=="water" and not noNameWater[water] then
      --LayerAsCentroid("water_name_detail")
      SetNameAttributes(way, 14)
      --SetMinZoomByArea(way)
      --Attribute("class", class)
      AttributeNumeric("area", Area())
    end

    return -- in case we get any landuse processing
  end

  -- special case since valleys are mapped as ways
  if natural=="valley" then
    local len = Length()
    Layer("landuse", false)
    SetMinZoomByArea(way, len*len);
    Attribute("natural", natural)
    SetNameAttributes(way)
  end

  -- landuse/landcover
  --"landcover":        { "minzoom":  0, "maxzoom": 14, "simplify_below": 13, "simplify_level": 0.0003, "simplify_ratio": 2, "write_to": "landuse" },
  --"park":             { "minzoom": 11, "maxzoom": 14 },
  local landuse_poi = false
  if landuseAreas[landuse] or naturalAreas[natural] or leisureAreas[leisure] or amenityAreas[amenity] or tourismAreas[tourism] then
    Layer("landuse", true)
    SetMinZoomByArea(way)
    --Attribute("class", landuseKeys[l])
    --if landuse=="residential" and Area()<ZRES8^2 then MinZoom(8) else SetMinZoomByArea(way) end
    if landuse~="" then Attribute("landuse", landuse) end
    if natural~="" then Attribute("natural", natural) end
    if leisure~="" then Attribute("leisure", leisure) end
    if amenity~="" then Attribute("amenity", amenity) end
    if tourism~="" then Attribute("tourism", tourism) end
    if natural=="wetland" then Attribute("wetland", Find("wetland")) end
    landuse_poi = true
  end

  -- Parks
  local park_boundary = parkValues[boundary]
  if park_boundary or leisure=="nature_reserve" then
    local protect_class = Find("protect_class")
    Layer("landuse", true)
    SetMinZoomByArea(way)
    Attribute("class", park_boundary and boundary or leisure)
    if park_boundary then Attribute("boundary", boundary) end
    if leisure~="" then Attribute("leisure", leisure) end
    if protect_class~="" then Attribute("protect_class", protect_class) end
    SetNameAttributes(way)
    landuse_poi = true
  end

  -- POIs ('poi' and 'poi_detail')
  if NewWritePOI(way, landuse_poi and Area() or 0) then  -- empty
  elseif (building~="" or landuse_poi) and Holds("name") then
    LayerAsCentroid("poi_detail")
    SetNameAttributes(way)
    if landuse_poi then AttributeNumeric("area", Area()) end
  end
end

-- attributes for features from shapefiles (set in config.json)
-- we set featurecla so scene YAML can distinguish features from Natural Earth
-- an alternative approach to using shapefiles is to convert shapefiles to OSM PBF using ogr2osm script and
--  pass multiple PBFs to Tilemaker
function attribute_function(attr, layer)
  local featurecla = attr["featurecla"]

  if featurecla=="Glaciated areas" then
    return { class="ice", natural="glacier" }
  elseif featurecla=="Antarctic Ice Shelf" then
    return { class="ice", natural="glacier", glacier_type="shelf" }
  elseif featurecla=="Urban area" then
    return { class="residential" }
  elseif layer=="ocean" then
    return { class="ocean" }

  -- ne_10m_lakes
  elseif layer=="ne_lakes" then
    return { class="lake", water="lake", name=attr["name"], wikidata=attr["wikidataid"], rank=attr["scalerank"], featurecla=featurecla }

  -- ne_10m_admin_0_boundary_lines_land; can't use ne_10m_admin_0_countries because it includes shoreline
  elseif layer=="ne_boundaries" then
    local res = { admin_level=2, adm0_l=attr["adm0_left"], adm0_r=attr["adm0_right"], featurecla=featurecla }
    if featurecla~="International boundary (verify)" then res["disputed"] = 1 end
    return res

  -- ne_10m_populated_places
  elseif layer=="ne_populated_places" then
    local rank = attr["scalerank"]
    local z = rank < 6 and 3 or 6
    return { _minzoom=z, class="city", place="city", name=attr["name"], population=attr["pop_max"], rank=rank, wikidata=attr["wikidataid"], featurecla=featurecla }

  -- ne_10m_roads
  elseif layer=="ne_roads" then
    if featurecla=="Ferry" then
      return { _minzoom=3, class="ferry", route="ferry", ref=attr["name"], rank=attr["scalerank"], featurecla=featurecla }
    elseif attr["expressway"] == 1 then
      return { _minzoom=3, class="motorway", highway="motorway", ref=attr["name"], rank=attr["scalerank"], featurecla=featurecla }
    end
    return { _minzoom=6, class="trunk", highway="trunk", ref=attr["name"], rank=attr["scalerank"], featurecla=featurecla }

  else
    return attr
  end
end

-- ==========================================================
-- Common functions

extraPoiTags = Set { "cuisine", "station", "religion", "operator" }  -- atm:operator
function NewWritePOI(obj, area, osm_type)
  for k,lists in pairs(poiTags) do
    local val = Find(k)
    if val ~= "" then
      if type(next(lists)) ~= "number" then lists = { [poiMinZoom] = lists } end
      for minzoom, list in pairs(lists) do
        local exclude = list["__EXCLUDE"] == true
        if next(list) == nil or (exclude and not list[val] or list[val]) then
          LayerAsCentroid("poi")
          MinZoom(area > 0 and 12 or minzoom)
          SetNameAttributes(obj, 0, osm_type)
          if area > 0 then AttributeNumeric("area", area) end
          -- write value for all tags in poiTags (if present)
          for tag, _ in pairs(poiTags) do
            local v = Find(tag)
            if v ~= "" then Attribute(tag, v) end
          end
          for tag, _ in pairs(extraPoiTags) do
            local v = Find(tag)
            if v ~= "" then Attribute(tag, v) end
          end
          return true
        end
      end
    end
  end
  return false
end

-- Set name attributes on any object
function SetNameAttributes(obj, minzoom, osm_type)
  local name = Find("name"), iname
  local main_written = name
  minzoom = minzoom or 0
  osm_type = osm_type or (Find("type") == "multipolygon" and "relation" or "way");
  -- if we have a preferred language, then write that (if available), and additionally write the base name tag
  if preferred_language and Holds("name:"..preferred_language) then
    iname = Find("name:"..preferred_language)
    Attribute(preferred_language_attribute, iname)
    if iname~=name and default_language_attribute then
      Attribute(default_language_attribute, name)
    else main_written = iname end
  else
    Attribute(preferred_language_attribute, name)
  end
  -- then set any additional languages
  for i,lang in ipairs(additional_languages) do
    iname = Find("name:"..lang)
    if iname=="" then iname=name end
    if iname~=main_written then Attribute("name_"..lang, iname) end
  end
  -- add OSM id
  Attribute("osm_id", Id())
  Attribute("osm_type", osm_type)
end

-- Set ele and ele_ft on any object
function SetEleAttributes(obj)
    local ele = Find("ele")
  if ele ~= "" then
    local meter = math.floor(tonumber(ele) or 0)
    --local feet = math.floor(meter * 3.2808399)
    AttributeNumeric("ele", meter)
    --AttributeNumeric("ele_ft", feet)
  end
end

function SetBrunnelAttributes(obj)
  if     Find("bridge") == "yes" then Attribute("brunnel", "bridge")
  elseif Find("tunnel") == "yes" then Attribute("brunnel", "tunnel")
  elseif Find("ford")   == "yes" then Attribute("brunnel", "ford")
  end
end

-- Set minimum zoom level by area
function SetMinZoomByArea(way, area)
  area = area or Area()
  if     area>ZRES5^2  then MinZoom(6)
  elseif area>ZRES6^2  then MinZoom(7)
  elseif area>ZRES7^2  then MinZoom(8)
  elseif area>ZRES8^2  then MinZoom(9)
  elseif area>ZRES9^2  then MinZoom(10)
  elseif area>ZRES10^2 then MinZoom(11)
  elseif area>ZRES11^2 then MinZoom(12)
  elseif area>ZRES12^2 then MinZoom(13)
  else                      MinZoom(14) end
end

function SetBuildingHeightAttributes(way)
  local height = tonumber(Find("height"), 10)
  local minHeight = tonumber(Find("min_height"), 10)
  local levels = tonumber(Find("building:levels"), 10)
  local minLevel = tonumber(Find("building:min_level"), 10)

  local renderHeight = BUILDING_FLOOR_HEIGHT
  if height or levels then
    renderHeight = height or (levels * BUILDING_FLOOR_HEIGHT)
  end
  local renderMinHeight = 0
  if minHeight or minLevel then
    renderMinHeight = minHeight or (minLevel * BUILDING_FLOOR_HEIGHT)
  end

  -- Fix upside-down buildings
  if renderHeight < renderMinHeight then
    renderHeight = renderHeight + renderMinHeight
  end

  AttributeNumeric("height", renderHeight)
  AttributeNumeric("min_height", renderMinHeight)
  -- TODO: remove these
  AttributeNumeric("render_height", renderHeight)
  AttributeNumeric("render_min_height", renderMinHeight)
end

-- Implement z_order as calculated by Imposm
-- See https://imposm.org/docs/imposm3/latest/mapping.html#wayzorder for details.
function SetZOrder(way)
  local highway = Find("highway")
  local layer = tonumber(Find("layer"))
  local bridge = Find("bridge")
  local tunnel = Find("tunnel")
  local zOrder = 0
  if bridge ~= "" and bridge ~= "no" then
    zOrder = zOrder + 10
  elseif tunnel ~= "" and tunnel ~= "no" then
    zOrder = zOrder - 10
  end
  if not (layer == nil) then
    if layer > 7 then
      layer = 7
    elseif layer < -7 then
      layer = -7
    end
    zOrder = zOrder + layer * 10
  end
  local hwClass = 0
  -- See https://github.com/omniscale/imposm3/blob/53bb80726ca9456e4a0857b38803f9ccfe8e33fd/mapping/columns.go#L251
  if highway == "motorway" then
    hwClass = 9
  elseif highway == "trunk" then
    hwClass = 8
  elseif highway == "primary" then
    hwClass = 6
  elseif highway == "secondary" then
    hwClass = 5
  elseif highway == "tertiary" then
    hwClass = 4
  else
    hwClass = 3
  end
  zOrder = zOrder + hwClass
  ZOrder(zOrder)
end

function WriteBoundary(boundary, way, relation)
  local adm = relation and FindInRelation("admin_level") or Find("admin_level")
  local admin_level = math.min(11, tonumber(adm) or 11)
  local mz = 0
  if     admin_level>=3 and admin_level<5 then mz=4
  elseif admin_level>=5 and admin_level<7 then mz=8
  elseif admin_level==7 then mz=10
  elseif admin_level>=8 then mz=12
  end
  Layer("boundary", false)
  MinZoom(mz)
  AttributeNumeric("admin_level", admin_level)
  -- names
  if relation then
    local name = FindInRelation("name")
    if name~="" then Attribute("name", name) end
    for i,lang in ipairs(additional_languages) do
      local iname = FindInRelation("name:"..lang)
      if iname~="" and iname~=name then Attribute("name_"..lang, iname) end
    end
    Attribute("osm_id", relation)  --[1])
    Attribute("osm_type", "relation")
  else
    SetNameAttributes(way)
  end
  -- flags
  if Find("maritime")=="yes" or (relation and FindInRelation("maritime")=="yes") then
    AttributeNumeric("maritime", 1)
  end
  if boundary=="disputed" or Find("disputed")=="yes" or (relation and FindInRelation("disputed")=="yes") then
    AttributeNumeric("disputed", 1)
  end
end

-- ==========================================================
-- Lua utility functions

function split(inputstr, sep) -- https://stackoverflow.com/a/7615129/4288232
  if sep == nil then
    sep = "%s"
  end
  local t={} ; i=1
  for str in string.gmatch(inputstr, "([^"..sep.."]+)") do
    t[i] = str
    i = i + 1
  end
  return t
end

-- vim: tabstop=2 shiftwidth=2 noexpandtab
