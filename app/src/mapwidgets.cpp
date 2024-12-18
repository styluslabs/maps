#include "mapwidgets.h"
#include "usvg/svgpainter.h"


SvgNode* uiIcon(const char* id)
{
  const SvgDocument* uiIcons = SvgGui::useFile(":/ui-icons.svg");
  SvgNode* res = uiIcons ? uiIcons->namedNode(id) : NULL;
  if(!res)
    PLATFORM_LOG("Icon missing: %s\n", id);  //ASSERT(res && "UI icon missing!");
  return res;
}

Menu* createRadioMenu(std::vector<std::string> titles, std::function<void(size_t)> onChanged, size_t initial)
{
  Menu* menu = createMenu(Menu::VERT_RIGHT);
  for(size_t ii = 0; ii < titles.size(); ++ii) {
    Button* item = createCheckBoxMenuItem(titles[ii].c_str(), "#radiobutton");
    item->setChecked(ii == initial);
    menu->addItem(item);
    item->onClicked = [=](){
      for(Widget* btn : menu->select(".cbmenuitem"))
        static_cast<Button*>(btn)->setChecked(btn == item);
      onChanged(ii);
    };
  }
  return menu;
}

Button* createActionbutton(const SvgNode* icon, const char* title, bool showTitle)
{
  Button* widget = new Button(widgetNode("#actionbutton"));
  widget->setIcon(icon);
  widget->setTitle(title);
  widget->setShowTitle(showTitle);
  return widget;
}

ScrollWidget* createScrollWidget(Widget* contents, real minHeight, real maxHeight)  //real minw, real maxw
{
  auto scrollWidget = new ScrollWidget(new SvgDocument(), contents);
  scrollWidget->node->setAttribute("box-anchor", "fill");
  if(minHeight == 0 && maxHeight == 0) return scrollWidget;

  scrollWidget->node->addClass("scrollwidget-var-height");
  auto oldPrepLayout = scrollWidget->onPrepareLayout;
  scrollWidget->onPrepareLayout = [=](){
    if((scrollWidget->layBehave & LAY_VFILL) == LAY_VFILL) return oldPrepLayout();
    // set height to percent units to force relayout
    static_cast<SvgDocument*>(scrollWidget->node)->setHeight(SvgLength(100, SvgLength::PERCENT));
    Rect bbox = oldPrepLayout();
    real minh = minHeight < 0 ? scrollWidget->window()->winBounds().height() + minHeight : minHeight;
    real maxh = maxHeight < 0 ? scrollWidget->window()->winBounds().height() + maxHeight : maxHeight;
    return Rect::wh(bbox.width(), bbox.height() > 0 ? std::min(std::max(bbox.height(), minh), maxh) : 0);
  };
  return scrollWidget;
}

static SvgDocument* createMobileDialogNode()
{
  return setupWindowNode(static_cast<SvgDocument*>(widgetNode("#mobile-dialog")));
}

static Dialog* setupMobileDialog(Dialog* dialog, const char* title, const char* acceptTitle, Widget* content = NULL)
{
  TextBox* titleText = new TextLabel(createTextNode(title));
  titleText->node->addClass("dialog-title");
  titleText->node->setAttribute("box-anchor", "hfill");
  Toolbar* titleTb = createToolbar();
  titleTb->node->addClass("title-toolbar invert-theme");
  dialog->cancelBtn = createToolbutton(uiIcon("cancel"), "Cancel");
  dialog->cancelBtn->onClicked = [=](){ dialog->finish(Dialog::CANCELLED); };
  titleTb->addWidget(dialog->cancelBtn);
  titleTb->addWidget(titleText);
  //titleTb->addWidget(createStretch());
  if(acceptTitle) {
    dialog->acceptBtn = createToolbutton(uiIcon("accept"), acceptTitle, true);
    dialog->acceptBtn->node->addClass("accept-btn");
    dialog->acceptBtn->onClicked = [=](){ dialog->finish(Dialog::ACCEPTED); };
    titleTb->addWidget(dialog->acceptBtn);
  }
  dialog->selectFirst(".title-container")->addWidget(titleTb);
  if(content)
    dialog->selectFirst(".body-container")->addWidget(content);
  return dialog;
}

// dialog with accept (optional) and cancel controls along top of dialog
Dialog* createMobileDialog(const char* title, const char* acceptTitle, Widget* content)
{
  return setupMobileDialog(new Dialog(createMobileDialogNode()), title, acceptTitle, content);
}

