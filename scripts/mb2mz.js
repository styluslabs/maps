/* jshint node:true,esnext:true */
// Mapbox-GL style to Tangram scene converter - based on https://gitlab.com/stevage/mapbox2tangram

//let schemaCross = require('./schemaCross');
//let yaml = require('js-yaml');
//let cssColor = require('color');
//let logger = require('logging').default('mb2mz');
logger = { debug: console.log, info: console.log, warn: console.log, error: console.log };

/*
TODO:
- line-gap-width: could maybe fake with 'outline' but it's complicated
- line-translate: not supported in Tangram
- fill-pattern: could maybe be supported through textures
*/

function empty(obj) { return !obj || !Object.keys(obj).length; }

const unsupportedProps = /opacity|blur|line-gap-width|line-translate|text-line-height|text-letter-spacing|text-max-angle|antialias|fill-pattern/;

// Return a specific "not supported" message for a specific property, or undefined if it is (supposedly) supported.
function msgUnsupported(prop) {
    Object.keys(unsupportedProps).forEach(k => {
        if (prop.match(k)) {
            return unsupportedProps[k] || 'Not supported.';
        }
    });
}

function reportUnknownProps(mbprops, knownProps, mblayer) {
    let unknownProps = Object.keys(mbprops).filter(k => !knownProps.has(k) && !k.match(unsupportedProps));
    //console.log(unknownProps.length);
    if (unknownProps.length) {
        logger.warn(`[${mblayer.id}] Unhandled style properties: `, unknownProps.join(','));
    }
}

