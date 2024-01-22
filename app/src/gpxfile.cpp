#include "gpxfile.h"
#include "pugixml.hpp"
#include "ulib/stringutil.h"
#include "util.h"


std::vector<Waypoint>::iterator GpxFile::findWaypoint(const std::string& uid)
{
  auto it = waypoints.begin();
  while(it != waypoints.end() && it->uid != uid) { ++it; }
  return it;
}

std::vector<Waypoint>::iterator GpxFile::addWaypoint(Waypoint wpt, const std::string& nextuid)
{
  modified = true;
  wpt.uid = std::to_string(++wayPtSerial);
  return waypoints.insert(nextuid.empty() ? waypoints.end() : findWaypoint(nextuid), wpt);
}

// https://www.topografix.com/GPX/1/1/ , https://www.topografix.com/gpx_manual.asp
// https://github.com/tkrajina/gpxpy/blob/dev/test_files/gpx1.0_with_all_fields.gpx
static Waypoint loadWaypoint(const pugi::xml_node& trkpt)
{
  double lat = trkpt.attribute("lat").as_double();
  double lng = trkpt.attribute("lon").as_double();
  //track.emplace_back(lng, lat);
  pugi::xml_node elenode = trkpt.child("ele");
  double ele = atof(elenode.child_value());
  //activeTrack.push_back({track.back(), dist, atof(ele.child_value())});
  double time = 0;
  pugi::xml_node timenode = trkpt.child("time");
  if(timenode) {
    std::tm tmb;
    std::stringstream(timenode.child_value()) >> std::get_time(&tmb, "%Y-%m-%dT%TZ");  //2023-03-31T20:19:15Z
    time = mktime(&tmb);
  }

  Waypoint wpt({time, lat, lng, 0, ele, 0, /*dir*/0, 0, 0, 0},
      trkpt.child("name").child_value(), trkpt.child("desc").child_value());
  pugi::xml_node extnode = trkpt.child("extensions").child("sl:route");
  if(extnode) {
    //wpt.visible = extnode.attribute("visible").as_bool(wpt.visible);
    wpt.routed = extnode.attribute("routed").as_bool(wpt.routed);
  }
  return wpt;
}

bool loadGPX(GpxFile* track, const char* gpxSrc)
{
  pugi::xml_document doc;
  if(gpxSrc)
    doc.load_string(gpxSrc);
  else
    doc.load_file(track->filename.c_str());
  pugi::xml_node gpx = doc.child("gpx");
  if(!gpx)
    return false;
  const char* gpxname = gpx.child("name").child_value();
  const char* gpxdesc = gpx.child("desc").child_value();
  // value set in UI (and stored in DB) takes precedence
  if(gpxname[0] && track->title.empty()) track->title = gpxname;
  if(gpxdesc[0]) track->desc = gpxdesc;

  pugi::xml_node wpt = gpx.child("wpt");
  while(wpt) {
    track->addWaypoint(loadWaypoint(wpt));
    wpt = wpt.next_sibling("wpt");
  }

  pugi::xml_node rte = gpx.child("rte");
  while(rte) {
    int rtestep = -1;
    double ttot = 0;
    track->routes.emplace_back(rte.child("name").child_value(), rte.child("desc").child_value());
    if(track->routeMode.empty())
      track->routeMode = rte.child("type").child_value();
    pugi::xml_node rtept = rte.child("rtept");
    while(rtept) {
      track->routes.back().pts.push_back(loadWaypoint(rtept));

      // handle openrouteservice extensions (other common/useful extensions to be added later)
      pugi::xml_node extnode = rtept.child("extensions");
      if(extnode) {
        const char* stepstr = extnode.child("step").child_value();
        const char* durstr = extnode.child("duration").child_value();
        if(stepstr[0] && durstr[0]) {
          int step = atoi(stepstr);
          track->routes.back().pts.back().loc.time = ttot;
          if(step > rtestep)
            ttot += atof(durstr);
          rtestep = step;
        }
      }

      rtept = rtept.next_sibling("rtept");
    }
    rte = rte.next_sibling("rte");
  }

  pugi::xml_node trk = gpx.child("trk");
  //if(!trk) logMsg("Error loading %s\n", gpxfile);
  while(trk) {
    track->tracks.emplace_back(trk.child("name").child_value(), trk.child("desc").child_value());
    pugi::xml_node trkseg = trk.child("trkseg");
    while(trkseg) {
      //std::vector<LngLat> track;
      pugi::xml_node trkpt = trkseg.child("trkpt");
      while(trkpt) {
        Waypoint pt1 = loadWaypoint(trkpt);
        if(!track->tracks.back().pts.empty()) {
          auto& pt0 = track->tracks.back().pts.back();
          pt1.dist = pt0.dist + 1000*lngLatDist(pt1.lngLat(), pt0.lngLat());
          if(pt1.loc.time > 0)
            pt1.loc.spd = (pt1.dist - pt0.dist)/(pt1.loc.time - pt0.loc.time);
        }
        track->tracks.back().pts.push_back(pt1);
        trkpt = trkpt.next_sibling("trkpt");
      }
      trkseg = trkseg.next_sibling("trkseg");
    }
    trk = trk.next_sibling("trk");
  }
  track->loaded = true;
  track->modified = false;
  return true;
}

