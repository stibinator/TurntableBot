#include "UIItem.h"
#include "MenuItem.h"
#include "Menu.h"

#include <LiquidCrystal.h>
#include <avr/interrupt.h>
#include <avr/io.h>

// Arduino defines
#define STEP_CLOCKWISE       HIGH   // the value to set the dir_pin to when changing direction
#define STEP_REVERSE         LOW
#define DIR_PIN              2
#define STEP_PIN             3
#define FOCUS_PIN            11
#define SHUTTER_PIN          12
#define ENABLE_PIN           13 //goes LOW to enable

#define BUTTON_ADC_PIN       A0     // A0 is the button ADC input
#define LCD_BACKLIGHT_PIN    10     // D10 controls LCD backlight
// ADC readings expected for the 5 buttons on the ADC input
#define RIGHT_10BIT_ADC      0      // right
#define UP_10BIT_ADC         100    // up
#define DOWN_10BIT_ADC       256    // down
#define LEFT_10BIT_ADC       408    // left
#define SELECT_10BIT_ADC     640    // right
#define BUTTONHYSTERESIS     40     // hysteresis for valid button sensing window

// motorState constants
#define STOPPED              0 //also used as operation state
#define ACCELERATING         1
#define RUNNING              2
#define DECELERATING         3

// operation state constants
// #define STOPPED              0 //defined already
#define RUNNING_AUTO        1
#define RUNNING_MAN         2

#define STEPS_PER_DEGREE    273.6   // 76 * 360 / 100
// #define STEPS_PER_REV         99468

//return values for readButtons()
#define BUTTON_TOPLEFT        3     //
#define BUTTON_TOPRIGHT       2     //
#define BUTTON_BOTTOMLEFT     1     //
#define BUTTON_BOTTOMRIGHT    0    //
#define BUTTON_SELECT         4     //
#define BUTTON_NONE           5     //
//some example macros with friendly labels for LCD backlight/pin control, tested and can be swapped into the example code as you like
#define LCD_BACKLIGHT_OFF()     digitalWrite(LCD_BACKLIGHT_PIN, LOW)
#define LCD_BACKLIGHT_ON()      digitalWrite(LCD_BACKLIGHT_PIN, HIGH)
#define LCD_BACKLIGHT(state)    { if (state) { digitalWrite(LCD_BACKLIGHT_PIN, HIGH); }else{ digitalWrite(LCD_BACKLIGHT_PIN, LOW); } }
// directions
#define CLOCKWISE               0
#define COUNTERCW               1
// Menus
#define RUN_MENU                0
#define MAIN_MENU               1
#define SETUP_MENU              2
#define DIRECTION_MENU          3
#define MOTOR_MENU              4
#define SPEED_MENU              5
#define ACCEL_MENU              6
#define STEPS_MENU              7
#define MANUAL_MENU             8
#define DELAYS_MENU             9
#define PREDELAY_MENU           10
#define FOCUSDELAY_MENU         11
#define SHUTTERDELAY_MENU       12


#define CLICKABLE               true
#define AUTOCLICKABLE           true
#define LEFT_JUSTIFY            true
#define RIGHT_JUSTIFY           false
#define MAX_STEPS_PER_SECOND    600
#define MIN_STEPS_PER_SECOND    8

#define BUTTON_LONG_PRESS       1000
#define BUTTON_REPEAT_TIME      1000

#define interruptDuration       0.000064

/*--------------------------------------------------------------------------------------
 *  Variables
 *  --------------------------------------------------------------------------------------*/
