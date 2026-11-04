// Tangram user functions implemented by the app: SDF glyph rendering with nanovg-sw and SVG sprite
//  rasterization with usvg. Kept separate from resources.cpp so the benchmark replay
//  (testing/bench, built by testing/bench.mk) can link the same implementations.

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "fontstash.h"
#include "nanovg.h"
#include "nanovg_sw.h"
#include "ulib/image.h"
#include "ulib/painter.h"
#include "usvg/svgparser.h"
#include "usvg/svgpainter.h"
#include "gl/texture.h"
#include "scene/spriteAtlas.h"

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

  int w = std::ceil(doc->width().px()), h = std::ceil(doc->height().px());
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
