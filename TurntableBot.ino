#include "UIItem.h"
#include "MenuItem.h"
#include "Menu.h"
#include "EEPROM_ints.h"

#include <EEPROM.h>
#include <LiquidCrystal.h>
#include <avr/interrupt.h>
#include <avr/io.h>

// Arduino defines
#define DIR_PIN 2 //the available pins on the LCD shield are 0,1,2,3,11,12,13
#define STEP_PIN 3
#define FOCUS_PIN 11
#define SHUTTER_PIN 12
#define ENABLE_PIN 13 //goes LOW to enable

#define STEP_CLOCKWISE HIGH // the value to set the dir_pin to when changing direction
#define STEP_REVERSE LOW
#define ENABLE LOW
#define DISABLE HIGH

#define BUTTON_ADC_PIN A0    // A0 is the button ADC input
#define LCD_BACKLIGHT_PIN 10 // D10 controls LCD backlight
// ADC readings expected for the 5 buttons on the ADC input
#define RIGHT_10BIT_ADC 0    // right
#define UP_10BIT_ADC 100     // up
#define DOWN_10BIT_ADC 256   // down
#define LEFT_10BIT_ADC 408   // left
#define SELECT_10BIT_ADC 640 // right
#define BUTTONHYSTERESIS 40  // hysteresis for valid button sensing window

//return values for readButtons()
#define BUTTON_BOTTOMRIGHT 3 //
#define BUTTON_BOTTOMLEFT 2  //
#define BUTTON_TOPRIGHT 1    //
#define BUTTON_TOPLEFT 0     //
#define BUTTON_SELECT 4      //
#define BUTTON_NONE 5        //

// motorState constants
#define STOPPED 0 //also used as operation state
#define ACCELERATING 1
#define RUNNING 2
#define DECELERATING 3
#define STOPPING 4
#define INCHING_CW 5
#define INCHING_CCW 6

#define RAMP_POWER 6 //exponent of the acceleration curve

// operation state constants
// #define STOPPED              0 //defined already
#define RUNNING_AUTO 1
#define RUNNING_MAN 2

// #define STEPS_PER_DEGREE 21.1111 // 76 * 100 / 360
// #define STEPS_PER_REV         7600
//some example macros with friendly labels for LCD backlight/pin control, tested and can be swapped into the example code as you like
#define LCD_BACKLIGHT_OFF() digitalWrite(LCD_BACKLIGHT_PIN, LOW)
#define LCD_BACKLIGHT_ON() digitalWrite(LCD_BACKLIGHT_PIN, HIGH)
#define LCD_BACKLIGHT(state)                       \
    {                                              \
        if (state)                                 \
        {                                          \
            digitalWrite(LCD_BACKLIGHT_PIN, HIGH); \
        }                                          \
        else                                       \
        {                                          \
            digitalWrite(LCD_BACKLIGHT_PIN, LOW);  \
        }                                          \
    }
// directions
#define CLOCKWISE 0
#define COUNTERCW 1
// Menus
#define RUN_MENU 0
#define MAIN_MENU 1
#define SETUP_MENU 2
#define DIRECTION_MENU 3
#define MOTOR_MENU 4
#define SPEED_MENU 5
#define ACCEL_MENU 6
#define STEPS_MENU 7
#define MANUAL_MENU 8
#define DELAYS_MENU 9
#define PREDELAY_MENU 10
#define FOCUSDELAY_MENU 11
#define SHUTTERDELAY_MENU 12
#define INCH_MENU 13

#define CLICKABLE true
#define AUTOCLICKABLE true
#define LEFT_JUSTIFY true
#define RIGHT_JUSTIFY false
#define MAX_STEPS_PER_SECOND 1200
#define MIN_STEPS_PER_SECOND 8

#define BUTTON_LONG_PRESS 1000
#define BUTTON_REPEAT_TIME 1000

#define interruptDuration 0.000064

// EEPROM addresses
#define OKTOREAD_EEPROM 0         // one byte
#define STEPSPERSECOND_EEPROM 1   // two bytes
#define PREDELAYTIME_EEPROM 3     // two bytes
#define FOCUSDELAYTIME_EEPROM 5   // two bytes
#define SHUTTERDELAYTIME_EEPROM 7 // two bytes
#define SHOTSPERREV_EEPROM 9      // two bytes
#define RAMPSTEPS_EEPROM 11       // two bytes

/*--------------------------------------------------------------------------------------
 *  Variables
 *  --------------------------------------------------------------------------------------*/
bool buttonJustPressed = false;  //this will be true after a readButtons() call if triggered
bool buttonJustReleased = false; //this will be true after a readButtons() call if triggered
bool buttonAutoClicked = false;
byte buttonWas = BUTTON_NONE; //used by readButtons() for detection of button events
byte button = BUTTON_NONE;
unsigned long btnPressMillis = 0;  //time the last button was pressed
unsigned long btnHoldTime = 0;     //how long the button has been held for
unsigned long lastAutoPressMs = 0; // counts untill next auto click
volatile long motorSpeed = 0;      //normalised speed motor is currently at, goes from 0→32767
volatile long targetSpeed = 0;     //normalised speed motor is aiming for, goes from 0→32767
volatile long maxSpeed = 32767;    //unit-less number
// When interuptLoopCount == 1 interrupt every ≈ 64 µs. There are two interrupts per step
volatile int minInterruptsPerStep;
//reduce the number of calculations we need to do in the interrupt handler
volatile int stepDiff;
volatile byte motorState = STOPPED;
volatile byte operationState = STOPPED;
bool takingShot = false;
volatile unsigned int stepDirection; //HIGH and LOW are u_ints
volatile int stepNumber;
volatile int stepTarget;
volatile bool leadingEdge;
volatile int accelerationIncrement;
volatile float relativeSpeed;
volatile const int stepsPerRev = 19800;

