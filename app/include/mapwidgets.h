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
  Widget* getItem(KeyType key);
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

class SharedMenu : public Menu
{
public:
  SharedMenu(SvgNode* n, int align);
  void show(Widget* _host);
  Point calcOffset(const Rect& pb) const override { return Menu::calcOffset(host ? host->node->bounds() : pb); }

  Widget* host = NULL;
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

class Pager : public Widget
{
public:
  Pager(SvgNode* n);

  Widget* currPage = NULL;
  Widget* nextPage = NULL;
  real xoffset = 0;
  Rect initialBounds;
  uint32_t initialTime;
  Point initialPos;
  bool behaveAsStack = false; // stack (next page slides over prev) or reel (prev slides out, next slides in)?

  std::function<void(Widget*)> onPageChanged;
  std::function<void(bool)> getNextPage;  // this should set nextPage (and currPage)
};

class ColorEditBox;

class ManageColorsDialog : public Dialog
{
public:
  ManageColorsDialog(std::vector<Color>& _colors);
  void setColor(Color initialColor);

  std::function<void(Color)> onColorAccepted;
  std::function<void()> onColorListChanged;

private:
  std::vector<Color>& colors;

  Widget* colorList;
  ColorEditBox* colorEditBox;
  Button* saveBtn;
  Button* deleteBtn;

  void setSaveDelState();
  void populateColorList();
};

class CrosshairWidget : public Widget
{
public:
  CrosshairWidget() : Widget(new SvgCustomNode) {}
  void draw(SvgPainter* svgp) const override {}
  Rect bounds(SvgPainter* svgp) const override;
  void directDraw(Painter* p) const;

  Point routePreviewOrigin;
};

SvgNode* uiIcon(const char* id);
SelectDialog* createSelectDialog(const char* title, const SvgNode* itemicon, const std::vector<std::string>& items = {});
SelectBox* createSelectBox(const char* title, const SvgNode* itemicon, const std::vector<std::string>& items);
Menu* createRadioMenu(std::vector<std::string> titles, std::function<void(size_t)> onChanged, size_t initial = 0);
Menubar* createMenubar();
Button* createActionbutton(const SvgNode* icon, const char* title, bool showTitle = false);
ColorPicker* createColorPicker(SharedMenu* menu, Color initialColor);
Button* createListItem(SvgNode* icon, const char* title, const char* note = NULL);
TextEdit* createTitledTextEdit(const char* title, const char* text = NULL);
Widget* createInlineDialog(std::initializer_list<Widget*> widgets,
    const char* acceptLabel, std::function<void()> onAccept, std::function<void()> onCancel = {});
void showInlineDialogModal(Widget* dialog);
void showModalCentered(Window* modal, SvgGui* gui);
void sendKeyPress(SvgGui* gui, Widget* widget, int sdlkey, int mods = 0);
Widget* createDatePicker(int year0, int month0, int day0, std::function<void(int year, int month, int day)> onChange);
Dialog* createMobileDialog(const char* title, const char* acceptTitle, Widget* content = NULL);
Dialog* createInputDialog(std::initializer_list<Widget*> widgets, const char* title,
    const char* acceptLabel, std::function<void()> onAccept, std::function<void()> onCancel = {});
class ScrollWidget;
ScrollWidget* createScrollWidget(Widget* contents, real minHeight = 120, real maxHeight = -160);
