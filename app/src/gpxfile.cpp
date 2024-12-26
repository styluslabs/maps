#include "gpxfile.h"
#include "pugixml.hpp"
#include "ulib/stringutil.h"
#include "ulib/fileutil.h"
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

static double parseGpxTime(const char* s)
{
  if(!s || !s[0]) return 0;
  std::tm tmb;
  std::stringstream(s) >> std::get_time(&tmb, "%Y-%m-%dT%TZ");  //2023-03-31T20:19:15Z
  return timegm(&tmb);  //mktime(&tmb);
}

// https://www.topografix.com/GPX/1/1/ , https://www.topografix.com/gpx_manual.asp
// https://github.com/tkrajina/gpxpy/blob/dev/test_files/gpx1.0_with_all_fields.gpx
static Waypoint loadWaypoint(const pugi::xml_node& trkpt)
{
  double lat = trkpt.attribute("lat").as_double();
  double lng = trkpt.attribute("lon").as_double();
  double ele = atof(trkpt.child_value("ele"));
  // https://www.topografix.com/gpx_manual.asp#speed and used in actual GPX files
  float speed = atof(trkpt.child_value("speed"));
  float course = atof(trkpt.child_value("course"));
  double time = parseGpxTime(trkpt.child_value("time"));
  Waypoint wpt({time, lat, lng, 0, ele, 0, course, 0, speed, 0}, trkpt.child_value("name"), trkpt.child_value("desc"));
  pugi::xml_node extnode = trkpt.child("extensions");
  if(extnode) {
    //wpt.visible = extnode.attribute("visible").as_bool(wpt.visible);
    pugi::xml_node slroute = extnode.child("sl:route");
    if(slroute) {
      wpt.routed = slroute.attribute("routed").as_bool(wpt.routed);
      wpt.dist = slroute.attribute("dist").as_double(wpt.dist);
    }
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
  // we were previously writing name and desc to <gpx> instead of proper location of <gpx><metadata>
  const char* gpxname = gpx.child_value("name");
  const char* gpxdesc = gpx.child_value("desc");
  double gpxtime = 0;
  pugi::xml_node metadata = gpx.child("metadata");
  if(metadata) {
    gpxname = metadata.child_value("name");
    gpxdesc = metadata.child_value("desc");
    gpxtime = parseGpxTime(metadata.child_value("time"));
  }
  if(gpxtime > 0) track->timestamp = gpxtime;
  pugi::xml_node extnode = metadata.child("extensions").child("sl:gpx");
  if(!extnode)
    extnode = gpx.child("extensions").child("sl:gpx");
  if(extnode && track->style.empty())
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
        //if(!track->tracks.back().pts.empty()) {
        //  auto& pt0 = track->tracks.back().pts.back();
        //  pt1.dist = pt0.dist + 1000*lngLatDist(pt1.lngLat(), pt0.lngLat());
        //  if(pt1.loc.time > 0)
        //    pt1.loc.spd = (pt1.dist - pt0.dist)/(pt1.loc.time - pt0.loc.time);
        //}
        track->tracks.back().pts.push_back(pt1);
        if(pt1.loc.spd > 0) track->hasSpeed = true;
        trkpt = trkpt.next_sibling("trkpt");
      }
      trkseg = trkseg.next_sibling("trkseg");
    }
    trk = trk.next_sibling("trk");
  }
  // values set in UI (and stored in DB) takes precedence
  if(track->title.empty()) track->title = gpxname;
  if(track->title.empty() && !track->routes.empty()) track->title = track->routes[0].title;
  if(track->title.empty() && !track->tracks.empty()) track->title = track->tracks[0].title;
  if(track->title.empty()) track->title = FSPath(track->filename).baseName();
  if(track->desc.empty()) track->desc = gpxdesc;
  if(track->desc.empty() && !track->routes.empty()) track->desc = track->routes[0].desc;
  if(track->desc.empty() && !track->tracks.empty()) track->desc = track->tracks[0].desc;
  track->loaded = true;
  track->modified = false;
  return true;
}

static std::string saveGpxTime(double time)
{
  char timebuf[256];
  time_t t = time_t(time);
  strftime(timebuf, sizeof(timebuf), "%FT%TZ", gmtime(&t));
  return std::string(timebuf);
}

void saveWaypoint(pugi::xml_node trkpt, const Waypoint& wpt, bool savespd, bool savedist)
{
  trkpt.append_attribute("lat").set_value(fstring("%.7f", wpt.loc.lat).c_str());
  trkpt.append_attribute("lon").set_value(fstring("%.7f", wpt.loc.lng).c_str());
  trkpt.append_child("ele").append_child(pugi::node_pcdata).set_value(fstring("%.1f", wpt.loc.alt).c_str());
  if(savespd && wpt.loc.spd > 0)
    trkpt.append_child("speed").append_child(pugi::node_pcdata).set_value(fstring("%.2f", wpt.loc.spd).c_str());
  if(wpt.loc.time > 0)
    trkpt.append_child("time").append_child(pugi::node_pcdata).set_value(saveGpxTime(wpt.loc.time).c_str());
  if(!wpt.name.empty())
    trkpt.append_child("name").append_child(pugi::node_pcdata).set_value(wpt.name.c_str());
  if(!wpt.desc.empty())
    trkpt.append_child("desc").append_child(pugi::node_pcdata).set_value(wpt.desc.c_str());

  savedist = savedist && wpt.dist > 0;
  if(!wpt.routed || !wpt.props.empty() || savedist) {
    pugi::xml_node extnode = trkpt.append_child("extensions");
    if(!wpt.routed || savedist) {
      pugi::xml_node rtenote = extnode.append_child("sl:route");
      if(!wpt.routed) { rtenote.append_attribute("routed").set_value(wpt.routed); }
      if(savedist) { rtenote.append_attribute("dist").set_value(wpt.dist); }
    }
    if(!wpt.props.empty())
      extnode.append_child("sl:props").append_child(pugi::node_pcdata).set_value(wpt.props.c_str());
  }
}

