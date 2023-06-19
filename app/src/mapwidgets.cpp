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
  Button* cancelBtn = new Button(containerNode()->selectFirst(".cancel-btn"));
  cancelBtn->onClicked = [=](){ finish(Dialog::CANCELLED); };
  Widget* dialogBody = selectFirst(".body-container");
  dialogBody->addWidget(new ScrollWidget(new SvgDocument(), content));

  if(!_items.empty())
    addItems(_items, false);
}

void SelectDialog::addItems(const std::vector<std::string>& _items, bool replace)
{
  if(replace)
    window()->gui()->deleteContents(content);
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
    gui->showModal(dialog.get(), gui->windows.front()->modalOrSelf());
  };

  dialog->onSelected = [this](int idx){ updateIndex(idx); };

  addItems(_items);
  setText(items.front().c_str());
}

void SelectBox::addItems(const std::vector<std::string>& _items, bool replace)
{
  if(replace)
    items.clear();
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
          <g class="button-container toolbar" box-anchor="hfill" layout="flex" flex-direction="row">
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
  static_cast<SvgText*>(dialog->selectFirst(".dialog-title"))->addText("title");
  static_cast<SvgUse*>(dialog->selectFirst(".listitem-icon"))->setTarget(itemicon);
  return new SelectDialog(dialogProto->clone(), items);
}

SelectBox* createSelectBox(const char* title, const SvgNode* itemicon, const std::vector<std::string>& items)
{
  static const char* boxProtoSVG = R"#(
    <g class="inputbox combobox" layout="box" box-anchor="left" margin="0 10">
      <rect class="min-width-rect" width="150" height="36" fill="none"/>
      <rect class="inputbox-bg" box-anchor="fill" width="150" height="36"/>

      <g class="combo_content" box-anchor="fill" layout="flex" flex-direction="row" margin="0 2">
        <g class="textbox combo_text" box-anchor="fill" layout="box">
          <text box-anchor="left" margin="3 6"></text>
        </g>
        <g class="combo_open" box-anchor="vfill" layout="box">
          <rect fill="none" box-anchor="vfill" width="28" height="28"/>
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
