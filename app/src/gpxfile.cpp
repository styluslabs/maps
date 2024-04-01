#include "gpxfile.h"
#include "pugixml.hpp"
#include "ulib/stringutil.h"
#include "util.h"
#include <iomanip>  // std::get_time


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
  double ele = atof(trkpt.child_value("ele"));
  double time = 0;
  pugi::xml_node timenode = trkpt.child("time");
  if(timenode) {
    std::tm tmb;
    std::stringstream(timenode.child_value()) >> std::get_time(&tmb, "%Y-%m-%dT%TZ");  //2023-03-31T20:19:15Z
    time = mktime(&tmb);
  }

  Waypoint wpt({time, lat, lng, 0, ele, 0, /*dir*/0, 0, 0, 0}, trkpt.child_value("name"), trkpt.child_value("desc"));
  pugi::xml_node extnode = trkpt.child("extensions");
  if(extnode) {
    //wpt.visible = extnode.attribute("visible").as_bool(wpt.visible);
    pugi::xml_node slroute = extnode.child("sl:route");
    if(slroute)
      wpt.routed = slroute.attribute("routed").as_bool(wpt.routed);
    pugi::xml_node slprops = extnode.child("sl:props");
    if(slprops)
      wpt.props = slprops.child_value();
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
  const char* gpxname = gpx.child_value("name");
  const char* gpxdesc = gpx.child_value("desc");
  // value set in UI (and stored in DB) takes precedence
  if(gpxname[0] && track->title.empty()) track->title = gpxname;
  if(gpxdesc[0]) track->desc = gpxdesc;
  pugi::xml_node extnode = gpx.child("extensions").child("sl:gpx");
  if(extnode)
    track->style = extnode.attribute("style").as_string();

  pugi::xml_node wpt = gpx.child("wpt");
  while(wpt) {
    track->addWaypoint(loadWaypoint(wpt));
    wpt = wpt.next_sibling("wpt");
  }

  pugi::xml_node rte = gpx.child("rte");
  while(rte) {
    int rtestep = -1;
    double ttot = 0;
    track->routes.emplace_back(rte.child_value("name"), rte.child_value("desc"));
    //if(track->routeMode.empty())
    track->routeMode = rte.child_value("type");
    pugi::xml_node rtept = rte.child("rtept");
    while(rtept) {
      track->routes.back().pts.push_back(loadWaypoint(rtept));

      // handle openrouteservice extensions (other common/useful extensions to be added later)
      pugi::xml_node extnode = rtept.child("extensions");
      if(extnode) {
        const char* stepstr = extnode.child_value("step");
        const char* durstr = extnode.child_value("duration");
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
    track->tracks.emplace_back(trk.child_value("name"), trk.child_value("desc"));
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

  if(!wpt.routed || !wpt.props.empty()) {
    pugi::xml_node extnode = trkpt.append_child("extensions");
    if(!wpt.routed)
      extnode.append_child("sl:route").append_attribute("routed").set_value(wpt.routed);
    if(!wpt.props.empty())
      extnode.append_child("sl:props").append_child(pugi::node_pcdata).set_value(wpt.props.c_str());
  }
}

bool saveGPX(GpxFile* track, const char* filename)
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
  return doc.save_file(filename ? filename : track->filename.c_str(), "  ");
}

// Tangram stores marker coords normalized to marker extent (max of bbox width, height) and line width is in
//  same coords, so for large marker (e.g. route), width becomes too small (passed as round(float * 4096))
//  and gets rounded to zero, so marker isn't drawn.  For even larger markers, use of floats instead of
//  doubles could become an issue.
// This is a known issue: github.com/tangrams/tangram-es/issues/994 , issues/1655 , issues/1463
// For now, we'll split large polyline markers into multiple markers
// Other potential solutions are to use ClientDataSource (generates tiles ... too slow?), change to float
//  for polyline width, or apply width after model transform
// Modifying Tangram to support arbitrarily large markers (e.g. by splitting into multiple polylines) looked
//  like it would be messy

#include "tangram.h"
#include "util/geom.h"
#include "util/mapProjection.h"

using Tangram::MapProjection;

TrackMarker::~TrackMarker()
{
  for(MarkerID id : markers) map->markerRemove(id);
}

void TrackMarker::setVisible(bool vis) { for(MarkerID id : markers) map->markerSetVisible(id, vis); }

void TrackMarker::setStylePath(const char* style)
{
  stylePath = style;
  for(MarkerID id : markers)
    map->markerSetStylingFromPath(id, style);
}

void TrackMarker::setProperties(Properties&& props)
{
  markerProps = std::move(props);
  for(MarkerID id : markers)
    map->markerSetProperties(id, Properties(markerProps));
}

bool TrackMarker::onPicked(MarkerID picked)
{
  for(MarkerID id : markers) {
    if(id == picked) return true;
  }
  return false;
}

void TrackMarker::setTrack(GpxWay* way, size_t nways)
{
  size_t nmarkers = 0;
  for(size_t jj = 0; jj < nways; ++jj) {
    std::vector<LngLat> pts;
    auto meters0 = MapProjection::lngLatToProjectedMeters(way->pts[0].lngLat());
    Tangram::BoundingBox bounds = {meters0, meters0};
    for(size_t ii = 0; ii < way->pts.size();) {
      const Waypoint& wp = way->pts[ii];
      auto meters = MapProjection::lngLatToProjectedMeters(wp.lngLat());
      auto b0 = bounds;
      bounds.expand(meters.x, meters.y);
      if(bounds.width() > maxExtent || bounds.height() > maxExtent || ++ii == way->pts.size()) {
        double slope = (meters.y - meters0.y)/(meters.x - meters0.x);
        auto clip = meters;
        if(clip.x < b0.min.x) clip.x = std::max(clip.x, b0.max.x - maxExtent);
        else if(clip.x > b0.max.x) clip.x = std::min(clip.x, b0.min.x + maxExtent);
        clip.y = meters0.y + (clip.x - meters0.x)*slope;

        if(clip.y < b0.min.y) clip.y = std::max(clip.y, b0.max.y - maxExtent);
        else if(clip.y > b0.max.y) clip.y = std::min(clip.y, b0.min.y + maxExtent);
        clip.x = meters0.x + (clip.y - meters0.y)/slope;

        pts.push_back(MapProjection::projectedMetersToLngLat(clip));

        if(++nmarkers >= markers.size()) {
          MarkerID id = map->markerAdd();
          markers.push_back(id);
          map->markerSetStylingFromPath(id, stylePath.c_str());
        }
        map->markerSetPolyline(markers[nmarkers-1], pts.data(), pts.size());
        map->markerSetProperties(markers[nmarkers-1], Properties(markerProps));
        pts = {pts.back()};
        bounds = {clip, clip};
        continue;  // repeat current point (i.e. segment from clip to meters)
      }
      pts.push_back(wp.loc.lngLat());
      meters0 = meters;
    }
    ++way;
  }
  for(size_t ii = nmarkers; ii < markers.size(); ++ii)
    map->markerRemove(markers[ii]);
  markers.resize(nmarkers);
}

// here we're using MapsApp::inst, while for TrackMarker we store Map* ... we should pick one way or the other
#include "mapsapp.h"

Waypoint::~Waypoint()
{
  if(marker > 0)
    MapsApp::inst->map->markerRemove(marker);
}
