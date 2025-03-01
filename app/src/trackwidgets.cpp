#include "trackwidgets.h"
#include "mapsapp.h"
#include "usvg/svgpainter.h"
#include "ulib/stringutil.h"

static constexpr int LEFT_MARGIN = 15;

TrackPlot::TrackPlot()  // : CustomWidget()
{
  addHandler([this](SvgGui* gui, SDL_Event* event){
    if(event->type == SvgGui::MULTITOUCH) {
      auto points = static_cast<std::vector<SDL_Finger>*>(event->user.data2);
      if(points->size() != 2) {
        prevPinchDist = 0;
        return false;
      }
      SDL_Finger& pt1 = points->front();
      SDL_Finger& pt2 = points->back();
      real pinchcenter = (pt1.x + pt2.x)/2;
      real pinchdist = std::abs(pt1.x - pt2.x);
      SDL_Event* fevent = static_cast<SDL_Event*>(event->user.data1);
      if(prevPinchDist > 0 && fevent->tfinger.type == SDL_FINGERMOTION) {
        real dx = pinchcenter - (node->bounds().left + LEFT_MARGIN);
        real s0 = zoomScale;
        zoomScale = std::min(std::max(1.0, zoomScale*pinchdist/prevPinchDist), maxZoom);
        updateZoomOffset(dx*(1-zoomScale/s0) + (pinchcenter - prevCOM));
        redraw();
      }
      prevCOM = pinchcenter;
      prevPinchDist = pinchdist;
    }
    else if(event->type == SDL_FINGERDOWN && event->tfinger.fingerId == SDL_BUTTON_LMASK) {
      prevCOM = event->tfinger.x;
      prevPinchDist = 0;
      gui->setPressed(this);
    }
    else if(event->type == SDL_FINGERMOTION && gui->pressedWidget == this) {
      updateZoomOffset(event->tfinger.x - prevCOM);
      //zoomOffset = std::min(std::max(-maxDist, zoomOffset + (maxDist/plotWidth/zoomScale)*(event->tfinger.x - prevCOM)), 0.0);
      prevCOM = event->tfinger.x;
      redraw();
    }
    else if(event->type == SDL_FINGERUP && event->tfinger.fingerId == SDL_BUTTON_LMASK) {
      if(gui->fingerClicks > 0 && !sliders->editMode) {
        sliders->trackSlider->setVisible(true);
        gui->setPressed(sliders->trackSlider);
        SDL_Event motion = *event;
        motion.type = SDL_FINGERMOTION;
        sliders->trackSlider->sdlEvent(gui, &motion);
      }
    }
    else if(event->type == SDL_MOUSEWHEEL) {
      real s0 = zoomScale;
      real dx = gui->prevFingerPos.x - (node->bounds().left + LEFT_MARGIN);
      zoomScale = std::min(std::max(1.0, zoomScale*std::exp(0.25*event->wheel.y/120.0)), maxZoom);
      updateZoomOffset(dx*(1-zoomScale/s0));
      redraw();
    }
    else if(onHovered && event->type == SDL_FINGERMOTION && !gui->pressedWidget)
      onHovered((event->tfinger.x - mBounds.left)/mBounds.width());
    else if(onHovered && event->type == SvgGui::LEAVE)
      onHovered(-1);
    else
      return false;
    return true;
  });
}

void TrackPlot::updateZoomOffset(real dx)
{
  real w = plotVsDist ? maxDist : maxTime - minTime;
  minOffset = (1/zoomScale - 1)*w;
  zoomOffset = std::min(std::max(minOffset, zoomOffset + dx*w/plotWidth/zoomScale), 0.0);
  if(onPanZoom)
    onPanZoom();
}

real TrackPlot::plotPosToTrackPos(real s) const
{
  real w = plotVsDist ? maxDist : maxTime - minTime;
  return s/zoomScale - zoomOffset/w;
}

real TrackPlot::trackPosToPlotPos(real s) const
{
  real w = plotVsDist ? maxDist : maxTime - minTime;
  return zoomScale*(s + zoomOffset/w);
}