int currentShot = 0;
int maxInterruptsPerStep;

// these values get stored in the EEPROM
int stepsPerSecond = 800; //the max speed of the stepper
int preDelayTime = 2000;
int focusDelayTime = 1000;
int shutterDelayTime = 1000;
int shotsPerRev = 12;
int rampSteps; // number of steps needed to get to full speed

//--------------------------------------------------------------------------------------
// Init the LCD library with the LCD pins to be used
LiquidCrystal lcd(8, 9, 4, 5, 6, 7); //Pins for the freetronics 16x2 LCD shield.
//--------------------------------------------------------------------------------------
byte direction = CLOCKWISE;
byte currentMenu = RUN_MENU;

//--------------------------------------------------------------------------------------

// the real hooh-hah

//--------------------------------------------------------------------------------------

void toggleAutoRun()
{
    if (operationState == STOPPED)
    {
        // take first shot
        takeShot();
        digitalWrite(ENABLE_PIN, ENABLE);
        // Serial.println("ENABLE");
        digitalWrite(DIR_PIN, direction);
        operationState = RUNNING_AUTO;
        stepTarget = stepsPerRev / shotsPerRev;
        stepNumber = 0;
        currentShot = 0;
        targetSpeed = maxSpeed;
    }
    else
    {
        targetSpeed = 0;
        operationState = STOPPING;
    }
}

void intToDisplayChar(int theInt, char theChars[STRING_LENGTH], bool justify)
{
    String s = String(theInt);
    byte n = min(s.length(), STRING_LENGTH);

    if (justify)
    {
        // LEFT_JUSTIFY
        for (byte i = 0; i < STRING_LENGTH - 1; i++)
        {
            if (i < n)
            {
                theChars[i] = s.charAt(i);
            }
            else
            {
                theChars[i] = char(' ');
            }
        }
    }
    else
    {
        // RIGHT_JUSTIFY
        for (byte i = 0; i < STRING_LENGTH - 1; i++)
        {
            if (i < (STRING_LENGTH - 1 - n))
            {
                theChars[i] = char(' ');
            }
            else
            {
                theChars[i] = s.charAt(i - (STRING_LENGTH - 1 - n));
            }
        }
    }
}

// blank menuItem
MenuItem blankMI;
UIItem blankUII;
// 0 - runMenu ------------------------------------
// runMenu buttons
void gotoMainMenuIfNotRunning()
{
    if (operationState == STOPPED)
    {
        currentMenu = MAIN_MENU;
    }
}

char lMenu[] = "<Menu";
char greyedOut[] = "     ";
char *updateMainMenuButton()
// grey out the menu button if the motor is running
{
    return ((operationState == STOPPED) ? lMenu : greyedOut);
}

MenuItem mainMenuBtn(lMenu, gotoMainMenuIfNotRunning, updateMainMenuButton, CLICKABLE);

char lSTART[] = "START";
char lSTOP[] = " STOP";
char *updateStartBtn()
{
    return ((operationState == STOPPED) ? lSTART : lSTOP);
}

MenuItem startBtn(lSTART, toggleAutoRun, updateStartBtn, CLICKABLE);
char lShotCount[] = "    0";
char *countShots()
{
    intToDisplayChar(currentShot, lShotCount, RIGHT_JUSTIFY);
    return (lShotCount);
}

MenuItem shotCounter(lShotCount, countShots);
char lCounter[] = "00000";
char *shotsIndicate()
{
    intToDisplayChar(shotsPerRev, lCounter, LEFT_JUSTIFY);
    return (lCounter);
}

MenuItem shotsIndicator(lCounter, shotsIndicate);
MenuItem *runMenuButtons[4] =
    {
        &mainMenuBtn,
        &startBtn,
        &shotCounter,
        &shotsIndicator};
// runMenu UIItems
char motorIndicatorFrames[9][STRING_LENGTH] = {"<||  ", "<-+- ", "<-=- ", "<-|- ", " ||> ", "-+-> ", "-=-> ", "-|-> ", ">[o]<"};
char *motorIndicate()
{
    if (takingShot)
    {
        return (motorIndicatorFrames[8]);
    }
    else
    {
        return (motorIndicatorFrames[motorState + (4 * direction)]);
    }
}

UIItem motorStateIndicator(motorIndicatorFrames[0], motorIndicate);
char lSlash[] = "  /  ";
UIItem slash(lSlash);
UIItem *runMenuUIItems[2] =
    {
        &motorStateIndicator,
        &slash};
Menu runMenu(
    runMenuButtons,
    runMenuUIItems,
    &lcd);

// 1 - mainMenu ------------------------------------
// mainMenu buttons
char lAuto[] = "Auto ";
MenuItem runMenuBtn(lAuto, []() {
    currentMenu = RUN_MENU;
},
                    CLICKABLE);
char lMan[] = "  Man";
MenuItem manualMenuBtn(lMan, []() {
    currentMenu = MANUAL_MENU;
},
                       CLICKABLE);
char lSetup[] = "Setup";
MenuItem setupMenuBtn(lSetup, []() {
    currentMenu = SETUP_MENU;
},
                      CLICKABLE);
char lInchMenu[] = " Inch";
MenuItem inchMenuBtn(lInchMenu, []() {
    currentMenu = INCH_MENU;
},
                     CLICKABLE);

MenuItem *mainMenuButtons[4] =
    {
        &runMenuBtn,
        &manualMenuBtn,
        &setupMenuBtn,
        &inchMenuBtn};
