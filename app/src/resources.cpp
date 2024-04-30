#include "resources.h"
#include <fstream>
#include "mapsapp.h"
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

const char* mapsColorsCSS = R"#(
svg.window  /* dark theme */
{
  --dark: #101010;
  --window: #303030;
  --light: #505050;
  --base: #202020;
  --button: #555555;
  --hovered: #32809C;
  --pressed: #32809C;
  --checked: #0000C0;
  --title: #2EA3CF;
  --text: #F2F2F2;
  --text-weak: #A0A0A0;
  --text-bg: #000000;
  --icon: #CDCDCD;
  --icon-disabled: #808080;
  --text-sel: #F2F2F2;
  --text-sel-bg: #2E7183;
}

svg.window.light  /* light theme */
{
  --dark: #FFFFFF;  /* was #F0F0F0; */
  --window: #EEEEEE;  /* was #DDDDDD */
  --light: #CCCCCC;
  --base: #FFFFFF;
  --button: #D0D0D0;
  --hovered: #B8D8F9;
  --pressed: #B8D8F9;
  --checked: #A2CAEF;
  --title: #2EA3CF;
  --text: #000000;
  --text-weak: #606060;
  --text-bg: #F2F2F2;
  --icon: #404040;  /* was #303030 */
  --icon-disabled: #A0A0A0;
  --text-sel: #FFFFFF;
  --text-sel-bg: #0078D7;
}
)#";

static const char* moreCSS = R"#(
.listitem.checked { fill: var(--checked); }
.legend text { fill: inherit; }
.panel-container { fill: var(--dark); }
.menu { fill: var(--window); }  /* same color as menuitem to eliminate dividers */
.splitter { fill: var(--window); }
/* .toolbar { fill: var(--base); } */
.roundbutton { fill: var(--base); }
.roundbutton .background { stroke: var(--light); stroke-width: 0.5 }
.inputbox-bg { stroke: var(--light); }  /* show low-contrast border by default for input boxes */
tspan.text-selection { fill: var(--text-sel); }
.text-selection-bg { fill: var(--text-sel-bg); }
.checkmark { color: #0078D7; }
.scroll-handle { fill: #808080; }  /* make scroll handle grey since it's currently not draggable */
)#";

static const char* moreWidgetSVG = R"#(
<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink">
  <g id="listitem-icon" class="image-container" margin="2 5">
    <use class="listitem-icon icon" width="30" height="30" xlink:href=""/>
  </g>

  <g id="listitem-text-2" layout="box" box-anchor="fill">
    <text class="title-text" box-anchor="hfill" margin="0 10"></text>
    <text class="note-text weak" box-anchor="hfill bottom" margin="0 10" font-size="12"></text>
  </g>

  <g id="panel-header-title" margin="0 3" layout="flex" flex-direction="row" box-anchor="hfill">
    <use class="panel-icon icon" width="32" height="32" margin="3 9 3 3" xlink:href="" />
    <text class="panel-title" box-anchor="hfill"></text>
  </g>

  <g id="toolbutton" class="toolbutton" layout="box">
    <rect class="background" box-anchor="hfill" width="36" height="42"/>
    <rect class="checkmark" box-anchor="bottom hfill" margin="0 2" fill="none" width="36" height="3"/>
    <g margin="0 5" box-anchor="fill" layout="flex" flex-direction="row">
      <use class="icon" width="30" height="30" xlink:href="" />
      <text class="title" display="none" margin="0 9"></text>
    </g>
  </g>

  <g id="roundbutton" class="toolbutton roundbutton" layout="box">
    <circle class="background" cx="21" cy="21" r="21"/>
    <use class="icon" margin="5 5" width="30" height="30" xlink:href="" />
  </g>

  <g id="colorbutton" class="color_preview previewbtn">
    <rect fill="none" width="42" height="42"/>
    <circle class="btn-color" stroke="currentColor" stroke-width="2" fill="blue" cx="21" cy="21" r="15.5" />
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
#if PLATFORM_IOS || PLATFORM_OSX
  const char* dfltFont = "scenes/fonts/SanFranciscoDisplay-Regular.otf";
#else
  const char* dfltFont = "scenes/fonts/roboto-regular.ttf";
#endif
  std::string uiFont = MapsApp::config["ui"]["font"].as<std::string>(dfltFont);
  Painter::loadFont("sans", FSPath(baseDir, uiFont).c_str());
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
  styleSheet->parse_stylesheet(mapsColorsCSS);  //defaultColorsCSS
  styleSheet->parse_stylesheet(defaultStyleCSS);
  styleSheet->parse_stylesheet(moreCSS);
  styleSheet->sort_rules();
  SvgDocument* widgetDoc = SvgParser().parseString(defaultWidgetSVG);

  std::unique_ptr<SvgDocument> moreWidgets(SvgParser().parseString(moreWidgetSVG));
  for(SvgNode* node : moreWidgets->children()) {
    // remove widgets we want to replace
    SvgNode* oldnode = widgetDoc->namedNode(node->xmlId());
    if(oldnode) {
      widgetDoc->removeChild(oldnode);
      delete oldnode;
    }
    widgetDoc->addChild(node);
  }
  moreWidgets->children().clear();

  // replace widget default icons (e.g. combo box chevron)
  const SvgDocument* uiIcons = SvgGui::useFile(":/ui-icons.svg");
  auto useNodes = widgetDoc->select("use");
  for(SvgNode* n : useNodes) {
    SvgUse* usenode = static_cast<SvgUse*>(n);
    SvgNode* target = uiIcons->namedNode(usenode->href());
    if(target)
      usenode->setTarget(target);
  }

  setGuiResources(widgetDoc, styleSheet);
}