// https://mapzen.com/documentation/tangram/draw/
// Construct the 'draw' element, given a layer and its position in the layers array.
function walkStyle(mblayer, order) {
    // source: https://github.com/mapbox/mapbox-gl-js/blob/8909e261b303f3a8fd2579d80f97d339c15a35a9/src/style-spec/expression/definitions/curve.js
    function exponentialInterpolation(input, base, lowerValue, upperValue) {
        const difference = upperValue - lowerValue;
        const progress = input - lowerValue;

        if (difference === 0) {
            return 0;
        } else if (base === 1) {
            return progress / difference;
        } else {
            return (Math.pow(base, progress) - 1) / (Math.pow(base, difference) - 1);
        }
    }


    /* Convert a zoom-based property, possibly with exponential interpolation.
     { stops: [ [ 12, 2], [ 16, 6 ] ] } =>
     [ [ 12, 2px], [13, 3px]... [16, 6px] ]
    */
    // TODO property funcs and property-and-zoom funcs
    function walkStops(val, transform = x => x, noStops = false) {
        if (val === undefined) {
            return undefined;
        } else if (!Array.isArray(val.stops)) {
            return transform(val);
        } else if (noStops) {
            // a property type that Tangram doesn't support interpolating, we just pick one value.
            return transform(val.stops[0]);
        }
        //logger.info(val.stops);
        if (typeof val.stops[0][1] === 'number') { // TODO check for exponential, categorical...
            // exponentially interpolate
            let ret = [], zooms = val.stops.map(s => s[0]), vals = val.stops.map(s => s[1]);
            let base = val.base || 1; // Mapbox default
            // iterate over each of the explicitly provided stops
            for (let si = 0;  si < zooms.length - 1; si++) {
                const dz = 1; // Can reduce for more piece-wise approximations of the curve.
                // iterate over fixed intervals between stops.
                for (let z = zooms[si]; z < zooms[si+1]; z += dz) {

                    let factor = exponentialInterpolation(z, base, zooms[si], zooms[si+1]);
                    let nextv = vals[si] + (vals[si+1] - vals[si]) * factor;
                    nextv = +(nextv).toFixed(2); // round to two decimal places
                    ret.push([z, transform(nextv)]);
                }
            }
            // Add the final stop.
            ret.push([zooms[zooms.length-1], transform(vals[vals.length-1])]);
            return ret;
        } else
            return val.stops.map(stop => [ stop[0], transform(stop[1]) ]);
    }

    function setDrawDefaults(draw, type) {

        const propDefaults = {
            lines: [ [ 'width', '1px' ] ],
            polygons: [],
            points: [],
            text: [],
            raster: []
        };

        // We can't leave all properties undefined in Mapzen
        propDefaults[type].forEach(p => {
            if (draw[p[0]] === undefined) {
                draw[p[0]] = p[1];
            }
        });
        return draw;
    }

    let knownProps = new Set(); // all the properties that did map to something.

    /*
     Return props from mbprops converted through walk where walk is:
     {
        mbPropName: [ tangramPropName, transform ]
     }
    */
    function walkProps(mbprops, walk) {
        Object.keys(walk).forEach(k => knownProps.add(k));
        let ret = {};
        Object.keys(mbprops).forEach(mbprop => {
            let prop = walk[mbprop], propTransform;
            let noStops;
            if (!prop) {
                return;
            } else if (typeof prop === 'object') {
                propTransform = prop[1];
                noStops = prop[2];
                prop = prop[0];
            }
            ret[prop] = walkStops(mbprops[mbprop], propTransform, noStops);
        });
        return ret;
    }

    // smoosh layout and paint props and hope for the best.
    let mbprops = mblayer.paint || {};
    Object.keys(mblayer.layout || {}).forEach(k => mbprops[k] = mblayer.layout[k]);

    // properties directly on the style object, not the draw object.
    let style = walkProps(mbprops, {
        'line-dasharray':           [ 'dash', x => x.map(d => Math.max(d,0.1)), true ]
    });
    //let style = {};

    // https://mapzen.com/documentation/tangram/Styles-Overview/#using-styles
    style.base = {line: 'lines', fill: 'polygons', 'fill-extrusion': 'polygons', circle: 'points',
        symbol: 'text', raster: 'raster' }[mblayer.type];  // base for symbol determined below
    if (style.base === undefined) {
        logger.error('Unrecognised layer type', mblayer.type);
    }

    let fix_anchors = x => x.replace("top", "temp").replace("bottom", "top").replace("temp", "bottom").replace("left", "temp").replace("right", "left").replace("temp", "right");
    //let px = x => x + 'px';
    let px = x => Array.isArray(x) ? x.map(px) : x + 'px';
    let draw = walkProps(mbprops, {
        'line-cap':                   'cap', // butt, square round
        'line-join':                  'join', // bevel, round, miter
        'line-miter-limit':           'miter_limit',
        //line-round-limit
        'line-color':                 'color',
        'line-opacity':               'alpha',
        //line-translate, line-translate-anchor
        'line-width':               [ 'width', px ],
        //line-gap-width
        //line-offset
        //line-pattern
        'fill-color':                 'color',
        'fill-opacity':               'alpha',
        'fill-extrusion-color':       'color',
        'fill-extrusion-opacity':     'alpha',

        //'line-dasharray':           [ 'dash', x => x.map(d => Math.max(d,0.1)), true ],

        'circle-radius':            [ 'size' , px ],
        'icon-size':                [ 'size', x => Number(x)*100 + '%'],
        'icon-image':                 'sprite',
        'icon-allow-overlap':       [ 'collide', x => !x ], // "collide" means "check for collisions"
        'icon-padding':             [ 'buffer', x => [x+'px', x+'px']],
        'icon-ignore-placement':    [ 'collide', x => !x ], // TODO check these subtleties
        'icon-rotation-alignment':  [ 'angle', x => x === 'map' ? 'auto' : 0],
        'icon-color':                 'color',
        'icon-opacity':               'alpha',
        'icon-translate':           [ 'offset', px ],
        //icon-optional
        'symbol-placement':         [ 'placement', x => x === 'point' ? 'vertex' : 'spaced'],
        'symbol-spacing':           [ 'placement_spacing', px],
        //symbol-avoid-edges (move_into_tile doesn't help)
        'visibility':               [ 'visible', x => x === 'visible']

    });

    let drawText = walkProps(mbprops, {
        'text-field':               [ 'text_source', t => t.replace(/[{}]/g, '').replace('name_en', 'name') ], // limited
        'text-max-width':             'text_wrap', // units: ems ~> characters
        'text-optional':              'optional',
        'text-offset':              [ 'offset', px ],
        'text-anchor':              [ 'anchor', fix_anchors ],
        'text-padding':             [ 'buffer', x => [x, x].map(px)],
        'text-allow-overlap':       [ 'collide', x => !x ],
        //'text-rotation-alignment':  [ 'angle', x => x === 'viewport' ? 0 : 'auto'], // not sure if it applies
        'text-pitch-alignment':     [ 'flat', x => x === 'map' ],
        'text-justify':               'align'  // left, center, right
    });

    // TODO do we need to transform Bold, Italic etc into specific props?
    let font = walkProps(mbprops, {
        'text-font':        [ 'family' ],
        'text-size':        [ 'size', px ],
        'text-transform':   [ 'transform' ],
        'text-color':       [ 'fill' ],
        'text-opacity':     [ 'alpha' ]
    });
    let fontStroke = walkProps(mbprops, {
        'text-halo-width': [ 'width' , px ],
        'text-halo-color': 'color'
    });

    if (!empty(font)) {
        if (!empty(fontStroke)) {
            font.stroke = fontStroke;
        }
        font.size = font.size || '14px';
        drawText.font = font;
    }

    if(mblayer.type == "symbol") {
        if(!empty(drawText) && !drawText.anchor) {
            drawText.anchor = "center";  // mapbox default is center, different from tangram
        }
        if(draw.sprite) {
            style.base = "points";
            if(!empty(drawText))
                draw.text = drawText;
        } else {
            style.base = "text";
            draw.repeat_distance = draw.placement_spacing;
            delete draw.placement_spacing
            delete draw.placement;
            Object.assign(draw, drawText);
        }
    } else if(Object.keys(drawText).length > 0) {
        logger.warn(`[${mblayer.id}] Ignoring text properties set on non-symbol layer`);
    }

    let outline = walkProps(mbprops, {
        'fill-outline-color': 'color',
        'icon-halo-color':    'color',
        'icon-halo-width':  [ 'width', px ]
    });
    if (!empty(outline)) {
        draw.outline = outline;
        draw.outline.width = draw.outline.width || '1px';
    }

    if (mblayer.type === 'fill-extrusion') {
        // TODO convert functions like 'identity' and 'interpolate' to a JS function
        draw.extrude = true;
    }

    if(style.base == 'lines' || style.base == 'polygons')
        draw.order = order;

    // for function or array of stops, Number() will return NaN and NaN != 1 is true
    if (draw.alpha && Number(draw.alpha) != 1) {
        if(style.dash)
            style.blend = 'inlay';
        else if(style.base == 'lines')
            draw.style = 'lines-inlay';
        else if(style.base == 'polygons')
            draw.style = 'polygons-inlay';
    }

    reportUnknownProps(mbprops, knownProps, mblayer);

    draw = setDrawDefaults(draw, style.base);
    //draw.interactive = true; // TODO remove, for debugging.
    style.draw = draw;
    return style;
}

