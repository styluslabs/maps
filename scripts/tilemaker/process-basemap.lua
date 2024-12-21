-- issues: country names? admin_level 3,4 boundaries?

-- Some relevant threads:
-- - github.com/systemed/tilemaker/discussions/531
-- - github.com/systemed/tilemaker/discussions/434
-- - github.com/systemed/tilemaker/issues/204
-- - github.com/systemed/tilemaker/issues/322

-- .\build\tilemaker.exe --output basemap.mbtiles --bbox -180,-85,180,85 --process resources/process-basemap.lua --config resources/config-basemap.json

-- dbview cultural/ne_10m_admin_0_boundary_lines_land/ne_10m_admin_0_boundary_lines_land.dbf | grep Featurecla | sort -u
--disputedBoundaries = Set { "Disputed (please verify)", "Indefinite (please verify)", "Indeterminant frontier", "Lease limit", "Line of control (please verify)", "Overlay limit", "Unrecognized" }

-- old config
--[[
{
	"layers": {
    "water": { "minzoom": 6, "maxzoom": 14, "simplify_below": 12, "simplify_level": 0.0003, "simplify_ratio": 2 },

		"ocean": { "minzoom": 0, "maxzoom": 14, "source": "coastline/water_polygons.shp", "simplify_below": 13, "simplify_level": 0.0001, "simplify_ratio": 2, "write_to": "water" },

		"ne_lakes": { "minzoom": 0, "maxzoom": 14, "source": "landcover/ne_10m_lakes/ne_10m_lakes.shp", "source_columns": ["wikidataid", "name", "scalerank"], "simplify_below": 13, "simplify_level": 0.0001, "simplify_ratio": 2, "write_to": "water" },

    "place": { "minzoom": 0, "maxzoom": 14 },

    "ne_populated_places": { "minzoom": 3, "maxzoom": 14, "source": "cultural/ne_10m_populated_places/ne_10m_populated_places.shp", "source_columns": ["featurecla", "name", "scalerank", "pop_max", "wikidataid"], "write_to": "place" },

    "transportation": { "minzoom": 4, "maxzoom": 14, "simplify_below": 13, "simplify_level": 0.0003 },

    "ne_roads": { "minzoom": 3, "maxzoom": 14, "source": "cultural/ne_10m_roads/ne_10m_roads.shp", "source_columns": ["featurecla", "name", "scalerank", "expressway"], "simplify_below": 13, "simplify_level": 0.0003, "write_to": "transportation" },

		"boundary": { "minzoom": 0, "maxzoom": 14, "simplify_below": 12, "simplify_level": 0.0003, "simplify_ratio": 2 },

    "ne_boundaries": { "minzoom": 0, "maxzoom": 8, "source": "cultural/ne_10m_admin_0_boundary_lines_land/ne_10m_admin_0_boundary_lines_land.shp", "source_columns": ["featurecla", "adm0_left", "adm0_right"], "simplify_below": 7, "simplify_level": 0.0003, "simplify_ratio": 2, "write_to": "boundary" },

		"landuse": { "minzoom":  0, "maxzoom": 14, "simplify_below": 13, "simplify_level": 0.0003, "simplify_ratio": 2 },

    "urban_areas": { "minzoom": 4, "maxzoom": 8, "source": "landcover/ne_10m_urban_areas/ne_10m_urban_areas.shp", "source_columns": ["featurecla"], "simplify_below": 7, "simplify_level": 0.0003, "simplify_ratio": 2, "write_to": "landuse" },
		"ice_shelf": { "minzoom": 0, "maxzoom": 9, "source": "landcover/ne_10m_antarctic_ice_shelves_polys/ne_10m_antarctic_ice_shelves_polys.shp", "source_columns": ["featurecla"], "simplify_below": 13, "simplify_level": 0.0005, "write_to": "landuse" },
		"glacier": { "minzoom": 2, "maxzoom": 9, "source": "landcover/ne_10m_glaciated_areas/ne_10m_glaciated_areas.shp", "source_columns": ["featurecla"], "simplify_below": 13, "simplify_level": 0.0005, "write_to": "landuse" }
	},
	"settings": {
		"minzoom": 0,
		"maxzoom": 8,
		"basezoom": 8,
		"include_ids": false,
		"combine_below": 14,
		"name": "Tilemaker Stylus Labs schema",
		"version": "3.0",
		"description": "Tile config based on Stylus Labs schema",
		"compress": "gzip",
		"filemetadata": {
			"tilejson": "2.0.0",
			"scheme": "xyz",
			"type": "baselayer",
			"format": "pbf",
            "tiles": ["https://example.com/liechtenstein/{z}/{x}/{y}.pbf"]
		}
	}
}


]]


-- Enter/exit Tilemaker
function init_function()
end
function exit_function()
end

node_keys = {}
function node_function()
end

function way_function()
end


