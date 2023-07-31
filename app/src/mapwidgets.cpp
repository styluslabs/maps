#include "mapwidgets.h"


Menu* createRadioMenu(std::vector<std::string> titles, std::function<void(size_t)> onChanged, size_t initial)
{
  Menu* menu = createMenu(Menu::VERT_RIGHT);
  for(size_t ii = 0; ii < titles.size(); ++ii) {
    Button* item = createCheckBoxMenuItem(titles[ii].c_str(), "#radiobutton");
    item->setChecked(ii == initial);
    menu->addItem(item);
    item->onClicked = [=](){
      for(Widget* btn : menu->select(".radiobutton"))
        static_cast<Button*>(btn)->setChecked(btn == item);
      onChanged(ii);
    };
  }
  return menu;
}

SelectDialog::SelectDialog(SvgDocument* n, const std::vector<std::string>& _items) : Dialog(setupWindowNode(n))
{
  content = createColumn();
  content->node->setAttribute("box-anchor", "hfill");  // vertical scrolling only
  Button* cancelBtn = new Button(containerNode()->selectFirst(".cancel-btn"));
  cancelBtn->onClicked = [=](){ finish(Dialog::CANCELLED); };
  Widget* dialogBody = selectFirst(".body-container");
  ScrollWidget* scrollWidget = new ScrollWidget(new SvgDocument(), content);
  scrollWidget->node->setAttribute("box-anchor", "fill");
  dialogBody->addWidget(scrollWidget);

  if(!_items.empty())
    addItems(_items, false);
}

void SelectDialog::addItems(const std::vector<std::string>& _items, bool replace)
{
  if(replace) {
    //window()->gui()->deleteContents(content); ... not available when not displayed!!!
    for(Widget* child : content->select("*")) {
      child->removeFromParent();
      delete child->node;
    }
  }

  SvgNode* proto = containerNode()->selectFirst(".listitem-proto");
  for(int ii = 0; ii < int(_items.size()); ++ii) {
    Button* btn = new Button(proto->clone());
    btn->setVisible(true);
    btn->onClicked = [=](){
      finish(Dialog::ACCEPTED);
      if(onSelected)
        onSelected(ii);
    };
    SvgText* titlenode = static_cast<SvgText*>(btn->containerNode()->selectFirst(".title-text"));
    titlenode->addText(_items[ii].c_str());
    content->addWidget(btn);
  }
}

SelectBox::SelectBox(SvgNode* boxnode, SelectDialog* _dialog, const std::vector<std::string>& _items)
    : Widget(boxnode), dialog(_dialog), currIndex(0)
{
  comboText = new TextBox(containerNode()->selectFirst(".combo_text"));
  Button* comboopen = new Button(containerNode()->selectFirst(".combo_content"));

  comboopen->onClicked = [this](){
    SvgGui* gui = window()->gui();
    Window* win = gui->windows.front();  //->modalOrSelf();
    Rect pbbox = win->winBounds();
    dialog->setWinBounds(Rect::centerwh(pbbox.center(), std::min(pbbox.width() - 20, real(500)), pbbox.height() - 60));
    gui->showModal(dialog.get(), win);
  };

  dialog->onSelected = [this](int idx){ updateIndex(idx); };

  addItems(_items);
}

void SelectBox::addItems(const std::vector<std::string>& _items, bool replace)
{
  if(replace) {
    items.clear();
    if(!_items.empty())
      setText(_items.front().c_str());
  }
  items.insert(items.end(), _items.begin(), _items.end());
  dialog->addItems(_items, replace);
}

void SelectBox::updateIndex(int idx)
{
  setIndex(idx);
  if(onChanged)
    onChanged(idx);
}

void SelectBox::setIndex(int idx)
{
  if(idx >= 0 && idx < int(items.size())) {
    const char* s = items[idx].c_str();
    setText(s);
    currIndex = idx;
  }
}