// mainMenu UIItems
UIItem *mainMenuUIItems[2] =
    {
        &blankUII,
        &blankUII};
Menu mainMenu(
    mainMenuButtons,
    mainMenuUIItems,
    &lcd);

// 2 - setupMenu ------------------------------------
// setupMenu buttons
char lMotr[] = "Motor";
MenuItem motorMenuBtn(lMotr, []() {
    currentMenu = MOTOR_MENU;
},
                      CLICKABLE);
char lDir[] = "Dir  ";
MenuItem directionMenuBtn(lDir, []() {
    currentMenu = DIRECTION_MENU;
},
                          CLICKABLE);
char lSteps[] = "Shots";
MenuItem stepsMenuBtn(lSteps, []() {
    currentMenu = STEPS_MENU;
},
                      CLICKABLE);

MenuItem *setupMenuButtons[4] =
    {
        &mainMenuBtn,
        &motorMenuBtn,
        &directionMenuBtn,
        &stepsMenuBtn,
};

// setupMenu UIItems
char lset[] = "set  ";
UIItem setupMnuLabel(lset);
UIItem *setupMenuUIItems[2] =
    {
        &setupMnuLabel,
        &blankUII};
Menu setupMenu(
    setupMenuButtons,
    setupMenuUIItems,
    &lcd);

// 3 - directionMenu ------------------------------------
// directionMenu buttons
char lBack[] = "<Back";
MenuItem backSetupBtn(lBack, []() {
    currentMenu = SETUP_MENU;
},
                      CLICKABLE);
char lCW[] = "[<]  ";
MenuItem dirCWBtn(lCW, []() {
    direction = CLOCKWISE;
},
                  CLICKABLE);
char lCCW[] = "  [>]";
MenuItem dirCCBtn(lCCW, []() {
    direction = COUNTERCW;
},
                  CLICKABLE);

MenuItem *directionMenuButtons[4] =
    {
        &backSetupBtn,
        &blankMI,
        &dirCWBtn,
        &dirCCBtn};
// directionMenu UIItems
char ldir[] = " dir ";
UIItem dirMenuLabel(ldir);
char dirIndicatorFrames[2][STRING_LENGTH] = {"<-CW-", "CCW->"};
UIItem directionIndicator(
    dirIndicatorFrames[direction], []() {
        return (dirIndicatorFrames[direction]);
    });
UIItem *directionMenuUIItems[2] = {
    &dirMenuLabel,
    &directionIndicator};
Menu directionMenu(
    directionMenuButtons,
    directionMenuUIItems,
    &lcd);

// 4 - motorMenu ------------------------------------
// motorMenu buttons
char lDelayMnuBtn[] = "Delay";
MenuItem delayMenuBtn(lDelayMnuBtn, []() {
    currentMenu = DELAYS_MENU;
},
                      CLICKABLE);

char lSpeed[] = "Speed";
MenuItem speedMenuBtn(lSpeed, []() {
    currentMenu = SPEED_MENU;
},
                      CLICKABLE);
char lAccelMnuBtn[] = "Accel";
MenuItem accelMenuBtn(lAccelMnuBtn, []() {
    currentMenu = ACCEL_MENU;
},
                      CLICKABLE);

MenuItem *motorMenuButtons[4] =
    {
        &backSetupBtn,
        &delayMenuBtn,
        &speedMenuBtn,
        &accelMenuBtn};
// motorMenu UIItems
char lmotor[] = "mot  ";
UIItem motorMenuLabel(lmotor);
UIItem *motorMenuUIItems[2] = {
    &motorMenuLabel,
    &blankUII};
Menu motorMenu(
    motorMenuButtons,
    motorMenuUIItems,
    &lcd);

// 5 - speed ------------------------------------
// speedMenu buttons
// char lBack[] = "<Back";
MenuItem backMtrBtn(lBack, []() {
    currentMenu = MOTOR_MENU;
},
                    CLICKABLE);
char lMINUS[] = "[-]  ";
void maxSpeedDecrease()
{
    stepsPerSecond = max(MIN_STEPS_PER_SECOND, stepsPerSecond - 1);
    EEPROM_writeInt(STEPSPERSECOND_EEPROM, stepsPerSecond);
}

MenuItem slowDownBtn(lMINUS, maxSpeedDecrease, CLICKABLE, AUTOCLICKABLE);

char lPLUS[] = "  [+]";
void increaseMaxSpeed()
{
    stepsPerSecond = min(MAX_STEPS_PER_SECOND, stepsPerSecond + 1);
    EEPROM_writeInt(STEPSPERSECOND_EEPROM, stepsPerSecond);
}

MenuItem speedUpBtn(lPLUS, increaseMaxSpeed, CLICKABLE, AUTOCLICKABLE);

MenuItem *speedMenuButtons[4] =
    {
        &backMtrBtn,
        &blankMI,
        &slowDownBtn,
        &speedUpBtn};
// speedMenu UIItems
char lspeedmnu[] = "speed";
UIItem speedMenuLabel(lspeedmnu);
// char   lCounter[] = "00000";
char *speedIndicate()
{
    intToDisplayChar(stepsPerSecond, lCounter, RIGHT_JUSTIFY);
    return (lCounter);
}

UIItem speedIndicator(lCounter, (speedIndicate));
UIItem *speedMenuUIItems[2] = {
    &speedMenuLabel,
    &speedIndicator};
Menu speedMenu(
    speedMenuButtons,
    speedMenuUIItems,
    &lcd);