void TrackPlot::setTrack(const std::vector<Waypoint>& locs, const std::vector<Waypoint>& wpts)
{
  double prevTrackDist = maxDist, prevTrackTime = maxTime - minTime;
  minAlt = 0;  maxAlt = 0;  minSpd = 0;  maxSpd = 0;
  minTime = 0;  maxTime = 10;  maxDist = 100;
  altDistPlot.clear();
  altTimePlot.clear();
  spdDistPlot.clear();
  spdTimePlot.clear();
  if(locs.empty()) return;
  minAlt = FLT_MAX;  maxAlt = -FLT_MAX;  minSpd = FLT_MAX;  maxSpd = -FLT_MAX;
  minTime = locs.front().loc.time;
  maxTime = std::max(minTime + 10, locs.back().loc.time);
  maxDist = std::max(locs.back().dist, 100.0);
  // keep slider handle in same position for recorded track; handle hidden when changing track, so no
  //  harm in updating position
  sliders->trackSlider->sliderPos *= plotVsDist ? prevTrackDist/maxDist : prevTrackTime/(maxTime - minTime);
  altDistPlot.addPoint(-1E6, -1000);  //locs.front().dist
  altTimePlot.addPoint(-1E6, -1000);  //locs.front().loc.time
  double prevDist = -1;
  for(auto& wpt : locs) {
    const Location& tpt = wpt.loc;
    double alt = MapsApp::metricUnits ? tpt.alt : tpt.alt*3.28084;
    if(wpt.dist > prevDist) altDistPlot.addPoint(Point(wpt.dist, alt));
    altTimePlot.addPoint(Point(tpt.time - minTime, alt));
    minAlt = std::min(minAlt, alt);
    maxAlt = std::max(maxAlt, alt);
    double spd = MapsApp::metricUnits ? tpt.spd*3600*0.001 : tpt.spd*3600*0.000621371;
    if(wpt.dist > prevDist) spdDistPlot.addPoint(Point(wpt.dist, spd));
    spdTimePlot.addPoint(Point(tpt.time - minTime, spd));
    minSpd = std::min(minSpd, spd);
    maxSpd = std::max(maxSpd, spd);
    prevDist = wpt.dist;
  }
  altDistPlot.addPoint(1E6, -1000);  //locs.back().dist, -1000);
  altTimePlot.addPoint(1E6, -1000);  //locs.back().loc.time - minTime, -1000);
  if(maxTime - minTime <= 0)
    plotVsDist = true;

  // rounding for altitude range
  real elev = std::max(10.0, maxAlt - minAlt);
  real expnt = std::pow(10, std::floor(std::log10(elev)));
  real lead = elev/expnt;
  real quant = lead > 5 ? expnt : lead > 2 ? expnt/2 : expnt/5;
  if(quant < 5) quant = 5;
  maxAlt = std::ceil((minAlt + elev)/quant)*quant;
  minAlt = std::floor(minAlt/quant)*quant;
  // assuming 5 vertical divisions
  minSpd = 0;
  maxSpd = std::ceil(maxSpd/5)*5;

  maxZoom = locs.size()/8;  // min 8 points in view
  // exclude first and last waypoints and waypoints missing name or distance along path
  waypoints.clear();
  for(size_t ii = 0; ii < wpts.size(); ++ii) {
    if(!wpts[ii].name.empty() && wpts[ii].dist > 0 && wpts[ii].dist < locs.back().dist)
      waypoints.push_back(wpts[ii]);
  }
  updateZoomOffset(0);
  redraw();
}

// return the number w/ the fewest significant digits between x0 and x1
//static real fewestSigDigits(real x0, real x1)
//{
//  real dx = x1 - x0;
//  real expnt = std::pow(10, std::floor(std::log10(dx)));
//  real lead = dx/expnt;
//  real quant = lead > 5 ? 5*expnt : lead > 2 ? 2*expnt : expnt;
//  return std::ceil(x0/quant)*quant;
//}

static void sparseDrawPath(Painter* p, const Path2D& path)
{
  real xscale = p->getTransform().xscale();  // note that this includes paint scale
  int previdx = 0;
  p->beginPath();
  nvgMoveTo(p->vg, path.point(0).x, path.point(0).y);
  for(int ii = 1; ii < path.size(); ++ii) {
    if((path.point(ii).x - path.point(previdx).x)*xscale > 0.5) {
      nvgLineTo(p->vg, path.point(ii).x, path.point(ii).y);
      previdx = ii;
    }
  }
  p->endPath();
}