SelectDialog::SelectDialog(SvgDocument* n, const std::vector<std::string>& _items) : Dialog(setupWindowNode(n))
{
  content = createColumn();
  content->node->setAttribute("box-anchor", "hfill");  // vertical scrolling only
  Button* cancelBtn = new Button(containerNode()->selectFirst(".cancel-btn"));
  cancelBtn->onClicked = [=](){ finish(Dialog::CANCELLED); };
  Widget* dialogBody = selectFirst(".body-container");
  ScrollWidget* scrollWidget = createScrollWidget(content, 0, 0);
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

  int offset = int(content->containerNode()->children().size());
  SvgNode* proto = containerNode()->selectFirst(".listitem-proto");
  for(int ii = 0; ii < int(_items.size()); ++ii) {
    Button* btn = new Button(proto->clone());
    btn->setVisible(true);
    btn->onClicked = [=](){
      finish(Dialog::ACCEPTED);
      if(onSelected)
        onSelected(offset + ii);
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
                <use class="icon" height="36" width="36" xlink:href=":/ui-icons.svg#back" />
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

SharedMenu::SharedMenu(SvgNode* n, int align) : Menu(n, align)
{
  addHandler([=](SvgGui* gui, SDL_Event* event){
    if(!host) {}
    else if(event->type == SvgGui::VISIBLE)
      host->node->addClass("pressed");
    else if(event->type == SvgGui::INVISIBLE)
      host->node->removeClass("pressed");
    return false;
  });
}

Color ColorPicker::color() const
{
  return selectFirst(".current-color")->node->getColorAttr("fill");
}

void ColorPicker::setColor(Color c)
{
  selectFirst(".current-color")->node->setAttr<color_t>("fill", c.color);
}

ColorPicker* createColorPicker(SharedMenu* menu, Color initialColor) //const std::vector<Color>& colors, Color initialColor)
{
  ColorPicker* widget = new ColorPicker(widgetNode("#colorbutton"));
  widget->containerNode()->selectFirst(".btn-color")->addClass("current-color");
  widget->setColor(initialColor.color);
  widget->onPressed = [=](){ menu->show(widget); };
  return widget;
}

DragDropList::DragDropList(Widget* _content) : Widget(new SvgG)
{
  node->setAttribute("box-anchor", "fill");
  node->setAttribute("layout", "box");
  content = _content ? _content->selectFirst(".list-container") : createColumn();
  Widget* scroll_content = _content ? _content : content;
  scrollWidget = createScrollWidget(scroll_content);
  scroll_content->node->setAttribute("box-anchor", "hfill");  // vertical scrolling only
  addWidget(scrollWidget);

  SvgNode* fnode = loadSVGFragment(R"#(<g display="none" class="floating" position="absolute"></g>)#");
  floatWidget = new AbsPosWidget(fnode);
  addWidget(floatWidget);
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
  if(placeholder) {
    // cancel drag in progress!
    placeholder->removeFromParent();
    delete placeholder->node;
    placeholder = NULL;
    floatWidget->setVisible(false);
  }
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

Widget* DragDropList::getItem(KeyType key)
{
  for(SvgNode* node : content->containerNode()->children()) {
    if(node->getStringAttr("__sortkey") == key)
      return static_cast<Widget*>(node->ext(false));
  }
  return NULL;
}

void DragDropList::addItem(KeyType key, Widget* item, KeyType nextkey)
{
  const auto& children = content->containerNode()->children();
  auto it = children.begin();
  if(!nextkey.empty())
    while(it != children.end() && (*it)->getStringAttr("__sortkey") != nextkey) { ++it;}
  content->containerNode()->addChild(item->node, nextkey.empty() || it == children.end() ? NULL : *it);
  //content->addWidget(item);
  item->node->setAttr("__sortkey", key.c_str());

  SvgNode* dragInd = item->containerNode()->selectFirst(".drag-indicator");
  if(dragInd)
    dragInd->setDisplayMode(SvgNode::BlockMode);
  Button* dragBtn = new Button(item->containerNode()->selectFirst(".drag-btn"));
  dragBtn->node->addClass("draggable");
  dragBtn->addHandler([=](SvgGui* gui, SDL_Event* event){
    if(event->type == SDL_FINGERDOWN && event->tfinger.fingerId == SDL_BUTTON_LMASK) {
      yOffset = event->tfinger.y - item->node->bounds().top;
    }
    else if(event->type == SvgGui::TIMER) {
      if(gui->pressedWidget != dragBtn)
        return false;  // stop timer
      Rect b = item->node->bounds();
      Rect scrollb = scrollWidget->node->bounds();
      if(gui->prevFingerPos.y < scrollb.top + b.height())
        scrollWidget->scroll(Point(0, b.height()/5));
      else if(gui->prevFingerPos.y > scrollb.bottom - b.height())
        scrollWidget->scroll(Point(0, -b.height()/5));
      return true;  // continue running timer
    }
    else if(event->type == SDL_FINGERMOTION && gui->pressedWidget == dragBtn) {
      Rect b = placeholder ? placeholder->node->bounds() : item->node->bounds();
      floatWidget->node->setAttribute("left", "0");
      floatWidget->node->setAttribute("top", fstring("%g", event->tfinger.y - node->bounds().top - yOffset).c_str());
      if(!placeholder) {
        SvgRect* rnode = new SvgRect(item->node->bounds().toSize());
        rnode->setAttribute("fill", "none");
        placeholder = new Widget(rnode);
        content->containerNode()->addChild(rnode, item->node);
        SvgNode* next = content->containerNode()->removeChild(item->node);
        nextItemKey = next ? next->getStringAttr("__sortkey", "") : "";
        floatWidget->addWidget(item);
        floatWidget->setVisible(true);
        gui->setTimer(50, dragBtn);
      }
      else if(placeholder->node->m_dirty == SvgNode::BOUNDS_DIRTY)
        return true;  // once placeholder has been moved, have to wait until layout is recalculated

      // if finger > height above or below center, shift position
      real dy = event->tfinger.y - (b.top + yOffset);  //+ b.height()/2 - b.center().y;
      if(std::abs(dy) > b.height()/2) {
        SvgContainerNode* parent = content->containerNode();
        auto& items = parent->children();
        auto it = std::find(items.begin(), items.end(), placeholder->node);
        if(it == items.end() || (dy < 0 && it == items.begin()) || (dy > 0 && ++it == items.end()))
          return true;
        SvgNode* next = dy > 0 ? (++it == items.end() ? NULL : *it) : *(--it);
        parent->removeChild(placeholder->node);
        parent->addChild(placeholder->node, next);
      }
      return true;
    }
    else if(event->type == SDL_FINGERUP || event->type == SvgGui::OUTSIDE_PRESSED) {
      if(placeholder) {
        gui->removeTimer(dragBtn);
        item->removeFromParent();
        content->containerNode()->addChild(item->node, placeholder->node);
        SvgNode* next = content->containerNode()->removeChild(placeholder->node);
        delete placeholder->node;
        placeholder = NULL;
        floatWidget->setVisible(false);
        std::string newnextkey = next ? next->getStringAttr("__sortkey", "") : "";
        if(onReorder && newnextkey != nextItemKey)
          onReorder(item->node->getStringAttr("__sortkey", ""), newnextkey);
      }
      //return true;
    }
    return false;
  });
}

// copied from syncscribble/touchwidgets.cpp


// most modern applications (at least on mobile) won't have any menubars, so complicating Button class to
//  support menubar doesn't seem right
Menubar::Menubar(SvgNode* n) : Toolbar(n)
{
  // same logic as Menu except we use `this` instead of `parent()` - any way to deduplicate?
  addHandler([this](SvgGui* gui, SDL_Event* event){
    if(event->type == SvgGui::OUTSIDE_PRESSED) {
      // close unless button up over parent (assumed to include opening button)
      Widget* target = static_cast<Widget*>(event->user.data2);
      if(!target || !target->isDescendantOf(this))
        gui->closeMenus();  // close entire menu tree
      return true;
    }
    if(event->type == SvgGui::OUTSIDE_MODAL) {
      Widget* target = static_cast<Widget*>(event->user.data2);
      gui->closeMenus();  // close entire menu tree
      // swallow event (i.e. return true) if click was within menu's parent to prevent reopening
      return target && target->isDescendantOf(this);
    }
    return false;
  });

  isPressedGroupContainer = true;
}

void Menubar::addButton(Button* btn)
{
  // this will run before Button's handler
  btn->addHandler([btn, this](SvgGui* gui, SDL_Event* event){
    if(event->type == SvgGui::ENTER) {
      // if our menu is open, enter event can't close it, and we have class=pressed, so don't add hovered
      if(!btn->mMenu || !btn->mMenu->isVisible()) {
        bool isPressed = gui->pressedWidget != NULL;
        // if a menu is open, we won't be sent enter event unless we are in same pressed group container
        bool sameMenuTree = !gui->menuStack.empty();
        gui->closeMenus(btn);  // close sibling menu if any
        // note that we only receive pressed enter event if button went down in our group
        if(btn->mMenu && (isPressed || sameMenuTree)) {
          btn->node->addClass("pressed");
          gui->showMenu(btn->mMenu);
        }
        else
          btn->node->addClass(isPressed ? "pressed" : "hovered");
      }
      return true;
    }
    else if(event->type == SDL_FINGERDOWN && event->tfinger.fingerId == SDL_BUTTON_LMASK) {
      // close menu bar menu on 2nd click
      if(btn->mMenu && !gui->menuStack.empty() && btn->mMenu == gui->menuStack.front()) {
        btn->node->removeClass("hovered");
        gui->closeMenus();
        return true;  // I don't think it makes sense to send onPressed when we are clearing pressed state!
      }
    }
    else if(event->type == SDL_FINGERUP && (!btn->mMenu || autoClose || btn->mMenu->autoClose)) {
      gui->closeMenus();
      return false;  // continue to button handler
    }
    else if(isLongPressOrRightClick(event) && btn->mMenu) {
      if(!btn->mMenu->isVisible()) {
        btn->node->addClass("pressed");
        gui->showMenu(btn->mMenu);
      }
      gui->pressedWidget = NULL;  //setPressed(btn->mMenu) doesn't work as menubar is pressed group container
      return true;
    }
    return false;
  });

  addWidget(btn);
}

// would be nice to deduplicate this cut and paste from Toolbar::addAction() (w/o using virtual!)
Button* Menubar::addAction(Action* action)
{
  Button* item = createToolbutton(action->icon(), action->title.c_str());
  action->addButton(item);
  addButton(item);
  // handler added in addButton() stops propagation of ENTER event, so tooltip handler must preceed it
  setupTooltip(item, action->tooltip.empty() ? action->title.c_str() : action->tooltip.c_str());
  return item;
}

Menubar* createMenubar() { return new Menubar(widgetNode("#toolbar")); }
//Menubar* createVertMenubar() { return new Menubar(widgetNode("#vert-toolbar")); }

void SharedMenu::show(Widget* _host)
{
  SvgGui* gui = window()->gui();
  if(gui->lastClosedMenu == this && host == _host)
    return;
  if(window() != _host->window()) {
    removeFromParent();
    _host->window()->addWidget(this);
  }
  host = _host;
  gui->showMenu(this);
  gui->setPressed(this);
}

Button* createListItem(SvgNode* icon, const char* title, const char* note)
{
  // previously used margin="0 5", but I think any margin should be on container
  static const char* listItemProtoSVG = R"(
    <g class="listitem" margin="0 0" layout="box" box-anchor="hfill">
      <rect class="listitem-bg" box-anchor="hfill" width="48" height="42"/>
      <use class="drag-indicator icon" display="none" box-anchor="left" opacity="0.7" width="8" height="32" xlink:href=":/ui-icons.svg#drag-indicator"/>
      <g class="child-container" layout="flex" flex-direction="row" box-anchor="fill">
        <g class="toolbutton drag-btn" margin="2 3 2 7">
          <use class="listitem-icon icon" width="30" height="30" xlink:href=""/>
        </g>
        <g layout="box" box-anchor="fill">
          <text class="title-text" box-anchor="hfill" margin="0 10 0 10"></text>
          <text class="detail-text weak" box-anchor="hfill bottom" margin="0 10 2 10" font-size="12"></text>
        </g>
      </g>
      <rect class="listitem-separator separator" margin="0 0 0 48" box-anchor="bottom hfill" width="20" height="1"/>
    </g>
  )";
  static std::unique_ptr<SvgNode> listItemProto;
  if(!listItemProto)
    listItemProto.reset(loadSVGFragment(listItemProtoSVG));

  Button* item = new Button(listItemProto->clone());
  static_cast<SvgUse*>(item->containerNode()->selectFirst(".listitem-icon"))->setTarget(icon);
  TextLabel* titleLabel = new TextLabel(item->containerNode()->selectFirst(".title-text"));
  titleLabel->setText(title);
  TextLabel* noteLabel = new TextLabel(item->containerNode()->selectFirst(".detail-text"));
  if(note && note[0]) {
    titleLabel->node->setAttribute("margin", "0 10 6 10");
    noteLabel->setText(note);
  }
  return item;
}

TextEdit* createTitledTextEdit(const char* title, const char* text)
{
  static const char* titledTextEditSVG = R"#(
    <g class="inputbox textbox" box-anchor="hfill" layout="flex" flex-direction="column">
      <text class="inputbox-title" font-size="14" box-anchor="left" margin="2 2"></text>
    </g>
  )#";
  /*static const char* titledTextEditSVG = R"#(
    <g id="textedit" class="inputbox textbox" layout="box">
      <!-- invisible rect to set minimum width -->
      <rect class="min-width-rect" width="150" height="46" fill="none"/>
      <rect class="inputbox-bg" box-anchor="fill" margin="10 0 0 0" width="20" height="20"/>

      <g box-anchor="top left" margin="0 0 0 8" layout="box">
        <rect class="inputbox-title-bg" box-anchor="fill" width="20" height="20"/>
        <text class="inputbox-title" font-size="14" margin="2 5"></text>
      </g>

      <g class="textbox-wrapper" box-anchor="fill" layout="box" margin="10 0 0 0">
      </g>
    </g>
  )#";*/
  static std::unique_ptr<SvgNode> proto;
  if(!proto)
    proto.reset(loadSVGFragment(titledTextEditSVG));

  SvgNode* titledTextEditNode = proto->clone();
  SvgNode* textEditNode = widgetNode("#textedit");
  textEditNode->setXmlClass("textedit-wrapper");
  textEditNode->setAttribute("box-anchor", "fill");
  textEditNode->asContainerNode()->addChild(textEditInnerNode());
  titledTextEditNode->asContainerNode()->addChild(textEditNode);
  TextEdit* widget = new TextEdit(titledTextEditNode);
  widget->selectFirst(".inputbox-title")->setText(title);
  setupFocusable(widget);

  //TextEdit* widget = createTextEdit();
  //widget->node->setAttribute("box-anchor", "hfill");
  //widget->setEmptyText(title);
  if(text)
    widget->setText(text);
  return widget;
}

Widget* createInlineDialog(std::initializer_list<Widget*> widgets,
    const char* acceptLabel, std::function<void()> onAccept, std::function<void()> onCancel)
{
  static const char* inlineDialogSVG = R"#(
    <g class="inline-dialog col-layout" box-anchor="hfill" layout="flex" flex-direction="column" margin="2 5">
      <g class="button-container toolbar" margin="0 0" box-anchor="hfill" layout="flex" flex-direction="row"></g>
      <g class="child-container" box-anchor="hfill" layout="flex" flex-direction="column"></g>
    </g>
  )#";
  static std::unique_ptr<SvgNode> proto;
  if(!proto)
    proto.reset(loadSVGFragment(inlineDialogSVG));

  Widget* dialog = new Widget(proto->clone());
  Widget* container = dialog->selectFirst(".child-container");
  for(Widget* child : widgets)
    container->addWidget(child);
  Widget* btns = dialog->selectFirst(".button-container");
  Button* acceptBtn = createToolbutton(uiIcon("accept"), acceptLabel, true);
  Button* cancelBtn = createToolbutton(uiIcon("cancel"), "Cancel", true);
  //Button* acceptBtn = createPushbutton(acceptLabel);
  //Button* cancelBtn = createPushbutton("Cancel");
  acceptBtn->isFocusable = false;  // make this the default for pushbuttons?
  cancelBtn->isFocusable = false;
  acceptBtn->onClicked = [=](){ dialog->setVisible(false); onAccept(); };
  cancelBtn->onClicked = [=](){ dialog->setVisible(false); if(onCancel) onCancel(); };
  acceptBtn->node->addClass("accept-btn");
  btns->addWidget(cancelBtn);
  btns->addWidget(createStretch());
  btns->addWidget(acceptBtn);
  dialog->setVisible(false);

  dialog->addHandler([=](SvgGui* gui, SDL_Event* event){
    if(event->type == SDL_KEYDOWN) {
      if(event->key.keysym.sym == SDLK_ESCAPE)
        cancelBtn->onClicked();
      else if(event->key.keysym.sym == SDLK_RETURN)
        acceptBtn->onClicked();
      else if(event->key.keysym.sym == SDLK_TAB) {
        Widget* w = SvgGui::findNextFocusable(dialog,
            dialog->window()->focusedWidget, event->key.keysym.mod & KMOD_SHIFT);
        if(w)
          gui->setFocused(w, SvgGui::REASON_TAB);
      }
      else
        return false;
      return true;
    }
    //if(event->type == SvgGui::OUTSIDE_MODAL) {
    //  gui->closeMenus(dialog->parent(), true);
    //  if(onCancel) onCancel();
    //  return true;
    //}
    return false;
  });

  return dialog;
}

// show inline dialog modally - also possible to show non-modal just using setVisible()
void showInlineDialogModal(Widget* dialog)
{
  //Window* win = dialog->window();
  //if(win && win->gui())
  //  win->gui()->showMenu(dialog);
  dialog->setVisible(true);
}

void showModalCentered(Window* modal, SvgGui* gui)
{
  Window* win = gui->windows.front()->modalOrSelf();
  Rect pbbox = win->winBounds();
  modal->setWinBounds(Rect::centerwh(pbbox.center(), std::min(pbbox.width() - 20, real(500)), pbbox.height() - 60));
  gui->showModal(modal, win);
}

void sendKeyPress(SvgGui* gui, Widget* widget, int sdlkey, int mods)
{
  SDL_Event event = {0};
  event.key.type = SDL_KEYDOWN;
  event.key.state = SDL_PRESSED;
  event.key.repeat = 0;
  event.key.keysym.scancode = (SDL_Scancode)0;
  event.key.keysym.sym = sdlkey;
  event.key.keysym.mod = mods;
  event.key.windowID = 0;  //keyboard->focus ? keyboard->focus->id : 0;
  widget->sdlEvent(gui, &event);
  event.key.type = SDL_KEYUP;
  event.key.state = SDL_RELEASED;
  widget->sdlEvent(gui, &event);
}

static int getMonthDays(int year, int month)
{
  static constexpr int monthDays[12] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  bool leapfeb = (month == 2 && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)));
  return leapfeb ? 29 : monthDays[month-1];
}

