#pragma once

#include "ugui/widgets.h"

class SelectDialog : public Dialog
{
public:
  SelectDialog(SvgDocument* n, const std::vector<std::string>& _items = {});
  void addItems(const std::vector<std::string>& _items, bool replace = true);
  std::function<void(int)> onSelected;

private:
  Widget* content;
};

class SelectBox : public Widget
{
public:
  SelectBox(SvgNode* boxnode, SelectDialog* _dialog, const std::vector<std::string>& _items = {});
  void setText(const char* s) { comboText->setText(s); }
  int index() const { return currIndex; }
  void setIndex(int idx);
  void updateIndex(int idx);
  void addItems(const std::vector<std::string>& _items, bool replace = true);

  std::function<void(int)> onChanged;

private:
  std::unique_ptr<SelectDialog> dialog;
  TextBox* comboText;
  int currIndex;

  std::vector<std::string> items;
};

SelectDialog* createSelectDialog(const char* title, const SvgNode* itemicon, const std::vector<std::string>& items = {});
SelectBox* createSelectBox(const char* title, const SvgNode* itemicon, const std::vector<std::string>& items);

Menu* createRadioMenu(std::vector<std::string> titles, std::function<void(size_t)> onChanged, size_t initial = 0);
Widget* createColorPicker(const std::vector<Color>& colors, Color initialColor, std::function<void(Color)> onColor);
