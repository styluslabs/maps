#include "trackwidgets.h"
#include "mapsapp.h"
#include "usvg/svgpainter.h"


TrackPlot::TrackPlot()  // : CustomWidget()
{
  addHandler([this](SvgGui* gui, SDL_Event* event){
    if(event->type == SvgGui::MULTITOUCH) {
      auto points = static_cast<std::vector<SDL_Finger>*>(event->user.data2);
      if(points->size() != 2) return false;
      SDL_Finger& pt1 = points->front();
      SDL_Finger& pt2 = points->back();
      real pinchcenter = (pt1.x - pt2.x)/2;
      real pinchdist = std::abs(pt1.x - pt2.x);
      SDL_Event* fevent = static_cast<SDL_Event*>(event->user.data1);
      if(fevent->tfinger.type == SDL_FINGERMOTION) {
        //zoomOffset += pinchcenter - prevCOM;
        zoomScale = std::max(1.0, zoomScale*pinchdist/prevPinchDist);
        updateZoomOffset(pinchcenter - prevCOM);
        redraw();
      }
      prevCOM = pinchcenter;
      prevPinchDist = pinchdist;
    }
    else if(event->type == SDL_FINGERDOWN && event->tfinger.fingerId == SDL_BUTTON_LMASK) {
      prevCOM = event->tfinger.x;
      gui->setPressed(this);
    }
    else if(event->type == SDL_FINGERMOTION && gui->pressedWidget == this) {
      updateZoomOffset(event->tfinger.x - prevCOM);
      //zoomOffset = std::min(std::max(-maxDist, zoomOffset + (maxDist/plotWidth/zoomScale)*(event->tfinger.x - prevCOM)), 0.0);
      prevCOM = event->tfinger.x;
      redraw();
    }
    else if(event->type == SDL_MOUSEWHEEL) {
      zoomScale = std::min(std::max(1.0, zoomScale*std::exp(0.25*event->wheel.y/120.0)), maxZoom);
      updateZoomOffset(0);
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
  zoomOffset = std::min(std::max((1/zoomScale - 1)*w, zoomOffset + dx*w/plotWidth/zoomScale), 0.0);
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
  minAlt = FLT_MAX;
  maxAlt = -FLT_MAX;
  minSpd = FLT_MAX;
  maxSpd = -FLT_MAX;
  minTime = locs.front().loc.time;
  maxTime = std::max(minTime + 10, locs.back().loc.time);
  maxDist = std::max(locs.back().dist, 100.0);
  altDistPlot.clear();
  altTimePlot.clear();
  spdDistPlot.clear();
  spdTimePlot.clear();
  altDistPlot.addPoint(locs.front().dist, -1000);
  altTimePlot.addPoint(0, -1000);  //locs.front().loc.time
  for(auto& wpt : locs) {
    const Location& tpt = wpt.loc;
    altDistPlot.addPoint(Point(wpt.dist, MapsApp::metricUnits ? tpt.alt : tpt.alt*3.28084));
    altTimePlot.addPoint(Point(tpt.time - minTime, MapsApp::metricUnits ? tpt.alt : tpt.alt*3.28084));
    minAlt = std::min(minAlt, tpt.alt);
    maxAlt = std::max(maxAlt, tpt.alt);
    spdDistPlot.addPoint(Point(wpt.dist, MapsApp::metricUnits ? tpt.spd*3600*0.001 : tpt.spd*3600*0.000621371));
    spdTimePlot.addPoint(Point(tpt.time - minTime, MapsApp::metricUnits ? tpt.spd*3600*0.001 : tpt.spd*3600*0.000621371));
    minSpd = std::min(minSpd, tpt.spd);
    maxSpd = std::max(maxSpd, tpt.spd);
  }
  altDistPlot.addPoint(locs.back().dist, -1000);
  altTimePlot.addPoint(locs.back().loc.time - minTime, -1000);
  if(maxTime - minTime <= 0)
    plotVsDist = true;

  // rounding for altitude range
  real elev = maxAlt - minAlt;
  real expnt = std::pow(10, std::floor(std::log10(elev)));
  real lead = elev/expnt;
  real quant = lead > 5 ? expnt : lead > 2 ? expnt/2 : expnt/5;
  minAlt = std::floor(minAlt/quant)*quant;  //-= 0.05*elev;
  maxAlt = std::ceil(maxAlt/quant)*quant;  //+= 0.05*elev;
  maxZoom = locs.size()/8;  // min 8 points in view
  // exclude first, last, and unnamed waypoints
  waypoints.clear();
  for(int ii = 1; ii < int(wpts.size()) - 1; ++ii) {
    if(!wpts[ii].name.empty())
      waypoints.push_back(wpts[ii]);
  }
}

// return the number w/ the fewest significant digits between x0 and x1
static real fewestSigDigits(real x0, real x1)
{
  real dx = x1 - x0;
  real expnt = std::pow(10, std::floor(std::log10(dx)));
  real lead = dx/expnt;
  real quant = lead > 5 ? 5*expnt : lead > 2 ? 2*expnt : expnt;
  return std::ceil(x0/quant)*quant;
}

// should we highlight zoomed region of track on map?
void TrackPlot::draw(SvgPainter* svgp) const
{
  Painter* p = svgp->p;
  int w = mBounds.width() - 4;
  int h = mBounds.height() - 4;
  p->translate(2, 2);
  p->clipRect(Rect::wh(w, h));
  p->fillRect(Rect::wh(w, h), Color::WHITE);

  // labels
  p->setFillBrush(Color::BLACK);
  p->setFontSize(12);
  p->setTextAlign(Painter::AlignLeft | Painter::AlignBaseline);
  real labelw = 0;
  int nvert = 5;
  real dh = (maxAlt - minAlt)/nvert;
  for(int ii = 0; ii < nvert; ++ii)
    labelw = std::max(labelw, p->textBounds(0, 0, fstring("%.0f", minAlt + ii*dh).c_str()));
  p->setTextAlign(Painter::AlignRight | Painter::AlignVCenter);
  for(int ii = 0; ii < nvert; ++ii)
    p->drawText(labelw, h*(1-real(ii)/nvert), fstring("%.0f", minAlt + ii*dh).c_str());

  int plotw = w - (labelw + 10);
  int ploth = h - 15;
  plotWidth = plotw;
  p->setTextAlign(Painter::AlignHCenter | Painter::AlignBottom);
  if(plotVsDist) {
    real xMin = -zoomOffset/1000;
    real xMax = xMin + maxDist/1000/zoomScale;
    real dx = xMax - xMin;
    real anch = fewestSigDigits(xMin, xMax);
    real dw = dx/4;
    int prec = 0;
    while(dw > 10) { dw /= 10; ++prec; }
    while(dw < 1) { dw *= 10; --prec; }
    dw = (dw > 5 ? 5 : dw > 2 ? 2 : 1) * std::pow(10, prec);
    prec = std::max(0, -prec);
    // for n labels, find step size between 1/(n-1) and 1/n of dx
    //real dw = fewestSigDigits(dx/nhorz, dx/(nhorz - 1));
    //int prec = std::max(0, -int(std::floor(std::log10(dw))));
    //while(anch - dw > xMin) anch -= dw;
    real dxlbl = dw*plotw/dx;
    real anchlbl = (anch - xMin)*(plotw/dx) + labelw + 10;
    for(int ii = 0; anch + ii*dw < xMax; ++ii)
      p->drawText(anchlbl + ii*dxlbl, h, fstring("%.*f", prec, anch + ii*dw).c_str());
    // vert grid lines
    p->setFillBrush(Brush::NONE);
    p->setStroke(Color(0, 0, 0, 64), 0.75);
    for(int ii = 0; anchlbl + ii*dxlbl < w; ++ii)
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
      p->drawText(ii*plotw/nhorz + labelw + 10, h, fstring("%02.0f:%02.0f", hrs, mins).c_str());
    }
  }
  // horz grid lines
  for(int ii = 0; ii < nvert; ++ii)
    p->drawLine(Point(labelw, h*(1-real(ii)/nvert)), Point(w, h*(1-real(ii)/nvert)));

  // axes
  //drawCheckerboard(p, w, h, 4, 0x18000000);
  p->setStroke(Color::BLUE, 1.5);
  p->setFillBrush(Brush::NONE);
  p->drawLine(Point(labelw + 5, h-15), Point(labelw + 5, 0));
  p->drawLine(Point(labelw + 5, h-15), Point(w, h-15));

  // plot
  p->clipRect(Rect::ltrb(labelw + 6, 0, w, h-15));  // clip plot to axes
  p->save();
  p->translate(labelw + 10, 0);
  p->scale(plotVsDist ? plotw/maxDist : plotw/(maxTime - minTime), -ploth/(maxAlt - minAlt));

  p->translate(0, -maxAlt + minAlt);

  p->scale(zoomScale, 1);
  p->translate(zoomOffset, 0);
  if(plotAlt) {
    p->translate(0, -minAlt);
    p->setFillBrush(Color(0, 0, 255, 128));
    p->setStroke(Color::NONE);
    p->drawPath(plotVsDist ? altDistPlot : altTimePlot);
    p->translate(0, minAlt);
  }
  if(plotSpd) {
    p->setFillBrush(Brush::NONE);
    p->setStroke(Color::RED, 2.0);
    p->drawPath(plotVsDist ? spdDistPlot : spdTimePlot);
  }

  // draw markers for waypoints
  p->restore();
  p->setFontSize(11);
  p->setTextAlign(Painter::AlignLeft | Painter::AlignBaseline);
  real texty = 20;
  // lines
  p->setFillBrush(Brush::NONE);
  p->setStroke(Color::GREEN, 1.5);
  for(const Waypoint& wpt : waypoints) {
    real s = trackPosToPlotPos(plotVsDist ? wpt.dist/maxDist : (wpt.loc.time - minTime)/(maxTime - minTime));
    if(s < 0 || s > 1) continue;
    real x = s*plotw + (labelw + 10);
    p->drawLine(Point(x, 15), Point(x, h));
  }
  // text (we want text on top of all lines)
  p->setFillBrush(Color::BLACK);
  p->setStroke(Color::NONE);
  for(const Waypoint& wpt : waypoints) {
    real s = trackPosToPlotPos(plotVsDist ? wpt.dist/maxDist : (wpt.loc.time - minTime)/(maxTime - minTime));
    if(s < 0 || s > 1) continue;
    real x = s*plotw + (labelw + 10);
    if(texty >= h - 20) texty = 20;  // back to top
    //real textw = p->textBounds(x0, ploth - 20, wpt.name.c_str(), NULL);
    p->drawText(x + 4, texty, wpt.name.c_str());  // note clip rect is still set
    texty += 16;
  }
}

// TrackSparkline

void TrackSparkline::setTrack(const std::vector<Waypoint>& locs)
{
  altDistPlot.clear();
  if(locs.empty()) return;
  minAlt = FLT_MAX;
  maxAlt = -FLT_MAX;
  maxDist = locs.back().dist;
  altDistPlot.addPoint(locs.front().dist, -1000);
  for(auto& wpt : locs) {
    const Location& tpt = wpt.loc;
    altDistPlot.addPoint(Point(wpt.dist, MapsApp::metricUnits ? tpt.alt : tpt.alt*3.28084));
    minAlt = std::min(minAlt, tpt.alt);
    maxAlt = std::max(maxAlt, tpt.alt);
  }
  altDistPlot.addPoint(locs.back().dist, -1000);

  real elev = maxAlt - minAlt;
  minAlt -= 0.05*elev;
  maxAlt += 0.05*elev;
}

void TrackSparkline::draw(SvgPainter* svgp) const
{
  Painter* p = svgp->p;
  int w = mBounds.width() - 4;
  int h = mBounds.height() - 4;
  p->translate(2, 2);
  p->clipRect(Rect::wh(w, h));
  p->fillRect(Rect::wh(w, h), Color::WHITE);

  // plot
  int plotw = w;
  int ploth = h;
  p->clipRect(Rect::ltrb(0, 0, w, h));  // clip plot to axes
  p->scale(plotw/maxDist, -ploth/(maxAlt - minAlt));

  p->translate(0, -maxAlt);  // + minAlt);
  p->setFillBrush(Color(0, 0, 255, 128));
  p->setStroke(Color::NONE);
  p->drawPath(altDistPlot);
}

// TrackSliders

TrackSliders::TrackSliders(SvgNode* n) : Slider(n)
{
  startHandle = new Button(containerNode()->selectFirst(".start-handle"));
  endHandle = new Button(containerNode()->selectFirst(".end-handle"));
  //startHandle->setLayoutIsolate(true);
  //endHandle->setLayoutIsolate(true);

  startHandle->addHandler([this](SvgGui* gui, SDL_Event* event){
    if(event->type == SDL_FINGERMOTION && gui->pressedWidget == startHandle) {
      Rect rect = sliderBg->node->bounds();
      real startpos = (event->tfinger.x - rect.left)/rect.width();
      setCropHandles(startpos, std::max(startpos, endHandlePos), UPDATE);
      return true;
    }
    return false;
  });

  endHandle->addHandler([this](SvgGui* gui, SDL_Event* event){
    if(event->type == SDL_FINGERMOTION && gui->pressedWidget == endHandle) {
      Rect rect = sliderBg->node->bounds();
      real endpos = (event->tfinger.x - rect.left)/rect.width();
      setCropHandles(std::min(startHandlePos, endpos), endpos, UPDATE);
      return true;
    }
    return false;
  });

  auto sliderOnApplyLayout = onApplyLayout;
  onApplyLayout = [this, sliderOnApplyLayout](const Rect& src, const Rect& dest){
    sliderOnApplyLayout(src, dest);
    if(src.toSize() != dest.toSize()) {
      Rect rect = sliderBg->node->bounds();
      startHandle->setLayoutTransform(Transform2D().translate(rect.width()*startHandlePos, 0));
      endHandle->setLayoutTransform(Transform2D().translate(rect.width()*endHandlePos, 0));
    }
    return false;  // we do not replace the normal layout (although that should be a no-op)
  };
  setEditMode(false);
}

void TrackSliders::setCropHandles(real start, real end, int update)
{
  start = std::min(std::max(start, 0.0), 1.0);
  end = std::min(std::max(end, 0.0), 1.0);
  Rect rect = sliderBg->node->bounds();
  if(startHandlePos != start || update > 1) {
    startHandlePos = start;
    startHandle->setLayoutTransform(Transform2D().translate(rect.width()*startHandlePos, 0));
    if(update > 0 && onStartHandleChanged)
      onStartHandleChanged();
  }
  if(endHandlePos != end || update > 1) {
    endHandlePos = end;
    endHandle->setLayoutTransform(Transform2D().translate(rect.width()*endHandlePos, 0));
    if(update > 0 && onEndHandleChanged)
      onEndHandleChanged();
  }
}

void TrackSliders::setEditMode(bool editmode)
{
  selectFirst(".start-handle")->setVisible(editmode);
  selectFirst(".end-handle")->setVisible(editmode);
  selectFirst(".slider-handle")->setVisible(!editmode);
}

TrackSliders* createTrackSliders()
{
  static const char* slidersSVG = R"#(
    <g id="slider" class="slider" box-anchor="hfill" layout="box">
      <rect class="slider-bg background" box-anchor="hfill" fill="blue" width="200" height="4"/>
      <g class="slider-handle-container" box-anchor="left">
        <!-- invisible rect to set left edge of box so slider-handle can move freely -->
        <rect width="1" height="16" fill="none"/>
        <g class="start-handle">
          <rect class="slider-handle-outer" x="-6" y="-2" width="12" height="16"/>
          <rect fill="green" x="-4" y="0" width="8" height="12"/>
        </g>

        <g class="slider-handle">
          <rect class="slider-handle-outer" x="-6" y="-2" width="12" height="16"/>
          <rect class="slider-handle-inner" x="-4" y="0" width="8" height="12"/>
        </g>

        <g class="end-handle">
          <rect class="slider-handle-outer" x="-6" y="-2" width="12" height="16"/>
          <rect fill="red" x="-4" y="0" width="8" height="12"/>
        </g>
      </g>
    </g>
  )#";

  return new TrackSliders(loadSVGFragment(slidersSVG));
}
