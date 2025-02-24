#include "CaptureCell.hpp"

CaptureCell* CaptureCell::create(const HttpInfo::URL& url, const CCSize& size, const std::function<void(CaptureCell*)>& switchCell) {
    CaptureCell* instance = new CaptureCell(url, size, switchCell);

    if (instance && instance->init(size)) {
        instance->autorelease();

        return instance;
    } else {
        CC_SAFE_DELETE(instance);

        return nullptr;
    }
}

CaptureCell::CaptureCell(const HttpInfo::URL& url, const CCSize& size, const std::function<void(CaptureCell*)>& switchCell) : GenericListCell("", size),
m_url(url),
m_switchCell(switchCell) {
    this->setID("capture"_spr);
}

bool CaptureCell::init(const CCSize& size) {
    const std::string method(m_url.getMethod());
    const std::string path(m_url.getPath());
    std::string cutoffPath(path == "/" ? "" : path.substr(path.substr(0, path.size() - 1).find_last_of('/')));

    if (cutoffPath.empty()) {
        cutoffPath = m_url.getPortHost();
    }

    CCLabelBMFont* label = CCLabelBMFont::create((method + ' ' + cutoffPath).c_str(), "bigFont.fnt");
    ButtonSprite* button = ButtonSprite::create("View", "bigFont.fnt", "GJ_button_01.png");
    CCMenu* menu = CCMenu::createWithItem(CCMenuItemSpriteExtra::create(
        button,
        this,
        menu_selector(CaptureCell::onView)
    ));

    menu->setAnchorPoint(BOTTOM_LEFT);
    menu->setPosition({ size.width - 20, size.height / 2 });
    menu->setScale(0.3f);
    button->setID("view"_spr);
    label->setAnchorPoint(CENTER_LEFT);
    label->setPosition({ PADDING, size.height / 2 });
    label->limitLabelWidth(menu->getPositionX() - button->getContentWidth() * menu->getScale() / 2 - PADDING * 2, 0.2f, 0.05f);
    m_mainLayer->addChild(label);
    m_mainLayer->addChild(menu);



    for (size_t i = 0; i < method.size(); i++) {
        cocos::getChild<CCSprite>(label, i)->setColor(this->colorForMethod());
    }

    return true;
}

ccColor3B CaptureCell::colorForMethod() {
    const std::string& method(m_url.getMethod());

    if (method == "GET") {
        return { 0xA8, 0x96, 0xFF };
    } else if (method == "POST") {
        return { 0x7E, 0xCF, 0x2B };
    } else if (method == "PUT") {
        return { 0xFF, 0x9A, 0x1F };
    } else if (method == "DELETE") {
        return { 0xFF, 0x56, 0x31 };
    } else {
        return { 0x46, 0xC1, 0xE6 };
    }
}

void CaptureCell::activate() {
    m_switchCell(this);
    as<ButtonSprite*>(m_mainLayer->getChildByIDRecursive("view"_spr))->updateBGImage("GJ_button_05.png");
}

void CaptureCell::deactivate() {
    as<ButtonSprite*>(m_mainLayer->getChildByIDRecursive("view"_spr))->updateBGImage("GJ_button_01.png");
}

void CaptureCell::onView(CCObject* obj) {
    this->activate();
}