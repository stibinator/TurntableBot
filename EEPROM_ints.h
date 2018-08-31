#include <EEPROM.h>
#include <Arduino.h> // for type definitions

int EEPROM_readInt(int ee)
{
    byte high = EEPROM.read(ee);
    byte low = EEPROM.read(ee + 1);
    return (word(high, low));
}

void EEPROM_writeInt(int ee, int theInt)
{
    EEPROM.write(ee, highByte(theInt));
    EEPROM.write(ee + 1, lowByte(theInt));
}