Widget* createDatePicker(int year0, int month0, int day0, std::function<void(int year, int month, int day)> onChange)
{
  SpinBox* yearBox = createTextSpinBox(year0, 1, 0, 9999, "%.0f");
  SpinBox* monthBox = createTextSpinBox(month0, 1, 0, 13, "%02.0f");
  SpinBox* dayBox = createTextSpinBox(day0, 1, 0, 32, "%02.0f");
  monthBox->setMargins(0, 5);
  setMinWidth(yearBox, 110);
  setMinWidth(monthBox, 90);
  setMinWidth(dayBox, 90);
  Widget* row = createRow();
  row->addWidget(yearBox);
  row->addWidget(monthBox);
  row->addWidget(dayBox);

  // we'll leave it to user-supplied onChange() to limit date range (e.g. past or future only)
  yearBox->onValueChanged = [=](real val){
    if(monthBox->value() == 2 && dayBox->value() > 28)
      dayBox->setValue(28);
    onChange(yearBox->value(), monthBox->value(), dayBox->value());
  };
  monthBox->onValueChanged = [=](real val){
    if(val < 1) { monthBox->setValue(12); yearBox->updateValue(yearBox->value() - 1); }
    else if(val > 12) { monthBox->setValue(1); yearBox->updateValue(yearBox->value() + 1); }
    else {
      int monthdays = getMonthDays(yearBox->value(), monthBox->value());
      if(dayBox->value() > monthdays) { dayBox->setValue(monthdays); }
      onChange(yearBox->value(), monthBox->value(), dayBox->value());
    }
  };
  dayBox->onValueChanged = [=](real val){
    if(val < 1) { dayBox->setValue(31); monthBox->updateValue(monthBox->value() - 1); }
    else if(val > getMonthDays(yearBox->value(), monthBox->value())) {
      dayBox->setValue(1); monthBox->updateValue(monthBox->value() + 1);
    }
    else { onChange(yearBox->value(), monthBox->value(), dayBox->value()); }
  };
  return row;
}