SelectDialog* createSelectDialog(const char* title, const SvgNode* itemicon, const std::vector<std::string>& items)
{
  static const char* dialogProtoSVG = R"#(
    <svg id="dialog" class="window dialog" layout="box">
      <rect class="dialog-bg background" box-anchor="fill" width="20" height="20"/>
      <g class="dialog-layout" box-anchor="fill" layout="flex" flex-direction="column">
        <g class="titlebar-container" box-anchor="hfill" layout="box">
          <g class="title-container" box-anchor="hfill" layout="flex" flex-direction="row" justify-content="center">
            <text class="dialog-title"></text>
          </g>
          <g class="button-container toolbar" box-anchor="left" layout="flex" flex-direction="row">
            <g class="toolbutton cancel-btn" layout="box">
              <rect class="background" box-anchor="hfill" width="36" height="42"/>
              <g margin="0 3" box-anchor="fill" layout="flex" flex-direction="row">
                <use class="icon" height="36" xlink:href=":/icons/ic_menu_back.svg" />
                <text class="title" display="none" margin="0 9">Cancel</text>
              </g>
            </g>
          </g>
        </g>
        <rect class="hrule title" box-anchor="hfill" width="20" height="2"/>
        <g class="body-container" box-anchor="fill" layout="box"></g>
      </g>

      <g class="listitem-proto listitem" display="none" margin="0 5" layout="box" box-anchor="hfill">
        <rect box-anchor="fill" width="48" height="48"/>
        <g layout="flex" flex-direction="row" box-anchor="left">
          <g class="image-container" margin="2 5">
            <use class="listitem-icon" width="36" height="36" xlink:href=""/>
          </g>
          <g layout="box" box-anchor="vfill">
            <text class="title-text" box-anchor="left" margin="0 10"></text>
          </g>
        </g>
      </g>
    </svg>
  )#";
  static std::unique_ptr<SvgDocument> dialogProto;
  if(!dialogProto)
    dialogProto.reset(static_cast<SvgDocument*>(loadSVGFragment(dialogProtoSVG)));

  SvgDocument* dialog = dialogProto->clone();
  static_cast<SvgText*>(dialog->selectFirst(".dialog-title"))->addText(title);
  static_cast<SvgUse*>(dialog->selectFirst(".listitem-icon"))->setTarget(itemicon);
  return new SelectDialog(dialog, items);
}

SelectBox* createSelectBox(const char* title, const SvgNode* itemicon, const std::vector<std::string>& items)
{
  static const char* boxProtoSVG = R"#(
    <g class="inputbox combobox" layout="box" box-anchor="left" margin="0 10">
      <rect class="min-width-rect" width="150" height="36" fill="none"/>
      <rect class="inputbox-bg" box-anchor="fill" width="150" height="36"/>

      <g class="combo_content toolbutton" box-anchor="fill" layout="flex" flex-direction="row" margin="0 2">
        <g class="textbox combo_text" box-anchor="fill" layout="box">
          <rect box-anchor="fill" width="28" height="28"/>
          <text box-anchor="left" margin="3 6"></text>
        </g>
        <g class="combo_open" box-anchor="vfill" layout="box">
          <rect box-anchor="vfill" width="28" height="28"/>
          <use class="icon" width="28" height="28" xlink:href=":/icons/chevron_down.svg" />
        </g>
      </g>
    </g>
  )#";
  static std::unique_ptr<SvgNode> boxProto;
  if(!boxProto)
    boxProto.reset(loadSVGFragment(boxProtoSVG));

  SelectBox* widget = new SelectBox(boxProto->clone(), createSelectDialog(title, itemicon), items);
  //widget->isFocusable = true;
  return widget;
}

Button* createColorPicker(const std::vector<Color>& colors, Color initialColor, std::function<void(Color)> onColor)
{
  static const char* menuSVG = R"#(
    <g class="menu" display="none" position="absolute" box-anchor="fill" layout="box">
      <rect box-anchor="fill" width="20" height="20"/>
      <g class="child-container" box-anchor="fill"
          layout="flex" flex-direction="row" flex-wrap="wrap" justify-content="flex-start" margin="6 6">
      </g>
    </g>
  )#";
  static std::unique_ptr<SvgNode> menuNode;
  if(!menuNode)
    menuNode.reset(loadSVGFragment(menuSVG));

  static const char* colorBtnSVG = R"#(
    <g class="color_preview previewbtn">
      <pattern id="checkerboard" x="0" y="0" width="18" height="18"
          patternUnits="userSpaceOnUse" patternContentUnits="userSpaceOnUse">
        <rect fill="black" fill-opacity="0.1" x="0" y="0" width="9" height="9"/>
        <rect fill="black" fill-opacity="0.1" x="9" y="9" width="9" height="9"/>
      </pattern>

      <rect fill="white" x="1" y="1" width="35" height="35" />
      <rect fill="url(#checkerboard)" x="1" y="1" width="35" height="35" />
      <rect class="current-color" stroke="currentColor" stroke-width="2" fill="blue" x="1" y="1" width="35" height="35" />
    </g>
  )#";
  static std::unique_ptr<SvgNode> colorBtnNode;
  if(!colorBtnNode)
    colorBtnNode.reset(loadSVGFragment(colorBtnSVG));

  Menu* menu = new Menu(menuNode->clone(), Menu::VERT_LEFT);
  Button* widget = new Button(colorBtnNode->clone());
  widget->selectFirst(".current-color")->node->setAttr<color_t>("fill", initialColor.color);
  widget->setMenu(menu);

  for(size_t ii = 0; ii < colors.size(); ++ii) {
    Color color = colors[ii];
    Button* btn = new Button(colorBtnNode->clone());
    btn->selectFirst(".current-color")->node->setAttr<color_t>("fill", color.color);
    if(ii > 0 && ii % 4 == 0)
      btn->node->setAttribute("flex-break", "before");
    btn->onClicked = [=](){
      widget->selectFirst(".current-color")->node->setAttr<color_t>("fill", color.color);
      onColor(color);
    };
    menu->addItem(btn);  //addWidget(btn);  // use addItem() to support press-drag-release?
  }
  return widget;
}