bool          buttonJustPressed  = false;                                               //this will be true after a readButtons() call if triggered
bool          buttonJustReleased = false;                                               //this will be true after a readButtons() call if triggered
bool          buttonAutoClicked  = false;
byte          buttonWas          = BUTTON_NONE;                                         //used by readButtons() for detection of button events
byte          button             = BUTTON_NONE;
unsigned long btnPressMillis     = 0;                                                   //time the last button was pressed
unsigned long btnHoldTime        = 0;                                                   //how long the button has been held for
unsigned long lastAutoPressMs    = 0;                                                   // counts untill next auto click
volatile int  motorSpeed         = 0;                                                   //normalised speed motor is currently at, goes from 0→1
volatile int  targetSpeed        = 0;                                                   //normalised speed motor is aiming for, goes from 0→1
volatile int  maxSpeed           = 10000;                                               //TODO check floats
// When interuptLoopCount == 1 interrupt every ≈ 64 µs. There are two interrupts per step
int          stepsPerSecond       = 500;                                                //the max speed of the stepper
int          maxInterruptsPerStep = 1 / (MIN_STEPS_PER_SECOND * 2 * interruptDuration); // 8 steps/second.
volatile int minInterruptsPerStep = 1 / (stepsPerSecond * 2 * interruptDuration);       // 500 steps/second.
int          rampSteps            = 1000;                                               // number of steps needed to get to full speed
//reduce the number of calculations we need to do in the interrupt handler
volatile int          accelerationIncrement = maxSpeed / rampSteps;
volatile int          stepDiff       = maxInterruptsPerStep - minInterruptsPerStep;
volatile byte         motorState     = STOPPED;
volatile byte         operationState = STOPPED;
bool                  takingShot     = false;
volatile unsigned int stepDirection;     //HIGH and LOW are u_ints
volatile int          stepNumber;
volatile int          stepTarget;
volatile bool         leadingEdge;
// auto run parameters
volatile const unsigned long stepsPerRev = 99468;
unsigned long shotsPerRev      = 12;
unsigned long currentShot      = 0;
int           preDelayTime     = 3000;
int           focusDelayTime   = 1000;
int           shutterDelayTime = 1000;
//--------------------------------------------------------------------------------------
// Init the LCD library with the LCD pins to be used
LiquidCrystal lcd(8, 9, 4, 5, 6, 7);     //Pins for the freetronics 16x2 LCD shield.
//--------------------------------------------------------------------------------------

byte direction   = CLOCKWISE;
byte currentMenu = RUN_MENU;

void toggleAutoRun()
{
    if (operationState != RUNNING_AUTO)
    {
        operationState = RUNNING_AUTO;
        stepTarget     = stepsPerRev / shotsPerRev;
        stepNumber     = 0;
        currentShot    = 0;
        targetSpeed    = maxSpeed;
    }
    else
    {
        targetSpeed    = 0;
        operationState = STOPPED;
    }
}

void stopMotor()
{
    if (motorSpeed != 0)
    {
        targetSpeed = 0;
    }
}