// 6 - accel ------------------------------------
// accelMenu buttons
// char lBack[] = "<Back";
// MenuItem backMtrBtn(lBack, [](){currentMenu = MOTOR_MENU;}, CLICKABLE);
// char lMINUS[] = "[ - ]";
void increaseAccel()
{
    rampSteps = max(rampSteps - 1, 1);
    EEPROM_writeInt(RAMPSTEPS_EEPROM, rampSteps);
}

MenuItem increaseAccelBtn(lMINUS, increaseAccel, CLICKABLE, AUTOCLICKABLE);
// char lPLUS[] = "[ + ]";
void decreaseAccel()
{
    rampSteps++;
    EEPROM_writeInt(RAMPSTEPS_EEPROM, rampSteps);
}

MenuItem decreaseAccelBtn(lPLUS, decreaseAccel, CLICKABLE, AUTOCLICKABLE);

MenuItem *accelMenuButtons[4] =
    {
        &backMtrBtn,
        &blankMI,
        &increaseAccelBtn,
        &decreaseAccelBtn};
// accelMenu UIItems
char laccelmnu[] = "accel";
UIItem accelMenuLabel(laccelmnu);
// char   lCounter[] = "00000";
char *accelIndicate()
{
    intToDisplayChar(rampSteps, lCounter, RIGHT_JUSTIFY);
    return (lCounter);
}

UIItem accelIndicator(lCounter, accelIndicate);
UIItem *accelMenuUIItems[2] = {
    &accelMenuLabel,
    &accelIndicator};
Menu accelMenu(
    accelMenuButtons,
    accelMenuUIItems,
    &lcd);

// 7 - shots ------------------------------------
// shotsMenu buttons
// char lBack[] = "<Back";
// MenuItem backMtrBtn(lBack, [](){currentMenu = MOTOR_MENU;}, CLICKABLE);
// char lMINUS[] = "[ - ]";
void increaseShots()
{
    shotsPerRev = max(shotsPerRev - 1, 1);
    EEPROM_writeInt(SHOTSPERREV_EEPROM, shotsPerRev);
}

MenuItem increaseShotsBtn(lMINUS, increaseShots, CLICKABLE, AUTOCLICKABLE);
// char lPLUS[] = "[ + ]";
void shotsDecrease()
{
    shotsPerRev = min(shotsPerRev + 1, stepsPerRev);
    EEPROM_writeInt(SHOTSPERREV_EEPROM, shotsPerRev);
}

MenuItem shotsDecreaseBtn(lPLUS, shotsDecrease, CLICKABLE, AUTOCLICKABLE);

MenuItem *shotsMenuButtons[4] =
    {
        &backSetupBtn,
        &blankMI,
        &increaseShotsBtn,
        &shotsDecreaseBtn};
// shotsMenu UIItems
char lshotsmnu[] = "shots";
UIItem shotsMenuLabel(lshotsmnu);
// char   lCounter[] = "00000";
UIItem shotsIndicatorUI(lCounter, []() {
    intToDisplayChar(shotsPerRev, lCounter, LEFT_JUSTIFY);
    return (lCounter);
});
UIItem *shotsMenuUIItems[2] = {
    &shotsMenuLabel,
    &shotsIndicatorUI};
Menu shotsMenu(
    shotsMenuButtons,
    shotsMenuUIItems,
    &lcd);

// 8 - manMenu ------------------------------------
// manMenu buttons
void stopMotor()
{
    if (motorSpeed != 0)
    {
        targetSpeed = 0;
        operationState = STOPPING;
    }
    else
    {
        operationState = STOPPED;
    }
}

char lSHOOT[] = "SHOOT";
void manStopStart()
{
    if (motorState)
    {
        stopMotor();
    }
    else
    {
        takeShot();
    };
}

char *updateManStopButton()
{
    return ((motorState) ? lSTOP : lSHOOT);
}

MenuItem stopBtn(lSTOP, manStopStart, updateManStopButton, CLICKABLE);
// if running CW speed up, if CCW slow down, otherwise start running CW
void runCW()
{
    if (motorState)
    // while running the buttons become speedup or slowdown buttons
    {
        if (direction == CLOCKWISE)
        {
            // giddy up
            targetSpeed = min(targetSpeed + 20, maxSpeed);
        }
        else
        // whoah neddy
        {
            targetSpeed = max(targetSpeed - 20, 20);
        }
    }
    else
    {
        direction = CLOCKWISE;
        digitalWrite(DIR_PIN, direction);
        targetSpeed = maxSpeed / 2;
        operationState = RUNNING_MAN;
    }
}

char lPLUSL[] = "[+]  ";
char *updateCWBtn()
{
    if (motorState)
    {
        if (direction == CLOCKWISE)
        {
            return (lPLUSL);
        }
        else
        {
            return (lMINUS);
        }
    }
    else
    {
        return (lCW);
    }
}

MenuItem runCWBtn(lCW, runCW, updateCWBtn, CLICKABLE, AUTOCLICKABLE);

//if running CCW speed up, if CW slow down, otherwise start running CCW
void runCCW()
{
    if (motorState)
    // while running the buttons become speedup or slowdown buttons
    {
        if (direction == COUNTERCW)
        {
            // giddy up
            targetSpeed = min(targetSpeed + 20, maxSpeed);
        }
        else
        // whoah neddy
        {
            targetSpeed = max(targetSpeed - 20, 20);
        }
    }
    else
    {
        direction = COUNTERCW;

        targetSpeed = maxSpeed / 2;
        digitalWrite(DIR_PIN, direction);
        operationState = RUNNING_MAN;
    }
}

char lMINUSR[] = "  [-]";
char *updateCCWBtn()
{
    if (motorState)
    {
        if (direction == COUNTERCW)
        {
            return (lPLUS);
        }
        else
        {
            return (lMINUSR);
        }
    }
    else
    {
        return (lCCW);
    }
}