// recursively convert one Mapbox layer filter
function walkFilter(mbfilter) {
    const geomtype = {
        Polygon: 'polygon',
        LineString: 'line',
        Point: 'point'
    };

    if (!mbfilter)
        return;

    let op = mbfilter[0],
        key = mbfilter[1],
        vals = mbfilter.slice(2)
            // not 100% sure about this.
            .map(v => /^(true|false)$/.test(v) ? Boolean(v) : v),
        nonkey = mbfilter.slice(1);

    if (key === '$type') {
        key = '$geometry';
        vals = vals.map( v => geomtype[v] );
    }

    // TODO support >= and <= properly. Yuck.

    let f = {};
    const opFunc = {
        '=='   : _ => f[key] = vals[0],
        '!='   : _ => f.not  = { [key]: vals[0] },
        'in'   : _ => f[key] = vals,
        '!in'  : _ => f.not  = { [key]: vals },
        'has'  : _ => f[key] = true,
        '!has' : _ => f[key] = false,
        '<'    : _ => f[key] = { max: vals[0] },
        '<='   : _ => f[key] = { max: vals[0] },
        '>'    : _ => f[key] = { min: vals[0] },
        '>='   : _ => f[key] = { min: vals[0] },
        'any'  : _ => f.any  = nonkey.map(walkFilter),
        'all'  : _ => f.all  = nonkey.map(walkFilter),
        'none' : _ => f.none = nonkey.map(walkFilter)
    }[op] || (_ => void logger.warn(`Unrecognised operator: ${op}`));
    //let filter = {};
    opFunc();
    return f;
}