void intToDisplayChar(int theInt, char theChars[STRING_LENGTH], bool justify)
{
    String s = String(theInt);
    byte   n = min(s.length(), STRING_LENGTH);

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
UIItem   blankUII;
// 0 - runMenu ------------------------------------
// runMenu buttons
void gotoMainMenuIfNotRunning()
{
    if (operationState == STOPPED)
    {
        currentMenu = MAIN_MENU;
    }
}

char lMenu[]     = "Menu ";
char greyedOut[] = "     ";
char *updateMainMenuButton()
// grey out the menu button if the motor is running
{
    return((operationState == STOPPED) ? lMenu : greyedOut);
}

MenuItem mainMenuBtn(lMenu, gotoMainMenuIfNotRunning, updateMainMenuButton, CLICKABLE);

char lSTART[] = "START";
char lSTOP[]  = " STOP";
char *updateStartBtn()
{
    return((operationState == STOPPED) ? lSTART : lSTOP);
}

MenuItem startBtn(lSTART, toggleAutoRun, updateStartBtn, CLICKABLE);
char     lShotCount[] = "    0";
char *countShots()
{
    intToDisplayChar(currentShot, lShotCount, RIGHT_JUSTIFY);
    return(lShotCount);
}

MenuItem shotCounter(lShotCount, countShots);
char     lCounter[] = "00000";
char *shotsIndicate()
{
    intToDisplayChar(shotsPerRev, lCounter, LEFT_JUSTIFY);
    return(lCounter);
}

MenuItem  shotsIndicator(lCounter, shotsIndicate);
MenuItem *runMenuButtons[4] =
{
    &mainMenuBtn,
    &startBtn,
    &shotCounter,
    &shotsIndicator
};
// runMenu UIItems
char motorIndicatorFrames[9][STRING_LENGTH] = { "<||  ", "<-+- ", "<-=- ", "<-|- ", " ||> ", "-+-> ", "-=-> ", "-|-> ", ">[o]<" };
char *motorIndicate()
{
    if (takingShot)
    {
        return(motorIndicatorFrames[8]);
    }
    else
    {
        return(motorIndicatorFrames[motorState + (4 * direction)]);
    }
}

UIItem  motorStateIndicator(motorIndicatorFrames[0], motorIndicate);
char    lSlash[] = "  /  ";
UIItem  slash(lSlash);
UIItem *runMenuUIItems[2] =
{
    &motorStateIndicator,
    &slash
};
Menu runMenu
(
    runMenuButtons,
    runMenuUIItems,
    &lcd
);

// 1 - mainMenu ------------------------------------
// mainMenu buttons
char     lAuto[] = "Auto ";
MenuItem runMenuBtn(lAuto, []() {
                    currentMenu = RUN_MENU;
    }, CLICKABLE);
char     lMan[] = "  Man";
MenuItem manualMenuBtn(lMan, []() {
                       currentMenu = MANUAL_MENU;
    }, CLICKABLE);
char     lSetup[] = "Setup";
MenuItem setupMenuBtn(lSetup, []() {
                      currentMenu = SETUP_MENU;
    }, CLICKABLE);

MenuItem *mainMenuButtons[4] =
{
    &runMenuBtn,
    &manualMenuBtn,
    &setupMenuBtn,
    &blankMI
};
// mainMenu UIItems
UIItem *mainMenuUIItems[2] =
{
    &blankUII,
    &blankUII
};
Menu mainMenu
(
    mainMenuButtons,
    mainMenuUIItems,
    &lcd
);

// 2 - setupMenu ------------------------------------
// setupMenu buttons
char     lMotr[] = "Motor";
MenuItem motorMenuBtn(lMotr, []() {
                      currentMenu = MOTOR_MENU;
    }, CLICKABLE);
char     lDir[] = "Dir  ";
MenuItem directionMenuBtn(lDir, []() {
                          currentMenu = DIRECTION_MENU;
    }, CLICKABLE);
char     lSteps[] = "Shots";
MenuItem stepsMenuBtn(lSteps, []() {
                      currentMenu = STEPS_MENU;
    }, CLICKABLE);

MenuItem *setupMenuButtons[4] =
{
    &mainMenuBtn,
    &motorMenuBtn,
    &directionMenuBtn,
    &stepsMenuBtn,
};

// setupMenu UIItems
char    lset[] = "set  ";
UIItem  setupMnuLabel(lset);
UIItem *setupMenuUIItems[2] =
{
    &setupMnuLabel,
    &blankUII
};
Menu setupMenu
(
    setupMenuButtons,
    setupMenuUIItems,
    &lcd
);

// 3 - directionMenu ------------------------------------
// directionMenu buttons
char     lBack[] = "<Back";
MenuItem backSetupBtn(lBack, []() {
                      currentMenu = SETUP_MENU;
    }, CLICKABLE);
char     lCW[] = "[<]  ";
MenuItem dirCWBtn(lCW, [] () {
                  direction = CLOCKWISE;
    }, CLICKABLE);
char     lCCW[] = "  [>]";
MenuItem dirCCBtn(lCCW, [] () {
                  direction = COUNTERCW;
    }, CLICKABLE);

MenuItem *directionMenuButtons[4] =
{
    &backSetupBtn,
    &blankMI,
    &dirCWBtn,
    &dirCCBtn
};
// directionMenu UIItems
char   ldir[] = " dir ";
UIItem dirMenuLabel(ldir);
char   dirIndicatorFrames[2][STRING_LENGTH] = { "<-CW-", "CCW->" };
UIItem directionIndicator(
    dirIndicatorFrames[direction], [] () {
    return(dirIndicatorFrames[direction]);
    });
UIItem *directionMenuUIItems[2] = {
    &dirMenuLabel,
    &directionIndicator
};
Menu    directionMenu
(
    directionMenuButtons,
    directionMenuUIItems,
    &lcd
);

// 4 - motorMenu ------------------------------------
// motorMenu buttons
char     lDelayMnuBtn[] = "Delay";
MenuItem delayMenuBtn(lDelayMnuBtn, []() {
                      currentMenu = DELAYS_MENU;
    }, CLICKABLE);

char     lSpeed[] = "Speed";
MenuItem speedMenuBtn(lSpeed, []() {
                      currentMenu = SPEED_MENU;
    }, CLICKABLE);
char     lAccelMnuBtn[] = "Accel";
MenuItem accelMenuBtn(lAccelMnuBtn, []() {
                      currentMenu = ACCEL_MENU;
    }, CLICKABLE);

MenuItem *motorMenuButtons[4] =
{
    &backSetupBtn,
    &delayMenuBtn,
    &speedMenuBtn,
    &accelMenuBtn
};
// motorMenu UIItems
char    lmotor[] = "motor";
UIItem  motorMenuLabel(lmotor);
UIItem *motorMenuUIItems[2] = {
    &motorMenuLabel,
    &blankUII
};
Menu    motorMenu
(
    motorMenuButtons,
    motorMenuUIItems,
    &lcd
);

// 5 - speed ------------------------------------
// speedMenu buttons
// char lBack[] = "<Back";
MenuItem backMtrBtn(lBack, []() {
                    currentMenu = MOTOR_MENU;
    }, CLICKABLE);
char lMINUS[] = "[-]  ";
void maxSpeedDecrease()
{
    stepsPerSecond = max(MIN_STEPS_PER_SECOND, stepsPerSecond - 1);
}

MenuItem slowDownBtn(lMINUS, maxSpeedDecrease, CLICKABLE, AUTOCLICKABLE);

char lPLUS[] = "  [+]";
void maxSpeedIncrease()
{
    stepsPerSecond = min(MAX_STEPS_PER_SECOND, stepsPerSecond + 1);
}

MenuItem speedUpBtn(lPLUS, maxSpeedIncrease, CLICKABLE, AUTOCLICKABLE);

MenuItem *speedMenuButtons[4] =
{
    &backMtrBtn,
    &blankMI,
    &slowDownBtn,
    &speedUpBtn
};
// speedMenu UIItems
char   lspeedmnu[] = "speed";
UIItem speedMenuLabel(lspeedmnu);
// char   lCounter[] = "00000";
char *speedIndicate()
{
    intToDisplayChar(stepsPerSecond, lCounter, RIGHT_JUSTIFY);
    return(lCounter);
}

UIItem  speedIndicator(lCounter, (speedIndicate));
UIItem *speedMenuUIItems[2] = {
    &speedMenuLabel,
    &speedIndicator
};
Menu    speedMenu
(
    speedMenuButtons,
    speedMenuUIItems,
    &lcd
);

// 6 - accel ------------------------------------
// accelMenu buttons
// char lBack[] = "<Back";
// MenuItem backMtrBtn(lBack, [](){currentMenu = MOTOR_MENU;}, CLICKABLE);
// char lMINUS[] = "[ - ]";
void accelIncrease()
{
    rampSteps = max(rampSteps - 1, 1);
}

MenuItem accelIncreaseBtn(lMINUS, accelIncrease, CLICKABLE, AUTOCLICKABLE);
// char lPLUS[] = "[ + ]";
void accelDecrease()
{
    rampSteps++;
}

MenuItem accelDecreaseBtn(lPLUS, accelDecrease, CLICKABLE, AUTOCLICKABLE);

MenuItem *accelMenuButtons[4] =
{
    &backMtrBtn,
    &blankMI,
    &accelIncreaseBtn,
    &accelDecreaseBtn
};
// accelMenu UIItems
char   laccelmnu[] = "accel";
UIItem accelMenuLabel(laccelmnu);
// char   lCounter[] = "00000";
char *accelIndicate()
{
    intToDisplayChar(rampSteps, lCounter, RIGHT_JUSTIFY);
    return(lCounter);
}

UIItem  accelIndicator(lCounter, accelIndicate);
UIItem *accelMenuUIItems[2] = {
    &accelMenuLabel,
    &accelIndicator
};
Menu    accelMenu
(
    accelMenuButtons,
    accelMenuUIItems,
    &lcd
);

// 7 - shots ------------------------------------
// shotsMenu buttons
// char lBack[] = "<Back";
// MenuItem backMtrBtn(lBack, [](){currentMenu = MOTOR_MENU;}, CLICKABLE);
// char lMINUS[] = "[ - ]";
void shotsIncrease()
{
    shotsPerRev = max(shotsPerRev - 1, 1);
}

MenuItem shotsIncreaseBtn(lMINUS, shotsIncrease, CLICKABLE, AUTOCLICKABLE);
// char lPLUS[] = "[ + ]";
void shotsDecrease()
{
    shotsPerRev = min(shotsPerRev + 1, stepsPerRev);
}

MenuItem shotsDecreaseBtn(lPLUS, shotsDecrease, CLICKABLE, AUTOCLICKABLE);

MenuItem *shotsMenuButtons[4] =
{
    &backSetupBtn,
    &blankMI,
    &shotsIncreaseBtn,
    &shotsDecreaseBtn
};
// shotsMenu UIItems
char   lshotsmnu[] = "shots";
UIItem shotsMenuLabel(lshotsmnu);
// char   lCounter[] = "00000";
UIItem shotsIndicatorUI(lCounter, [] () {
                        intToDisplayChar(shotsPerRev, lCounter, LEFT_JUSTIFY);
                        return(lCounter);
    });                                                   //TODO sprintf function
UIItem *shotsMenuUIItems[2] = {
    &shotsMenuLabel,
    &shotsIndicatorUI
};
Menu    shotsMenu
(
    shotsMenuButtons,
    shotsMenuUIItems,
    &lcd
);

// 8 - manMenu ------------------------------------
// manMenu buttons

char lSHOOT[] = "SHOOT";
void manStopStart()
{
    if (motorState)
    {
        stopMotor();
        operationState = STOPPED;
    }
    else
    {
        takeShot();
    };
}

char *updateManStopButton()
{
    return((motorState) ? lSTOP : lSHOOT);
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
            Serial.println(String(targetSpeed) + String(motorSpeed));
        }
        else
        // whoah neddy
        {
            targetSpeed = max(targetSpeed - 20, 20);
            Serial.println(String(targetSpeed) + String(motorSpeed));
        }
    }
    else
    {
        direction = CLOCKWISE;
        digitalWrite(DIR_PIN, direction);
        targetSpeed    = maxSpeed / 2;
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
            return(lPLUSL);
        }
        else
        {
            return(lMINUS);
        }
    }
    else
    {
        return(lCW);
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
            Serial.println(String(targetSpeed) + " -> " + String(motorSpeed));
        }
        else
        // whoah neddy
        {
            targetSpeed = max(targetSpeed - 20, 20);
            Serial.println(String(targetSpeed) + " -> " + String(motorSpeed));
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
            return(lPLUS);
        }
        else
        {
            return(lMINUSR);
        }
    }
    else
    {
        return(lCCW);
    }
}