// should we highlight zoomed region of track on map?
void TrackPlot::draw(SvgPainter* svgp) const
{
  //parseColor(MapsApp::inst->win->node->getStringAttr("--text"))
  Color textColor = darkMode ? Color::WHITE : Color::BLACK;
  Color bgColor = darkMode ? Color::BLACK : Color::WHITE;
  Color gridColor = darkMode ? Color(255, 255, 255, 64) : Color(0, 0, 0, 64);  // Color(128, 128, 128, 128);
  Color axisColor = darkMode ? Color(100, 200, 255) : Color::BLUE;
  Color altiColor = axisColor;  //Color::BLUE;  //Color(0, 0, 255, 255);
  Color spdColor = Color::RED;
  Color wayptColor = axisColor;  //Color::BLUE;
  Color sliderColor = darkMode ? Color::WHITE : Color::BLACK;

  Painter* p = svgp->p;
  int w = mBounds.width() - 4;
  int h = mBounds.height() - 4;
  p->translate(2, 2);
  p->clipRect(Rect::wh(w, h));
  //p->fillRect(Rect::wh(w, h), Color::WHITE);

  // determine width of vertical axis labels (which will be right aligned!)
  p->setFillBrush(textColor);  //Color::BLACK);
  p->setFontSize(12);
  p->setTextAlign(Painter::AlignLeft | Painter::AlignBaseline);
  real labelw = 0;
  int nvert = h < 100 ? 2 : h < 300 ? 5 : h < 600 ? 10 : 20;  //5 * std::exp2(std::floor(std::log2(h/200)));
  real dhalt = (maxAlt - minAlt)/nvert;
  for(int ii = 0; ii <= nvert; ++ii)
    labelw = std::max(labelw, p->textBounds(0, 0, fstring("%.0f", minAlt + (nvert-ii)*dhalt).c_str()));

  int lmargin = vertAxis ? labelw+10 : LEFT_MARGIN;
  int tmargin = 5;  // need to leave some room for topmost vertical axis label
  int plotw = w - 2*lmargin;
  int ploth = h - 20 - tmargin;
  plotWidth = plotw;
  p->translate(lmargin, tmargin);
  p->setTextAlign(Painter::AlignHCenter | Painter::AlignBottom);
  if(plotVsDist) {
    real userDist = MapsApp::metricUnits ? maxDist/1000 : maxDist*0.000621371;  // km or miles
    real xMin = zoomOffset < 0 ? -zoomOffset/1000 : 0.0;  // prevent "-0" axis label
    real xMax = xMin + userDist/zoomScale;
    real dx = xMax - xMin;
    //real anch = fewestSigDigits(xMin, xMax);
    real dw = dx/4;
    int prec = 0;
    while(dw > 10) { dw /= 10; ++prec; }
    while(dw < 1) { dw *= 10; --prec; }
    dw = (dw > 5 ? 5 : dw > 2 ? 2 : 1) * std::pow(10, prec);
    prec = std::max(0, -prec);
    //while(anch - dw > xMin) anch -= dw;
    real anch = std::ceil(xMin/dw)*dw;
    real dxlbl = dw*plotw/dx;
    real anchlbl = (anch - xMin)*(plotw/dx);
    for(int ii = 1; anch + ii*dw < xMax; ++ii)
      p->drawText(anchlbl + ii*dxlbl, h - tmargin, fstring("%.*f", prec, anch + ii*dw).c_str());
    real wlbl0 = p->drawText(anchlbl, h - tmargin, fstring("%.*f", anch > 0 ? prec : 0, anch).c_str());
    p->setTextAlign(Painter::AlignLeft | Painter::AlignBottom);
    p->drawText(wlbl0, h - tmargin, MapsApp::metricUnits ? " km" : " mi");
    // vert grid lines
    p->setFillBrush(Brush::NONE);
    p->setStroke(gridColor, 0.75);
    for(int ii = 0; anchlbl + ii*dxlbl < plotw; ++ii)
      p->drawLine(Point(anchlbl + ii*dxlbl, ploth), Point(anchlbl + ii*dxlbl, 0));
  }
  else {
    int nhorz = 5;
    real xMin = -zoomOffset;
    real xMax = xMin + (maxTime - minTime)/zoomScale;
    real dw = (xMax - xMin)/nhorz;
    for(int ii = 0; ii < nhorz; ++ii) {
      real secs = xMin + ii*dw;
      real hrs = std::floor(secs/3600);
      real mins = (secs - hrs*3600)/60;
      p->drawText(ii*plotw/nhorz, h - tmargin, fstring("%02.0f:%02.0f", hrs, mins).c_str());
    }
    // vert grid lines
    real dxlbl = plotw/nhorz;
    p->setFillBrush(Brush::NONE);
    p->setStroke(gridColor, 0.75);
    for(int ii = 1; ii*dxlbl < plotw; ++ii)
      p->drawLine(Point(ii*dxlbl, ploth), Point(ii*dxlbl, 0));
  }
  // horz grid lines
  for(int ii = 0; ii < nvert; ++ii)
    p->drawLine(Point(0, (ii*ploth)/nvert), Point(plotw, (ii*ploth)/nvert));

  // axes
  //drawCheckerboard(p, w, h, 4, 0x18000000);
  p->setStroke(axisColor, 1.5);
  p->setFillBrush(Brush::NONE);
  if(vertAxis)
    p->drawLine(Point(labelw + 5, h-15), Point(labelw + 5, 0));
  p->drawLine(Point(0, ploth), Point(plotw, ploth));

  // plot
  p->save();
  p->setVectorEffect(Painter::NonScalingStroke);
  p->clipRect(Rect::ltrb(0, 0, plotw, ploth));  // clip plot to axes
  p->scale(plotVsDist ? plotw/maxDist : plotw/(maxTime - minTime), 1);
  p->scale(zoomScale, 1);
  p->translate(zoomOffset, 0);
  if(plotAlt && !altDistPlot.empty()) {
    p->save();
    p->scale(1, -ploth/(maxAlt - minAlt));
    p->translate(0, -maxAlt);
    p->setFillBrush(Color(altiColor).setAlpha(128));
    p->setStroke(altiColor, 2.0);  //Color::NONE);
    //p->drawPath(plotVsDist ? altDistPlot : altTimePlot);
    sparseDrawPath(p, plotVsDist ? altDistPlot : altTimePlot);
    p->restore();
  }
  if(plotSpd && maxSpd > 0) {
    p->scale(1, -ploth/(maxSpd - minSpd));
    p->translate(0, -maxSpd);
    p->setFillBrush(Brush::NONE);
    p->setStroke(spdColor, 2.0);
    //p->drawPath(plotVsDist ? spdDistPlot : spdTimePlot);
    sparseDrawPath(p, plotVsDist ? spdDistPlot : spdTimePlot);
  }
  p->restore();

  // draw indicators for sliders
  if(sliders->editMode) {
    real start = sliders->startSlider->sliderPos;
    real end = sliders->endSlider->sliderPos;
    if(end < start) std::swap(start, end);
    p->setFillBrush(Brush::NONE);
    p->setStroke(Color(0, 0xC0, 0), 1.5);
    p->drawLine(Point(start*plotw, 0), Point(start*plotw, ploth));
    p->setFillBrush(Color(0, 0xC0, 0, 128));
    p->drawPath(Path2D().addEllipse(start*plotw, ploth - 22, 10, 10));

    p->setFillBrush(Brush::NONE);
    p->setStroke(Color::RED, 1.5);
    p->drawLine(Point(end*plotw, 0), Point(end*plotw, ploth));
    p->setFillBrush(Color(Color::RED).setAlpha(128));
    p->drawPath(Path2D().addEllipse(end*plotw, ploth - 22, 10, 10));
  }
  else if(sliders->trackSlider->isVisible()) {
    real s = sliders->trackSlider->sliderPos;
    p->setFillBrush(Brush::NONE);
    p->setStroke(sliderColor, 1.5);
    p->drawLine(Point(s*plotw, 0), Point(s*plotw, ploth));
    p->setFillBrush(Color(sliderColor).setAlpha(128));
    p->drawPath(Path2D().addEllipse(s*plotw, ploth - 22, 10, 10));
  }

  // draw vertical labels
  if(plotAlt) {
    p->setFillBrush(altiColor);
    p->setStroke(bgColor, 4);
    p->setStrokeAlign(Painter::StrokeOuter);
    p->setFontSize(12);
    p->setTextAlign(Painter::AlignRight | Painter::AlignVCenter);
    for(int ii = 0; ii <= nvert; ++ii)
      p->drawText(labelw-lmargin, (ii*ploth)/nvert, fstring("%.0f", minAlt + (nvert-ii)*dhalt).c_str());
    // draw units for top-most label only
    p->setTextAlign(Painter::AlignLeft | Painter::AlignVCenter);
    p->drawText(labelw-lmargin, 0, MapsApp::metricUnits ? " m" : " ft");

  }
  if(plotSpd && maxSpd > 0) {
    real dhspd = (maxSpd - minSpd)/nvert;
    p->setFillBrush(spdColor);
    p->setStroke(bgColor, 4);
    p->setStrokeAlign(Painter::StrokeOuter);
    p->setTextAlign(Painter::AlignRight | Painter::AlignVCenter);
    const char* spdlbl = MapsApp::metricUnits ? "%.0f km/h" : "%.0f mph";
    p->drawText(w-lmargin-5, 0, fstring(spdlbl, minSpd + nvert*dhspd).c_str());
    for(int ii = 1; ii <= nvert; ++ii)
      p->drawText(w-lmargin-5, (ii*ploth)/nvert, fstring("%.0f", minSpd + (nvert-ii)*dhspd).c_str());
  }

  // draw markers for waypoints
  p->setFontSize(11);
  p->setTextAlign(Painter::AlignLeft | Painter::AlignBaseline);
  real texty = 20;
  // lines
  p->setFillBrush(Brush::NONE);
  p->setStroke(wayptColor, 1.5);
  for(const Waypoint& wpt : waypoints) {
    real s = trackPosToPlotPos(plotVsDist ? wpt.dist/maxDist : (wpt.loc.time - minTime)/(maxTime - minTime));
    if(s < 0 || s > 1) continue;
    p->drawLine(Point(s*plotw, 0), Point(s*plotw, ploth));
  }
  // text (we want text on top of all lines)
  p->setFillBrush(textColor);
  p->setStroke(bgColor, 2);
  p->setStrokeAlign(Painter::StrokeOuter);
  for(const Waypoint& wpt : waypoints) {
    real s = trackPosToPlotPos(plotVsDist ? wpt.dist/maxDist : (wpt.loc.time - minTime)/(maxTime - minTime));
    if(s < 0 || s > 1) continue;
    if(texty >= h - 20) texty = 20;  // back to top
    //real textw = p->textBounds(x0, ploth - 20, wpt.name.c_str(), NULL);
    p->drawText(s*plotw + 4, texty, wpt.name.c_str());  // note clip rect is still set
    texty += 16;
  }
  // slider label on top of everything
  if(sliders->trackSlider->isVisible()) {
    real s = sliders->trackSlider->sliderPos;
    p->setStroke(bgColor, 4);
    p->setStrokeAlign(Painter::StrokeOuter);
    p->setFontSize(11);
    p->setTextAlign((s > 0.5 ? Painter::AlignRight : Painter::AlignLeft) | Painter::AlignBaseline);
    p->setFillBrush(altiColor);
    if(plotAlt) p->drawText(s*plotw + (s > 0.5 ? -4 : 4), 20, sliderAlt.c_str());  // note clip rect is still set
    p->setFillBrush(spdColor);
    if(plotSpd) p->drawText(s*plotw + (s > 0.5 ? -4 : 4), 34, sliderSpd.c_str());
  }
}

