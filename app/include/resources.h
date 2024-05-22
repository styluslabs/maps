#pragma once

class SvgCssStylesheet;

void initResources(const char* baseDir);
SvgCssStylesheet* createStylesheet(bool light);  //const char* darkSel, const char* lightSel);
