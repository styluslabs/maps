-- Data processing based on openmaptiles.org schema
-- https://openmaptiles.org/schema/
-- Copyright (c) 2016, KlokanTech.com & OpenMapTiles contributors.
-- Used under CC-BY 4.0

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
  local aeroway = node:Find("aeroway")
  if aeroway=="aerodrome" then
    node:Layer("transportation", false)  --"aeroway"
    node:MinZoom(11)
    node:Attribute("aeroway", aeroway)
    node:Attribute("ref", node:Find("ref"))
    SetNameAttributesEx(node, "node")
    SetEleAttributes(node)
    node:Attribute("iata", node:Find("iata"))
    node:Attribute("icao", node:Find("icao"))
    local aerodrome = node:Find("aerodrome")
    node:Attribute("aerodrome", aerodromeValues[aerodrome] and aerodrome or "other")
  end

  -- Write 'housenumber'
  --local housenumber = node:Find("addr:housenumber")
  --if housenumber~="" then
  --  node:Layer("housenumber", false)
  --  node:Attribute("housenumber", housenumber)
  --end

  -- Write 'place'
  -- note that OpenMapTiles has a rank for countries (1-3), states (1-6) and cities (1-10+);
  --   we could potentially approximate it for cities based on the population tag
  local place = node:Find("place")
  if place ~= "" then
    local rank = nil
    local mz = 13
    local pop = tonumber(node:Find("population")) or 0
    local placeCN = node:Find("place:CN")

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

    node:Layer("place", false)
    node:Attribute("class", place)
    node:Attribute("place", place)
    node:MinZoom(mz)
    if rank then node:AttributeNumeric("rank", rank) end
    if pop then node:AttributeNumeric("population", pop) end
    if place=="country" then node:Attribute("iso_a2", node:Find("ISO3166-1:alpha2")) end
    if placeCN ~= "" then node:Attribute("place_CN", placeCN) end
    SetNameAttributesEx(node, "node")
    return
  end

  -- Write 'poi'
  --local rank, class, subclass = GetPOIRank(node)
  --if rank then WritePOI(node,class,subclass,rank) end
  if NewWritePOI(node, 0) then
    SetNameAttributesEx(node, "node")
  end

  -- Write 'mountain_peak' and 'water_name'
  local natural = node:Find("natural")
  if natural == "peak" or natural == "volcano" then
    node:Layer("mountain_peak", false)
    SetEleAttributes(node)
    node:AttributeNumeric("rank", 1)
    node:Attribute("class", natural)
    SetNameAttributesEx(node, "node")
    return
  end
  if natural == "bay" then
    node:Layer("water", false)  --node:Layer("water_name", false)
    SetNameAttributesEx(node, "node", 14)
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

aerowayBuildings = Set { "terminal", "gate", "tower" }
parkValues = Set { "protected_area", "national_park" }
landuseKeys = { school="school", university="school", kindergarten="school", college="school", library="library",
    hospital="hospital", railway="railway", bus_station="bus_station", cemetery="cemetery",
    military="military", residential="residential", commercial="commercial", industrial="industrial",
    retail="retail", stadium="stadium", pitch="pitch", playground="playground", theme_park="theme_park", zoo="zoo",
    -- previous landcover list
    wood="forest", forest="forest", wetland="wetland", beach="sand", sand="sand", glacier="ice", ice_shelf="ice",
    farmland="farmland", farm="farmland", orchard="farmland", vineyard="farmland", plant_nursery="farmland",
    grassland="grass", grass="grass", meadow="grass", allotments="grass",
    park="park", garden="park", recreation_ground="park", village_green="park", golf_course="golf_course" }

