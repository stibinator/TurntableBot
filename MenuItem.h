#ifndef MenuItem_h
#define MenuItem_h

#include "Arduino.h"
#include "UIItem.h"

typedef void (menuFn)(void);    //does the things
typedef char *(updateFn)(void); //updates the display string

class MenuItem : public UIItem {
    //a UIItem that can be clicked
public:
    MenuItem();
    // updatable but not clickable
    MenuItem(char dString[5], updateFn *onUpdFn);
    // clickable but not updatable
    MenuItem(char dString[5], menuFn *onCkFn, bool clickable);
    MenuItem(char dString[5], menuFn *onCkFn, bool clickable, bool autoclickable);
    // clickable, updateable
    MenuItem(char dString[5], menuFn *onCkFn, updateFn *onUpdFn, bool clickable);
    MenuItem(char dString[5], menuFn *onCkFn, updateFn *onUpdFn, bool clickable, bool autoclickable);
    void click(void);
    void autoclick(void);

private:
    bool _canBeAutoClicked;
    bool _isClickable;
    menuFn *_onClickFn;
};

#endif