MenuItem  runCCWBtn(lCCW, runCCW, updateCCWBtn, CLICKABLE, AUTOCLICKABLE);
MenuItem *manMenuButtons[4] =
{
    &mainMenuBtn,
    &stopBtn,
    &runCWBtn,
    &runCCWBtn,
};
// manMenu UIItems
char   lmanMnu[] = "man  ";
UIItem manMenuLabel(lmanMnu);
// char motorIndicatorFrames[8][STRING_LENGTH] = { " ||  ", " <+  ", " +>  ", " <=  ", " =>  ", " <-  ", " ->  " };
// UIItem motorStateIndicator(motorIndicatorFrames[0], , []() {return(motorIndicatorFrames[motorState * (1 + direction)]);});
UIItem *manMenuUIItems[2] =
{
    &manMenuLabel,
    &motorStateIndicator
};

Menu manMenu
(
    manMenuButtons,
    manMenuUIItems,
    &lcd
);

// TODO
// 2 - delaysMenu ------------------------------------
// delaysMenu buttons
char     lPre[] = "Pre  ";
MenuItem preshotDelayMenuBtn(lPre, []() {
                             currentMenu = PREDELAY_MENU;
    }, CLICKABLE);
char     lFocus[] = "Focus";
MenuItem focusDelayMenuBtn(lFocus, []() {
                           currentMenu = FOCUSDELAY_MENU;
    }, CLICKABLE);
