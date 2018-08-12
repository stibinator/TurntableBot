#ifndef UIItem_h
#define UIItem_h

#include "Arduino.h"
#define STRING_LENGTH    6

typedef void (menuFn)(void);    //does the things
typedef char *(updateFn)(void); //updates the display string

class UIItem {
public:
    UIItem();
    // static string
    UIItem(char dString[STRING_LENGTH]);
    // updatable string
    UIItem(char dString[STRING_LENGTH], updateFn *onUpdFn);
    void update(void);
    char *getDisplayString(void);
    void setDisplayString(char *str);

    void setUpdateFn(updateFn);
    bool canUpdate;
    static const char blank_string[];
private:
    char _displayString[STRING_LENGTH];
    updateFn *_onUpdateFn;
};

#endif
