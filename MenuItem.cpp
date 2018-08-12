#ifndef MenuItem_cpp
#define MenuItem_cpp

#include "MenuItem.h"
#include "Arduino.h"

MenuItem::MenuItem()
// default constructor
{
    _isClickable = false;
    _onClickFn   = NULL;
    // setDisplayString(BLANK_STRING);
    // canUpdate = false;
    // setUpdateFn(NULL);
}

// updatable but not clickable
MenuItem::MenuItem(char dString[STRING_LENGTH], updateFn *onUpdFn)
{
  _isClickable = false;
  _onClickFn   = NULL;
  setDisplayString(dString);
  canUpdate = true;
  setUpdateFn(onUpdFn);
}

// clickable but not updatable
MenuItem::MenuItem(char dString[STRING_LENGTH], menuFn *onCkFn, bool clickable)
{
    _isClickable = clickable;
    _onClickFn   = onCkFn;
    setDisplayString(dString);
    // canUpdate = false;
    // setUpdateFn(NULL);
}

// clickable, updateable
MenuItem::MenuItem(char dString[STRING_LENGTH], menuFn *onCkFn, updateFn *onUpdFn, bool clickable)
{
    _isClickable = clickable;
    _onClickFn   = onCkFn;
    setDisplayString(dString);
    canUpdate = true;
    setUpdateFn(onUpdFn);
}

void MenuItem::click(void)
{
    if (_isClickable)
    {
        _onClickFn();
    }
}

#endif
