#include "UIItem.h"
#include "MenuItem.h"
#include "Menu.h"

#include <LiquidCrystal.h>
#include <avr/interrupt.h>
#include <avr/io.h>

// Arduino defines
#define STEP_CLOCKWISE    HIGH      // the value to set the dir_pin to when changing direction
#define STEP_REVERSE      LOW
#define DIR_PIN           13
#define STEP_PIN          12
#define ENABLE_PIN        11    //goes LOW to enable

// motorState constants
#define STOPPED           0    //also used as operation state
#define ACCELERATING      1
#define RUNNING           2
#define DECELERATING      3

// operation state constants
// #define STOPPED              0 //defined already
#define RUNNING_AUTO          1
#define RUNNING_MAN           2

#define STEPS_PER_DEGREE      273.6 // 76 * 360 / 100
#define STEPS_PER_REV         99468
#define BUTTON_ADC_PIN        A0    // A0 is the button ADC input
#define LCD_BACKLIGHT_PIN     10    // D10 controls LCD backlight
// ADC readings expected for the 5 buttons on the ADC input
#define RIGHT_10BIT_ADC       0     // right
#define UP_10BIT_ADC          100   // up
#define DOWN_10BIT_ADC        256   // down
#define LEFT_10BIT_ADC        408   // left
#define SELECT_10BIT_ADC      640   // right
#define BUTTONHYSTERESIS      40    // hysteresis for valid button sensing window
//return values for readButtons()
#define BUTTON_TOPLEFT        0     //
#define BUTTON_TOPRIGHT       1     //
#define BUTTON_BOTTOMLEFT     2     //
#define BUTTON_BOTTOMRIGHT    3     //
#define BUTTON_SELECT         4     //
#define BUTTON_NONE           5     //
//some example macros with friendly labels for LCD backlight/pin control, tested and can be swapped into the example code as you like
#define LCD_BACKLIGHT_OFF()     digitalWrite(LCD_BACKLIGHT_PIN, LOW)
#define LCD_BACKLIGHT_ON()      digitalWrite(LCD_BACKLIGHT_PIN, HIGH)
#define LCD_BACKLIGHT(state)    { if (state) { digitalWrite(LCD_BACKLIGHT_PIN, HIGH); }else{ digitalWrite(LCD_BACKLIGHT_PIN, LOW); } }
// directions
#define CLOCKWISE         0
#define COUNTERCW         1
// Menus
#define RUN_MENU          0
#define MAIN_MENU         1
#define SETUP_MENU        2
#define DIRECTION_MENU    3
#define MOTOR_MENU        4
#define SPEED_MENU        5
#define ACCEL_MENU        6
#define STEPS_MENU        7
#define MANUAL_MENU       8
#define DELAY_MENU        9

#define CLICKABLE         true
#define NOT_CLICKABLE     false


/*--------------------------------------------------------------------------------------
 *  Variables
 *  --------------------------------------------------------------------------------------*/
bool           buttonJustPressed  = false;       //this will be true after a readButtons() call if triggered
bool           buttonJustReleased = false;       //this will be true after a readButtons() call if triggered
byte           buttonWas          = BUTTON_NONE; //used by readButtons() for detection of button events
byte           button             = BUTTON_NONE;
volatile int motorSpeed         = 0;     //normalised speed motor is currently at, goes from 0→1
volatile int targetSpeed        = 0;     //normalised speed motor is aiming for, goes from 0→1
volatile int maxSpeed           = 10000; //TODO check floats
// When interuptLoopCount == 1 interrupt every ≈ 64 µs. There are two interrupts per step
#define interruptDuration    0.000064
int          maxInterruptsPerStep = 1 / (8 * 2 * interruptDuration);   // 8 steps/second.
volatile int minInterruptsPerStep = 1 / (500 * 2 * interruptDuration); // 500 steps/second.
float        rampSteps           = 1000;                               // number of steps needed to get to full speed
//reduce the number of calculations we need to do in the interrupt handler
volatile int        accelerationIncrement = 10000 / rampSteps;
volatile int          stepDiff       = maxInterruptsPerStep - minInterruptsPerStep;
volatile byte         motorState     = STOPPED;
volatile byte         operationState = STOPPED;
volatile unsigned int stepDirection;     //HIGH and LOW are u_ints
volatile int          stepNumber;
volatile int          stepTarget;
volatile bool         leadingEdge;
// auto run parameters
int shotsPerRev = 12;
int currentShot = 0;
int delayTime   = 1;
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
        stepTarget     = STEPS_PER_REV / shotsPerRev;
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

