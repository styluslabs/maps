# Maps #
Working name: "Explore"

A cross-platform application for displaying vector and raster maps, built on [Tangram-ES](https://tangrams.readthedocs.io) and supporting [plugins](#plugin-system) for search, routing, map sources and more.  Includes a simple, customizable [style](#stylus-labs-osm-schema) for OpenStreetMap vector tiles.

Features include offline search, managing saved places, creating and editing tracks and routes, and saving map tiles for offline use.

Currently available for Android and Linux; iOS support coming soon.

<img alt="Offline POI search" src="https://github.com/styluslabs/maps/assets/1332998/3a05679d-3a00-4c80-9886-253c72a10e07" width="270">
<img alt="Wikipedia search; Shaded relief" src="https://github.com/styluslabs/maps/assets/1332998/f4f54f7f-9864-4908-9e03-7014879c99ee" width="270">
<img alt="Ski runs; Slope angle shading; Map legend" src="https://github.com/styluslabs/maps/assets/1332998/2df92e7d-33cf-47e2-89f0-8eb6c2a64a52" width="270">
<img alt="Saved places; Geotagged photo import; Bike lanes" src="https://github.com/styluslabs/maps/assets/1332998/c8a9cbef-71d4-4ee0-b59e-61617bb43acb" width="270">
<img alt="3D buildings; POIs; Place info" src="https://github.com/styluslabs/maps/assets/1332998/0eb0c7ad-2f7b-4f36-a1ca-41f4170dac0e" width="270">
<img alt="Sentinel-2 imagery; Map source GUI variables" src="https://github.com/styluslabs/maps/assets/1332998/e45b6de2-55d1-428c-aedc-ca376f710ce1" width="270">
<img alt="Heatmap overlay; Trail visibility tag" src="https://github.com/styluslabs/maps/assets/1332998/146881fa-bccc-47d6-9419-02f12b6b4676" width="270">
<img alt="Track recording" src="https://github.com/styluslabs/maps/assets/1332998/c798a867-9d7f-495a-ad48-4bc6bac73964" width="270">
<img alt="Routing; MTB trail scale" src="https://github.com/styluslabs/maps/assets/1332998/1a6e79ef-f5c4-4ef2-995d-2986ec9aa68c" width="270">

<!-- img alt="Wikipedia Search" src="https://github.com/styluslabs/maps/assets/1332998/6bf64978-79fb-43d1-ad6e-713cbd44c54a" width="400" -->
<!-- img alt="Hiking style" src="https://github.com/styluslabs/maps/assets/1332998/c088c07e-00f3-492e-aad5-a0d335205538" width="400" -->

## Quick start ##
1. [Build](#building) or [download](https://github.com/styluslabs/maps/releases)
1. Download OSM extracts and [generate tiles](#generating-tiles)
1. [Add](#adding-map-sources) some online tile sources
1. Submit [bug reports](https://github.com/styluslabs/maps/issues), [pull requests](https://github.com/styluslabs/maps/pulls), [feature suggestions](https://github.com/styluslabs/maps/discussions), and [plugin ideas](https://github.com/styluslabs/maps/discussions)!


### Building ###

On Linux, `git clone --recurse-submodules https://github.com/styluslabs/maps`, install build dependencies (`apt install cmake libgtk-3-dev libcurl4-openssl-dev libfontconfig-dev libxinerama-dev` on Debian/Ubuntu), then run `cd maps && make linux` to generate `Release/explore`.  To build and install for Android (on Linux w/ Android SDK and NDK installed), `cd maps/app/android && ./gww installRelease`.  The `gww` (gradle wrapper wrapper) script will download and run `gradlew`.  To install the Android SDK and NDK, run `gww --install-sdk`.


### Generating tiles ###

[scripts/tilemaker](scripts/tilemaker) contains the files necessary to generate tiles for the included vector map style [stylus-osm.yaml](assets/scenes/stylus-osm.yaml) using [Tilemaker](https://github.com/systemed/tilemaker).

1. download an OpenStreetMap [extract](https://wiki.openstreetmap.org/wiki/Planet.osm#Country_and_area_extracts), e.g., from [geofabrik](https://download.geofabrik.de/) or [osmtoday](https://osmtoday.com/)
1. [Setup tilemaker](https://github.com/systemed/tilemaker/blob/master/README.md) and run:
```
tilemaker --config maps/scripts/tilemaker/config.json --process maps/scripts/tilemaker/process.lua <extract>.osm.pbf --output <output>.mbtiles
```
 * it may be necessary to add `--skip-integrity` for some extracts.
1. In the application, navigate to Map sources -> Offline maps -> Open, choose the mbtiles file generated by tilemaker (if prompted, choose Stylus Labs OSM Flat as the destination source).  The tiles will be imported and indexed for offline search based on configuration `application.search_data` in [stylus-osm.yaml](assets/scenes/stylus-osm.yaml).  On Android, it is necessary to enable file access permission for Explore in the system app settings in order to open files outside the application's private folder.

To avoid time-consuming search indexing on mobile devices, the Linux version can be used to add the search data to the mbtiles file before copying it to the mobile device (note that this will also import the map to the Linux version's storage):
```
explore --import <output>.mbtiles --storage.export_pois true
```
The [releases](https://github.com/styluslabs/maps/releases) page includes pre-generated tiles for a few regions.


### Adding map sources ###

New map sources can be added by combining existing sources (the "+" button on the toolbar), editing mapsources.yaml (with the application closed), editing mapsources.default.yaml and choosing Restore default sources from the overflow menu, or via "Import source" on the overflow menu, where a YAML fragment or map tile URL (e.g. `https://some.tile.server/tiles/{z}/{x}/{y}.png`) can be entered, or a YAML scene file chosen, for custom vector map styles.

To save online tiles for offline use, pan and zoom the view to show the desired region, tap the download button on the Offline maps toolbar, enter the maximum zoom level to include, then tap Download.  Tiles for all layers of the current map source will be downloaded.

The default configuration includes [markers.yaml](assets/scenes/markers.yaml) for all maps to support the display of search results, saved places, tracks, routes, the location marker, and other markers.


### Application data ##

Application data is stored in the executable's folder on Linux (will be changed to `~/.config/styluslabs/maps` in the future) and in `/Android/media/com.styluslabs.maps/files` on Android (this folder can be read and written by other applications, so it is possible to edit files).  Currently, there are only a few configuration options available in the GUI, but many more can be set by editing `config.yaml` (after exiting the app) in the application data folder.  See `config.default.yaml` for documentation.


## Plugin system ##

Plugins written in Javascript can add search providers, routing providers, map tile providers, and more.

Files in `plugins/` with `.js` extension are executed at application startup.  To reload, tap the reload button on the Plugin Console toolbar.  Some plugins (`openroute.js` and `sentinel2.js` currently) require third-party API keys to work - follow the instructions in the plugin's js file to obtain an API key, then create a file `plugins/_secrets.js` to set `secrets = { "some_api_key": value, "another_api_key": value }`.

Currently uses Duktape, so support for features from ES2015 and later is limited.  On iOS, JavascriptCore will be used.

The included plugins give a sample of what's possible:
* [google-import.js](assets/plugins/google-import.js) - import list of places from GeoJSON exported by Google Maps; run from the plugin console
* [nominatim-search.js](assets/plugins/nominatim-search.js) - search with nominatim service
* [openroute.js](assets/plugins/openroute.js) - routing with OpenRouteService
* [osm-place-info.js](assets/plugins/osm-place-info.js) - gather place information (website, opening hours, etc.) from OSM API and Wikipedia
* [sentinel2.js](assets/plugins/sentinel2.js) - weekly worldwide 10m satellite imagery from ESA Sentinel-2, with date picker in GUI.
* [transform-query.js](assets/plugins/transform-query.js) - modify search query, e.g., for categorial searches
* [valhalla-osmde.js](assets/plugins/valhalla-osmde.js) - routing with Valhalla provided by osm.de
* [wikipedia-search.js](assets/plugins/wikipedia-search.js) - search for geotagged Wikipedia articles
* [worldview.js](assets/plugins/worldview.js) - show daily worldwide satellite imagery from NASA Worldview, with date picker in GUI.

If you have an idea for a plugin, I'm happy to help and to expose the necessary APIs.


## Vector Maps ##

Vector maps are styled using [Tangram YAML scene files](https://tangrams.readthedocs.io) with a few additions:
* `application.gui_variables` to provide GUI controls in the Edit Source panel linked to scene file globals or shader uniforms
* `application.search_data` to define which features and tags should be indexed for offline search
* `application.legend` to define SVG images to be optionally displayed over the map
* `$latitude` and `$longitude` for tile center are available for filters to make location-specific adjustments to styling
* SVG images are supported for textures; sprites can be created automatically using SVG id attributes.

Tangram styles can include custom shaders.  The hillshading, contour lines, and slope angle shading in the above screenshots are all calculated on-the-fly from elevation tiles in a shader - see [raster-contour.yaml](assets/scenes/raster-contour.yaml).

[scripts/mb2mz.js](scripts/mb2mz.js) is provided to convert a Mapbox style spec JSON file to a Tangram scene file.  See [scripts/runmb2mz.jz](scripts/runmb2mz.js) for an example.  The included [osm-bright.yaml](assets/scenes/osm-bright.yaml) applies the widely-used OSM Bright style to vector tiles using the OpenMapTiles schema.


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


### License ###

The application is provided under the GPL-3.0 license.  The modified Tangram-ES library retains the MIT license.


## More ##

### Roadmap ###

* builds for iOS, Windows, and Mac
* 3D terrain and globe view
* contour line labels
* integrate [Valhalla](https://github.com/valhalla/valhalla/) for offline routing
* more plugins
* Use QuickJS javascript engine instead of Duktape
* pmtiles support


### Internationalization ###

Tangram-ES supports RTL text and complex shaping through Harfbuzz and Freetype.  However, the ugui library used for the GUI does not (yet), so this support is not enabled by default in the application (which incidentally reduces the executable size by almost 50%).  Set the `TANGRAM_USE_FONTCONTEXT_STB` cmake option to OFF to restore RTL and complex shaping for the map display.  Note that such text will not be displayed correctly in the GUI, e.g., when showing place information.


### Application data contents ##

* `cache/*.mbtiles` - map tile storage; deleting this folder will delete all map tiles, including offline maps
* `plugins/*.js` - plugins
* `res/` - various resources for application, e.g. GUI icons
* `scenes/` - map styles
* `tracks/*.gpx` - tracks and routes
* `config.yaml` - settings; exit application before editing
* `fts1.sqlite` - index for offline search
* `mapsources.yaml` - map sources; exit application before editing
* `places.sqlite` - saved places, etc.

Storage use can be controlled with the `shrink_at` and `shrink_to` values in the `storage` section of `config.yaml`.


### GUI ###

[styluslabs/ugui](https://github.com/styluslabs/ugui) is used for cross-platform GUI.  It is a work-in-progress.  Suggestions and contributions are welcome.


### Major features ###

Search: offline search for local vector tiles, online search via plugins
Saved places (bookmarks): import via plugins, export and import GPX, create place list from geotagged photos, choose colors in GUI; further customize styling in [markers.yaml](assets/scenes/markers.yaml]
Tracks and routes: record and edit tracks, draw direct (straight-line segments) routes or using plugin for routing
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