Pager::Pager(SvgNode* _node) : Widget(_node)
{
  //behaveAsStack = true;
  onPrepareLayout = [this](){
    if(nextPage) {
      currPage->setLayoutTransform(Transform2D());  // reset transforms before layout
      nextPage->setLayoutTransform(Transform2D());
    }
    return Rect();  // return invalid Rect to continue w/ normal layout
  };

  onApplyLayout = [this](const Rect& src, const Rect& dest){
    if(!nextPage) return false;
    real w = (xoffset < 0 ? 1 : -1)*initialBounds.width();
    if(xoffset >= 0 || !behaveAsStack) currPage->setLayoutTransform(Transform2D::translating(xoffset, 0));
    if(xoffset <= 0 || !behaveAsStack) nextPage->setLayoutTransform(Transform2D::translating(xoffset + w, 0));
    return true;
  };

  eventFilter = [=](SvgGui* gui, Widget* widget, SDL_Event* event){
    if(event->type == SDL_FINGERDOWN && event->tfinger.fingerId == SDL_BUTTON_LMASK) {
      initialPos = Point(event->tfinger.x, event->tfinger.y);
      initialTime = event->tfinger.timestamp;
      initialBounds = node->bounds();
      //gui->pressedWidget = this;
      nextPage = NULL;  // shouldn't be necessary
    }
    else if(event->type == SDL_FINGERMOTION  //gui->pressedWidget && gui->pressedWidget->isDescendantOf(this)) {
        && event->tfinger.fingerId == SDL_BUTTON_LMASK && event->tfinger.touchId != SDL_TOUCH_MOUSEID) {
      xoffset = event->tfinger.x - initialPos.x;
      real xc = initialBounds.center().x;
      bool left = initialPos.x > xc;
      if(left != (xoffset < 0)) xoffset = 0;

      if(!nextPage) {
        uint32_t dt = event->tfinger.timestamp - initialTime;
        if(gui->pressedWidget == this  // allow another handler to make us pressed
            || (std::abs(xoffset) > 60 && std::abs(event->tfinger.y - initialPos.y) < 20 && dt < 400)) {
          if(getNextPage)
            getNextPage(!left);
          else {
            auto& children = containerNode()->children();
            auto it = std::find_if(children.begin(), children.begin(), [](SvgNode* n){
              return n->hasExt() && static_cast<Widget*>(n->ext())->isVisible();
            });
            if(it == children.end()) return false;
            currPage = static_cast<Widget*>((*it)->ext());
            if(xoffset > 0 && !left && it-- != children.begin())  // swiping right
              nextPage = static_cast<Widget*>((*it)->ext(false));
            else if(xoffset < 0 && left && ++it != children.end())  // swiping left
              nextPage = static_cast<Widget*>((*it)->ext(false));
          }
          if(nextPage) {
            if(gui->pressedWidget && gui->pressedWidget != this)
              gui->pressedWidget->sdlUserEvent(gui, SvgGui::OUTSIDE_PRESSED, 0, event, this);
            gui->pressedWidget = this;
            nextPage->setVisible(true);
          }
        }
      }
    }
    if(nextPage) {  //&& gui->pressedWidget == this) {
      gui->sendEvent(window(), this, event);
      return true;
    }
    return false;
  };

  addHandler([this](SvgGui* gui, SDL_Event* event){
    if(!nextPage) return false;
    if(event->type == SDL_FINGERMOTION && gui->pressedWidget == this) {
      xoffset = event->tfinger.x - initialPos.x;
      if((initialPos.x > initialBounds.center().x) != (xoffset < 0)) xoffset = 0;
      real w = (xoffset < 0 ? 1 : -1)*initialBounds.width();
      // setLayoutTransform will set bounds dirty and trigger layout; instead, we only want to dirty pixels
      if(xoffset >= 0 || !behaveAsStack) currPage->setLayoutTransform(Transform2D::translating(xoffset, 0));  // * child->m_layoutTransform;
      if(xoffset <= 0 || !behaveAsStack) nextPage->setLayoutTransform(Transform2D::translating(xoffset + w, 0));  // * child->m_layoutTransform;
      currPage->node->invalidateBounds(true);
      nextPage->node->invalidateBounds(true);
      redraw();
    }
    else if(event->type == SDL_FINGERUP || event->type == SvgGui::OUTSIDE_PRESSED) {
      currPage->setLayoutTransform(Transform2D());
      nextPage->setLayoutTransform(Transform2D());
      if(std::abs(xoffset) > initialBounds.width()/2) {
        currPage->setVisible(false);
        currPage = nextPage;
        if(onPageChanged)
          onPageChanged(currPage);
      }
      else {
        nextPage->setVisible(false);
      }
      nextPage = NULL;
      redraw();
      return true;
    }
    return false;
  });
}