// convert one whole Mapbox style to a Tangram style
function walkStyleFile(mbstyle, options) {
    if (options.customLogger) logger = options.customLogger;
    let scene = { global: {}, sources: {}, scene: {}, lights: {}, textures: {}, fonts: {}, styles: {}, layers: {} };
    if (options.globalColors) scene.global.color = {};
    scene.styles["lines-inlay"] = { base: 'lines', blend: 'inlay' };
    scene.styles["polygons-inlay"] = { base: 'polygons', blend: 'inlay' };
    mbstyle.layers.forEach((mblayer, order) => {
        if(mblayer.type == "background") {
          scene.scene.background = { color: mblayer.paint['background-color'] };
          return;
        }

        let layer = {};
        if(!mblayer.ref)
            layer.data = { source: mblayer['source'] || 'mapfit', layer: mblayer['source-layer'] };
        scene.sources[mblayer.source] = {};
        layer.filter = walkFilter(mblayer.filter);

        if(mblayer.minzoom || mblayer.maxzoom) {
            if(layer.filter.all)
                layer.filter.all.unshift({ "$zoom": {} });
            else
                layer.filter = { all: [ { "$zoom": {} }, layer.filter ] };
            if(mblayer.minzoom) layer.filter.all[0]["$zoom"].min = mblayer.minzoom;
            if(mblayer.maxzoom) layer.filter.all[0]["$zoom"].max = mblayer.maxzoom;
        }

        // "ref" is deprecated in mapbox style spec; we'll treat as a sublayer
        if (mblayer.ref && !mblayer.type) mblayer.type = mbstyle.layers[mblayer.ref]
        let style = walkStyle(mblayer, order);

        if (options.globalColors) {
          if(style.draw.color) {
            scene.global.color[mblayer.id] = style.draw.color;
            style.draw.color = "global.color." + mblayer.id;
          }
          const font = style.draw.text ? style.draw.text.font : style.draw.font;
          if(font) {
            if(font.fill) {
              scene.global.color[mblayer.id + "_text"] = font.fill;
              font.fill = "global.color." + mblayer.id + "_text";
            }
            if(font.stroke && font.stroke.color) {
              scene.global.color[mblayer.id + "_halo"] = font.stroke.color;
              font.stroke.color = "global.color." + mblayer.id + "_halo";
            }
          }
        }

        if(style.blend || style.dash) {
            scene.styles[mblayer.id] = style;
            layer.draw = { [mblayer.id]: {} };
        } else {
            layer.draw = {[style.base]: style.draw};
        }

        if (mblayer.ref)
            scene.layers[mblayer.ref][mblayer.id] = layer;  // add as sublayer
        else
            scene.layers[mblayer.id] = layer;
    });
    return scene;
}

module.exports.toTangram = walkStyleFile;