MenuItem runCCWBtn(lCCW, runCCW, updateCCWBtn, CLICKABLE, AUTOCLICKABLE);
MenuItem *manMenuButtons[4] =
    {
        &mainMenuBtn,
        &stopBtn,
        &runCWBtn,
        &runCCWBtn,
};
// manMenu UIItems
char lmanMnu[] = "man  ";
UIItem manMenuLabel(lmanMnu);
// char motorIndicatorFrames[8][STRING_LENGTH] = { " ||  ", " <+  ", " +>  ", " <=  ", " =>  ", " <-  ", " ->  " };
// UIItem motorStateIndicator(motorIndicatorFrames[0], , []() {return(motorIndicatorFrames[motorState * (1 + direction)]);});
UIItem *manMenuUIItems[2] =
    {
        &manMenuLabel,
        &motorStateIndicator};

Menu manMenu(
    manMenuButtons,
    manMenuUIItems,
    &lcd);

// 9 - delaysMenu ------------------------------------
// delaysMenu buttons
char lPre[] = "  Pre";
MenuItem preshotDelayMenuBtn(lPre, []() {
    currentMenu = PREDELAY_MENU;
},
                             CLICKABLE);
char lFocus[] = "Focus";
MenuItem focusDelayMenuBtn(lFocus, []() {
    currentMenu = FOCUSDELAY_MENU;
},
                           CLICKABLE);
char lShuttr[] = "Shuttr";
MenuItem shuttrDelayMenuBtn(lShuttr, []() {
    currentMenu = SHUTTERDELAY_MENU;
},
                            CLICKABLE);

MenuItem *delaysMenuButtons[4] =
    {
        &backMtrBtn,
        &preshotDelayMenuBtn,
        &focusDelayMenuBtn,
        &shuttrDelayMenuBtn,
};

// delaysMenu UIItems
char ldelay[] = "delay";
UIItem delaysMnuLabel(ldelay);
UIItem *delaysMenuUIItems[2] =
    {
        &delaysMnuLabel,
        &blankUII};
Menu delaysMenu(
    delaysMenuButtons,
    delaysMenuUIItems,
    &lcd);
// 10 - predelay ------------------------------------
void backToDelayMenu()
{
    currentMenu = DELAYS_MENU;
}

MenuItem bactkToDelayMenuBtn(lBack, backToDelayMenu, CLICKABLE);

void decreasePreDelay()
{
    preDelayTime = max(preDelayTime - 100, 0);
    EEPROM_writeInt(PREDELAYTIME_EEPROM, preDelayTime);
}

MenuItem preDelayDecreaseBtn(lMINUS, decreasePreDelay, CLICKABLE, AUTOCLICKABLE);

void increasePreDelay()
{
    preDelayTime += 100;
    EEPROM_writeInt(PREDELAYTIME_EEPROM, preDelayTime);
}

MenuItem preDelayIncreaseBtn(lPLUS, increasePreDelay, CLICKABLE, AUTOCLICKABLE);
char ldelayText[] = "delay";
MenuItem delayLable(ldelayText, []() {
    return (ldelayText);
});
MenuItem *preDelayMenuItems[4] =
    {
        &bactkToDelayMenuBtn,
        &delayLable,
        &preDelayDecreaseBtn,
        &preDelayIncreaseBtn};
// delayMenu UIItems
char lpreDelaymnu[] = "  pre";
UIItem preDelayMenuLabel(lpreDelaymnu);
// char lCounter[] = "00000";
char *indicatePreDelayTime()
{
    intToDisplayChar(preDelayTime, lCounter, LEFT_JUSTIFY);
    return (lCounter);
}

UIItem preDelayIndicator(lCounter, indicatePreDelayTime); //TODO sprintf function
UIItem *preDelayMenuUIItems[2] = {
    &preDelayMenuLabel,
    &preDelayIndicator};
Menu preDelayMenu(
    preDelayMenuItems,
    preDelayMenuUIItems,
    &lcd);

// 11 - focusdelay ------------------------------------
void decreaseFocusDelay()
{
    focusDelayTime = max(focusDelayTime - 100, 0);
    EEPROM_writeInt(FOCUSDELAYTIME_EEPROM, focusDelayTime);
}

MenuItem focusDelayDecreaseBtn(lMINUS, decreaseFocusDelay, CLICKABLE, AUTOCLICKABLE);

void increaseFocusDelay()
{
    focusDelayTime += 100;
    EEPROM_writeInt(FOCUSDELAYTIME_EEPROM, focusDelayTime);
}

MenuItem focusDelayIncreaseBtn(lPLUS, increaseFocusDelay, CLICKABLE, AUTOCLICKABLE);
MenuItem *focusDelayMenuItems[4] =
    {
        &bactkToDelayMenuBtn,
        &delayLable,
        &focusDelayDecreaseBtn,
        &focusDelayIncreaseBtn};
// delayMenu UIItems
char lfocusDelayAF[] = "AF   ";
char lfocusDelayMF[] = "MF   ";
char *switchAFMF()
{
    return ((focusDelayTime > 0) ? lfocusDelayAF : lfocusDelayMF);
}

UIItem focusDelayMenuLabel(lfocusDelayAF, switchAFMF);
char *indicateFocusDelayTime()
{
    intToDisplayChar(focusDelayTime, lCounter, LEFT_JUSTIFY);
    return (lCounter);
}

UIItem focusDelayIndicator(lCounter, indicateFocusDelayTime); //TODO sprintf function
UIItem *focusDelayMenuUIItems[2] = {
    &focusDelayMenuLabel,
    &focusDelayIndicator};
