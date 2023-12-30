#include "resources.h"
#include <fstream>
#include "tangram.h"
#include "scene/scene.h"
#include "usvg/svgparser.h"
#include "usvg/svgpainter.h"
#include "ugui/widgets.h"

// put single header library implementations here instead of in mapsapp.cpp which is rebuilt more often
#define FONTSTASH_IMPLEMENTATION
#include "fontstash.h"

#if PLATFORM_DESKTOP
#include "platform_gl.h"
//#define NANOVG_GL3_IMPLEMENTATION
#define NANOVG_GLES3_IMPLEMENTATION
#elif PLATFORM_IOS
#include <OpenGLES/ES3/gl.h>
#include <OpenGLES/ES3/glext.h>
#define NANOVG_GLES3_IMPLEMENTATION
#elif PLATFORM_ANDROID
#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>
#define NANOVG_GLES3_IMPLEMENTATION
#endif

#define NVG_LOG PLATFORM_LOG
#ifndef NO_PAINTER_GL
#include "nanovg-2/src/nanovg_vtex.h"
#include "nanovg-2/src/nanovg_gl_utils.h"
#endif

#define NANOVG_SW_IMPLEMENTATION
#define NVGSWU_GLES2
#define NVGSW_QUIET_FRAME  // suppress axis-aligned scissor warning
#include "nanovg-2/src/nanovg_sw.h"
#include "nanovg-2/src/nanovg_sw_utils.h"

#include "ugui/theme.cpp"


// String resources:
typedef std::unordered_map<std::string, const char*> ResourceMap;
static ResourceMap resourceMap;

static void addStringResources(std::initializer_list<ResourceMap::value_type> values)
{
  resourceMap.insert(values);
}

const char* getResource(const std::string& name)
{
  auto it = resourceMap.find(name);
  return it != resourceMap.end() ? it->second : NULL;
}

static std::string uiIconStr;

static const char* moreCSS = R"#(
.listitem.checked { fill: var(--checked); }
.legend text { fill: inherit; }
)#";

static const char* moreWidgetSVG = R"#(
<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink">
  <g id="listitem-icon" class="image-container" margin="2 5">
    <use class="listitem-icon icon" width="36" height="36" xlink:href=""/>
  </g>

  <g id="listitem-text-2" layout="box" box-anchor="fill">
    <text class="title-text" box-anchor="hfill" margin="0 10"></text>
    <text class="note-text weak" box-anchor="hfill bottom" margin="0 10" font-size="12"></text>
  </g>

  <g id="panel-header-title" margin="0 3" layout="flex" flex-direction="row" box-anchor="hfill">
    <use class="panel-icon icon" width="36" height="36" xlink:href="" />
    <text class="panel-title" box-anchor="hfill" margin="0 9"></text>
  </g>
</svg>
)#";

void fonsDeleter(FONScontext* fons) { fonsDeleteInternal(fons); };
static std::unique_ptr<FONScontext, decltype(&fonsDeleter)> fontStash(NULL, fonsDeleter);

static std::unique_ptr<Painter> boundsPainter;
static std::unique_ptr<SvgPainter> boundsSvgPainter;