char     lShuttr[] = "Shuttr";
MenuItem shuttrDelayMenuBtn(lShuttr, []() {
                            currentMenu = SHUTTERDELAY_MENU;
    }, CLICKABLE);

MenuItem *delaysMenuButtons[4] =
{
    &setupMenuBtn,
    &preshotDelayMenuBtn,
    &focusDelayMenuBtn,
    &shuttrDelayMenuBtn,
};

// delaysMenu UIItems
char    ldelay[] = "delay";
UIItem  delaysMnuLabel(ldelay);
UIItem *delaysMenuUIItems[2] =
{
    &delaysMnuLabel,
    &blankUII
};
Menu delaysMenu
(
    delaysMenuButtons,
    delaysMenuUIItems,
    &lcd
);
// 9 - predelay ------------------------------------
void backToDelayMenu()
{
    currentMenu = DELAYS_MENU;
}

MenuItem bactkToDelayMenuBtn(lBack, backToDelayMenu, CLICKABLE);

void decreasePreDelay()
{
    preDelayTime = max(preDelayTime - 100, 0);
}

MenuItem preDelayDecreaseBtn(lMINUS, decreasePreDelay, CLICKABLE, AUTOCLICKABLE);

void increasePreDelay()
{
    preDelayTime += 100;
}