-- POI key/value pairs: based on https://github.com/openmaptiles/openmaptiles/blob/master/layers/poi/mapping.yaml
poiMinZoom = 14
poiTags = { aerialway = Set { "station" },
          -- all amenity values with count > 1000 (as of Jan 2024) we wish to exclude
          amenity = { [12] = Set { "bus_station" }, [poiMinZoom] = Set { "__EXCLUDE", "bus_station", "parking_space", "bench", "shelter", "waste_basket", "bicycle_parking", "recycling", "hunting_stand", "vending_machine", "post_box", "parking_entrance", "telephone", "bbq", "motorcycle_parking", "grit_bin", "clock", "letter_box", "watering_place", "loading_dock", "payment_terminal", "mobile_money_agent", "trolley_bay", "ticket_validator", "lounger", "feeding_place", "vacuum_cleaner", "game_feeding", "smoking_area", "photo_booth", "kneipp_water_cure", "table", "fixme", "office", "chair" } },
          barrier = Set { "bollard", "border_control", "cycle_barrier", "gate", "lift_gate", "sally_port", "stile", "toll_booth" },
          building = Set { "dormitory" },
          highway = { [12] = Set { "bus_stop" }, [poiMinZoom] = Set { "traffic_signals" } },
          historic = Set { "monument", "castle", "ruins" },
          landuse = Set { "basin", "brownfield", "cemetery", "reservoir", "winter_sports" },
          leisure = Set { "dog_park", "escape_game", "fitness_centre", "garden", "golf_course", "ice_rink", "hackerspace", "marina", "miniature_golf", "park", "pitch", "playground", "sports_centre", "stadium", "swimming_area", "swimming_pool", "water_park" },
          railway = { [12] = Set { "halt", "station", "tram_stop" }, [poiMinZoom] = Set { "subway_entrance", "train_station_entrance" } },
          shop = {},
          sport = {},
          tourism = { [12] = Set { "attraction", "viewpoint" }, [poiMinZoom] = Set { "alpine_hut", "aquarium", "artwork", "bed_and_breakfast", "camp_site", "caravan_site", "chalet", "gallery", "guest_house", "hostel", "hotel", "information", "motel", "museum", "picnic_site", "theme_park", "zoo" } },
          waterway = Set { "dock" } }

-- POI "class" values: based on https://github.com/openmaptiles/openmaptiles/blob/master/layers/poi/poi.yaml
poiClasses      = { townhall="town_hall", public_building="town_hall", courthouse="town_hall", community_centre="town_hall",
          golf="golf", golf_course="golf", miniature_golf="golf",
          fast_food="fast_food", food_court="fast_food",
          park="park", bbq="park",
          bus_stop="bus", bus_station="bus",
          subway_entrance="entrance", train_station_entrance="entrance",
          camp_site="campsite", caravan_site="campsite",
          laundry="laundry", dry_cleaning="laundry",
          supermarket="grocery", deli="grocery", delicatessen="grocery", greengrocer="grocery", marketplace="grocery",  --department_store="grocery"
          library="library",  --books="library",
          university="college", college="college",
          hotel="lodging", motel="lodging", bed_and_breakfast="lodging", guest_house="lodging", hostel="lodging", chalet="lodging", alpine_hut="lodging", dormitory="lodging",
          chocolate="ice_cream", confectionery="ice_cream",
          post_box="post",  post_office="post",
          cafe="cafe", restaurant="restaurant", fast_food="restaurant",
          school="school",  kindergarten="school",
          alcohol="alcohol_shop", wine="alcohol_shop",  --beverages="alcohol_shop",
          bar="bar", nightclub="bar",
          marina="harbor", dock="harbor",
          car="car", car_repair="car", taxi="car",
          hospital="hospital", nursing_home="hospital",  clinic="hospital",
          grave_yard="cemetery", cemetery="cemetery",
          attraction="attraction", viewpoint="attraction",
          biergarten="beer", pub="beer",
          music="music", musical_instrument="music",
          american_football="stadium", stadium="stadium", soccer="stadium",
          art="art_gallery", artwork="art_gallery", gallery="art_gallery", arts_centre="art_gallery",
          bag="clothing_store", clothes="clothing_store",
          swimming_area="swimming", swimming="swimming",
          castle="castle", ruins="castle" }
poiClassRanks   = { hospital=1, railway=2, bus=3, attraction=4, harbor=5, college=6,
          school=7, stadium=8, zoo=9, town_hall=10, campsite=11, cemetery=12,
          park=13, library=14, police=15, post=16, golf=17, shop=18, grocery=19,
          fast_food=20, clothing_store=21, bar=22 }