Menu focusDelayMenu(
    focusDelayMenuItems,
    focusDelayMenuUIItems,
    &lcd);

// 12 shutterDelay ------------------------------------
void decreaseShutterDelay()
{
    shutterDelayTime = max(shutterDelayTime - 100, 0);
    EEPROM_writeInt(SHUTTERDELAYTIME_EEPROM, shutterDelayTime);
}

MenuItem shutterDelayDecreaseBtn(lMINUS, decreaseShutterDelay, CLICKABLE, AUTOCLICKABLE);

void increaseShutterDelay()
{
    shutterDelayTime += 100;
    EEPROM_writeInt(SHUTTERDELAYTIME_EEPROM, shutterDelayTime);
}

MenuItem shutterDelayIncreaseBtn(lPLUS, increaseShutterDelay, CLICKABLE, AUTOCLICKABLE);
MenuItem *shutterDelayMenuItems[4] =
    {
        &bactkToDelayMenuBtn,
        &delayLable,
        &shutterDelayDecreaseBtn,
        &shutterDelayIncreaseBtn};
// delayMenu UIItems
char lshutterDelaymnu[] = "shutr";
UIItem shutterDelayMenuLabel(lshutterDelaymnu);
char *indicateShutterDelayTime()
{
    intToDisplayChar(shutterDelayTime, lCounter, LEFT_JUSTIFY);
    return (lCounter);
}

UIItem shutterDelayIndicator(lCounter, indicateShutterDelayTime); //TODO sprintf function
UIItem *shutterDelayMenuUIItems[2] = {
    &shutterDelayMenuLabel,
    &shutterDelayIndicator};
Menu shutterDelayMenu(
    shutterDelayMenuItems,
    shutterDelayMenuUIItems,
    &lcd);

// 13 inch----------------------------------------------------------------
void startInchCW()
{
    digitalWrite(DIR_PIN, CLOCKWISE);
    digitalWrite(ENABLE_PIN, ENABLE);
    // Serial.println("ENABLE");
    operationState = INCHING_CW;
    // Serial.println("Started inching");
}

void startInchCounterCW()
{
    digitalWrite(DIR_PIN, COUNTERCW);
    digitalWrite(ENABLE_PIN, ENABLE);
    // Serial.println("ENABLE");
    operationState = INCHING_CCW;
    // Serial.println("Started inching");
}
char lIinchCW[] = "-<-  ";
char *updateInchCW()
{
    if (buttonJustReleased)
    {
        operationState = STOPPED;
        digitalWrite(ENABLE_PIN, DISABLE);
        // Serial.println("DISABLE3");
        // Serial.println("Stopped inching");
        return (lCW);
    }
    else if (operationState == INCHING_CW)
    {
        return (lIinchCW);
    }
    return (lCW);
}

char lInchCCW[] = "  ->-";
char *updateInchCCW()
{
    if (buttonJustReleased)
    {
        operationState = STOPPED;
        digitalWrite(ENABLE_PIN, DISABLE);
        // Serial.println("DISABLE4");
        // Serial.println("Stopped inching");
        return (lCCW);
    }
    else if (operationState == INCHING_CCW)
    {
        return (lInchCCW);
    }
    return (lCCW);
}

MenuItem inchCWBtn(lCW, startInchCW, updateInchCW, CLICKABLE);
MenuItem inchCCWBtn(lCCW, startInchCounterCW, updateInchCCW, CLICKABLE);
MenuItem *inchMenuButtons[4] = {
    &mainMenuBtn,
    &blankMI,
    &inchCWBtn,
    &inchCCWBtn};
char lInchText[] = "inch ";
UIItem inchMenuLabel(lInchText);
UIItem *inchMenuLabels[2] = {
    &inchMenuLabel,
    &blankUII};
Menu inchMenu(
    inchMenuButtons,
    inchMenuLabels,
    &lcd);

// --------------------------------------------------------------------------------------------------------------------------
Menu *allTheMenus[] = {
    &runMenu,          //0
    &mainMenu,         //1
    &setupMenu,        //2
    &directionMenu,    //3
    &motorMenu,        //4
    &speedMenu,        //5
    &accelMenu,        //6
    &shotsMenu,        //7
    &manMenu,          //8
    &delaysMenu,       //9
    &preDelayMenu,     //10
    &focusDelayMenu,   //11
    &shutterDelayMenu, //12
    &inchMenu          //13
};

