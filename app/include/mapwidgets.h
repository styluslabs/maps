#pragma once

#include "ugui/widgets.h"
#include "ugui/textedit.h"

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

class DragDropList : public Widget
{
public:
  using KeyType = std::string;

  DragDropList(Widget* _content = NULL);
  void addItem(KeyType key, Widget* item, KeyType nextkey = {});
  void deleteItem(KeyType key);
  void clear();

  void setOrder(const std::vector<KeyType>& order);
  std::vector<KeyType> getOrder();

  std::function<void(std::string, std::string)> onReorder;

  Widget* content = NULL;
  ScrollWidget* scrollWidget = NULL;
  AbsPosWidget* floatWidget = NULL;
  Widget* placeholder = NULL;
  real yOffset = 0;
  std::string nextItemKey;
};

class Menubar : public Toolbar
{
public:
  Menubar(SvgNode* n);
  void addButton(Button* btn);
  Button* addAction(Action* action);

  bool autoClose = false;
};

class ColorPicker : public Button
{
public:
  using Button::Button;
  Color color() const;
  void setColor(Color c);
  void updateColor(Color c) { setColor(c);  if(onColor) { onColor(c); } }
  std::function<void(Color)> onColor;
};

SelectDialog* createSelectDialog(const char* title, const SvgNode* itemicon, const std::vector<std::string>& items = {});
SelectBox* createSelectBox(const char* title, const SvgNode* itemicon, const std::vector<std::string>& items);
Menu* createRadioMenu(std::vector<std::string> titles, std::function<void(size_t)> onChanged, size_t initial = 0);
Menubar* createMenubar();
ColorPicker* createColorPicker(const std::vector<Color>& colors, Color initialColor);
Button* createListItem(SvgNode* icon, const char* title, const char* note = NULL);
TextEdit* createTitledTextEdit(const char* title, const char* text = NULL);
Widget* createInlineDialog(std::initializer_list<Widget*> widgets,
    const char* acceptLabel, std::function<void()> onAccept, std::function<void()> onCancel = {});
void showInlineDialogModal(Widget* dialog);
void showModalCentered(Window* modal, SvgGui* gui);