MenuItem preDelayIncreaseBtn(lPLUS, increasePreDelay, CLICKABLE, AUTOCLICKABLE);
char     ldelayText[] = "delay";
MenuItem delayLable(ldelayText, []() {
                    return(ldelayText);
    });
MenuItem *preDelayMenuItems[4] =
{
    &bactkToDelayMenuBtn,
    &delayLable,
    &preDelayDecreaseBtn,
    &preDelayIncreaseBtn
};
// delayMenu UIItems
char   lpreDelaymnu[] = "  pre";
UIItem preDelayMenuLabel(lpreDelaymnu);
// char lCounter[] = "00000";
char *indicatePreDelayTime()
{
    intToDisplayChar(preDelayTime, lCounter, LEFT_JUSTIFY);
    return(lCounter);
}

UIItem  preDelayIndicator(lCounter, indicatePreDelayTime);                                                //TODO sprintf function
UIItem *preDelayMenuUIItems[2] = {
    &preDelayMenuLabel,
    &preDelayIndicator
};
Menu    preDelayMenu
(
    preDelayMenuItems,
    preDelayMenuUIItems,
    &lcd
);

// 10 - focusdelay ------------------------------------
void decreaseFocusDelay()
{
    focusDelayTime = max(focusDelayTime - 100, 0);
}

MenuItem focusDelayDecreaseBtn(lMINUS, decreaseFocusDelay, CLICKABLE, AUTOCLICKABLE);

void increaseFocusDelay()
{
    focusDelayTime += 100;
}

MenuItem  focusDelayIncreaseBtn(lPLUS, increaseFocusDelay, CLICKABLE, AUTOCLICKABLE);
MenuItem *focusDelayMenuItems[4] =
{
    &bactkToDelayMenuBtn,
    &delayLable,
    &focusDelayDecreaseBtn,
    &focusDelayIncreaseBtn
};
// delayMenu UIItems
char lfocusDelayAF[] = "AF   ";
char lfocusDelayMF[] = "MF   ";
char *switchAFMF()
{
    return((focusDelayTime > 0) ? lfocusDelayAF : lfocusDelayMF);
}

