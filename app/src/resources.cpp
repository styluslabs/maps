#include "resources.h"

// nanosvgrast.h doesn't support stroke-alignment - separate widths for left, right, make one zero, no joins for that side?
const char* markerSVG = R"#(<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24">
  <g fill="%s" stroke="#000" stroke-opacity="0.2" stroke-width="1" stroke-alignment="inner">
    <path d="M5 8c0-3.517 3.271-6.602 7-6.602s7 3.085 7 6.602c0 3.455-2.563 7.543-7 14.527-4.489-7.073-7-11.072-7-14.527"/>
  </g>
</svg>)#";

// icons and text are linked by set Label::setRelative() in PointStyleBuilder::addFeature()
// labels are collected and collided by LabelManager::updateLabelSet() - sorted by priority (lower number
//  is higher priority), collided, then sorted by order set by markerSetDrawOrder (not YAML "order") - higher
//  order means drawn later, i.e., on top
// blend_order only supported for style blocks: https://github.com/tangrams/tangram-es/issues/2039
// for pins, used `offset: [0px, -11px]` for marker and text
const char* searchMarkerStyleStr = R"#(
style: points
texture: %s
sprite: global.poi_sprite_fn
sprite_default: generic
size: [[13, 16px], [15, 20px]]
interactive: true
collide: false
priority: 'function() { return feature.priority; }'
text:
  text_source: name
  anchor: [right, left, bottom, top]
  collide: true
  optional: true
  max_lines: 2
  font:
    family: Open Sans
    weight: 600
    size: 12px
    fill: black
    stroke: { color: white, width: 2px }
)#";
// considered using text.optional: 'function() { return $zoom > 18; }' but we don't want risk of marker being hidden
// note that we must cannot use single quotes for text_source function since name could contain them!

// outline: https://github.com/tangrams/tangram-es/pull/1702
const char* dotMarkerStyleStr = R"#(
style: points
collide: false
size: 6px
color: "#CF513D"
outline:
  width: 1px
  color: "#9A291D"
)#";

// current location
const char* locMarkerStyleStr = R"#(
style: points
collide: false
size: 24px
color: "#0000FF"
outline:
  width: 3px
  color: "#FFFFFF"
)#";
