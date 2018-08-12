#include "UIItem.h"
#include "MenuItem.h"
#include "Menu.h"

#include <LiquidCrystal.h>
#include <avr/interrupt.h>
#include <avr/io.h>

// Arduino defines
#define STEP_CLOCKWISE    HIGH      // the value to set the dir_pin to when changing direction
#define STEP_REVERSE    LOW
#define DIR_PIN         13
#define STEP_PIN        12
#define ENABLE_PIN      11      //goes LOW to enable

// motorState constants
#define STOPPED         0      //also used as operation state
#define ACCELERATING    1
#define RUNNING         2
#define DECELERATING    3

// operation state constants
// #define STOPPED              0 //defined already
#define RUNNING_AUTO         1
#define RUNNING_MAN          2

#define STEPS_PER_DEGREE     273.6 // 76 * 360 / 100

#define BUTTON_ADC_PIN       A0    // A0 is the button ADC input
#define LCD_BACKLIGHT_PIN    10    // D10 controls LCD backlight
// ADC readings expected for the 5 buttons on the ADC input
#define RIGHT_10BIT_ADC      0     // right
#define UP_10BIT_ADC         100   // up
#define DOWN_10BIT_ADC       256   // down
#define LEFT_10BIT_ADC       408   // left
#define SELECT_10BIT_ADC     640   // right
#define BUTTONHYSTERESIS     40    // hysteresis for valid button sensing window
//return values for readButtons()
#define BUTTON_TOPLEFT        0 //
#define BUTTON_TOPRIGHT       1 //
#define BUTTON_BOTTOMLEFT     2 //
#define BUTTON_BOTTOMRIGHT    3 //
#define BUTTON_SELECT         4 //
#define BUTTON_NONE           5                               //
//some example macros with friendly labels for LCD backlight/pin control, tested and can be swapped into the example code as you like
#define LCD_BACKLIGHT_OFF()     digitalWrite(LCD_BACKLIGHT_PIN, LOW)
#define LCD_BACKLIGHT_ON()      digitalWrite(LCD_BACKLIGHT_PIN, HIGH)
#define LCD_BACKLIGHT(state)    { if (state) { digitalWrite(LCD_BACKLIGHT_PIN, HIGH); }else{ digitalWrite(LCD_BACKLIGHT_PIN, LOW); } }
// directions
#define CLOCKWISE    0
#define COUNTERCW    1
// Menus
#define RUN_MENU          0
#define MAIN_MENU         1
#define SETUP_MENU        2
#define DIRECTION_MENU    3
#define MOTOR_MENU     4
#define SPEED_MENU        5
#define ACCEL_MENU        6
#define STEPS_MENU        7
#define MANUAL_MENU       8

#define CLICKABLE        true
#define NOT_CLICKABLE    false


/*--------------------------------------------------------------------------------------
 *  Variables
 *  --------------------------------------------------------------------------------------*/
bool           buttonJustPressed  = false;       //this will be true after a readButtons() call if triggered
bool           buttonJustReleased = false;       //this will be true after a readButtons() call if triggered
byte           buttonWas          = BUTTON_NONE; //used by readButtons() for detection of button events
byte           button             = BUTTON_NONE;
volatile byte  rampPower          = 2;
volatile float motorSpeed         = 0;     //normalised speed motor is currently at, goes from 0→1
volatile float targetSpeed        = 0;     //normalised speed motor is aiming for, goes from 0→1
// When interuptLoopCount == 1 interrupt every ≈ 64 µs. There are two interrupts per step
#define interruptDuration    0.000064
int          maxInteruptsPerStep = 1 / (8 * 2 * interruptDuration);   // 8 steps/second.
volatile int minInteruptsPerStep = 1 / (500 * 2 * interruptDuration); // 500 steps/second.
int          rampSteps           = 400;                               // number of steps needed to get to full speed
//reduce the number of calculations we need to do in the interrupt handler
volatile float        accelerationIncrement = 1.0 / float(rampSteps);
volatile int          stepDiff       = maxInteruptsPerStep - minInteruptsPerStep;
byte                  motorState     = STOPPED;
byte                  operationState = STOPPED;
volatile unsigned int stepDirection;     //HIGH and LOW are u_ints
volatile int          stepNumber;
volatile int          stepTarget;
volatile bool         leadingEdge;
int                   shotsPerRev;
int                   currentShot;