UIItem focusDelayMenuLabel(lfocusDelayAF, switchAFMF);
char *indicateFocusDelayTime()
{
    intToDisplayChar(focusDelayTime, lCounter, LEFT_JUSTIFY);
    return(lCounter);
}

UIItem  focusDelayIndicator(lCounter, indicateFocusDelayTime);                                                //TODO sprintf function
UIItem *focusDelayMenuUIItems[2] = {
    &focusDelayMenuLabel,
    &focusDelayIndicator
};
Menu    focusDelayMenu
(
    focusDelayMenuItems,
    focusDelayMenuUIItems,
    &lcd
);

// 11 shutterDelay ------------------------------------
void decreaseShutterDelay()
{
    shutterDelayTime = max(shutterDelayTime - 100, 0);
}

MenuItem shutterDelayDecreaseBtn(lMINUS, decreaseShutterDelay, CLICKABLE, AUTOCLICKABLE);

void increaseShutterDelay()
{
    shutterDelayTime += 100;
}

MenuItem  shutterDelayIncreaseBtn(lPLUS, increaseShutterDelay, CLICKABLE, AUTOCLICKABLE);
MenuItem *shutterDelayMenuItems[4] =
{
    &bactkToDelayMenuBtn,
    &delayLable,
    &shutterDelayDecreaseBtn,
    &shutterDelayIncreaseBtn
};
// delayMenu UIItems
char   lshutterDelaymnu[] = "shutter";
UIItem shutterDelayMenuLabel(lshutterDelaymnu);
char *indicateShutterDelayTime()
{
    intToDisplayChar(shutterDelayTime, lCounter, LEFT_JUSTIFY);
    return(lCounter);
}

UIItem  shutterDelayIndicator(lCounter, indicateShutterDelayTime);                                                //TODO sprintf function
UIItem *shutterDelayMenuUIItems[2] = {
    &shutterDelayMenuLabel,
    &shutterDelayIndicator
};
Menu    shutterDelayMenu
(
    shutterDelayMenuItems,
    shutterDelayMenuUIItems,
    &lcd
);



// --------------------------------------------------------------------------------------------------------------------------
Menu *allTheMenus[13] = {
    &runMenu,         //0
    &mainMenu,        //1
    &setupMenu,       //2
    &directionMenu,   //3
    &motorMenu,       //4
    &speedMenu,       //5
    &accelMenu,       //6
    &shotsMenu,       //7
    &manMenu,         //8
    &delaysMenu,      //9
    &preDelayMenu,    //10
    &focusDelayMenu,  //11
    &shutterDelayMenu //12
};

void setup()
{
    motorSpeed = 0;
    Serial.begin(9600);     //debug
    // motor contreol stuff
    leadingEdge = true;
    stepNumber  = 0;
    stepTarget  = stepsPerRev / shotsPerRev;
    pinMode(BUTTON_ADC_PIN, INPUT);        //ensure A0 is an input
    digitalWrite(BUTTON_ADC_PIN, LOW);     //ensure pullup is off on A0
    //lcd backlight control
    digitalWrite(LCD_BACKLIGHT_PIN, HIGH); //backlight control pin D10 is high (on)
    pinMode(LCD_BACKLIGHT_PIN, OUTPUT);    //D10 is an output
    //set up the LCD number of columns and rows:
    //button adc input
    pinMode(FOCUS_PIN, OUTPUT);
    pinMode(SHUTTER_PIN, OUTPUT);

    lcd.begin(16, 2);
    motorSpeed  = 0.0;
    targetSpeed = 0.0;
    shotsPerRev = 10;
    currentShot = 0;
    setupTimer();
}