void setup()
{
    // Serial.begin(9600);
    // pin assignments
    pinMode(BUTTON_ADC_PIN, INPUT);    //ensure A0 is an input
    digitalWrite(BUTTON_ADC_PIN, LOW); //ensure pullup is off on A0
    //lcd backlight control
    digitalWrite(LCD_BACKLIGHT_PIN, HIGH); //backlight control pin D10 is high (on)
    pinMode(LCD_BACKLIGHT_PIN, OUTPUT);    //D10 is an output
    //set up the LCD number of columns and rows:
    //button adc input
    pinMode(FOCUS_PIN, OUTPUT);
    pinMode(SHUTTER_PIN, OUTPUT);

    lcd.begin(16, 2);

    for (byte i = 16; i > 0; i--)
    {
        lcd.setCursor(i - 1, 0);
        lcd.print("ROTATATRON 2000 ");
        delay(200);
    }

    // read stuff from eeprom
    byte okToReadEeprom;
    okToReadEeprom = EEPROM.read(OKTOREAD_EEPROM);
    // Serial.println(okToReadEeprom);
    button = readButtons();
    // Serial.begin(9600);
    // Serial.println(button);
    if (button != BUTTON_NONE)
    {
        okToReadEeprom = 0;
        lcd.setCursor(0, 1);
        lcd.print("Default Settings");
    }

    if (okToReadEeprom == 1)
    {
        lcd.setCursor(0, 1);
        lcd.print("Reading EEPROM");
        stepsPerSecond = EEPROM_readInt(STEPSPERSECOND_EEPROM); //int
        preDelayTime = EEPROM_readInt(PREDELAYTIME_EEPROM);
        focusDelayTime = EEPROM_readInt(FOCUSDELAYTIME_EEPROM);
        shutterDelayTime = EEPROM_readInt(SHUTTERDELAYTIME_EEPROM);
        shotsPerRev = EEPROM_readInt(SHOTSPERREV_EEPROM);
        rampSteps = EEPROM_readInt(RAMPSTEPS_EEPROM);
    }
    else //initialise the eeprom
    {
        EEPROM_writeInt(STEPSPERSECOND_EEPROM, stepsPerSecond);     //int
        EEPROM_writeInt(PREDELAYTIME_EEPROM, preDelayTime);         //3000;
        EEPROM_writeInt(FOCUSDELAYTIME_EEPROM, focusDelayTime);     //1000;
        EEPROM_writeInt(SHUTTERDELAYTIME_EEPROM, shutterDelayTime); //1000;
        EEPROM_writeInt(SHOTSPERREV_EEPROM, shotsPerRev);           //12;
        rampSteps = stepsPerRev / (shotsPerRev * 3);
        EEPROM_writeInt(RAMPSTEPS_EEPROM, rampSteps);
        okToReadEeprom = 1;
        EEPROM.write(OKTOREAD_EEPROM, okToReadEeprom);
    }
    delay(1000);
    accelerationIncrement = maxSpeed / rampSteps;
    maxInterruptsPerStep = 1 / (MIN_STEPS_PER_SECOND * 2 * interruptDuration); // 8 steps/second.
    minInterruptsPerStep = 1 / (stepsPerSecond * 2 * interruptDuration);       // 500 steps/second.
    stepDiff = maxInterruptsPerStep - minInterruptsPerStep;
    // motor contreol stuff
    motorSpeed = 0;
    leadingEdge = true;
    stepNumber = 0;
    currentShot = 0;
    stepTarget = stepsPerRev / shotsPerRev;
    setupTimer();
    digitalWrite(ENABLE_PIN, DISABLE);
    // Serial.println("DISABLE1");
    delay(2000);
    lcd.clear();
}

void loop()
{
    // do some calculations
    maxInterruptsPerStep = 1 / (MIN_STEPS_PER_SECOND * 2 * interruptDuration); // 8 steps/second.
    minInterruptsPerStep = 1 / (stepsPerSecond * 2 * interruptDuration);       // 500 steps/second.
    stepDiff = maxInterruptsPerStep - minInterruptsPerStep;
    accelerationIncrement = maxSpeed / rampSteps;

    // display the current menu
    allTheMenus[currentMenu]->display();
    button = readButtons();
    if (buttonJustPressed)
    {
        allTheMenus[currentMenu]->click(button);
        // Serial.println(button);
    }
    if (buttonAutoClicked)
    {
        allTheMenus[currentMenu]->autoclick(button);
    }

    if (motorSpeed > 0 || targetSpeed > 0)
    {
        digitalWrite(ENABLE_PIN, ENABLE);
        if (motorSpeed < targetSpeed)
        {
            motorState = ACCELERATING;
        }
        else if (motorState > targetSpeed)
        {
            motorState = DECELERATING;
        }
        else
        {
            motorState = RUNNING;
        }
    }
    else
    {
        // STOPPED
        motorState = STOPPED;
    }
    if (operationState == RUNNING_AUTO)
    {
        if (stepNumber == stepTarget)
        {
            targetSpeed = 0.0;
            if (currentShot < shotsPerRev)
            {
                stepTarget = stepsPerRev / shotsPerRev;
                stepNumber = 0;
                // take a shot
                takeShot();
                targetSpeed = maxSpeed;
            }
            else
            {
                operationState = STOPPED;
            }
        }
    }
    if ((operationState == STOPPING || operationState == STOPPED) && motorSpeed == 0 && targetSpeed == 0)
    {
        operationState = STOPPED;
        digitalWrite(ENABLE_PIN, DISABLE);
    }
}

// timer interrupt handler - where the business logic happens
ISR(TIMER1_COMPA_vect)
{
    if (targetSpeed > 0 || motorSpeed > 0)
    // running, or should be running
    {
        // leading edge of the step cycle
        if (leadingEdge)
        {
            // turn the step pin HIGH
            digitalWrite(STEP_PIN, HIGH);
            // we've taken a new step
            stepNumber++;

            if ((stepNumber >= stepTarget - rampSteps) && (operationState == RUNNING_AUTO))
            {
                targetSpeed = 0;
                // make sure we don't overshoot - shouldn't happen
                if (stepNumber >= stepTarget)
                {
                    motorSpeed = 0;
                    targetSpeed = 0;
                }
            }

            // check speed and adjust if necesary
            if (motorSpeed < targetSpeed)
            { // add to the motor speed up to targetSpeed
                motorSpeed = min(motorSpeed + accelerationIncrement, targetSpeed);
            }
            else if (motorSpeed > targetSpeed)
            { //subtract from motorSpeed down to targetSpeed
                motorSpeed = max(motorSpeed - accelerationIncrement, targetSpeed);
            }
            // Serial.println("Leading");
            leadingEdge = false;
        }
        // trailing edge
        else
        {
            digitalWrite(STEP_PIN, LOW);
            // Serial.println("Trailing");
            leadingEdge = true;
        }
    }

    if (operationState == INCHING_CW || operationState == INCHING_CCW)
    {
        OCR1A = maxInterruptsPerStep;
        if (leadingEdge)
        {
            digitalWrite(STEP_PIN, HIGH);
            // Serial.println("Leading");
        }
        else
        {
            digitalWrite(STEP_PIN, LOW);
            // Serial.println("Trailing");
        }
        leadingEdge = !leadingEdge;
    }
    else
    {
        relativeSpeed = float(motorSpeed) / float(maxSpeed);
        // OCR1A = minInterruptsPerStep * (pow(float(motorSpeed) / float(maxSpeed), RAMP_POWER)) + (pow(1.0 - float(motorSpeed) / float(maxSpeed), RAMP_POWER)) * maxInterruptsPerStep; //stepDiff = maxInterruptsPerStep - minInterruptsPerStep
        OCR1A = minInterruptsPerStep * relativeSpeed + (1.0 - relativeSpeed) * maxInterruptsPerStep; //stepDiff = maxInterruptsPerStep - minInterruptsPerStep
    }
}