//--------------------------------------------------------------------------------------
// Init the LCD library with the LCD pins to be used
LiquidCrystal lcd(8, 9, 4, 5, 6, 7);     //Pins for the freetronics 16x2 LCD shield.
//--------------------------------------------------------------------------------------

byte direction   = CLOCKWISE;
byte currentMenu = MAIN_MENU;

void toggleMotor()
{
    if (motorState == STOPPED)
    {
        motorState = ACCELERATING;
        if (currentMenu == RUN_MENU)
        {
            operationState = RUNNING_AUTO;
        }
        else
        {
            operationState = RUNNING_MAN;
        }
    }
    else
    {
        motorState = DECELERATING;
    }
}

void stopMotor()
{
    if (motorState != STOPPED)
    {
        motorState = DECELERATING;
    }
}


// blank menuItem
MenuItem blankMI;
UIItem blankUII;
// 0 - runMenu ------------------------------------
// runMenu buttons
const char lMenu[] = "Menu ";
MenuItem mainMenuBtn(lMenu , []() {currentMenu = MAIN_MENU;}, CLICKABLE);
const char lSTART[] = "START";
const char lSTOP[] = " STOP";
MenuItem startBtn(lSTART, toggleMotor, [](){return((motorState)? lSTOP  : lSTART);}, CLICKABLE); // motorState > 0 means running
MenuItem shotCounter("12345", [](){return("54321");}); //TODO frame counter
MenuItem shotsIndicator("12345", [](){return("54321");}); //TODO frame counter
MenuItem *runMenuButtons[4] =
{
  &mainMenuBtn,
  &startBtn,
  &shotCounter,
  &shotsIndicator
};
// runMenu UIItems
const char motorIndicatorFrames[8][STRING_LENGTH] = { " ||  ", " <+  ", " +>  ", " <=  ", " =>  ", " <-  ", " ->  " };
UIItem motorStateIndicator(motorIndicatorFrames[0], []() {return(motorIndicatorFrames[motorState * (1 + direction)]);});
const char lSlash[] = "  /  ";
UIItem slash(lSlash);
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
const char lAuto[] = "Auto ";
MenuItem runMenuBtn(lAuto , []() {currentMenu = RUN_MENU;}, CLICKABLE);
const char lMan[] = "  Man";
MenuItem manualMenuBtn(lMan, []() {currentMenu = MANUAL_MENU;}, CLICKABLE);
const char lSetup[] = "Setup";
MenuItem setupMenuBtn(lSetup, []() {currentMenu = SETUP_MENU;}, CLICKABLE);

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
const char lMotr[] = "Motor";
MenuItem motorMenuBtn(lMotr, []() {currentMenu = MOTOR_MENU;}, CLICKABLE);
const char lDir[] = "Dir  ";
MenuItem directionMenuBtn(lDir  , []() {currentMenu = DIRECTION_MENU;}, CLICKABLE);
const char lSteps[] = "Shots";
MenuItem stepsMenuBtn(lSteps, []() {currentMenu = STEPS_MENU;}, CLICKABLE);

MenuItem *setupMenuButtons[4] =
{
    &mainMenuBtn,
    &motorMenuBtn,
    &directionMenuBtn,
    &stepsMenuBtn,
};

// setupMenu UIItems
const char lset[] = "set  ";
UIItem setupMnuLabel(lset);
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
const char lBack[] = "<Back";
MenuItem backSetupBtn(lBack, []() {currentMenu = SETUP_MENU;}, CLICKABLE);
const char lCW[] = "[<]  ";
MenuItem dirCWBtn(lCW  , []() {direction = CLOCKWISE;}, CLICKABLE);
const char lCCW[] = "  [>]";
MenuItem dirCCBtn(lCCW, []() {direction = COUNTERCW;}, CLICKABLE);

MenuItem *directionMenuButtons[4] =
{
    &backSetupBtn,
    &blankMI,
    &dirCWBtn,
    &dirCCBtn
};
// directionMenu UIItems
const char ldir[] = " dir ";
UIItem dirMenuLabel(ldir);
const char dirIndicatorFrames[2][STRING_LENGTH] = {"<-CW-", "CCW->"};
UIItem directionIndicator(
  dirIndicatorFrames[direction], [](){return(dirIndicatorFrames[direction]);});
UIItem *directionMenuUIItems[2] = {
  &dirMenuLabel,
  &directionIndicator
};
Menu directionMenu
(
    directionMenuButtons,
    directionMenuUIItems,
    &lcd
);