#include "ugui/colorwidgets.h"

void ManageColorsDialog::setSaveDelState()
{
  bool issaved = std::find(colors.begin(), colors.end(), colorEditBox->color()) != colors.end();
  saveBtn->setVisible(!issaved);
  deleteBtn->setVisible(issaved);
};

void ManageColorsDialog::populateColorList()
{
  gui()->deleteContents(colorList);
  for(size_t ii = 0; ii < colors.size(); ++ii) {
    Color color = colors[ii];
    Button* btn = createColorBtn();  //new Button(widgetNode("#colorbutton"));
    btn->selectFirst(".btn-color")->node->setAttr<color_t>("fill", color.color);
    btn->onClicked = [=](){ colorEditBox->setColor(color); setSaveDelState(); };
    colorList->addWidget(btn);
  }
  setSaveDelState();
}

ManageColorsDialog::ManageColorsDialog(std::vector<Color>& _colors) : Dialog(createMobileDialogNode()), colors(_colors)
{
  static const char* colorListSVG = R"#(
    <g box-anchor="fill" layout="flex" flex-direction="row" flex-wrap="wrap" justify-content="flex-start" margin="6 6">
    </g>
  )#";

  colorList = new Widget(loadSVGFragment(colorListSVG));
  colorEditBox = createColorEditBox(false, false);
  colorEditBox->onColorChanged = [=](Color c){ setSaveDelState(); };

  saveBtn = createToolbutton(uiIcon("save"), "Save", true);
  deleteBtn = createToolbutton(uiIcon("discard"), "Delete", true);
  saveBtn->onClicked = [=](){
    colors.insert(colors.begin(), colorEditBox->color());
    populateColorList();
    if(onColorListChanged)
      onColorListChanged();
  };
  deleteBtn->onClicked = [=](){
    colors.erase(std::remove(colors.begin(), colors.end(), colorEditBox->color()));
    populateColorList();
    if(onColorListChanged)
      onColorListChanged();
  };

  Toolbar* toolbar = createToolbar();
  toolbar->node->setAttribute("margin", "0 3");
  toolbar->addWidget(colorEditBox);
  toolbar->addWidget(createStretch());
  toolbar->addWidget(saveBtn);
  toolbar->addWidget(deleteBtn);

  Widget* content = createColumn();
  content->node->setAttribute("box-anchor", "fill");
  content->addWidget(toolbar);
  content->addWidget(colorList);
  content->addWidget(createStretch());

  //auto scrollWidget = new ScrollWidget(new SvgDocument(), colorList);
  setupMobileDialog(this, "Colors", "Accept", content);
  onFinished = [=](int res){
    if(res == Dialog::ACCEPTED && onColorAccepted) {
      onColorAccepted(colorEditBox->color());
    }
  };
}