void intToDisplayChar(int theInt, char theChars[STRING_LENGTH])
{
    String(theInt).toCharArray(theChars, STRING_LENGTH);
}

// blank menuItem
MenuItem blankMI;
UIItem   blankUII;
// 0 - runMenu ------------------------------------
// runMenu buttons
char     lMenu[] = "Menu ";
MenuItem mainMenuBtn(lMenu, []() {
                     currentMenu = MAIN_MENU;
    }, CLICKABLE);
char     lSTART[] = "START";
char     lSTOP[]  = " STOP";
MenuItem startBtn(lSTART, toggleAutoRun, []() {
                  return((motorState) ? lSTOP  : lSTART);
    }, CLICKABLE);
char     lShotCount[] = "    0";
MenuItem shotCounter(lShotCount, []() {
                     intToDisplayChar(currentShot, lShotCount);
                     return(lShotCount);
    });
char     lShotsPerRev[] = "00000";
MenuItem shotsIndicator(lShotsPerRev, []() {
                        intToDisplayChar(shotsPerRev, lShotsPerRev);
                        return(lShotsPerRev);
    });
MenuItem *runMenuButtons[4] =
{
    &mainMenuBtn,
    &startBtn,
    &shotCounter,
    &shotsIndicator
};
// runMenu UIItems
char   motorIndicatorFrames[8][STRING_LENGTH] = { " ||  ", " <+  ", " +>  ", " <=  ", " =>  ", " <-  ", " ->  " };
UIItem motorStateIndicator(motorIndicatorFrames[0], []() {
                           return(motorIndicatorFrames[motorState * (1 + direction)]);
    });
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
MenuItem dirCWBtn(lCW, []() {
                  direction = CLOCKWISE;
    }, CLICKABLE);
