# Ascend Maps #

A cross-platform application for displaying vector and raster maps, built on [Tangram-ES](https://tangrams.readthedocs.io) and supporting [plugins](#plugin-system) for search, routing, map sources and more.  Includes a simple, customizable [style](#stylus-labs-osm-schema) for OpenStreetMap vector tiles.

Features include 3D terrain, offline search, track recording and editing, managing saved places, saving map tiles for offline use, and more.

Available for [Android](https://github.com/styluslabs/maps/releases), [Linux](https://github.com/styluslabs/maps/releases), [Windows](https://github.com/styluslabs/maps/releases), and iOS: [App Store](https://apps.apple.com/us/app/ascend-maps/id6504321706), [Testflight](https://testflight.apple.com/join/3N1AUhj9).

<img alt="Wikipedia search; Shaded relief" src="https://github.com/user-attachments/assets/d00ca2ca-f4d1-4f71-bef8-4dfcbb0b7d36" width="270">
<img alt="3D Terrain" src="https://github.com/user-attachments/assets/c59d61bb-f09b-41e8-902c-1e7984cbeae4" width="270">
<img alt="Ski runs; Slope angle shading; Map legend" src="https://github.com/user-attachments/assets/22ea4327-b8af-47f0-b230-e57f4a8f0356" width="270">
<img alt="Offline POI search" src="https://github.com/user-attachments/assets/b6ebf4a1-11c1-4665-9e34-da29ddc1ddbd" width="270">
<img alt="3D buildings; POIs; Place info" src="https://github.com/user-attachments/assets/ddc5ec69-a052-4b9e-af6a-66b93286ea41" width="270">
<img alt="Sentinel-2 imagery; Map source GUI variables" src="https://github.com/user-attachments/assets/b1d53aa8-c147-41c3-afe3-61f9e0d14514" width="270">
<img alt="Track recording" src="https://github.com/user-attachments/assets/c699381a-4c61-4a68-8847-047cbc2cdce6" width="270">
<img alt="3D Terrain; Heatmap overlay; Trail visibility tag" src="https://github.com/user-attachments/assets/480ee145-9d18-4e0f-a218-c56e1ae6573f" width="270">
<img alt="Routing; MTB trail scale" src="https://github.com/user-attachments/assets/0fb87956-062b-4543-9906-454d9f482f99" width="270">


## Quick start ##
* Toggle 3D terrain from the "..." menu.  Swipe up with two fingers to tilt the map.
* Search: offline search is available for downloaded Ascend OSM map regions.  For worldwide place name search, try the Nominatim search plugin.  For POI search in the current map view, try the Overpass search plugin.  To see notable places in the current map view, try the Wikipedia search plugin.  Tap the icon in the search box to change the current search plugin.
* Customize map: in the map sources list, tap the eye icon to toggle additional map layers.  Tap the edit icon to edit a map source.  The Ascend OSM maps have options to show hiking, biking, and transit routes.  Tap the plus icon to create a new map source combining existing sources.  See [Adding map sources](#adding-map-sources) for more detail.
* Offline maps: tap the offline icon on the map sources toolbar, then pan and zoom the view to show the desired region, tap the download button on the Offline maps toolbar, enter the maximum zoom level to include, then tap Download.  Tiles for all layers of the current map source will be downloaded.
* Submit [bug reports](https://github.com/styluslabs/maps/issues), [pull requests](https://github.com/styluslabs/maps/pulls), [feature suggestions](https://github.com/styluslabs/maps/discussions), and [plugin ideas](https://github.com/styluslabs/maps/discussions)!


### Building ###

On Linux, `git clone --recurse-submodules https://github.com/styluslabs/maps`, install build dependencies (`apt install libgtk-3-dev libcurl4-openssl-dev libfontconfig-dev libxinerama-dev` on Debian/Ubuntu), then run `cd maps && make` to generate `build/Release/ascend`.  To build and install for Android (on Linux w/ Android SDK and NDK installed), `cd maps/app/android && ./gww installRelease`.  The `gww` (gradle wrapper wrapper) script will download and run `gradlew`.  To install the Android SDK and NDK, run `gww --install-sdk`.


### Adding map sources ###

New map sources can be added by combining existing sources (the "+" button on the toolbar), editing mapsources.yaml (with the application closed), editing mapsources.default.yaml and choosing Restore default sources from the overflow menu, or via "Import source" on the overflow menu, where a YAML fragment or map tile URL (e.g. `https://some.tile.server/tiles/{z}/{x}/{y}.png`) can be entered, or a YAML scene file chosen, for custom vector map styles.

To add an overlay source (raster tiles with transparency), import a YAML fragment with the URL and the layer property set: `{url:"https://...", layer: true}`

The default configuration includes [markers.yaml](assets/scenes/markers.yaml) for all maps to support the display of search results, saved places, tracks, routes, the location marker, and other markers.


### Application data ###

Application data is stored in the executable's folder on Linux (will be changed to `~/.config/styluslabs/maps` in the future).  On Android, `/Android/data/com.styluslabs.maps/files` is used by default but if All Files Access is enabled for Ascend, it is possible to choose a more accessible shared folder when Ascend is first installed.  Currently, there are only a few configuration options available in the GUI, but many more are available in `config.yaml` in the application data folder.  it can be edited directly after exiting the app or via the plugin console within the app, e.g., `readSceneValue("config.storage.shrink_at")`, `writeSceneValue("config.storage.shrink_at", 500000000)`.  See [config.default.yaml](assets/config.default.yaml) for documentation.

On Android, all files created by the application (except `config.yaml` and `mapsources.yaml`) will be replaced when a newer APK is installed, so edits should only be made to copies, not the original files.


## Plugin system ##

Plugins written in Javascript can add search providers, routing providers, map tile providers, and more.

Files in `plugins/` with `.js` extension are executed at application startup.  To reload, tap the reload button on the Plugin Console toolbar.  Some plugins (`openroute.js` and `sentinel2.js` currently) require third-party API keys to work - follow the instructions in the plugin's js file to obtain an API key, then create a file `plugins/_secrets.js` to set `secrets = { "some_api_key": value, "another_api_key": value }`.

Currently uses Duktape, so support for features from ES2015 and later is limited.

The included plugins give a sample of what's possible:
* [google-import.js](assets/plugins/google-import.js) - import list of places from GeoJSON exported by Google Maps; run from the plugin console
* [nominatim-search.js](assets/plugins/nominatim-search.js) - search with nominatim service
* [overpass.js](assets/plugins/overpass.js) - search with overpass-turbo; supports overpass query language expressions or plain keyword search (matches values of all tags)
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

Tangram styles can include custom shaders.  The hillshading, contour lines, and slope angle shading in the above screenshots are all calculated on-the-fly from elevation tiles in a shader - see [hillshade.yaml](assets/scenes/hillshade.yaml).

[scripts/mb2mz.js](scripts/mb2mz.js) is provided to convert a Mapbox style spec JSON file to a Tangram scene file.  See [scripts/runmb2mz.jz](scripts/runmb2mz.js) for an example.  The included [osm-bright.yaml](assets/scenes/osm-bright.yaml) applies the widely-used OSM Bright style to vector tiles using the OpenMapTiles schema.


### Generating tiles ###

[geodesk-tiles](https://github.com/styluslabs/geodesk-tiles) can be used to serve tiles on demand or generate mbtiles files for import.

To import an mbtiles file, navigate to Map sources -> Offline maps -> Open in the application.  If prompted, choose "Ascend OSM Base" as the destination source.  The tiles will be imported and indexed for offline search based on the configuration `application.search_data` in [stylus-osm.yaml](assets/scenes/stylus-osm.yaml).  On Android, it is necessary to enable file access permission for Ascend in the system app settings in order to open files outside the application's private folder.

To speed up import a bit, the Linux version can be used to add the search data to the mbtiles file before copying it to a mobile device (note that this will also import the map to the Linux version's storage):
```
ascend --import <output>.mbtiles --storage.export_pois true
```

### Vector Tiles Background ###

Displaying a vector map requires a source of vector tiles and a style for specifying how to draw features from the tiles.

The hierarchy for vector tiles consists of:

* Container: filesystem (each tile as a separate file), mbtiles (an sqlite file), pmtiles (special flat file format)
* Tile encoding: PBF (protocol buffer format), GeoJSON, etc.
* Tile format: [mapbox vector tiles](https://github.com/mapbox/vector-tile-spec) is dominant - specifies PBF encoding
* Tile schema: [Shortbread](https://shortbread-tiles.org/), [OpenMapTiles](https://openmaptiles.org/schema/), [Mapzen/Tilezen](https://tilezen.readthedocs.io/en/latest/layers/), [Mapbox](https://docs.mapbox.com/data/tilesets/reference/mapbox-streets-v8/)

A vector tile schema specifies how to group features into layers, which features to include at each zoom level, and which attributes to include for each feature.


### Ascend OSM schema ###

The included vector map style [stylus-osm.yaml](assets/scenes/stylus-osm.yaml) (seen in the above screenshots) uses a custom schema because none of the existing tile schemas satisfied all the requirements for the application.  For example, most other schemas define some mapping from OSM tags to feature attributes, but because of the complex and fluid state of OSM tagging this schema just uses unmodified OSM tags for feature attributes.

The schema aims to include more information for transit and for outdoor activities, such as bike and trail features at lower zoom levels and the common trail_visibility tag.  The schema layers are: place, boundary, poi, transportation, transit, building, water, and landuse.

The schema is a work-in-progress.  Suggestions and comments are welcome.


### License ###

The application is provided under the GPL-3.0 license.  The modified Tangram-ES library retains the MIT license.


## More ##

### Roadmap ###

* integrate [Valhalla](https://github.com/valhalla/valhalla/) for offline routing
* Use QuickJS javascript engine instead of Duktape
* more plugins
* builds for Windows and Mac
* globe view
* pmtiles support


### Internationalization ###

Tangram-ES supports RTL text and complex shaping through Harfbuzz and Freetype.  However, the ugui library used for the GUI does not (yet), so this support is not enabled by default in the application (which incidentally reduces the executable size by almost 50%).  Remove `FONTCONTEXT_STB=1` from `MODULE_DEFS_PUBLIC` in to restore RTL and complex shaping for the map display.  Note that such text will not be displayed correctly in the GUI, e.g., when showing place information.


### Application data contents ##

* `cache/*.mbtiles` - map tile storage; deleting this folder will delete all map tiles, including offline maps
* `plugins/*.js` - plugins
* `res/` - various resources for application, e.g. GUI icons
* `scenes/` - map styles
* `tracks/*.gpx` - tracks and routes
* `config.yaml` - settings; exit application before editing or use plugin console commands (see above)
* `fts1.sqlite` - index for offline search
* `mapsources.yaml` - map sources; exit application before editing
* `places.sqlite` - saved places, etc.

Storage use can be controlled with the `shrink_at` and `shrink_to` values in the `storage` section of `config.yaml`.


### GUI ###

[styluslabs/ugui](https://github.com/styluslabs/ugui) is used for cross-platform GUI.  It is a work-in-progress.  Suggestions and contributions are welcome.


### Major features ###

* Search: offline search for local vector tiles, online search via plugins
* Saved places (bookmarks): import via plugins, export and import GPX, create place list from geotagged photos, choose colors in GUI; further customize styling in [markers.yaml](assets/scenes/markers.yaml)
* Tracks and routes: record and edit tracks, draw direct (straight-line segments) routes or using plugin for routing
* Map sources: create and manage map sources, access GUI controls for current map source, show legends
* Offline maps (via Map sources): create offline map from current source and view, import mbtiles file, manage offline maps
* Plugin console (via overflow menu): reload plugins, execute Javascript in plugin environment


### Major components ###

* [modified version of Tangram-ES](https://github.com/pbsurf/maps) - full compatibility with upstream will be restored in the future
    * Duktape javascript interpreter
    * yaml-cpp
    * sqlite for mbtiles support
* [ugui](https://github.com/styluslabs/ugui), [usvg](https://github.com/styluslabs/usvg), [ulib](https://github.com/styluslabs/ulib), [nanovgXC](https://github.com/styluslabs/nanovgXC) for GUI
    * [pugixml](https://github.com/zeux/pugixml)
    * [nfd](https://github.com/btzy/nativefiledialog-extended) for file dialogs
    * sqlite for saved places, offline search, etc.