// 4 - motorMenu ------------------------------------
// motorMenu buttons
const char lSpeed[] = "Speed";
MenuItem speedMenuBtn(lSpeed, []() {currentMenu = SPEED_MENU;}, CLICKABLE);
const char lAccel[] = "Accel";
MenuItem accelMenuBtn(lAccel, []() {currentMenu = ACCEL_MENU;}, CLICKABLE);

MenuItem *motorMenuButtons[4] =
{
    &backSetupBtn,
    &blankMI,
    &speedMenuBtn,
    &accelMenuBtn
};
// motorMenu UIItems
const char lmotor[] = "motor";
UIItem motorMenuLabel(lmotor);
UIItem *motorMenuUIItems[2] = {
  &motorMenuLabel,
  &blankUII
};
Menu motorMenu
(
    motorMenuButtons,
    motorMenuUIItems,
    &lcd
);

// 5 - speed ------------------------------------
// speedMenu buttons
// const char lBack[] = "<Back";
MenuItem backMtrBtn(lBack, []() {currentMenu = MOTOR_MENU;}, CLICKABLE);
const char lMINUS[] = "[-]  ";
MenuItem slowDownBtn(lMINUS, []() {slowDown();}, CLICKABLE);
const char lPLUS[] = "  [+]";
MenuItem speedUpBtn(lPLUS, []() {speedUp();}, CLICKABLE);

MenuItem *speedMenuButtons[4] =
{
    &backMtrBtn,
    &blankMI,
    &slowDownBtn,
    &speedUpBtn
};
// speedMenu UIItems
const char lspeedmnu[] = "speed";
UIItem speedMenuLabel(lspeedmnu);
UIItem speedIndicator("12345", [](){return("12345");}); //TODO sprintf function
UIItem *speedMenuUIItems[2] = {
  &speedMenuLabel,
  &speedIndicator
};
Menu speedMenu
(
    speedMenuButtons,
    speedMenuUIItems,
    &lcd
);

// 6 - accel ------------------------------------
// accelMenu buttons
// const char lBack[] = "<Back";
// MenuItem backMtrBtn(lBack, []() {currentMenu = MOTOR_MENU;}, CLICKABLE);
// const char lMINUS[] = "[ - ]";
MenuItem accelIncreaseBtn(lMINUS  , []() {rampSteps = (rampSteps>1)?rampSteps-1:1;}, CLICKABLE);
// const char lPLUS[] = "[ + ]";
MenuItem accelDecreaseBtn(lPLUS, []() {rampSteps++;}, CLICKABLE);

MenuItem *accelMenuButtons[4] =
{
    &backMtrBtn,
    &blankMI,
    &accelIncreaseBtn,
    &accelDecreaseBtn
};
// accelMenu UIItems
const char laccelmnu[] = "accel";
UIItem accelMenuLabel(laccelmnu);
UIItem accelIndicator("12345", [](){return("12345");}); //TODO sprintf function
UIItem *accelMenuUIItems[2] = {
  &accelMenuLabel,
  &accelIndicator
};
Menu accelMenu
(
    accelMenuButtons,
    accelMenuUIItems,
    &lcd
);

// 6 - shots ------------------------------------
// shotsMenu buttons
// const char lBack[] = "<Back";
// MenuItem backMtrBtn(lBack, []() {currentMenu = MOTOR_MENU;}, CLICKABLE);
// const char lMINUS[] = "[ - ]";
MenuItem shotsIncreaseBtn(lMINUS  , []() {shotsPerRev = (shotsPerRev>1)?shotsPerRev-1:1;}, CLICKABLE);
// const char lPLUS[] = "[ + ]";
MenuItem shotsDecreaseBtn(lPLUS, []() {shotsPerRev = (shotsPerRev<=3600)?shotsPerRev+1:3600;}, CLICKABLE);

MenuItem *shotsMenuButtons[4] =
{
    &backSetupBtn,
    &blankMI,
    &shotsIncreaseBtn,
    &shotsDecreaseBtn
};
// shotsMenu UIItems
const char lshotsmnu[] = "shots";
UIItem shotsMenuLabel(lshotsmnu);
UIItem shotsIndicatorUI("12345", [](){return("12345");}); //TODO sprintf function
UIItem *shotsMenuUIItems[2] = {
  &shotsMenuLabel,
  &shotsIndicatorUI
};
Menu shotsMenu
(
    shotsMenuButtons,
    shotsMenuUIItems,
    &lcd
);