static void saveWaypoint(pugi::xml_node trkpt, const Waypoint& wpt)
{
  trkpt.append_attribute("lat").set_value(fstring("%.7f", wpt.loc.lat).c_str());
  trkpt.append_attribute("lon").set_value(fstring("%.7f", wpt.loc.lng).c_str());
  trkpt.append_child("ele").append_child(pugi::node_pcdata).set_value(fstring("%.1f", wpt.loc.alt).c_str());
  if(wpt.loc.time > 0) {
    char timebuf[256];
    time_t t = time_t(wpt.loc.time);
    strftime(timebuf, sizeof(timebuf), "%FT%TZ", gmtime(&t));
    trkpt.append_child("time").append_child(pugi::node_pcdata).set_value(timebuf);
  }
  if(!wpt.name.empty())
    trkpt.append_child("name").append_child(pugi::node_pcdata).set_value(wpt.name.c_str());
  if(!wpt.desc.empty())
    trkpt.append_child("desc").append_child(pugi::node_pcdata).set_value(wpt.desc.c_str());
  if(!wpt.routed) {  //|| !wpt.visible
    pugi::xml_node extnode = trkpt.append_child("extensions").append_child("sl:route");
    //extnode.append_attribute("visible").set_value(wpt.visible);
    extnode.append_attribute("routed").set_value(wpt.routed);
  }
}

bool saveGPX(GpxFile* track)
{
  // saving track
  pugi::xml_document doc;
  pugi::xml_node gpx = doc.append_child("gpx");
  gpx.append_child("name").append_child(pugi::node_pcdata).set_value(track->title.c_str());
  gpx.append_child("desc").append_child(pugi::node_pcdata).set_value(track->desc.c_str());
  if(!track->style.empty()) {
    pugi::xml_node extnode = gpx.append_child("extensions").append_child("sl:gpx");
    extnode.append_attribute("style").set_value(track->style.c_str());
  }

  for(const Waypoint& wpt : track->waypoints)
    saveWaypoint(gpx.append_child("wpt"), wpt);

  for(const GpxWay& route : track->routes) {
    pugi::xml_node rte = gpx.append_child("rte");
    rte.append_child("name").append_child(pugi::node_pcdata).set_value(route.title.c_str());
    rte.append_child("desc").append_child(pugi::node_pcdata).set_value(route.desc.c_str());
    rte.append_child("type").append_child(pugi::node_pcdata).set_value(track->routeMode.c_str());
    for(const Waypoint& wpt : route.pts)
      saveWaypoint(rte.append_child("rtept"), wpt);
  }

  for(const GpxWay& t : track->tracks) {
    pugi::xml_node trk = gpx.append_child("trk");
    trk.append_child("name").append_child(pugi::node_pcdata).set_value(t.title.c_str());
    trk.append_child("desc").append_child(pugi::node_pcdata).set_value(t.desc.c_str());
    pugi::xml_node seg = trk.append_child("trkseg");
    for(const Waypoint& wpt : t.pts)
      saveWaypoint(seg.append_child("trkpt"), wpt);
  }
  return doc.save_file(track->filename.c_str(), "  ");
}
