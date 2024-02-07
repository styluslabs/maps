-- issues: country names? admin_level 3,4 boundaries?

-- Some relevant threads:
-- - github.com/systemed/tilemaker/discussions/531
-- - github.com/systemed/tilemaker/discussions/434
-- - github.com/systemed/tilemaker/issues/204
-- - github.com/systemed/tilemaker/issues/322

-- .\build\tilemaker.exe --output basemap.mbtiles --bbox -180,-85,180,85 --process resources/process-basemap.lua --config resources/config-basemap.json

-- dbview cultural/ne_10m_admin_0_boundary_lines_land/ne_10m_admin_0_boundary_lines_land.dbf | grep Featurecla | sort -u
--disputedBoundaries = Set { "Disputed (please verify)", "Indefinite (please verify)", "Indeterminant frontier", "Lease limit", "Line of control (please verify)", "Overlay limit", "Unrecognized" }

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

-- Remap coastlines and landcover
function attribute_function(attr,layer)
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
		return { class="lake", water="lake", name=attr["name"], wikidata=attr["wikidataid"] }

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
    end
    if attr["expressway"] == 1 then
      return { _minzoom=3, class="motorway", highway="motorway", ref=attr["name"], rank=attr["scalerank"] }
    end
    return { _minzoom=6, class="trunk", highway="trunk", ref=attr["name"], rank=attr["scalerank"] }

else
		return attr
	end
end
