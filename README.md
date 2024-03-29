# Maps #
Working name: "Explore"

A cross-platform application for displaying vector and raster maps, built on [Tangram-ES](https://tangrams.readthedocs.io) and supporting [plugins](#plugin-system) for search, routing, map sources and more.  Includes a simple, customizable [style](#stylus-labs-osm-schema) for OpenStreetMap vector tiles.

Features include offline search, managing saved places, creating and editing tracks and routes, and saving map tiles for offline use.

Currently available for Android and Linux; iOS support coming soon.

<img alt="Wikipedia Search" src="https://github.com/styluslabs/maps/assets/1332998/6bf64978-79fb-43d1-ad6e-713cbd44c54a" width="400">
<img alt="Hiking style" src="https://github.com/styluslabs/maps/assets/1332998/c088c07e-00f3-492e-aad5-a0d335205538" width="400">


## Quick start ##
1. [Build](#building) or [download](https://github.com/styluslabs/maps/releases)
1. Download OSM extracts and [generate tiles](#generating-tiles)
1. [Add](#adding-map-sources) some online tile sources
1. Submit [bug reports](https://github.com/styluslabs/maps/issues), [pull requests](https://github.com/styluslabs/maps/pulls), [feature suggestions](https://github.com/styluslabs/maps/discussions), and [plugin ideas](https://github.com/styluslabs/maps/discussions)!


### Building ###

On Linux, `git clone --recurse-submodules https://github.com/styluslabs/maps`, then ensure cmake is installed and `cd maps && make linux` to generate `Release/tangram`.  To build and install for Android (on Linux w/ Android SDK and NDK installed), `cd maps/app/android && ./gradlew installRelease`.


### Generating tiles ###

[scripts/tilemaker](scripts/tilemaker) contains the files necessary to generate tiles for the included vector map style [stylus-osm.yaml](assets/scenes/stylus-osm.yaml) using [Tilemaker](https://github.com/systemed/tilemaker).

1. download a OpenStreetMap [extract](https://wiki.openstreetmap.org/wiki/Planet.osm#Country_and_area_extracts), e.g., from [geofabrik](https://download.geofabrik.de/) or [osmtoday](https://osmtoday.com/)
1. [Setup tilemaker](https://github.com/systemed/tilemaker/blob/master/README.md) and run:
```
tilemaker --config maps/scripts/tilemaker/config.json --process maps/scripts/tilemaker/process.lua <extract>.osm.pbf --output <output>.mbtiles
```
 * it may be necessary to add `--skip-integrity` for some extracts.
1. In the application, navigate to Map sources -> Offline maps -> Open, choose the mbtiles file generated by tilemaker, then choose Stylus Labs OSM Flat as the destination source.  The tiles will be imported and indexed for offline search.


### Adding map sources ###

New map sources can be added by combining existing sources (the "+" button on the toolbar), editing mapsources.yaml (with the application closed), editing mapsources.default.yaml and choosing Restore default sources from the overflow menu, or via "Import source" on the overflow menu, where a YAML fragment or map tile URL (e.g. `https://some.tile.server/tiles/{z}/{x}/{y}.png`) can be entered, or a YAML scene file chosen, for custom vector map styles.

To save online tiles for offline use, pan and zoom the view to show the desired region, tap the download button on the Offline maps toolbar, enter the maximum zoom level in include, then tap Download.  Tiles for all layers of the current map source will be downloaded.

The default configuration includes [markers.yaml](assets/scenes/markers.yaml) for all maps to support the display of search results, saved places, tracks, routes, the location marker, and other markers.


## Plugin system ##

Plugins written in Javascript can add search providers, routing providers, map tile providers, and more.

Files in `assets/plugins` with `.js` extension are executed at application startup.  To reload, tap the reload button on the Plugin Console toolbar.

Currently uses Duktape, so support for features from ES2015 and later is limited.  On iOS, JavascriptCore will be used.

The included plugins give a sample of what's possible:
* [google-import.js](assets/plugins/google-import.js) - import list of places from GeoJSON exported by Google Maps; run from the plugin console
* [nominatim-search.js](assets/plugins/nominatim-search.js) - search with nominatim service
* [openroute.js](assets/plugins/openroute.js) - routing with OpenRouteService
* [osm-place-info.js](assets/plugins/osm-place-info.js) - gather place information (website, opening hours, etc.) from OSM API and Wikipedia
* [transform-query.js](assets/plugins/transform-query.js) - modify search query, e.g., for categorial searches
* [valhalla-osmde.js](assets/plugins/valhalla-osmde.js) - routing with Valhalla provided by osm.de
* [wikipedia-search.js](assets/plugins/wikipedia-search.js) - search for geotagged Wikipedia articles
* [worldview.js](assets/plugins/worldview.js) - show daily worldwide satellite imagery from NASA Worldview, with date picker in GUI.

If you have an idea for a plugin, I'm happy to help and to expose the necessary APIs.


## Vector Maps ##

Vector maps are styled using [Tangram YAML scene files](https://tangrams.readthedocs.io) with a few additions:
* `global.gui_variables` to provide GUI controls in the Edit Source panel linked to scene file globals or shader uniforms
* `global.search_data` to define which features and tags should be indexed for offline search
* `global.__legend` to define SVG images to be optionally displayed over the map
* `$latitude` and `$longitude` for tile center are available for filters to make location-specific adjustments to styling
* SVG images are supported for textures; sprites can be created automatically using SVG id attributes.

Tangram styles can include custom shaders.  The hillshading, contour lines, and slope angle shading in the above screenshots are all calculated on-the-fly from elevation tiles in a shader - see [raster-contour.yaml](assets/scenes/raster-contour.yaml).

[scripts/mb2mz.js] is provided to convert a Mapbox style spec JSON file to a Tangram scene file.  See [scripts/runmb2mz.jz](scripts/runmb2mz.js) for an example.  The included [osm-bright.yaml](assets/scenes/osm-bright.yaml) applies the widely-used OSM Bright style to vector tiles using the OpenMapTiles schema.


### Vector Tiles Background ###

Displaying a vector map requires a source of vector tiles and a style for specifying how to draw features from the tiles.

The hierarchy for vector tiles consists of:

* Container: filesystem (each tile as a separate file), mbtiles (an sqlite file), pmtiles (special flat file format)
* Tile encoding: PBF (protocol buffer format), GeoJSON, etc.
* Tile format: [mapbox vector tiles](https://github.com/mapbox/vector-tile-spec) is dominant - specifies PBF encoding
* Tile schema: [Shortbread](https://shortbread-tiles.org/), [OpenMapTiles](https://openmaptiles.org/schema/), [Mapzen/Tilezen](https://tilezen.readthedocs.io/en/latest/layers/), [Mapbox](https://docs.mapbox.com/data/tilesets/reference/mapbox-streets-v8/)

A vector tile schema specifies how to group features into layers, which features to include at each zoom level, and which attributes to include for each feature.


### Stylus Labs OSM schema ###

The included vector map style [stylus-osm.yaml](assets/scenes/stylus-osm.yaml) (seen in the above screenshots) uses a custom schema because none of the existing tile schemas satisfied all the requirements for the application.  For example, most other schemas define some mapping from OSM tags to feature attributes, but because of the complex and fluid state of OSM tagging this schema just uses unmodified OSM tags for feature attributes.

The schema aims to include more information for transit and for outdoor activities, such as bike and trail features at lower zoom levels and the common trail_visibility tag.  The schema layers are: place, boundary, poi, transportation, transit, building, water, and landuse.

The schema is a work-in-progress.  Suggestions and comments are welcome.


## More ##

### Roadmap ###

* builds for iOS, Windows, and Mac
* 3D terrain and globe view
* integrate [Valhalla](https://github.com/valhalla/valhalla/) for offline routing
* Use QuickJS javascript engine instead of Duktape
* pmtiles support


### Internationalization ###

Tangram-ES supports RTL text and complex shaping through Harfbuzz and Freetype.  However, the ugui library used for the GUI does not (yet), so this support is not enabled by default in the application (which incidentally reduces the executable size by almost 50%).  Set the `TANGRAM_USE_FONTCONTEXT_STB` cmake option to OFF to restore RTL and complex shaping for the map display.  Note that such text will not be displayed correctly in the GUI, e.g., when showing place information.


### Application data ##

Application data is stored in `~/.config/styluslabs/maps` on Linux and `/Android/media/com.styluslabs.maps/files` on Android (files in this folder can be read and written by other applications, so it is possible to edit config.yaml, etc.).

Contents:
* cache/*.mbtiles - map tile storage; deleting this folder will delete all map tiles, including offline maps
* plugins/*.js - plugins
* res/ - various resources for application, e.g. GUI icons
* scenes/ - map styles
* tracks/*.gpx - tracks and routes
* config.yaml - settings; exit application before editing
* fts1.sqlite - index for offline search
* mapsources.yaml - map sources; exit application before editing
* places.sqlite - saved places, etc.


### GUI ###

[styluslabs/ugui](https://github.com/styluslabs/ugui) is used for cross-platform GUI.  It is a work-in-progress.  Suggestions and contributions are welcome.


### Major features ###

Search: offline search for local vector tiles, online search via plugins
Saved places (bookmarks): import via plugins, export and import GPX, choose colors in GUI; further customize styling in [markers.yaml](assets/scenes/markers.yaml]
Tracks and routes: record and edit tracks,  draw direct (straight-line segments) routes or using plugin for routing
Map sources: create and manage map sources, access GUI controls for current map source, show legends
Offline maps (via Map sources): create offline map from current source and view, import mbtiles file, manage offline maps
Plugin console (via overflow menu): reload plugins, execute Javascript in plugin environment.


### Major components ###

* [modified version of Tangram-ES](https://github.com/pbsurf/maps) - full compatibility with upstream will be restored in the future
    * Duktape javascript interpreter
    * yaml-cpp
    * sqlite for mbtiles support
* [ugui](https://github.com/styluslabs/ugui), [usvg](https://github.com/styluslabs/usvg), [ulib](https://github.com/styluslabs/ulib), [nanovgXC](https://github.com/styluslabs/nanovgXC) for GUI
    * [pugixml](https://github.com/zeux/pugixml)
    * [nfd](https://github.com/btzy/nativefiledialog-extended) for file dialogs
    * sqlite for saved places, offline search, etc.