char     lCCW[] = "  [>]";
MenuItem dirCCBtn(lCCW, []() {
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
    dirIndicatorFrames[direction], []() {
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
char     lSpeed[] = "Speed";
MenuItem speedMenuBtn(lSpeed, []() {
                      currentMenu = SPEED_MENU;
    }, CLICKABLE);
char     lAccelMnuBtn[] = "Accel";
MenuItem accelMenuBtn(lAccelMnuBtn, []() {
                      currentMenu = ACCEL_MENU;
    }, CLICKABLE);
char     lDelayMnuBtn[] = "Delay";
MenuItem delayMenuBtn(lDelayMnuBtn, []() {
                      currentMenu = DELAY_MENU;
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
char     lMINUS[] = "[-]  ";
MenuItem slowDownBtn(lMINUS, []() {
                     maxSpeed = (maxSpeed > 0.1) ? maxSpeed - 0.05 : 0.1;
    }, CLICKABLE);
char     lPLUS[] = "  [+]";
MenuItem speedUpBtn(lPLUS, []() {
                    maxSpeed = (maxSpeed < 1) ? maxSpeed + 0.05 : 1;
    }, CLICKABLE);

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
char   lMaxSpeed[] = "00000";
UIItem speedIndicator(lMaxSpeed, []() {
                      intToDisplayChar(maxSpeed, lMaxSpeed);
                      return(lMaxSpeed);
    });
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
// MenuItem backMtrBtn(lBack, []() {currentMenu = MOTOR_MENU;}, CLICKABLE);
// char lMINUS[] = "[ - ]";
MenuItem accelIncreaseBtn(lMINUS, []() {
                          rampSteps = (rampSteps > 1) ? rampSteps - 1 : 1;
    }, CLICKABLE);
// char lPLUS[] = "[ + ]";
MenuItem accelDecreaseBtn(lPLUS, []() {
                          rampSteps++;
    }, CLICKABLE);

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
char   lAccel[] = "00000";
UIItem accelIndicator(lAccel, []() {
                      intToDisplayChar(100 / rampSteps, lAccel);
                      return(lAccel);
    });
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
// MenuItem backMtrBtn(lBack, []() {currentMenu = MOTOR_MENU;}, CLICKABLE);
// char lMINUS[] = "[ - ]";
MenuItem shotsIncreaseBtn(lMINUS, []() {
                          shotsPerRev = (shotsPerRev > 1) ? shotsPerRev - 1 : 1;
    }, CLICKABLE);
// char lPLUS[] = "[ + ]";
MenuItem shotsDecreaseBtn(lPLUS, []() {
                          shotsPerRev = (shotsPerRev <= 3600) ? shotsPerRev + 1 : 3600;
    }, CLICKABLE);

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
char   lShots[] = "00000";
UIItem shotsIndicatorUI(lShots, []() {
                        intToDisplayChar(shotsPerRev, lShots);
                        return(lShots);
    });                                                     //TODO sprintf function
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
// char lMenu[] = "Menu ";
// MenuItem mainMenuBtn(lMenu , []() {currentMenu = MAIN_MENU;}, CLICKABLE);
// char lSTOP[] = "STOP ";
MenuItem stopBtn(lSTOP, stopMotor, CLICKABLE);
// if running CW speed up, if CCW slow down, otherwise start running CW
MenuItem runCWBtn(lCW, []() {
                  if (motorState)
                  // while running the buttons become speedup or slowdown buttons
                  {
                      if (direction == CLOCKWISE)
                      {
                          // giddy up
                          targetSpeed = (targetSpeed < maxSpeed) ? targetSpeed + 0.1 : maxSpeed;
                      }
                      else
                      // whoah neddy
                      {
                          targetSpeed = (targetSpeed > 0.05) ? targetSpeed - 0.05 : 0.05;
                      }
                  }
                  else
                  {
                      direction = CLOCKWISE;
                      digitalWrite(DIR_PIN, direction);
                      targetSpeed    = maxSpeed;
                      operationState = RUNNING_MAN;
                  }
    }, CLICKABLE);
//if running CCW speed up, if CW slow down, otherwise start running CCW
MenuItem runCCWBtn(lCW, []() {
                   if (motorState)
                   // while running the buttons become speedup or slowdown buttons
                   {
                       if (direction == COUNTERCW)
                       {
                           // giddy up
                           targetSpeed = (targetSpeed < maxSpeed) ? targetSpeed + 0.1 : maxSpeed;
                       }
                       else
                       // whoah neddy
                       {
                           targetSpeed = (targetSpeed > 0.05) ? targetSpeed - 0.05 : 0.05;
                       }
                   }
                   else
                   {
                       direction   = COUNTERCW;
                       targetSpeed = maxSpeed;
                       digitalWrite(DIR_PIN, direction);
                       operationState = RUNNING_MAN;
                   }
    }, CLICKABLE);
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
// UIItem motorStateIndicator(motorIndicatorFrames[0], []() {return(motorIndicatorFrames[motorState * (1 + direction)]);});
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


// 9 - delay ------------------------------------
// delayMenu buttons
// char lBack[] = "<Back";
// MenuItem backMtrBtn(lBack, []() {currentMenu = MOTOR_MENU;}, CLICKABLE);
// char lMINUS[] = "[ - ]";
MenuItem delayIncreaseBtn(lMINUS, []() {
                          if (delayTime > 0)
                          {
                              delayTime--;
                          }
    }, CLICKABLE);
// char lPLUS[] = "[ + ]";
MenuItem delayDecreaseBtn(lPLUS, []() {
                          delayTime++;
    }, CLICKABLE);

MenuItem *delayMenuButtons[4] =
{
    &backMtrBtn,
    &blankMI,
    &delayIncreaseBtn,
    &delayDecreaseBtn
};
// delayMenu UIItems
char   ldelaymnu[] = "delay";
UIItem delayMenuLabel(ldelaymnu);
char   lDelayTime[] = "00000";
UIItem delayIndicator(lDelayTime, []() {
                      intToDisplayChar(delayTime, lDelayTime);
                      return(lDelayTime);
    });                                                   //TODO sprintf function
UIItem *delayMenuUIItems[2] = {
    &delayMenuLabel,
    &delayIndicator
};
Menu    delayMenu
(
    delayMenuButtons,
    delayMenuUIItems,
    &lcd
);


// --------------------------------------------------------------------------------------------------------------------------
Menu *allTheMenus[10] = {
    &runMenu,
    &mainMenu,
    &setupMenu,
    &directionMenu,
    &motorMenu,
    &speedMenu,
    &accelMenu,
    &shotsMenu,
    &manMenu,
    &delayMenu
};

void setup()
{
    motorSpeed = 0;
    Serial.begin(9600); //debug
    // motor contreol stuff
    leadingEdge = true;
    stepNumber  = 0;
    stepTarget  = STEPS_PER_REV / shotsPerRev;
    //button adc input
    pinMode(BUTTON_ADC_PIN, INPUT);        //ensure A0 is an input
    digitalWrite(BUTTON_ADC_PIN, LOW);     //ensure pullup is off on A0
    //lcd backlight control
    digitalWrite(LCD_BACKLIGHT_PIN, HIGH); //backlight control pin D10 is high (on)
    pinMode(LCD_BACKLIGHT_PIN, OUTPUT);    //D10 is an output
    //set up the LCD number of columns and rows:
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
        Serial.println(String("button: " + String(button)));//debug
        allTheMenus[currentMenu]->click(button);
    }
    // if (motorSpeed > 0)
    // {
    //     if (motorSpeed < targetSpeed)
    //     {
    //         motorState = ACCELERATING;
    //     }
    //     else if (motorState > targetSpeed)
    //     {
    //         motorState = DECELERATING;
    //     }
    //     else
    //     {
    //         motorState = RUNNING;
    //     }
    // }
    // else
    // {
    //     // STOPPED
    //     motorState = STOPPED;
    // }
    if (operationState == RUNNING_AUTO)
    {
        if (stepNumber == stepTarget)
        {
            if (currentShot < shotsPerRev)
            {
                Serial.print("------------taking a shot ---------");
                targetSpeed = 0.0;
                currentShot++;
                stepTarget  = STEPS_PER_REV / shotsPerRev;
                stepNumber  = 0;
                targetSpeed = maxSpeed;
                // take a shot
                delay(delayTime * 1000);
                // TODO
            }
            else
            {
                Serial.println("-----------Finished shots-------");
                operationState = STOPPED;
            }
        }
    }
    Serial.print("tarsp:");
    Serial.print(targetSpeed);
    Serial.print(" mtrSp:");
    Serial.print(motorSpeed);
    Serial.print(" mtrSt:");
    Serial.print(motorState);
    Serial.print(" opSt:");
    Serial.print(operationState);
    Serial.print(" stTrg:");
    Serial.print(stepTarget);
    Serial.print(" stepN:");
    Serial.print(stepNumber);
    Serial.print(" Shot:");
    Serial.println(currentShot);
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
            if (motorSpeed == targetSpeed)
            {
                motorState = RUNNING;
            }
            else if (motorSpeed < targetSpeed)
            {
                // Serial.print("motorSpeed++ ");
                // Serial.println(motorSpeed);
                motorSpeed += 1.0 / rampSteps;
                motorState  = ACCELERATING;
            }
            else if (motorSpeed > targetSpeed)
            {
                // Serial.print("motorSpeed-- ");
                // Serial.println(motorSpeed);
                motorSpeed -= 1.0 / rampSteps;
                motorState  = DECELERATING;
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
    OCR1A = minInterruptsPerStep + (stepDiff * (1 - motorSpeed)); //stepDiff = maxInterruptsPerStep - minInterruptsPerStep
}

void manRun()
{
    targetSpeed    = maxSpeed;
    operationState = RUNNING_MAN;
}

byte readButtons()
{
    unsigned int buttonVoltage;
    byte         button = BUTTON_NONE; // return no button pressed if the below checks don't write to btn

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
    if ((buttonWas == BUTTON_NONE) && (button != BUTTON_NONE))
    {
        //the button was just pressed, set buttonJustPressed, this can optionally be used to trigger a once-off action for a button press event
        //it's the duty of the receiver to clear these flags if it wants to detect a new button change event
        buttonJustPressed  = true;
        buttonJustReleased = false;
    }
    else if ((buttonWas != BUTTON_NONE) && (button == BUTTON_NONE))
    {
        buttonJustPressed  = false;
        buttonJustReleased = true;
    }
    else
    {
        buttonJustPressed  = false;
        buttonJustReleased = false;
    }

    //save the latest button value, for change event detection next time round
    buttonWas = button;

    return(button);
}

void stopStepping()
{
    // reset the motor stepping loop
    digitalWrite(ENABLE_PIN, HIGH);
}

void startStepping()
{
    leadingEdge = true;
    digitalWrite(STEP_PIN, LOW);
    digitalWrite(ENABLE_PIN, LOW);
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