void initResources(const char* baseDir)
{
  Painter::initFontStash(FONS_DELAY_LOAD | FONS_SUMMED);
  Painter::loadFont("sans", FSPath(baseDir, "scenes/fonts/roboto-regular.ttf").c_str());
  if(Painter::loadFont("fallback", FSPath(baseDir, "scenes/fonts/DroidSansFallback.ttf").c_str()))
    Painter::addFallbackFont(NULL, "fallback");  // base font = NULL to set as global fallback

  boundsPainter.reset(new Painter(Painter::PAINT_NULL));
  boundsSvgPainter.reset(new SvgPainter(boundsPainter.get()));
  SvgDocument::sharedBoundsCalc = boundsSvgPainter.get();

  // hook to support loading from resources; can we move this somewhere to deduplicate w/ other projects?
  SvgParser::openStream = [](const char* name) -> std::istream* {
    if(name[0] == ':' && name[1] == '/') {
      const char* res = getResource(name + 2);
      if(res)
        return new std::istringstream(res);
      name += 2;  //return new std::ifstream(PLATFORM_STR(FSPath(basepath, name+2).c_str()), std::ios_base::in | std::ios_base::binary);
    }
    return new std::ifstream(PLATFORM_STR(name), std::ios_base::in | std::ios_base::binary);
  };

  //loadIconRes();
  // we could just use SvgGui::useFile directly; presumably, ui-icons will eventually be embedded in exe
  uiIconStr = readFile(FSPath(baseDir, "res/ui-icons.svg").c_str());
  addStringResources({{"ui-icons.svg", uiIconStr.c_str()}});

  SvgCssStylesheet* styleSheet = new SvgCssStylesheet;
  styleSheet->parse_stylesheet(defaultColorsCSS);
  styleSheet->parse_stylesheet(defaultStyleCSS);
  styleSheet->parse_stylesheet(moreCSS);
  styleSheet->sort_rules();
  SvgDocument* widgetDoc = SvgParser().parseString(defaultWidgetSVG);

  std::unique_ptr<SvgDocument> moreWidgets(SvgParser().parseString(moreWidgetSVG));
  for(SvgNode* node : moreWidgets->children())
    widgetDoc->addChild(node);
  moreWidgets->children().clear();

  const SvgDocument* uiIcons = SvgGui::useFile(":/ui-icons.svg");
  widgetDoc->removeChild(widgetDoc->selectFirst("defs"));
  SvgDefs* defs = new SvgDefs;
  defs->addChild(uiIcons->namedNode("chevron-down")->clone());
  defs->addChild(uiIcons->namedNode("chevron-left")->clone());
  defs->addChild(uiIcons->namedNode("chevron-right")->clone());
  widgetDoc->addChild(defs, widgetDoc->firstChild());

  setGuiResources(widgetDoc, styleSheet);
}

// rasterizing SVG markers (previous nanosvg impl removed 2023-08-13)

namespace Tangram {

bool userLoadSvg(char* svg, Texture* texture)
{
  std::unique_ptr<SvgDocument> doc(SvgParser().parseString(svg));
  if(!doc) return false;

  Painter boundsPaint(Painter::PAINT_NULL);
  SvgPainter boundsCalc(&boundsPaint);
  doc->boundsCalculator = &boundsCalc;

  if(doc->hasClass("reflow-icons")) {
    real pad = doc->hasClass("reflow-icons-pad") ? 2 : 0;
    size_t nicons = doc->children().size();
    int nside = int(std::sqrt(nicons) + 0.5);
    int ii = 0;
    real rowheight = 0;
    SvgDocument* prev = NULL;
    for(SvgNode* child : doc->children()) {
      if(child->type() != SvgNode::DOC) continue;
      auto childdoc = static_cast<SvgDocument*>(child);
      if(prev) {
        childdoc->m_x = ii%nside ? prev->m_x + prev->width().px() + pad : 0;
        childdoc->m_y = ii%nside ? prev->m_y : prev->m_y + rowheight;
        //childdoc->invalidate(false); ... shouldn't be necessary
      }
      real h = childdoc->height().px() + pad;
      rowheight = ii%nside ? std::max(rowheight, h) : h;
      prev = childdoc;
      ++ii;
    }
    Rect b = doc->bounds();
    doc->setWidth(b.width());
    doc->setHeight(b.height());
  }

  int w = int(doc->width().px() + 0.5), h = int(doc->height().px() + 0.5);
  Image img(w, h);
  Painter painter(Painter::PAINT_SW | Painter::NO_TEXT, &img);
  painter.setBackgroundColor(::Color::INVALID_COLOR);  // skip BG since image already inited to zeros
  painter.beginFrame();
  painter.translate(0, h);
  painter.scale(1, -1);
  SvgPainter(&painter).drawNode(doc.get());  //, dirty);
  painter.endFrame();

  auto atlas = std::make_unique<SpriteAtlas>();
  bool hasSprites = false;
  for(auto pair : doc->m_namedNodes) {
    if(pair.second->type() != SvgNode::DOC) continue;
    hasSprites = true;
    Rect b = pair.second->bounds();
    glm::vec2 pos(b.left, b.top);
    glm::vec2 size(b.width(), b.height());
    atlas->addSpriteNode(pair.first.c_str(), pos, size);
  }
  if(hasSprites)
    texture->setSpriteAtlas(std::move(atlas));
  texture->setPixelData(w, h, 4, img.bytes(), img.dataLen());
  return true;
}

}
