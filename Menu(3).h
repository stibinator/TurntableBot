#ifndef Menu_h
#define Menu_h

#include "Arduino.h"
#include "MenuItem.h"
#include <LiquidCrystal.h>

class Menu {
public:
    Menu();
    Menu(MenuItem *theMenuItems[4], UIItem *theStaticItems[2], LiquidCrystal *lcd);
    void setItem(MenuItem *itm, byte num);
    void setItem(UIItem *itm, byte num);
    void display(void);
    byte doItem(byte buttonPressed);
    void click(byte theClick);
private:
    MenuItem *_menuItems[4];
    UIItem *_staticItems[2];
    LiquidCrystal *_lcd;
    void _lcdPrintMenuItem(byte);
    void _lcdPrintStaticItem(byte);
};

#endif