// TrackSparkline

void TrackSparkline::setTrack(const std::vector<Waypoint>& locs)
{
  altDistPlot.clear();
  minAlt = 0; maxAlt = 0;
  if(locs.empty()) return;
  minAlt = FLT_MAX; maxAlt = -FLT_MAX;
  maxDist = locs.back().dist;
  altDistPlot.addPoint(locs.front().dist, -1000);
  for(auto& wpt : locs) {
    const Location& tpt = wpt.loc;
    altDistPlot.addPoint(Point(wpt.dist, tpt.alt));
    minAlt = std::min(minAlt, tpt.alt);
    maxAlt = std::max(maxAlt, tpt.alt);
  }
  altDistPlot.addPoint(locs.back().dist, -1000);
}

void TrackSparkline::draw(SvgPainter* svgp) const
{
  Color textColor = darkMode ? Color::WHITE : Color::BLACK;
  Color bgColor = darkMode ? Color::BLACK : Color::WHITE;
  Color plotColor = darkMode ? Color(100, 200, 255, 128) : Color(0, 0, 255, 128);

  Painter* p = svgp->p;
  int w = std::max(0.0, mBounds.width() - 4);
  int h = std::max(0.0, mBounds.height() - 4);
  p->translate(2, 2);
  p->clipRect(Rect::wh(w, h));
  //p->fillRect(Rect::wh(w, h), Color::WHITE);

  // plot
  real elev = maxAlt - minAlt;
  int plotw = w;
  int ploth = h;
  //p->clipRect(Rect::ltrb(0, 0, w, h));  // clip plot to axes
  p->save();
  p->scale(plotw/maxDist, -ploth/(maxAlt - minAlt + 0.1*elev));

  p->translate(0, -(maxAlt + 0.05*elev));  // + minAlt);
  p->setFillBrush(plotColor);
  p->setStroke(Color::NONE);
  p->drawPath(altDistPlot);
  p->restore();

  // vertical scale
  p->setStroke(bgColor, 2);
  p->setStrokeAlign(Painter::StrokeOuter);
  p->setFillBrush(textColor);
  p->setFontSize(11);
  p->setTextAlign(Painter::AlignLeft | Painter::AlignVCenter);
  p->drawText(2, 8, MapsApp::elevToStr(maxAlt).c_str());
  p->drawText(2, ploth - 8, MapsApp::elevToStr(minAlt).c_str());
}