-- processing Natural Earth shapefiles converted to .osm.pbf (w/ ogr2osm)
--[[

function way_function(way)


  local featurecla = way:Find("featurecla")

  if featurecla == "Road" or featurecla == "Ferry" then
    WriteRoad(way)
  elseif featurecla == "Lake" then
    WriteLake(way)
  elseif featurecla == "River" or featurecla == "Canal" then  -- ignore "Lake Centerline" and "River (Intermittent)"
    WriteRiver(way)

  end

end

function WriteBoundary(way)
  local featurecla = way:Find("FEATURECLA")
  way:Layer("boundary", false)
  way:MinZoom(0)
  way:AttributeNumeric("admin_level", 2)
  way:Attribute("name_left", way:Find("ADM0_LEFT"))
  way:Attribute("name_right", way:Find("ADM0_RIGHT"))
  if way:Find("TYPE")=="Water Indicator" then
    way:AttributeNumeric("maritime", 1)
  end
  if featurecla~="International boundary (verify)" then
    way:AttributeNumeric("disputed", 1)
  end
end

function WriteCountry(way)
  --local featurecla = way:Find("FEATURECLA")
  way:LayerAsCentroid("place")
  way:MinZoom(0)
  way:Attribute("name", way:Find("NAME"))
  way:Attribute("place", "country")
  local pop = math.floor(tonumber(way:Find("POP_EST")) or 0)
  if pop > 0 then
    way:AttributeNumeric("population", pop)
  end
end

function WriteCity(node)
  node:Layer("place")
  local rank = tonumber(node:Find("SCALERANK"))
  node:MinZoom(rank < 6 and 3 or 6)
  node:Attribute("place", "city")
  node:Attribute("name", node:Find("NAME"))
  node:Attribute("wikidata", node:Find("WIKIDATAID"))
  node:AttributeNumeric("rank", rank)
  local pop = math.floor(tonumber(node:Find("POP_MAX")) or 0)
  if pop > 0 then
    node:AttributeNumeric("population", pop)
  end
end

function WriteRoad(way)
  local featurecla = way:Find("featurecla")
  local rank = tonumber(way:Find("scalerank"))
  way:Layer("transportation", false)
  if featurecla=="Ferry" then
    way:MinZoom(3)
    way:Attribute("route", "ferry")
  elseif tonumber(way:Find("expressway")) == 1 then
    way:MinZoom(3)
    way:Attribute("highway", "motorway")
  else
    way:MinZoom(6)
    way:Attribute("highway", "trunk")
  end
  way:Attribute("name", way:Find("name"))
  way:AttributeNumeric("rank", rank)
end

function WriteLake(way)
  local featurecla = way:Find("featurecla")
  local rank = tonumber(way:Find("scalerank"))
  local area = way:Area()

  way:Layer("water", true)
  way:MinZoom(0)  --SetMinZoomByArea(way, area)
  way:Attribute("name", way:Find("name"))
  way:Attribute("water", "lake")
  way:Attribute("wikidata", way:Find("wikidataid"))
  way:AttributeNumeric("area", area)
  way:AttributeNumeric("rank", rank)
end


function WriteRiver(way)
  local featurecla = way:Find("featurecla")
  local rank = tonumber(way:Find("scalerank"))

  way:Layer("water", false)
  way:MinZoom(math.max(0, rank - 6))
  way:Attribute("name", way:Find("name"))
  way:Attribute("water", "river")
  way:Attribute("wikidata", way:Find("wikidataid"))
  way:AttributeNumeric("strokewidth", tonumber(way:Find("strokeweig")))
  way:AttributeNumeric("rank", rank)
end

]]


-- Remap coastlines and landcover
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
		return { class="lake", water="lake", name=attr["name"], wikidata=attr["wikidataid"], rank=attr["scalerank"] }

  -- ne_10m_admin_0_boundary_lines_land; can't use ne_10m_admin_0_countries because it includes shoreline
  elseif layer=="ne_boundaries" then
    local res = { admin_level=2, adm0_l=attr["adm0_left"], adm0_r=attr["adm0_right"] }
    if featurecla~="International boundary (verify)" then res["disputed"] = 1 end
    return res

  -- ne_10m_populated_places
  elseif layer=="ne_populated_places" then
    local rank = attr["scalerank"]
    local z = rank < 6 and 3 or 6
    return { _minzoom=z, class="city", place="city", name=attr["name"], population=attr["pop_max"], rank=rank, wikidata=attr["wikidataid"] }

  -- ne_10m_roads
  elseif layer=="ne_roads" then
    if featurecla=="Ferry" then
      return { _minzoom=3, class="ferry", route="ferry", ref=attr["name"], rank=attr["scalerank"] }
    elseif attr["expressway"] == 1 then
      return { _minzoom=3, class="motorway", highway="motorway", ref=attr["name"], rank=attr["scalerank"] }
    end
    return { _minzoom=6, class="trunk", highway="trunk", ref=attr["name"], rank=attr["scalerank"] }

  else
		return attr
	end
end