void ManageColorsDialog::setColor(Color initialColor)
{
  colorEditBox->setColor(initialColor);  // cannot be done until ColorEditBox is added to Window
  populateColorList();
}


Dialog* createInputDialog(std::initializer_list<Widget*> widgets, const char* title,
    const char* acceptLabel, std::function<void()> onAccept, std::function<void()> onCancel)
{
  static const char* inlineDialogSVG = R"#(
    <g class="col-layout" box-anchor="fill" layout="flex" flex-direction="column" margin="2 5">
    </g>
  )#";
  static std::unique_ptr<SvgNode> proto;
  if(!proto)
    proto.reset(loadSVGFragment(inlineDialogSVG));

  Widget* container = new Widget(proto->clone());
  Dialog* dialog = createMobileDialog(title, acceptLabel, container);

  for(Widget* child : widgets)
    container->addWidget(child);
  container->addWidget(createStretch());

  container->addHandler([=](SvgGui* gui, SDL_Event* event){
    if(event->type == SDL_KEYDOWN) {
      if(event->key.keysym.sym == SDLK_TAB) {
        Widget* w = SvgGui::findNextFocusable(dialog,
            dialog->window()->focusedWidget, event->key.keysym.mod & KMOD_SHIFT);
        if(w)
          gui->setFocused(w, SvgGui::REASON_TAB);
        return true;
      }
    }
    return false;
  });

  dialog->onFinished = [=](int res){
    if(res == Dialog::ACCEPTED) {
      if(onAccept) onAccept();
    }
    else if(onCancel)
      onCancel();
  };

  return dialog;
}

