#ifndef Menu_cpp
#define Menu_cpp
#include "Menu.h"
#include "MenuItem.h"
#include "Arduino.h"
#define LCD_WIDTH    16
Menu::Menu()
{
};

Menu::Menu(MenuItem *theMenuItems[4], UIItem *theStaticItems[2], LiquidCrystal *lcd)
{
    for (byte i = 0; i < 4; i++)
    {
        _menuItems[i] = theMenuItems[i];
    }
    for (byte i = 0; i < 2; i++)
    {
        _staticItems[i] = theStaticItems[i];
    }
    _lcd = lcd;
}

void Menu::setItem(MenuItem *itm, byte num)
{
    _menuItems[num] = itm;
}

void Menu::setItem(UIItem *itm, byte num) //oh, now I understand why overloading is cool
{
    _staticItems[num] = itm;
}

void Menu::display(void)
{
    for (byte i = 0; i < 4; i++)
    {
        if (_menuItems[i]->canUpdate)
        {
            // Serial.println(String("updating: " + String(i))); //debug
            _menuItems[i]->update();
        }
        if (_menuItems[i]->getDisplayString() != MenuItem::blank_string)
        {
            _lcdPrintMenuItem(i);
            // Serial.println(_menuItems[i]->getDisplayString());//debug
        }
    }
    for (byte i = 0; i < 2; i++)
    {
        if (_staticItems[i]->canUpdate)
        {
            _staticItems[i]->update();
        }
        if (_staticItems[i]->getDisplayString() != _staticItems[i]->blank_string)
        {
            _lcdPrintStaticItem(i);
            // Serial.println(_staticItems[i]->getDisplayString());//debug
        }
    }
    // Serial.println("displaying menu");
}

void Menu::click(byte theItem)
{
    _menuItems[theItem]->click();
}

void Menu::autoclick(byte theItem)
{
    _menuItems[theItem]->autoclick();
}

void Menu::_lcdPrintMenuItem(byte i)
{
    _lcd->setCursor((i % 2) * (LCD_WIDTH - STRING_LENGTH + 1), byte(i / 2));
    _lcd->print(_menuItems[i]->getDisplayString());
}

void Menu::_lcdPrintStaticItem(byte i)
{
    _lcd->setCursor(byte((LCD_WIDTH - STRING_LENGTH) / 2 + 1), i);
    _lcd->print(_staticItems[i]->getDisplayString());
}

#endif
