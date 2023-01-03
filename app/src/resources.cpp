#include "resources.h"

// nanosvgrast.h doesn't support stroke-alignment - separate widths for left, right, make one zero, no joins for that side?
const char* markerSVG = R"#(<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24">
  <g fill="%s" stroke="#000" stroke-opacity="0.2" stroke-width="1" stroke-alignment="inner">
    <path d="M5 8c0-3.517 3.271-6.602 7-6.602s7 3.085 7 6.602c0 3.455-2.563 7.543-7 14.527-4.489-7.073-7-11.072-7-14.527"/>
  </g>
</svg>)#";

// icons and text are linked by set Label::setRelative() in PointStyleBuilder::addFeature()
// labels are collected and collided by LabelManager::updateLabelSet() - sorted by priority (lower number
//  is higher priority), collided, then sorted by order (higher order means drawn later, i.e., on top)
const char* searchMarkerStyleStr = R"#(
style: points
texture: %s
interactive: true
collide: false
offset: [0px, -11px]
priority: 0.%06d
blend_order: 3
text:
  text_source: "function() { return \"%s\"; }"
  anchor: [right, left, bottom, top]
  offset: [0px, -11px]
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
blend_order: 2
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
blend_order: 1000
size: 24px
color: "#0000FF"
outline:
  width: 3px
  color: "#FFFFFF"
)#";