waterClasses    = Set { "river", "riverbank", "stream", "canal", "drain", "ditch", "dock" }
waterwayClasses = Set { "stream", "river", "canal", "drain", "ditch" }
manMadeClasses  = Set { "pier", "breakwater", "groyne" }  -- "storage_tank", "water_tap", "dyke", "lighthouse"

transitRoutes = { train = 8, subway = 10, tram = 12, share_taxi = 12, light_rail = 12, bus = 14, trolleybus = 14 }
otherRoutes = { road = 8, ferry = 9, bicycle = 10, hiking = 10, foot = 12, mtb = 10, ski = 12 }  --piste = 12,
--ignoredRoutes = Set { "power", "railway", "detour", "tracks", "horse", "emergency_access", "snowmobile", "historic", "running", "fitness_trail" }

-- Scan relations for use in ways

function relation_scan_function(relation)
  local reltype = relation:Find("type");
  if reltype == "boundary" and relation:Find("boundary")=="administrative" then
    relation:Accept()
  elseif reltype == "route" then
    relation:Accept()
  end
end

-- process relations for public transport routes

function relation_function(relation)
  if relation:Find("type")=="route" then
    local route = relation:Find("route")
    if route == "ferry" then
      relation:Layer("transportation", false)
      relation:Attribute("class", "ferry")
      relation:MinZoom(9)
      SetNameAttributesEx(relation, "relation", 12)
      return
    elseif transitRoutes[route] then
      relation:Layer("transit", false)
      relation:MinZoom(transitRoutes[route])
    elseif otherRoutes[route] then
      relation:Layer("transportation", false)
      relation:MinZoom(otherRoutes[route])
    else
      return
    end
    relation:Attribute("class", "route")
    relation:Attribute("route", route)
    relation:Attribute("name", relation:Find("name"))
    relation:Attribute("ref", relation:Find("ref"))
    relation:Attribute("network", relation:Find("network"))
    relation:Attribute("color", relation:Find("colour"))
    relation:Attribute("osm_id", relation:Id())
    relation:Attribute("osm_type", "relation")
  end
end

-- Process way tags