// TrackSliders

void SliderHandle::setValue(real value, int update)
{
  value = std::min(std::max(value, 0.0), 1.0);
  if(value == sliderPos && update < FORCE_UPDATE)
    return;
 // sliderHandle->setTransform(Transform2D::translate(rect.width()*sliderPos, 0));
  real w = sliderBg->node->bounds().width() - 2*bgMargin;
  setLayoutTransform(Transform2D::translating(w*(sliderPos-value), 0)*m_layoutTransform);
  sliderPos = value;
  sliderBg->redraw();
  if(update > NO_UPDATE && onValueChanged)
    onValueChanged(sliderPos);
}

SliderHandle::SliderHandle(SvgNode* n, Widget* bg, real margin) : Button(n), bgMargin(margin), sliderBg(bg)
{
  addHandler([this](SvgGui* gui, SDL_Event* event){
    if(event->type == SDL_FINGERMOTION && gui->pressedWidget == this) {
      Rect rect = sliderBg->node->bounds().pad(-bgMargin, 0);
      real pos = (event->tfinger.x - rect.left)/rect.width();
      setValue(pos);
      return true;
    }
    return false;
  });

  onApplyLayout = [this](const Rect& src, const Rect& dest){
    //if(src != dest) {
      Point dr = dest.origin() - src.center();
      real w = sliderBg->node->bounds().width() - 2*bgMargin;
      setLayoutTransform(Transform2D::translating(dr.x + w*sliderPos + bgMargin, dr.y)*m_layoutTransform);
    //}
    return true;
  };
}