void loop()
{
    // Serial.println(currentMenu);
    allTheMenus[currentMenu]->display();
    button = readButtons();
    if (buttonJustPressed)
    {
        Serial.println(String("button: " + String(button))); //debug
        allTheMenus[currentMenu]->click(button);
    }
    if (buttonAutoClicked)
    {
        Serial.println("autoclicked" + String(button));//debug
        allTheMenus[currentMenu]->autoclick(button);
    }
    // set some control variables
    accelerationIncrement = maxSpeed / rampSteps;
    minInterruptsPerStep  = 1 / (stepsPerSecond * 2 * interruptDuration);

    if (motorSpeed > 0 || targetSpeed > 0)
    {
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
                Serial.println("-----------Finished shots-------");
                operationState = STOPPED;
            }
        }
    }
    // Serial.print("tarsp:");
    // Serial.print(targetSpeed);
    // Serial.print(" mtrSp:");
    // Serial.print(motorSpeed);
    // Serial.print(" mtrSt:");
    // Serial.print(motorState);
    // Serial.print(" opSt:");
    // Serial.print(operationState);
    // Serial.print(" stTrg:");
    // Serial.print(stepTarget);
    // Serial.print(" stepN:");
    // Serial.print(stepNumber);
    // Serial.print(" Shot:");
    // Serial.println(currentShot);
}

// timer interrupt handler - where the business logic happens
ISR(TIMER1_COMPA_vect)
{
    if (targetSpeed > 0 || motorSpeed > 0)
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
                    motorSpeed  = 0;
                    targetSpeed = 0;
                }
            }

            // check speed and adjust if necesary
            else if (motorSpeed < targetSpeed)
            {
                motorSpeed = min(motorSpeed + accelerationIncrement, targetSpeed);
            }
            else if (motorSpeed > targetSpeed)
            {
                motorSpeed = max(motorSpeed - accelerationIncrement, targetSpeed);
            }

            leadingEdge = false;
        }
        // trailing edge
        else
        {
            digitalWrite(STEP_PIN, LOW);
            leadingEdge = true;
        }
    }
    else
    {
        // motorSpeed == 0 && targetSpeed == 0
        motorState = STOPPED;
        digitalWrite(STEP_PIN, LOW);
    }
    OCR1A = minInterruptsPerStep + (stepDiff * (maxSpeed - motorSpeed) / maxSpeed);     //stepDiff = maxInterruptsPerStep - minInterruptsPerStep
}

void manRun()
{
    targetSpeed    = maxSpeed;
    operationState = RUNNING_MAN;
}

void takeShot()
{
    takingShot = true;
    allTheMenus[currentMenu]->display();
    Serial.println("Taking shot");     //TODO
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
    byte         button = BUTTON_NONE; // return no button pressed if the below checks don't write to btn

    buttonJustPressed  = false;
    buttonJustReleased = false;
    buttonAutoClicked  = false;
    //read the button ADC pin voltage
    buttonVoltage = analogRead(BUTTON_ADC_PIN);
    //sense if the voltage falls within valid voltage windows
    if (buttonVoltage < (RIGHT_10BIT_ADC + BUTTONHYSTERESIS))
    {
        button = BUTTON_TOPLEFT;
    }
    else if (buttonVoltage >= (UP_10BIT_ADC - BUTTONHYSTERESIS) &&
             buttonVoltage <= (UP_10BIT_ADC + BUTTONHYSTERESIS))
    {
        button = BUTTON_TOPRIGHT;
    }
    else if (buttonVoltage >= (DOWN_10BIT_ADC - BUTTONHYSTERESIS) &&
             buttonVoltage <= (DOWN_10BIT_ADC + BUTTONHYSTERESIS))
    {
        button = BUTTON_BOTTOMLEFT;
    }
    else if (buttonVoltage >= (LEFT_10BIT_ADC - BUTTONHYSTERESIS) &&
             buttonVoltage <= (LEFT_10BIT_ADC + BUTTONHYSTERESIS))
    {
        button = BUTTON_BOTTOMRIGHT;
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
            btnPressMillis    = millis(); // reset button hold counter
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
                    Serial.println("autoclicked: " + String(button));
                    buttonAutoClicked = true;
                    lastAutoPressMs   = millis();
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
    return(button);
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