function way_function(way)
  local route    = way:Find("route")
  local highway  = way:Find("highway")
  local waterway = way:Find("waterway")
  local water    = way:Find("water")
  local building = way:Find("building")
  local natural  = way:Find("natural")
  --local historic = way:Find("historic")
  local landuse  = way:Find("landuse")
  local leisure  = way:Find("leisure")
  local amenity  = way:Find("amenity")
  local aeroway  = way:Find("aeroway")
  local railway  = way:Find("railway")
  local service  = way:Find("service")
  --local sport    = way:Find("sport")
  --local shop     = way:Find("shop")
  local tourism  = way:Find("tourism")
  local man_made = way:Find("man_made")
  local boundary = way:Find("boundary")
  local isClosed = way:IsClosed()
  local housenumber = way:Find("addr:housenumber")
  local write_name = false
  local construction = way:Find("construction")
  local piste_diff = way:Find("piste:difficulty")
  local aerialway = way:Find("aerialway")

  -- Miscellaneous preprocessing
  if way:Find("disused") == "yes" then return end
  if boundary~="" and way:Find("protection_title")=="National Forest" and way:Find("operator")=="United States Forest Service" then return end
  if highway == "proposed" then return end
  if aerowayBuildings[aeroway] then building="yes"; aeroway="" end
  if landuse == "field" then landuse = "farmland" end
  if landuse == "meadow" and way:Find("meadow")=="agricultural" then landuse="farmland" end

  -- Boundaries within relations
  local admin_level = 11
  local isBoundary = false
  while true do
    local rel = way:NextRelation()
    if not rel then break end
    isBoundary = true
    admin_level = math.min(admin_level, tonumber(way:FindInRelation("admin_level")) or 11)
  end

  -- Boundaries in ways
  if boundary=="administrative" then
    admin_level = math.min(admin_level, tonumber(way:Find("admin_level")) or 11)
    isBoundary = true
  end

  -- Administrative boundaries
  -- https://openmaptiles.org/schema/#boundary
  if isBoundary and not (way:Find("maritime")=="yes") then
    local mz = 0
    if     admin_level>=3 and admin_level<5 then mz=4
    elseif admin_level>=5 and admin_level<7 then mz=8
    elseif admin_level==7 then mz=10
    elseif admin_level>=8 then mz=12
    end

    way:Layer("boundary",false)
    way:AttributeNumeric("admin_level", admin_level)
    way:MinZoom(mz)
    -- disputed status (0 or 1). some styles need to have the 0 to show it.
    local disputed = way:Find("disputed")
    if disputed=="yes" then
      way:AttributeNumeric("disputed", 1)
    else
      way:AttributeNumeric("disputed", 0)
    end
  end

  -- Roads ('transportation' and 'transportation_name', plus 'transportation_name_detail')
  if highway~="" then
    local access = way:Find("access")
    if access=="private" or access=="no" then return end
    -- most footways are sidewalks or crossings, which are mapped inconsistently so just add clutter and
    --  confusion the map; we could consider keeping footway=="alley"
    if highway == "footway" and way:Find("footway") ~= "" then return end

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
      way:Layer(layer, false)
      way:MinZoom(minzoom)
      SetZOrder(way)
      way:Attribute("class", h)
      way:Attribute("highway", highway)  -- we want to start using OSM tags instead of class ... if h~=highway then
      SetBrunnelAttributes(way)
      if ramp then way:AttributeNumeric("ramp",1) end

      -- Service
      if highway == "service" and service ~="" then way:Attribute("service", service) end

      local oneway = way:Find("oneway")
      if oneway == "yes" or oneway == "1" then
        way:AttributeNumeric("oneway",1)
      end
      if oneway == "-1" then
        -- **** TODO
      end

      -- cycling
      local cycleway = way:Find("cycleway")
      if cycleway == "" then
        cycleway = way:Find("cycleway:both")
      end
      if cycleway ~= "" and cycleway ~= "no" then
        way:Attribute("cycleway", cycleway)
      end

      local cycleway_left = way:Find("cycleway:left")
      if cycleway_left ~= "" and cycleway_left ~= "no" then
        way:Attribute("cycleway_left", cycleway_left)
      end

      local cycleway_right = way:Find("cycleway:right")
      if cycleway_right ~= "" and cycleway_right ~= "no" then
        way:Attribute("cycleway_right", cycleway_right)
      end

      local bicycle = way:Find("bicycle")
      if bicycle ~= "" and bicycle ~= "no" then
        way:Attribute("bicycle", bicycle)
      end

      -- surface
      local surface = way:Find("surface")
      if pavedValues[surface] then
        way:Attribute("surface", "paved")
      elseif unpavedValues[surface] then
        way:Attribute("surface", "unpaved")
      end

      if highway == "path" and way:Find("golf") ~= "" then
        way:Attribute("subclass", "golf")
      end

      -- Write names
		  --"transportation_name":        { "minzoom": 8,  "maxzoom": 14 },
		  --"transportation_name_mid":    { "minzoom": 12, "maxzoom": 14, "write_to": "transportation_name" },
		  --"transportation_name_detail": { "minzoom": 14, "maxzoom": 14, "write_to": "transportation_name" },
      --if minzoom < 8 then
      --  minzoom = 8
      --end
      if highway == "motorway" or highway == "trunk" then
        --way:Layer("transportation_name", false)
        minzoom = math.max(minzoom, 8)  --way:MinZoom(minzoom)
      elseif h == "minor" or h == "track" or h == "path" or h == "service" then
        --way:Layer("transportation_name_detail", false)
        minzoom = math.max(minzoom, 14)  --way:MinZoom(minzoom)
      else
        --way:Layer("transportation_name_mid", false)
        minzoom = math.max(minzoom, 12)  --way:MinZoom(minzoom)
      end
      SetNameAttributes(way, minzoom)
      --way:Attribute("class",h)
      --if h~=highway then way:Attribute("subclass",highway) end
      --way:Attribute("network","road") -- **** could also be us-interstate, us-highway, us-state
      local maxspeed = way:Find("maxspeed")
      if maxspeed~="" then
        way:Attribute("maxspeed",maxspeed)
      end
      local lanes = way:Find("lanes")
      if lanes~="" then
        way:Attribute("lanes",lanes)
      end
      local ref = way:Find("ref")
      if ref~="" then
        way:Attribute("ref",ref)
        --way:AttributeNumeric("ref_length",ref:len())
      end
    end
  end

  -- Railways ('transportation' and 'transportation_name', plus 'transportation_name_detail')
  if railway~="" then
    way:Layer("transportation", false)
    way:Attribute("class", "rail")
    way:Attribute("railway", railway)
    SetZOrder(way)
    SetBrunnelAttributes(way)
    SetNameAttributes(way, 14)
    if service~="" then
      way:Attribute("service", service)
      way:MinZoom(12)
    else
      way:MinZoom(9)
    end
  end

  -- Pier, breakwater, etc.
  if manMadeClasses[man_made] then  --man_made=="pier" then
    way:Layer("landuse", isClosed)
    SetZOrder(way)
    way:Attribute("class", man_made)
    way:Attribute("man_made", man_made)
    SetMinZoomByArea(way)
  end

  -- 'Ferry'
  if route=="ferry" then
    way:Layer("transportation", false)
    way:Attribute("class", "ferry")
    way:Attribute("route", route)
    --SetZOrder(way)
    way:MinZoom(9)
    SetBrunnelAttributes(way)
    SetNameAttributes(way, 12)
  end

  if piste_diff~="" then
    way:Layer("transportation", isClosed)
    way:Attribute("class", "piste")
    way:Attribute("route", "piste")
    way:Attribute("difficulty", piste_diff)
    way:MinZoom(10)
    SetNameAttributes(way, 14)
  end

  if aerialway~="" then
    way:Layer("transportation", false)
    way:Attribute("class", "aerialway")
    way:Attribute("aerialway", aerialway)
    way:MinZoom(10)
    SetNameAttributes(way, 14)
  end

  -- 'Aeroway'
  if aeroway~="" then
    way:Layer("transportation", isClosed)  --"aeroway"
    way:MinZoom(10)
    way:Attribute("aeroway", aeroway)
    way:Attribute("ref", way:Find("ref"))
    write_name = true

    -- 'aerodrome_label'
    if aeroway=="aerodrome" then
      --way:LayerAsCentroid("aerodrome_label")
      SetNameAttributes(way)
      SetEleAttributes(way)
      way:Attribute("iata", way:Find("iata"))
      way:Attribute("icao", way:Find("icao"))
      local aerodrome = way:Find("aerodrome")
      way:Attribute("aerodrome", aerodromeValues[aerodrome] and aerodrome or "other")
    end
  end

  -- Set 'waterway' and associated
  if waterwayClasses[waterway] and not isClosed then
    if waterway == "river" and way:Holds("name") then
      way:Layer("waterway", false)
    else
      way:Layer("waterway_detail", false)
    end
    if way:Find("intermittent")=="yes" then way:AttributeNumeric("intermittent", 1) else way:AttributeNumeric("intermittent", 0) end
    way:Attribute("class", waterway)
    way:Attribute("waterway", waterway)
    SetNameAttributes(way)
    SetBrunnelAttributes(way)
  elseif waterway == "boatyard"  then way:Layer("landuse", isClosed); way:Attribute("class", "industrial"); way:MinZoom(12)
  elseif waterway == "dam"       then way:Layer("building", isClosed)
  elseif waterway == "fuel"      then way:Layer("landuse", isClosed); way:Attribute("class", "industrial"); way:MinZoom(14)
  end

  -- Set 'building' and associated
  if building~="" then
    way:Layer("building", true)
    SetBuildingHeightAttributes(way)
    SetMinZoomByArea(way)

    -- housenumber is also commonly set on poi nodes, but not very useful w/o at least street name too
    --"housenumber":      { "minzoom": 14, "maxzoom": 14 },
    if housenumber~="" then
      --way:LayerAsCentroid("housenumber", false)
      way:Attribute("housenumber", housenumber, 14)
    end
  end

  -- Set 'water'
  local waterbody = ""
  if natural=="water" or natural=="bay" then waterbody = natural
  elseif leisure=="swimming_pool" then waterbody = leisure
  elseif landuse=="reservoir" or landuse=="basin" then waterbody = landuse
  elseif waterClasses[waterway] then waterbody = waterway
  end

  if waterbody~="" then
    if way:Find("covered")=="yes" or not isClosed then return end
    local class="lake"; if natural=="bay" then class="ocean" elseif waterway~="" then class="river" end
    if class=="lake" and way:Find("wikidata")=="Q192770" then return end  -- crazy lake in Finland
    if class=="ocean" and isClosed and (way:AreaIntersecting("ocean")/way:Area() > 0.98) then return end
    way:Layer("water", true)
    SetMinZoomByArea(way)
    way:Attribute("class", class)
    way:Attribute("waterbody", waterbody)

    if way:Find("intermittent")=="yes" then way:Attribute("intermittent",1) end
    -- don't show names for minor man-made basins (e.g. ways 25958687, 27201902, 25309134, 24579306)
    if way:Holds("name") and natural=="water" and water ~= "basin" and water ~= "wastewater" then
      --way:LayerAsCentroid("water_name_detail")
      SetNameAttributes(way, 14)
      --SetMinZoomByArea(way)
      --way:Attribute("class", class)
      way:AttributeNumeric("area", way:Area())
    end

    return -- in case we get any landuse processing
  end

  -- landuse/landcover
  --"landcover":        { "minzoom":  0, "maxzoom": 14, "simplify_below": 13, "simplify_level": 0.0003, "simplify_ratio": 2, "write_to": "landuse" },
  --"park":             { "minzoom": 11, "maxzoom": 14 },
  local l = landuse
  if l=="" then l=natural end
  if l=="" then l=leisure end
  if l=="" then l=amenity end
  if l=="" then l=tourism end
  if landuseKeys[l] then
    way:Layer("landuse", true)
    way:Attribute("class", landuseKeys[l])
    --if landcover then
    SetMinZoomByArea(way)
    --else
    --  if landuse=="residential" then
    --    if way:Area()<ZRES8^2 then way:MinZoom(8) else SetMinZoomByArea(way) end
    --  else
    --    way:MinZoom(11)
    --  end
    --end
    if landuse~="" then way:Attribute("landuse", landuse)
    elseif natural~="" then way:Attribute("natural", natural)
    elseif leisure~="" then way:Attribute("leisure", leisure)
    elseif amenity~="" then way:Attribute("amenity", amenity)
    elseif tourism~="" then way:Attribute("tourism", tourism) end

    if natural=="wetland" then way:Attribute("wetland", way:Find("wetland")) end
    write_name = true
  end

  -- Parks
  local write_area = false;
  local park_boundary = parkValues[boundary]
  if park_boundary or leisure=="nature_reserve" then
    way:Layer("landuse", true)
    SetMinZoomByArea(way)  --way:MinZoom(11)
    way:Attribute("class", park_boundary and boundary or leisure)  --"park");
    --way:Attribute("subclass", park_boundary and boundary or leisure);
    SetNameAttributes(way)
    write_area = true
  end

  -- POIs ('poi' and 'poi_detail')
  if NewWritePOI(way, (write_name or write_area) and way:Area() or 0) then
    SetNameAttributes(way)
    return
  end

  -- Catch-all
  if (building~="" or write_name) and way:Holds("name") then
    way:LayerAsCentroid("poi_detail")
    SetNameAttributes(way)
    if write_name then
      rank=6
      way:AttributeNumeric("area", way:Area())
    else
      rank=25
    end
    way:AttributeNumeric("rank", rank)
  end