struct SDFcontext { NVGcontext* vg; std::vector<float> fbuff; int fbuffw, fbuffh; float sdfScale, sdfOffset; };

static constexpr float INITIAL_SDF_DIST = 1E6f;

static void sdfRender(void* uptr, void* fontimpl, unsigned char* output,
      int outWidth, int outHeight, int outStride, float scale, int padding, int glyph)
{
  SDFcontext* ctx = (SDFcontext*)uptr;

  nvgBeginFrame(ctx->vg, 0, 0, 1);
  nvgDrawSTBTTGlyph(ctx->vg, (stbtt_fontinfo*)fontimpl, scale, padding, glyph);
  nvgEndFrame(ctx->vg);

  for(int iy = 0; iy < outHeight; ++iy) {
    for(int ix = 0; ix < outWidth; ++ix) {
      float sd = ctx->fbuff[ix + iy*ctx->fbuffw] * ctx->sdfScale + ctx->sdfOffset;
      output[ix + iy*outStride] = (unsigned char)(0.5f + std::min(std::max(sd, 0.f), 255.f));
      ctx->fbuff[ix + iy*ctx->fbuffw] = INITIAL_SDF_DIST;   // will get clamped to 255
    }
  }
}

static void sdfDelete(void* uptr)
{
  SDFcontext* ctx = (SDFcontext*)uptr;
  nvgswDelete(ctx->vg);
  delete ctx;
}

namespace Tangram {

FONScontext* userCreateFontstash(FONSparams* params, int atlasFontPx)
{
  SDFcontext* ctx = new SDFcontext;
  ctx->fbuffh = ctx->fbuffw = atlasFontPx + 2*params->sdfPadding + 16;
  // we use dist < 0.0f inside glyph; but for scale > 0, stbtt uses >on_edge_value for inside
  ctx->sdfScale = -params->sdfPixelDist;
  ctx->sdfOffset = 127;  // stbtt on_edge_value

  ctx->fbuff.resize(ctx->fbuffw*ctx->fbuffh, INITIAL_SDF_DIST);
  ctx->vg = nvgswCreate(NVG_AUTOW_DEFAULT | NVG_NO_FONTSTASH | NVGSW_PATHS_XC | NVGSW_SDFGEN);
  nvgswSetFramebuffer(ctx->vg, ctx->fbuff.data(), ctx->fbuffw, ctx->fbuffh, params->sdfPadding, 0,0,0);

  params->userPtr = ctx;
  params->userSDFRender = sdfRender;
  params->userDelete = sdfDelete;
  return fonsCreateInternal(params);
}

// rasterizing SVG markers (previous nanosvg impl removed 2023-08-13)

bool userLoadSvg(const char* svg, size_t len, Texture* texture)
{
  std::unique_ptr<SvgDocument> doc(SvgParser().parseString(svg, len));
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