void manRun()
{
    targetSpeed = maxSpeed;
    operationState = RUNNING_MAN;
}

void takeShot()
{
    takingShot = true;
    allTheMenus[currentMenu]->display();
    delay(preDelayTime);
    if (focusDelayTime > 0)
    {
        digitalWrite(FOCUS_PIN, HIGH);
        delay(focusDelayTime);
    }
    digitalWrite(SHUTTER_PIN, HIGH);
    delay(shutterDelayTime);
    digitalWrite(SHUTTER_PIN, LOW);
    digitalWrite(FOCUS_PIN, LOW);
    currentShot++;
    takingShot = false;
}

byte readButtons()
{
    unsigned int buttonVoltage;
    unsigned int longPressCount;
    byte button = BUTTON_NONE; // return no button pressed if the below checks don't write to btn

    buttonJustPressed = false;
    buttonJustReleased = false;
    buttonAutoClicked = false;
    //read the button ADC pin voltage
    buttonVoltage = analogRead(BUTTON_ADC_PIN);
    //sense if the voltage falls within valid voltage windows
    if (buttonVoltage < (RIGHT_10BIT_ADC + BUTTONHYSTERESIS))
    {
        button = BUTTON_BOTTOMRIGHT;
    }
    else if (buttonVoltage >= (UP_10BIT_ADC - BUTTONHYSTERESIS) &&
             buttonVoltage <= (UP_10BIT_ADC + BUTTONHYSTERESIS))
    {
        button = BUTTON_BOTTOMLEFT;
    }
    else if (buttonVoltage >= (DOWN_10BIT_ADC - BUTTONHYSTERESIS) &&
             buttonVoltage <= (DOWN_10BIT_ADC + BUTTONHYSTERESIS))
    {
        button = BUTTON_TOPRIGHT;
    }
    else if (buttonVoltage >= (LEFT_10BIT_ADC - BUTTONHYSTERESIS) &&
             buttonVoltage <= (LEFT_10BIT_ADC + BUTTONHYSTERESIS))
    {
        button = BUTTON_TOPLEFT;
    }
    else if (buttonVoltage >= (SELECT_10BIT_ADC - BUTTONHYSTERESIS) &&
             buttonVoltage <= (SELECT_10BIT_ADC + BUTTONHYSTERESIS))
    {
        button = BUTTON_SELECT;
    }
    //handle button flags for just pressed and just released events
    if (button != BUTTON_NONE)
    // button is pressed
    {
        // button was not pressed before
        if ((buttonWas == BUTTON_NONE))
        {
            buttonJustPressed = true;
            btnPressMillis = millis(); // reset button hold counter
        }
        // button being held down
        else if (buttonWas == button)
        {
            btnHoldTime = millis() - btnPressMillis;
            if (btnHoldTime > BUTTON_LONG_PRESS)
            {
                longPressCount = min(btnHoldTime / BUTTON_LONG_PRESS, 10);
                if (millis() - lastAutoPressMs > BUTTON_REPEAT_TIME / longPressCount)
                {
                    buttonAutoClicked = true;
                    lastAutoPressMs = millis();
                }
            }
        }
    }
    // no button pressed
    else if (buttonWas != BUTTON_NONE)
    {
        buttonJustReleased = true;
    }
    buttonWas = button;
    return (button);
}

void setupTimer()
{
    // setup timer interrupt
    cli();      //disable global interrupts
    TCCR1A = 0; //set tccr1a and tccr1b to 0
    TCCR1B = 0;
    // TCCR1B cs12, cs11, cs10 = 0, 0, 1 -> no prescaler
    // TCCR1B cs12, cs11, cs10 = 0, 1, 0 -> prescaler = clock / 8
    // TCCR1B cs12, cs11, cs10 = 0, 1, 1 -> prescaler = clock / 64
    // TCCR1B cs12, cs11, cs10 = 1, 0, 0 -> prescaler = clock / 256
    // TCCR1B cs12, cs11, cs10 = 1, 0, 1 -> prescaler = clock / 1024

    //set TCCR1B bits
    TCCR1B |= (1 << CS10);
    //  TCCR1B |= (1 << CS11);
    TCCR1B |= (1 << CS12);

    // Compare Match Mode
    //set compare match register
    // OCR1A is the value the compare match register counts to
    OCR1A = maxInterruptsPerStep;
    // The step loop divides it by 2
    // turn on CTC mode
    TCCR1B |= (1 << WGM12);
    // enable Timer1 compare interrupt
    TIMSK1 = (1 << OCIE2A);

    //enable global interrupt
    sei();
}

//
// void lcdWrite(int x, int y, String msg)
// {
//     lcd.setCursor(x, y);
//     lcd.print(msg);
// }