DragDropList::DragDropList(Widget* _content) : Widget(new SvgG)
{
  node->setAttribute("box-anchor", "fill");
  node->setAttribute("layout", "box");
  content = _content ? _content->selectFirst(".list-container") : createColumn();
  Widget* scroll_content = _content ? _content : content;
  scrollWidget = new ScrollWidget(new SvgDocument(), scroll_content);
  scroll_content->node->setAttribute("box-anchor", "hfill");  // vertical scrolling only
  scrollWidget->node->setAttribute("box-anchor", "fill");
  addWidget(scrollWidget);
}

void DragDropList::setOrder(const std::vector<KeyType>& order)
{
  // if we want items not in order to be placed at end, iterate both lists in reverse and use push_front
  //std::unordered_map<KeyType, SvgNode*> items;
  auto& items = content->containerNode()->children();
  for(auto& key : order) {
    for(auto it = items.begin(); it != items.end(); ++it) {
      SvgNode* node = *it;
      if(node->getStringAttr("__sortkey") == key) {
        items.erase(it);
        items.push_back(node);
        break;
      }
    }
  }
}

std::vector<DragDropList::KeyType> DragDropList::getOrder()
{
  std::vector<KeyType> order;
  for(SvgNode* node : content->containerNode()->children()) {
    order.push_back(node->getStringAttr("__sortkey"));
  }
  return order;
}

void DragDropList::clear()
{
  window()->gui()->deleteContents(content);
}

void DragDropList::deleteItem(KeyType key)
{
  for(SvgNode* node : content->containerNode()->children()) {
    if(node->getStringAttr("__sortkey") == key) {
      window()->gui()->deleteWidget(static_cast<Widget*>(node->ext()));
      return;
    }
  }
}

void DragDropList::addItem(KeyType key, Widget* item)
{
  content->addWidget(item);
  item->node->setAttr("__sortkey", key.c_str());

  Button* dragBtn = new Button(item->containerNode()->selectFirst(".drag-btn"));
  dragBtn->node->addClass("draggable");
  dragBtn->addHandler([=](SvgGui* gui, SDL_Event* event){
    if(event->type == SDL_FINGERDOWN && event->tfinger.fingerId == SDL_BUTTON_LMASK)
      gui->setTimer(50, dragBtn);
    else if(event->type == SvgGui::TIMER) {
      if(gui->pressedWidget != dragBtn)
        return false;  // stop timer
      Rect b = dragBtn->node->bounds();
      Rect scrollb = scrollWidget->node->bounds();
      if(gui->prevFingerPos.y < scrollb.top + b.height())
        scrollWidget->scroll(Point(0, -b.height()/5));
      else if(gui->prevFingerPos.y > scrollb.bottom - b.height())
        scrollWidget->scroll(Point(0, b.height()/5));
      return true;  // continue running timer
    }
    else if(event->type == SDL_FINGERMOTION && gui->pressedWidget == dragBtn) {
      // if finger > height above or below center, shift position
      Rect b = dragBtn->node->bounds();
      real dy = event->tfinger.y - b.center().y;
      if(std::abs(dy) > b.height()) {
        SvgContainerNode* parent = content->containerNode();
        auto& items = parent->children();
        auto it = std::find(items.begin(), items.end(), item->node);
        if(it == items.end() || (dy < 0 && it == items.begin()) || (dy > 0 && ++it == items.end()))
          return true;
        SvgNode* next = dy > 0 ? (++it == items.end() ? NULL : *it) : *(--it);
        parent->removeChild(item->node);
        parent->addChild(item->node, next);
      }
      return true;
    }
    return false;
  });
}