end

-- Remap coastlines
function attribute_function(attr,layer)
  if attr["featurecla"]=="Glaciated areas" then
    return { subclass="glacier" }
  elseif attr["featurecla"]=="Antarctic Ice Shelf" then
    return { subclass="ice_shelf" }
  elseif attr["featurecla"]=="Urban area" then
    return { class="residential" }
  else
    return { class="ocean" }
  end
end

-- ==========================================================
-- Common functions

extraPoiTags = Set { "cuisine", "station", "religion", "operator" }  -- atm:operator
function NewWritePOI(obj, area)
  for k,lists in pairs(poiTags) do
    local val = obj:Find(k)
    if val ~= "" then
      if type(next(lists)) ~= "number" then lists = { [poiMinZoom] = lists } end
      for minzoom, list in pairs(lists) do
        local exclude = list["__EXCLUDE"] == true
        if next(list) == nil or (exclude and not list[val] or list[val]) then
          obj:LayerAsCentroid("poi")
          obj:MinZoom(area > 0 and 12 or minzoom)
          --SetNameAttributesEx(obj, osm_type)
          if area > 0 then obj:AttributeNumeric("area", area) end
          -- write value for all tags in poiTags (if present)
          for tag, _ in pairs(poiTags) do
            local v = obj:Find(tag)
            if v ~= "" then obj:Attribute(tag, v) end
          end
          for tag, _ in pairs(extraPoiTags) do
            local v = obj:Find(tag)
            if v ~= "" then obj:Attribute(tag, v) end
          end
          return true
        end
      end
    end
  end
  return false