bool saveGPX(GpxFile* track, const char* filename)
{
  // saving track
  pugi::xml_document doc;
  pugi::xml_node gpx = doc.append_child("gpx");
  pugi::xml_node metadata = gpx.append_child("metadata");
  metadata.append_child("name").append_child(pugi::node_pcdata).set_value(track->title.c_str());
  metadata.append_child("desc").append_child(pugi::node_pcdata).set_value(track->desc.c_str());
  if(track->timestamp > 0)
    metadata.append_child("time").append_child(pugi::node_pcdata).set_value(saveGpxTime(track->timestamp).c_str());
  if(!track->style.empty()) {
    pugi::xml_node extnode = metadata.append_child("extensions").append_child("sl:gpx");
    extnode.append_attribute("style").set_value(track->style.c_str());
  }

  for(const Waypoint& wpt : track->waypoints)
    saveWaypoint(gpx.append_child("wpt"), wpt, false, true);

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
      saveWaypoint(seg.append_child("trkpt"), wpt, track->hasSpeed);
  }
  return doc.save_file(filename ? filename : track->filename.c_str(), "  ");
}

// decode encoded polyline, used by Valhalla, Google, etc.
// - https://github.com/valhalla/valhalla/blob/master/docs/docs/decoding.md - Valhalla uses 1E6 precision
// - https://developers.google.com/maps/documentation/utilities/polylinealgorithm - Google uses 1E5 precision
std::vector<Waypoint> decodePolylineStr(const std::string& encoded, double precision)
{
  const double invprec = 1/precision;
  size_t i = 0;     // what byte are we looking at

  // Handy lambda to turn a few bytes of an encoded string into an integer
  auto deserialize = [&encoded, &i](const int previous) {
    // Grab each 5 bits and mask it in where it belongs using the shift
    int byte, shift = 0, result = 0;
    do {
      byte = static_cast<int>(encoded[i++]) - 63;
      result |= (byte & 0x1f) << shift;
      shift += 5;
    } while (byte >= 0x20);
    // Undo the left shift from above or the bit flipping and add to previous since it's an offset
    return previous + (result & 1 ? ~(result >> 1) : (result >> 1));
  };

  // Iterate over all characters in the encoded string
  std::vector<Waypoint> shape;
  int lon = 0, lat = 0;
  while (i < encoded.length()) {
    lat = deserialize(lat);
    lon = deserialize(lon);
    shape.emplace_back(LngLat(lon*invprec, lat*invprec));
  }
  return shape;
}

// Tangram stores marker coords normalized to marker extent (max of bbox width, height) and line width is in
//  same coords, so for large marker (e.g. route), width becomes too small (passed as round(float * 4096))
//  and gets rounded to zero, so marker isn't drawn.  For even larger markers, use of floats instead of
//  doubles could become an issue.
// This is a known issue: github.com/tangrams/tangram-es/issues/994 , issues/1655 , issues/1463
// Previously, we split large polyline markers into multiple markers (code removed 24 Aug 2024)
// We now use ClientDataSource since this allows tracks to be treated as regular map features and so work
//  with 3D terrain and have multiple styles (for direction indicators)
// Modifying Tangram to support arbitrarily large markers (e.g. by splitting into multiple polylines) looked
//  like it would be messy

#include "mapsapp.h"

UniqueMarkerID::~UniqueMarkerID()
{
  if(handle > 0)
    MapsApp::inst->map->markerRemove(handle);
}

void TrackMarker::setProperties(Properties&& props, bool replace)
{
  if(replace)
    markerProps = std::move(props);
  else {
    for(auto& item : props.items())
      markerProps.setValue(item.key, item.value);
  }
  MapsApp::inst->tracksDataSource->setProperties(featureId, Properties(markerProps));
  MapsApp::inst->tracksDataSource->clearData();  // this just increments generation counter
  MapsApp::inst->platform->requestRender();  // move into ClientDataSource?
}

void TrackMarker::setTrack(GpxWay* way, size_t nways)
{
  Tangram::ClientDataSource::PolylineBuilder builder;
  for(size_t jj = 0; jj < nways; ++jj) {
    builder.beginPolyline(way->pts.size());
    for(const Waypoint& wp : way->pts)
      builder.addPoint(wp.lngLat());
    ++way;
  }
  featureId = MapsApp::inst->tracksDataSource->addPolylineFeature(
      Properties(markerProps), std::move(builder), featureId);
  MapsApp::inst->tracksDataSource->generateTiles();
  MapsApp::inst->platform->requestRender();  // move into ClientDataSource?
}

//TrackMarker::TrackMarker() { markerProps.set("visible", 1); }
TrackMarker::~TrackMarker()
{
  // remove track by setting to empty geometry
  Tangram::ClientDataSource::PolylineBuilder builder;
  MapsApp::inst->tracksDataSource->addPolylineFeature(Properties(), std::move(builder), featureId);
}