void CrosshairWidget::setRoutePreviewOrigin(Point p)
{
  if(p != routePreviewOrigin) {
    routePreviewOrigin = p;
    node->invalidate(true);
  }
}

Rect CrosshairWidget::bounds(SvgPainter* svgp) const
{
  real w = std::max(2*std::abs(routePreviewOrigin.x), 32.0);
  real h = std::max(2*std::abs(routePreviewOrigin.y), 32.0);
  return svgp->p->getTransform().mapRect(Rect::wh(w, h));
}

void CrosshairWidget::draw(SvgPainter* svgp) const { if(!useDirectDraw) directDraw(svgp->p); }

void CrosshairWidget::directDraw(Painter* p) const
{
  //Painter* p = svgp->p;
  Rect bbox = node->bounds();
  if(!useDirectDraw)
    bbox = bbox.toSize();
  p->save();
  p->translate(bbox.center());
  bbox.translate(-bbox.center());
  p->setFillBrush(Color::NONE);
  p->setStroke(Color::RED, 2.5);  //, Painter::FlatCap, Painter::BevelJoin);
  if(routePreviewOrigin != Point(0, 0))
    p->drawLine(routePreviewOrigin, Point(0, 0));
  real hw = 16, hh = 16;
  p->drawLine(Point(-hw, 0), Point(-3, 0));
  p->drawLine(Point(3, 0), Point(hw, 0));
  p->drawLine(Point(0, -hh), Point(0, -3));
  p->drawLine(Point(0, 3), Point(0, hh));
  p->restore();
}