end

-- Set name attributes on any object
function SetNameAttributesEx(obj, osm_type, minzoom)
  local name = obj:Find("name"), iname
  local main_written = name
  minzoom = minzoom or 0
  -- if we have a preferred language, then write that (if available), and additionally write the base name tag
  if preferred_language and obj:Holds("name:"..preferred_language) then
    iname = obj:Find("name:"..preferred_language)
    obj:Attribute(preferred_language_attribute, iname)
    if iname~=name and default_language_attribute then
      obj:Attribute(default_language_attribute, name)
    else main_written = iname end
  else
    obj:Attribute(preferred_language_attribute, name)
  end
  -- then set any additional languages
  for i,lang in ipairs(additional_languages) do
    iname = obj:Find("name:"..lang)
    if iname=="" then iname=name end
    if iname~=main_written then obj:Attribute("name_"..lang, iname) end
  end
  -- add OSM id
  obj:Attribute("osm_id", obj:Id())
  obj:Attribute("osm_type", osm_type)
end

function SetNameAttributes(obj, minzoom)
  local osm_type = obj:Find("type") == "multipolygon" and "relation" or "way";
  SetNameAttributesEx(obj, osm_type, minzoom)
end

-- Set ele and ele_ft on any object
function SetEleAttributes(obj)
    local ele = obj:Find("ele")
  if ele ~= "" then
    local meter = math.floor(tonumber(ele) or 0)
    --local feet = math.floor(meter * 3.2808399)
    obj:AttributeNumeric("ele", meter)
    --obj:AttributeNumeric("ele_ft", feet)
  end
