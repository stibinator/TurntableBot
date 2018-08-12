#ifndef UIItem_cpp
#define UIItem_cpp

#include "UIItem.h"
#include "Arduino.h"


const char UIItem::blank_string[STRING_LENGTH] = "     ";

UIItem::UIItem()
// default constructor
{
    memcpy(_displayString, blank_string, STRING_LENGTH);
    _onUpdateFn    = NULL;
    canUpdate      = false;
    // Serial.print("creating UI item: " + _displayString); //debug
}

UIItem::UIItem(char dString[STRING_LENGTH])
//UIItem that does not need updating
{
  memcpy(_displayString, dString, STRING_LENGTH);
    _onUpdateFn    = NULL;
    canUpdate      = false;
    // Serial.print("creating UI item: " + _displayString); //debug
}

UIItem::UIItem(char dString[STRING_LENGTH], updateFn *onUpdFn)
// UIItem that must be updated before displaying
{
  memcpy(_displayString, dString, STRING_LENGTH);
    _onUpdateFn    = onUpdFn;
    canUpdate      = true;
    // Serial.print("creating UI item: " + _displayString); //debug
}

void UIItem::update(void)
{
    if (canUpdate)
    {
        memcpy(_displayString, _onUpdateFn(), STRING_LENGTH);
    }
}

char *UIItem::getDisplayString(void)
{
    return(_displayString);
}

void UIItem::setDisplayString(char theNewStr[STRING_LENGTH])
{
    memcpy(_displayString, theNewStr, STRING_LENGTH);
}

void UIItem::setUpdateFn(updateFn *theUDF)
{
    _onUpdateFn = theUDF;
}

#endif