// 0 - manMenu ------------------------------------
// manMenu buttons
// const char lMenu[] = "Menu ";
// MenuItem mainMenuBtn(lMenu , []() {currentMenu = MAIN_MENU;}, CLICKABLE);
// const char lSTOP[] = "STOP ";
MenuItem stopBtn(lSTOP, stopMotor, CLICKABLE);
// if running CW speed up, if CCW slow down, otherwise start running CW
MenuItem runCWBtn(lCW, [](){
  if(motorState){
    if (direction == CLOCKWISE)
    {
        speedUp();
    }
    else
    {
        slowDown();
    }
  } else {
    direction=CLOCKWISE;
    toggleMotor();
  }
}, CLICKABLE);
//if running CCW speed up, if CW slow down, otherwise start running CCW
MenuItem runCCWBtn(lCCW, [](){
  if(motorState){
    if (direction == COUNTERCW)
    {
        speedUp();
    }
    else
    {
        slowDown();
    }
  } else {
    direction=COUNTERCW;
    toggleMotor();
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
const char lmanMnu[] = "man  ";
UIItem manMenuLabel(lmanMnu);
// const char motorIndicatorFrames[8][STRING_LENGTH] = { " ||  ", " <+  ", " +>  ", " <=  ", " =>  ", " <-  ", " ->  " };
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

// --------------------------------------------------------------------------------------------------------------------------
Menu *allTheMenus[9] = {
  &runMenu,
  &mainMenu,
  &setupMenu,
  &directionMenu,
  &motorMenu,
  &speedMenu,
  &accelMenu,
  &shotsMenu,
  &manMenu
};

void setup()
{
    motorSpeed = 0;
    Serial.begin(9600); //debug
    // motor contreol stuff
    leadingEdge = true;
    stepNumber  = 0;
    stepTarget  = 10 * STEPS_PER_DEGREE;
    //button adc input
    pinMode(BUTTON_ADC_PIN, INPUT);        //ensure A0 is an input
    digitalWrite(BUTTON_ADC_PIN, LOW);     //ensure pullup is off on A0
    //lcd backlight control
    digitalWrite(LCD_BACKLIGHT_PIN, HIGH); //backlight control pin D10 is high (on)
    pinMode(LCD_BACKLIGHT_PIN, OUTPUT);    //D10 is an output
    //set up the LCD number of columns and rows:
    lcd.begin(16, 2);
    motorSpeed  = 0.0;
    targetSpeed = 0.5;
    shotsPerRev = 0;
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
      Serial.println(String("button: "+String(button)));//debug
      allTheMenus[currentMenu]->click(button);
    }
    if (motorState) {
    // Running
            digitalWrite(DIR_PIN, stepDirection);
    } else {
    // STOPPED
            stepNumber = 0;
    }
    delay (10);
}

// timer interrupt handler
ISR(TIMER1_COMPA_vect)
{
    if (motorState != STOPPED)
    {
        // leading edge of the step cycle
        if (leadingEdge)
        {
            // turn the step pin HIGH
            digitalWrite(STEP_PIN, HIGH);
            // we've taken a new step
            stepNumber++;
            if (stepNumber == stepTarget - rampSteps)
            {
                motorState = DECELERATING;
            }
            // check speed and adjust if necesary
            switch (motorState)
            {
            case RUNNING: {
                break;
            }

            case ACCELERATING: {
                motorSpeed += accelerationIncrement;
                if (motorSpeed == targetSpeed)
                {
                    // we've hit top speed
                    motorState = RUNNING;
                }
                break;
            }

            case DECELERATING: {
                motorSpeed -= accelerationIncrement;
                if (motorSpeed == 0)
                {
                    // we've hit top speed
                    motorState = STOPPED;
                }
                break;
            }
            }
            // TODO Check step target
            leadingEdge = false;
            // trailing edge
        }
        else
        {
            digitalWrite(STEP_PIN, LOW);
            leadingEdge = true;
        }
    }
    OCR1A = minInteruptsPerStep + stepDiff * pow((1 - motorSpeed), rampPower) * (1 - targetSpeed);
}

void manRun(){
  motorState = ACCELERATING;
}

void speedUp(){
  motorSpeed = (motorSpeed < 1)? motorSpeed + 0.1: 1;
}

void slowDown(){
  motorSpeed = (motorSpeed > 0.1)? motorSpeed - 0.1: 0.1;
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
    OCR1A = maxInteruptsPerStep;
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