void ProgressCircleWidget::setProgress(real p)
{
  if(p == mProgress) return;
  mProgress = p;
  redraw();
}

Rect ProgressCircleWidget::bounds(SvgPainter* svgp) const
{
  return svgp->p->getTransform().mapRect(Rect::wh(40, 40));
}

void ProgressCircleWidget::draw(SvgPainter* svgp) const
{
  Painter* p = svgp->p;
  Rect bbox = node->bounds().toSize();
  p->save();
  p->translate(bbox.center());
  bbox.translate(-bbox.center());

  real rad = mProgress*2*M_PI;
  p->setFillBrush(Color::NONE);
  //p->setStroke(Color(128, 128, 128, 96), 4);
  //p->drawPath(Path2D().addEllipse(0, 0, 18, 18));
  p->setStroke(Color(64, 128, 255, 128), 4);  //, Painter::FlatCap, Painter::BevelJoin);
  Path2D done;
  done.moveTo(0, -16);
  done.addArc(0, 0, 16, 16, -0.5*M_PI, rad);
  p->drawPath(done);

  p->setStroke(Color(64, 64, 64, 128), 4);
  Path2D rest;
  rest.moveTo(done.currentPosition());
  rest.addArc(0, 0, 16, 16, rad-0.5*M_PI, 2*M_PI - rad);
  p->drawPath(rest);

  p->restore();
}