SliderHandle* createSliderHandle(Widget* bg, real lmargin, real bmargin)
{
  // all sliders hidden intially
  static const char* slidersSVG = R"#(
    <g class="slider-handle draggable" display="none" box-anchor="left bottom">
      <rect fill="none" stroke="none" x="-11" y="-11" width="22" height="22"/>
      <!-- rect class="slider-handle-inner" x="-4" y="0" width="8" height="12"/ -->
    </g>
  )#";

  SliderHandle* widget = new SliderHandle(loadSVGFragment(slidersSVG), bg, lmargin);
  widget->setMargins(0, 0, bmargin, 0);
  widget->setLayoutIsolate(true);
  return widget;
}

TrackSliders* createTrackSliders(Widget* bg, real lmargin, real bmargin)
{
  TrackSliders* widget = new TrackSliders(new SvgG);
  widget->node->setAttribute("box-anchor", "fill");
  widget->node->setAttribute("layout", "box");

  widget->trackSlider = createSliderHandle(bg, lmargin, bmargin);
  widget->startSlider = createSliderHandle(bg, lmargin, bmargin);
  widget->endSlider = createSliderHandle(bg, lmargin, bmargin);

  widget->addWidget(bg);
  widget->addWidget(widget->trackSlider);
  widget->addWidget(widget->startSlider);
  widget->addWidget(widget->endSlider);

  return widget;
}

void TrackSliders::setCropHandles(real start, real end, int update)
{
  startSlider->setValue(start, update);
  endSlider->setValue(end, update);
}

void TrackSliders::setEditMode(bool editmode)
{
  editMode = editmode;
  startSlider->setVisible(editmode);
  endSlider->setVisible(editmode);
  if(editmode) trackSlider->setVisible(false);
}