end

function SetBrunnelAttributes(obj)
  if     obj:Find("bridge") == "yes" then obj:Attribute("brunnel", "bridge")
  elseif obj:Find("tunnel") == "yes" then obj:Attribute("brunnel", "tunnel")
  elseif obj:Find("ford")   == "yes" then obj:Attribute("brunnel", "ford")
  end
end

-- Set minimum zoom level by area
function SetMinZoomByArea(way)
  local area=way:Area()
  if     area>ZRES5^2  then way:MinZoom(6)
  elseif area>ZRES6^2  then way:MinZoom(7)
  elseif area>ZRES7^2  then way:MinZoom(8)
  elseif area>ZRES8^2  then way:MinZoom(9)
  elseif area>ZRES9^2  then way:MinZoom(10)
  elseif area>ZRES10^2 then way:MinZoom(11)
  elseif area>ZRES11^2 then way:MinZoom(12)
  elseif area>ZRES12^2 then way:MinZoom(13)
  else                      way:MinZoom(14) end
end

function SetBuildingHeightAttributes(way)
  local height = tonumber(way:Find("height"), 10)
  local minHeight = tonumber(way:Find("min_height"), 10)
  local levels = tonumber(way:Find("building:levels"), 10)
  local minLevel = tonumber(way:Find("building:min_level"), 10)

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

  way:AttributeNumeric("render_height", renderHeight)
  way:AttributeNumeric("render_min_height", renderMinHeight)
end

-- Implement z_order as calculated by Imposm
-- See https://imposm.org/docs/imposm3/latest/mapping.html#wayzorder for details.
function SetZOrder(way)
  local highway = way:Find("highway")
  local layer = tonumber(way:Find("layer"))
  local bridge = way:Find("bridge")
  local tunnel = way:Find("tunnel")
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
  way:ZOrder(zOrder)
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
