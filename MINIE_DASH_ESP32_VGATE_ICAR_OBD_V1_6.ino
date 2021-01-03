/*  This sketch asks relevant battery data from the ODB canbus of a Mini Cooper SE Electric (or BMW i3)
    Hardware needed :
    a VGATE ICAR PRO 4.0 BT OBD adapter to get to the data wirelessly
    an ESP32 arduino board made by adafruit : Huzzah32 Feather
    a 3.5" LCD made by adafruit : 3.5 TFT Featherwing
    a USB to microUSB cable to supply power to the board/LCD (or a 12V to microUSB)
    A housing for the feather (ESP32) and featherwing (LCD) : thingiverse search "featherwing 3.5"
    a micro SD card for storing log data

    soldering of 2 wires and a resistor is needed for full functionality :
    All soldering is done on the 3.5" TFT Featherwing LCD board
    1 wire from the INT (touchscreen interrupt) pad on the LCD to GPIO_27 pad, also on the LCD. This enables the wake-up when sleeping by touching the touchscreen (anywhere)
    1 wire from the LITE (LCD background LED) pad on the LCD to GPIO_21. This enables backlight dimming control and a much shorter white screen on startup
    1 resistor of 3.3 kOhm (max!) from GND to GPIO-21. This prevents a solid white screen on power-up (because the uC is not active yet)
    If the first wire is not connected then the only way to power-up the unit from after pressing the sleep button is to cycle the power
    The latter 2 modifications prevent or limit the "solid white screen" on power-up of the unit
    Not doing any of the above modifications still ensure normal functionality after start-up

    Boards to be installed from the arduino IDE
    ESP32

    libraries to be installed (from the arduino IDE):
    TFT_eSPI for the LCD
    BluetoothSerial.h for the interaction with the Bluetooth OBD module
    Adafruit_STMPE610 for the touchscreen
    SD.h for the SD card on the LCD (to log data)

    IMPORTANT : For the TFT-eSPI library alter the following in the user-setup.h

    #define HX8357D_DRIVER // uncheck all other drivers
    #define TFT_MISO 19
    #define TFT_MOSI 18 // 23
    #define TFT_SCLK 5 // 18
    #define TFT_CS 15 // Chip select control pin
    #define TFT_DC 33 // 2 // Data Command control pin
    //#define TFT_RST 4 // Reset pin (could connect to RST pin)
    #define TFT_RST -1 // Set TFT_RST to -1 if display RESET is connected to ESP32 board RST

    #define TOUCH_CS 32 // Chip select pin (T_CS) of touch screen
    #define SPI_FREQUENCY 20000000 // you can try higher frequencies but may fail
    #define SPI_READ_FREQUENCY 20000000
    #define SPI_TOUCH_FREQUENCY 2500000

    A huge huge thanks to the developer of the app "electrified" for a much easier reverse engineering of the PID's and AT commands needed to gain acces to the SME (battery control unit). Also thanks to wireshark for providing the software to decode the signals sent
    v1.1 second page with chargegraph and touchscreen actions.
    v1.2 touchscreen used to send the unit to sleep AND to wake it up (needs 2 wires and 1 resistor)
    v1.3 reduced flickering by not refreshing the screen when no value is altered.
    v1.4 logging to SD card (incl downloading through serial monitor)
    v1.5 touchscreen choice at startup (demo or live) to avoid the OBD and the alarm system of the MINI activating on startup when power is applied (i.e. from remote activation of pre-conditioning.
            also general cleanup and simplification of the code
    v1.6 car speed, range and outside temperature added to main screen (no space on charge screen)
*/

#include "BluetoothSerial.h"
#include <TFT_eSPI.h>
#include <Adafruit_STMPE610.h>
#include <SD.h>

BluetoothSerial SerialBT;
TFT_eSPI tft = TFT_eSPI();                   // Invoke custom library with default width and height
#define STMPE_CS 32
#define SD_CS    14
Adafruit_STMPE610 ts = Adafruit_STMPE610(STMPE_CS);
File LogFile;

#define TS_MINX 3800
#define TS_MAXX 100
#define TS_MINY 100
#define TS_MAXY 3750

#define DEBUG_PORT Serial
#define ELM_PORT   SerialBT
#define TFT_GRAY 0x7BEF
#define LARGEFONT &FreeSans24pt7b
#define MEDIUMFONT &FreeSans18pt7b
#define SMALLFONT &FreeSans12pt7b
#define XSMALLFONT &FreeSans9pt7b
#define STARTUP_TIMEOUT 12000                   // 10 second timeout on startup (takes 2 seconds to boot)
#define DEBUG 0


//#define REMOVE_BONDED_DEVICES 0

int TFT_OFFWHITE = ((0xFF & 0xF8) << 8) | ((0xFF & 0xFC) << 3) | (0xC0 >> 3);
unsigned long lastTimeData = 0;
unsigned long getDataInterval = 15000;       //get data every 10 seconds (8 data strings, about every second a new value is passed to the bus, gives 2 seconds "rest")|
unsigned long chargeStartTime = 0;
unsigned long startMillis = 0;
unsigned long stopMillis = 0;
String BTResponse = "";
float batterySOC = 0;
float batteryVoltage = 0;
float batteryCurrent = 0;
float batteryPower = 0;
float batteryTempAverage = 0;
float batteryTempMin = 0;
float batteryTempMax = 0;
float batteryCapacity = 0;
float batterySOCHV = 0;
float batterySOCHVMax = 0;
float batterySOCHVMin = 0;
float batteryCellVoltage = 0;
float batteryCellVoltageMin = 0;
float batteryCellVoltageMax = 0;
float batteryBalance = 0;
float batterySOH = 0;
float carRange = 0;
float carSpeed = 0;
float carOutsideTemp = 0;
String twelveV = "-.-";
RTC_DATA_ATTR float chargingPowerSOC[101];          // store this in memory when going to sleep
RTC_DATA_ATTR float chargingTempSOC[101];           // store this in memory when going to sleep
int FONT = 1;
int LCDbrightness = 128;
int screenNumber = 0;
int padding = 0;
bool headerLogged = 0;
bool demoMode = 1;
bool choiceMade = 0;
float timeToEightyPercent = 0;


void setup()
{
    ledcSetup(0, 1000, 8);   // backlight PWM on pin 21
    ledcAttachPin(21, 0);
    ledcWrite(0, 0);        // blank the backlight ASAP

    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);
    pinMode(GPIO_NUM_27, INPUT_PULLUP);  // interrupt from touchscreen

    DEBUG_PORT.begin(115200);
    //ELM_PORT.setPin("1234");
    tft.begin();
    tft.fillScreen(TFT_BLACK);
    tft.setRotation(2);
    tft.setTextSize(1);                                                             // Set the fontsize (see library for more fonts)
    tft.setTextWrap(0);                                                             // Set the text wrap of the TFT (0 = no text wrap)
    tft.setTextWrap(false, false);
    tft.setTextDatum(MC_DATUM);                                                     // text alignment middle center
    tft.setSwapBytes(true);
    tft.setTextColor(TFT_OFFWHITE, TFT_BLACK);
    ledcWrite(0, LCDbrightness);                               // set Backlight to "on" using the brightness setting
    tft.setCursor(0, 10);
    if (!ts.begin()) {
        tft.println("Couldn't start touchscreen controller");
        tft.println("restart arduino to try again");
        while (1);
    }
    //tft.println("Touchscreen started");
    startupScreen();
    while (!choiceMade) {
        checkTouchScreen();
        int secToGo = (STARTUP_TIMEOUT - millis()) / 1000;
        if (millis() > STARTUP_TIMEOUT) {
            goToSleep();
        }
        tft.setTextPadding(30);
        tft.drawString(String(secToGo), 160, 400, 4);

    }
    tft.fillScreen(TFT_BLACK);
    drawMiniCooperSE();
    if (demoMode == 0) {
        ELM_PORT.begin("ESP32test", true);
        tft.println("Attempting to connect to VGATE");
        if (!ELM_PORT.connect("Android-Vlink"))    // VGATE
        {
            tft.println("Couldn't connect to VGATE");
            tft.println("restart arduino to try again");
            while (1);
        }
        tft.println("Connected to VGATE");
        initVGate();
        tft.println("VGATE initilisation complete");
        initForSME();
        tft.println("Ready to read from SME");
        tft.fillScreen(TFT_BLACK);
    }

    if (demoMode == 1) {
        getDataInterval = 1000;  // 1000;
        batteryTempAverage = random(0, 40);
    }
    lastTimeData = -getDataInterval; // make sure the loop starts right away
}


void loop() {
    if (millis() > lastTimeData + getDataInterval) {
        lastTimeData = millis();
        if (demoMode == 1) {
            demoValues();
        } else {
            initForSME();
            getPIDfromSME();
            initForRangeSpeed();
            getPIDForRangeSpeed();
            logToSD();
        }
        logToChargeGraph(batterySOC, batteryPower, batteryTempAverage);
        showScreen();
    }
    checkTouchScreen();
    checkSerial();
}

void showScreen() {
    switch (screenNumber) {
        case 0:
            //startUpScreen();
            break;
        case 1: // main screen
            mainscreenFixed();
            mainscreenVariables();
            break;
        case 2: // charging graphscreen
            chargeScreenFixed();
            chargeScreenVariables();
            break;
        case 3:
            break;
        default:
            screenNumber = 1;
            break;
    }
    checkTouchScreen();
}




void startupScreen() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_OFFWHITE, TFT_BLACK);
    drawMiniCooperSE();
    tft.drawRoundRect(30, 10, 100, 80, 10, TFT_OFFWHITE);
    tft.drawString("SLEEP", 80, 50, 4);

    tft.drawRoundRect(30, 360, 100, 80, 10, TFT_OFFWHITE);
    tft.drawString("DEMO", 80, 400, 4);

    tft.drawRoundRect(190, 360, 100, 80, 10, TFT_OFFWHITE);
    tft.drawString("LIVE", 240, 400, 4);
}

void mainscreenFixed() {
    // UNITS in GREY
    // line 1
    tft.setTextFont(2);
    tft.setTextColor(TFT_GRAY, TFT_BLACK);
    padding = 0; //    tft.textWidth("[%]" );   padding is not neccessary because of stationary text
    tft.setTextPadding(padding);
    tft.drawString("12V Volt[V}"      , TFT_WIDTH / 6         , 10 );
    tft.drawString("SOC[%]"           , TFT_WIDTH / 2         , 10 );
    tft.drawString("Time [HH:MM]"     , TFT_WIDTH * 5 / 6     , 10 );
    // line 2
    tft.drawString("Voltage [V]"     , TFT_WIDTH / 6         , 85 );
    tft.drawString("Current [A]"     , TFT_WIDTH / 2         , 85 );
    tft.drawString("Power [kW]"      , TFT_WIDTH * 5 / 6     , 85 );
    //line 3
    tft.drawString("CellVoltage [V]" , TFT_WIDTH / 6         , 160 );
    tft.drawString("CellTemp.[C]"    , TFT_WIDTH / 2         , 160 );
    tft.drawString("SOC HV [%]"      , TFT_WIDTH * 5 / 6     , 160 );

    tft.drawString("Max"             , TFT_WIDTH / 3         , 215 );
    tft.drawString("Min"             , TFT_WIDTH / 3         , 235 );
    tft.drawString("Max"             , TFT_WIDTH * 2 / 3     , 215 );
    tft.drawString("Min"             , TFT_WIDTH * 2 / 3     , 235 );

    tft.drawString("Balance [mV]"    , TFT_WIDTH / 6         , 275 );
    tft.drawString("Capacity [Ah]"   , TFT_WIDTH / 2         , 275 );
    tft.drawString("SOH [%]"         , TFT_WIDTH * 5 / 6     , 275 );

    tft.drawString("Range [km]"      , TFT_WIDTH / 6         , 350 );
    tft.drawString("Speed [kph]"     , TFT_WIDTH / 2         , 350 );
    tft.drawString("Outs.Temp [C]"   , TFT_WIDTH * 5 / 6     , 350 );

    drawMiniCooperSE();
}

void mainscreenVariables() {
    // **** VARIABLES *************************************************
    // line 1
    String draw_String = "";
    tft.setTextColor(TFT_OFFWHITE, TFT_BLACK);
    tft.setFreeFont(LARGEFONT);
    padding = tft.textWidth("888.8");
    tft.setTextPadding(padding);
    tft.setTextDatum(MC_DATUM);                     // text alignment middle center
    // batterySOC
    if (batterySOC < 99.5)  draw_String = String(batterySOC, 1);
    else                    draw_String = String(batterySOC, 0);
    tft.drawString(draw_String                                                      , TFT_WIDTH / 2   , 35 );

    tft.setFreeFont(MEDIUMFONT);
    padding = tft.textWidth("88.8V") + 1;
    tft.setTextPadding(padding);
    tft.drawString(twelveV                                                          , TFT_WIDTH / 6   , 35);

    padding = tft.textWidth("8:88") + 1;
    tft.setTextPadding(padding);
    int Minutes = int(millis() * 0.001 / 60);
    String LeadingZero = "";
    if (Minutes % 60 < 10) {
        LeadingZero = "0";
    }
    tft.drawString(String(Minutes / 60) + ":" + LeadingZero + String(Minutes % 60) , TFT_WIDTH * 5 / 6, 35);

    // line 2
    // **** BATTERY VOLTAGE ********************************************
    padding = tft.textWidth("888.88") + 1;
    tft.setTextPadding(padding);
    draw_String = String(batteryVoltage, 2);
    tft.drawString(draw_String   , TFT_WIDTH / 6  , 110  );

    // **** BATTERY CURRENT ********************************************
    padding = tft.textWidth("-88.88");
    tft.setTextPadding(padding);
    if (abs(batteryCurrent) > 100)      draw_String = String(batteryCurrent, 0);
    else if (abs(batteryCurrent) > 10)  draw_String = String(batteryCurrent, 1);
    else                                draw_String = String(batteryCurrent, 2);
    tft.drawString(draw_String, TFT_WIDTH / 2     , 110  );

    // **** BATTERY POWER ********************************************
    padding = tft.textWidth("8888.8");
    tft.setTextPadding(padding);
    if (abs(batteryPower) < 100)        draw_String = String(batteryPower, 1);
    else                                draw_String = String(batteryPower, 0);
    tft.drawString(draw_String, TFT_WIDTH * 5 / 6 , 110 );

    // line 3
    // **** CELL VOLTAGE
    padding = tft.textWidth("99.99") + 1;
    tft.setTextPadding(padding);
    tft.drawString(String(batteryCellVoltage, 3)  , TFT_WIDTH / 6    , 185 );
    if (batteryTempAverage < 10 | batteryTempAverage > 40) {
        tft.setTextColor(TFT_RED, TFT_BLACK);
    }
    else tft.setTextColor(TFT_OFFWHITE, TFT_BLACK);
    tft.drawString(String(batteryTempAverage, 1)  , TFT_WIDTH / 2    , 185 );
    tft.setTextColor(TFT_OFFWHITE, TFT_BLACK);
    tft.drawString(String(batterySOCHV, 1)        , TFT_WIDTH * 5 / 6, 185 );

    // line 4
    // **** BALANCE , CAPACITY , HEALTH ********************************
    padding = tft.textWidth("99.99") + 1;
    tft.setTextPadding(padding);
    tft.drawString(String(batteryBalance, 0)      , TFT_WIDTH / 6     , 300 );
    tft.drawString(String(batteryCapacity, 1)     , TFT_WIDTH / 2     , 300 );
    tft.drawString(String(batterySOH, 0)          , TFT_WIDTH * 5 / 6 , 300 );

    // line 5
    padding = tft.textWidth("999.9") + 1;
    tft.setTextPadding(padding);
    tft.drawString(String(carRange, 0)          , TFT_WIDTH / 6     , 375 );
    tft.drawString(String(carSpeed, 1)          , TFT_WIDTH / 2     , 375 );
    tft.drawString(String(carOutsideTemp, 1)    , TFT_WIDTH * 5 / 6 , 375 );

    //line 3 small
    tft.setFreeFont(XSMALLFONT);
    // **** CELL VOLTAGE SMALL
    padding = tft.textWidth("99999") + 1;
    tft.setTextPadding(padding);
    tft.drawString(String(batteryCellVoltageMax, 3)  , TFT_WIDTH / 6 , 215 );
    tft.drawString(String(batteryCellVoltageMin, 3)  , TFT_WIDTH / 6 , 235 );

    padding = tft.textWidth("-99.9") + 1;
    tft.setTextPadding(padding);
    if (batteryTempMax < 10 | batteryTempMax > 40)    tft.setTextColor(TFT_RED, TFT_BLACK);
    else tft.setTextColor(TFT_OFFWHITE, TFT_BLACK);
    tft.drawString(String(batteryTempMax, 1)  , TFT_WIDTH / 2     , 215 );
    if (batteryTempMin < 10 | batteryTempMin > 40) {
        tft.setTextColor(TFT_RED, TFT_BLACK);
    }
    else tft.setTextColor(TFT_OFFWHITE, TFT_BLACK);
    tft.drawString(String(batteryTempMin, 1)  , TFT_WIDTH / 2     , 235 );
    tft.setTextColor(TFT_OFFWHITE, TFT_BLACK);
    padding = tft.textWidth("999.9") + 2;
    tft.setTextPadding(padding);
    tft.drawString(String(batterySOCHVMax, 1)  , TFT_WIDTH * 5 / 6, 215 );
    tft.drawString(String(batterySOCHVMin, 1)  , TFT_WIDTH * 5 / 6, 235 );

    if (demoMode == 1) {
        tft.setTextFont(4);
        tft.drawString("DEMOMODE"  , TFT_WIDTH / 2, 420 );
    }
    // responsetime or color
    tft.setTextFont(2);
    tft.setTextColor(TFT_GRAY, TFT_BLACK);
    //tft.drawString(String(stopMillis - startMillis) + " ms", 30, 465);        // show response time per value
    //tft.drawString(String("0x") + String(TFT_OFFWHITE, HEX), 30, 465);        // show color of OFF_WHITE
    tft.setTextColor(TFT_OFFWHITE, TFT_BLACK);
}

void chargeScreenFixed() {
    // draw the grpah box
    tft.setTextColor(TFT_OFFWHITE);
    tft.drawFastVLine(20, 0, 250, TFT_OFFWHITE);
    tft.drawFastHLine(20, 250, 320, TFT_OFFWHITE);
    tft.drawFastHLine(20, 0, 320, TFT_OFFWHITE);
    tft.drawFastVLine(319, 0, 250, TFT_OFFWHITE);

    for (int y = 25; y < 250; y = y + 25)  {
        for (int x = 25; x < 320 ; x = x + 5) {
            tft.drawPixel(x, y, TFT_GRAY);
        }
    }
    for (int y = 5; y < 250; y = y + 5)  {
        for (int x = 20; x < 320 ; x = x + 30) {
            tft.drawPixel(x, y, TFT_GRAY);
        }
    }
    //draw the graph unit lines (y-axis)
    for (int i = 250; i > 0; i = i - 25) {
        tft.drawLine(17, i, 19, i, TFT_OFFWHITE);
    }
    //draw the unit numbers (y-axis)
    for (int i = 250; i > 0; i = i - 25) {
        tft.drawString(String((250 - i) / 5), 8, i, 2);
    }

    for (int i = 20; i <= 320; i = i + 30) {
        tft.drawLine(i, 251, i, 253, TFT_OFFWHITE);
    }
    for (int i = 20; i < 320; i = i + 30) {
        tft.setFreeFont(XSMALLFONT);
        tft.drawString(String((i - 20) / 3), i, 265, 2);
    }
    // UNITS of values in GREY
    // line 1
    tft.setTextFont(2);
    //tft.setFreeFont(XSMALLFONT);
    tft.setTextColor(TFT_GRAY, TFT_BLACK);
    padding = 0; // tft.textWidth("[%]" );
    tft.setTextPadding(padding);
    tft.drawString("12V Volt[V}"      , TFT_WIDTH / 6         , 290 );
    tft.drawString("SOC[%]"           , TFT_WIDTH / 2         , 290 );
    tft.drawString("Time [HH:MM]"     , TFT_WIDTH * 5 / 6     , 290 );
    // line 2
    tft.setTextPadding(padding);
    tft.drawString("Voltage [V]"     , TFT_WIDTH / 6         , 345 );
    tft.drawString("Current [A]"     , TFT_WIDTH / 2         , 345 );
    tft.drawString("Power [kW]"      , TFT_WIDTH * 5 / 6     , 345 );
    //line 3
    tft.drawString("CellVoltage [V]" , TFT_WIDTH / 6         , 400 );
    tft.drawString("CellTemp.[C]"    , TFT_WIDTH / 2         , 400 );
    tft.drawString("Balance [mV]"    , TFT_WIDTH * 5 / 6     , 400 );

    // draw the thrashcan
    tft.drawLine(300, 10, 310, 10, TFT_OFFWHITE);
    tft.drawLine(302, 11, 302, 20, TFT_OFFWHITE);
    tft.drawLine(308, 11, 308, 20, TFT_OFFWHITE);
    tft.drawLine(302, 20, 308, 20, TFT_OFFWHITE);
    tft.drawLine(304, 13, 304, 18, TFT_OFFWHITE);
    tft.drawLine(306, 13, 306, 18, TFT_OFFWHITE);
    tft.drawLine(303, 8, 307, 8, TFT_OFFWHITE);
    tft.drawLine(303, 8, 303, 10, TFT_OFFWHITE);
    tft.drawLine(307, 8, 307, 10, TFT_OFFWHITE);

    if (demoMode == 1) {
        tft.setTextFont(4);
        tft.setTextColor(TFT_OFFWHITE, TFT_BLACK);
        tft.drawString("DEMOMODE", TFT_WIDTH / 2, 235 );
    }
    drawMiniCooperSE();
}

void drawMiniCooperSE() {
    tft.setTextPadding(0);
    tft.setTextColor(TFT_OFFWHITE, TFT_BLACK);
    tft.drawString("Mini Cooper SE"  , TFT_WIDTH / 2, 467, 4);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString("SE"  , TFT_WIDTH / 2 + 72, 467, 4);
    tft.setTextColor(TFT_OFFWHITE, TFT_BLACK);
}

void chargeScreenVariables() {

    // draw the arrays
    for (int i = 0; i <= 100; i++) {
        if (chargingPowerSOC[i] > 0 && chargingPowerSOC[i + 1] > 0) {
            tft.drawLine(20 + 3 * i, 250 - 5 * chargingPowerSOC[i], 20 + 3 * i + 3, 250 - 5 * chargingPowerSOC[i + 1], TFT_YELLOW);
        }
        if (chargingTempSOC[i] > 0 && chargingTempSOC[i + 1] > 0) {
            tft.drawLine(20 + 3 * i, 250 - 5 * chargingTempSOC[i], 20 + 3 * i + 3, 250 - 5 * chargingTempSOC[i + 1], TFT_RED);
        }
    }
    // line 1
    String draw_String = "";
    tft.setTextColor(TFT_OFFWHITE, TFT_BLACK);
    tft.setFreeFont(MEDIUMFONT);
    padding = tft.textWidth("188.8%");
    tft.setTextPadding(padding);
    tft.setTextDatum(MC_DATUM);                     // text alignment middle center
    // batterySOC
    draw_String = String(batterySOC, 1) + "%";
    tft.drawString(draw_String                                                      , TFT_WIDTH / 2   , 315 );

    padding = tft.textWidth("88.8V") + 1;
    tft.setTextPadding(padding);
    tft.drawString(twelveV                                                          , TFT_WIDTH / 6   , 315);

    padding = tft.textWidth("8:88") + 1;
    tft.setTextPadding(padding);
    int Minutes = int((millis() - chargeStartTime) * 0.001 / 60);
    String LeadingZero = "";
    if (Minutes % 60 < 10) {
        LeadingZero = "0";
    }
    tft.drawString(String(Minutes / 60) + ":" + LeadingZero + String(Minutes % 60) , TFT_WIDTH * 5 / 6, 315);

    // **** BATTERY VOLTAGE ********************************************
    padding = tft.textWidth("888.88") + 1;
    tft.setTextPadding(padding);
    draw_String = String(batteryVoltage, 2);
    tft.drawString(draw_String   , TFT_WIDTH / 6  , 370  );

    // **** BATTERY CURRENT ********************************************
    padding = tft.textWidth("-888.8");
    tft.setTextPadding(padding);
    if (abs(batteryCurrent) > 100)      draw_String = String(batteryCurrent, 1);
    else if (abs(batteryCurrent) > 10)  draw_String = String(batteryCurrent, 1);
    else                                draw_String = String(batteryCurrent, 2);
    tft.drawString(draw_String, TFT_WIDTH / 2     , 370  );

    // **** BATTERY POWER ********************************************
    padding = tft.textWidth("8888.8");
    tft.setTextPadding(padding);
    if (abs(batteryPower) < 100)        draw_String = String(batteryPower, 1);
    else                                draw_String = String(batteryPower, 0);
    tft.drawString(draw_String, TFT_WIDTH * 5 / 6 , 370 );

    // line 3
    // **** CELL VOLTAGE
    padding = tft.textWidth("99.99") + 1;
    tft.setTextPadding(padding);
    tft.drawString(String(batteryCellVoltage, 3)  , TFT_WIDTH / 6    , 425 );
    if (batteryTempAverage < 10 | batteryTempAverage > 40) {
        tft.setTextColor(TFT_RED, TFT_BLACK);
    }
    else tft.setTextColor(TFT_OFFWHITE, TFT_BLACK);
    tft.drawString(String(batteryTempAverage, 1)  , TFT_WIDTH / 2    , 425 );
    tft.setTextColor(TFT_OFFWHITE, TFT_BLACK);
    tft.drawString(String(batteryBalance, 0)        , TFT_WIDTH * 5 / 6, 425 );

    tft.setTextFont(2);
    padding = tft.textWidth("110.1 min") + 1;
    tft.setTextPadding(padding);
    tft.setTextColor(TFT_GRAY, TFT_BLACK);
    if (batteryPower > 3 && batterySOC <= 85) {
        //tft.drawString("to80%[min]", 285, 455);
        //tft.drawString(String(stopMillis - startMillis) + " ms", 30, 465);
        tft.setTextColor(TFT_OFFWHITE, TFT_BLACK);
        tft.drawString(String(timeToEightyPercent, 1) + " min" , 275, 235); //, 285, 470);
        tft.drawLine(275, 245, 275, 250, TFT_OFFWHITE);
    } else {
        //tft.drawString("", 285, 455);
        //tft.setTextColor(TFT_OFFWHITE, TFT_BLACK);
        tft.drawString("", 275, 235); //285, 470);
    }
    tft.setTextColor(TFT_OFFWHITE, TFT_BLACK);

}
void initVGate() {
    // Send a bunch of AT commands to the VGATE to initialise the ELM327 in the VGATE (ATD,ATE0,ATH1,ATAL,ATPBE101,ATSPB,ATBI,ATSH6F1,ATAT0,ATSTFF)
    tft.println("Initializing VGATE");
    sendATCommand("ATD");         // set to all defaults
    sendATCommand("ATE0");        // Echo off
    sendATCommand("ATH1");        // Headers On
    sendATCommand("ATAL");        // Allow long messages (> 7 bytes)
    sendATCommand("ATPBE101");    // Set protocol B options and baud rate
    sendATCommand("ATSPB");       // set protocol to B
    sendATCommand("ATBI");        // bypass the initialisation sequence
    sendATCommand("ATSH6F1");     // set header 6F1
    sendATCommand("ATAT0");       // adaptive timing off
    sendATCommand("ATSTEF");      // set timeout to FFx4 msec = 1024 msec = 1 sec
}

void initForSME() {                 // 607
    sendATCommand("ATCRA607");      // set can receive address to 607
    sendATCommand("ATCEA07");       // use can extended address 07
    sendATCommand("ATFCSH6F1");     // flow control set the header to 6F1
    sendATCommand("ATFCSD07300800");// flow control set data to 07 30 08 00
    sendATCommand("ATFCSM1");       //Can silent mode on
}

void initForCharger() {             // 615
    sendATCommand("ATCRA615");      // set can receive address to 615
    sendATCommand("ATCEA15");       // use can extended address 15
    sendATCommand("ATFCSH6F1");     // flow control set the header to 6F1
    sendATCommand("ATFCSD15300800");// flow control set data to 15 30 08 00
    sendATCommand("ATFCSM1");       //Can silent mode on
}

void initForFinishTime() {                 // 61A (charger finish time)
    sendATCommand("ATCRA61A");      // set can receive address to 61A
    sendATCommand("ATCEA1A");       // use can extended address 1A
    sendATCommand("ATFCSH6F1");     // flow control set the header to 6F1
    sendATCommand("ATFCSD15300800");// flow control set data to 15 30 08 00
    sendATCommand("ATFCSM1");       //Can silent mode on
}

void initForVIN() {                 // 640 VIN
    sendATCommand("ATCRA640");      // set can receive address to 640
    sendATCommand("ATCEA40");       // use can extended address 40
    sendATCommand("ATFCSH6F1");     // flow control set the header to 6F1
    sendATCommand("ATFCSD40300800");// flow control set data to 40 30 08 00
    sendATCommand("ATFCSM1");       //Can silent mode on
}

void initForRangeSpeed() {                  // 660 range electric & outside Temp
    sendATCommand("ATCRA660");      // set can receive address to 660
    sendATCommand("ATCEA60");       // use can extended address 60
    sendATCommand("ATFCSH6F1");     // flow control set the header to 6F1
    sendATCommand("ATFCSD60300800");// flow control set data to 60 30 08 00
    sendATCommand("ATFCSM1");       //Can silent mode on
}



void getPIDfromSME() {        // SME = PID 607
    int idx = 0;
    String Astring;
    int screenUpdate = false;

    //initialisation to read SME complete, reading values is next
    // get 12V from Vgate
    startMillis = millis();
    sendATCommand("ATRV");
    if (twelveV != BTResponse.substring(0, 4)) {
        twelveV  = BTResponse.substring(0, 4);
        screenUpdate = true;
    }

    // get State of Health
    sendATCommand("226335");        // byte 3 = SOH ; byte 0,1,2 unknown (/1)
    idx = BTResponse.indexOf("F1 21");
    stopMillis = millis();
    Astring = BTResponse.substring(idx + 9, idx + 11);
    long RAWsoh = hstol(Astring);
    if (batterySOH != RAWsoh or screenUpdate == true) {
        batterySOH = RAWsoh;
        showScreen();
    }

    //get battery voltage
    startMillis = millis();
    screenUpdate = false;
    sendATCommand("22DD68");        // byte 0,1 = battery voltage in deciVolt (/100)
    idx = BTResponse.indexOf("F1 05");
    stopMillis = millis();
    Astring = BTResponse.substring(idx + 15, idx + 17) + BTResponse.substring(idx + 18, idx + 20);
    long RAWvolt = hstol(Astring);
    if (batteryVoltage != RAWvolt * 0.01)  {
        batteryVoltage  = RAWvolt * 0.01;
        showScreen();
    }

    //get battery current
    startMillis = millis();
    screenUpdate = false;
    sendATCommand("22DD69");        // byte 0,1,2,3 = battery current in centi-amps (/100)
    idx = BTResponse.indexOf("F1 21");
    stopMillis = millis();
    Astring = BTResponse.substring(idx + 6, idx + 8) + BTResponse.substring(idx + 9, idx + 11);
    long RAWcurrent = hstol(Astring);
    if (RAWcurrent > 32768) RAWcurrent = RAWcurrent - 65535;
    if (batteryCurrent != RAWcurrent * 0.01) {
        batteryCurrent = RAWcurrent * 0.01;
        //additional calculations
        batteryPower = batteryVoltage * batteryCurrent / 1000;
        showScreen();
    }

    //get SOC
    startMillis = millis();
    screenUpdate = false;
    sendATCommand("22DDBC");        // byte 0,1 = SOC percent (/10) ; byte 2,3 = SOC_HVMax(/10) ; byte 4,5 = SOC_HVmin (/10)
    idx = BTResponse.indexOf("F1 10");
    stopMillis = millis();
    Astring = BTResponse.substring(idx + 18, idx + 20) + BTResponse.substring(idx + 21, idx + 23);
    long RAWsoc = hstol(Astring);
    if (batterySOC != RAWsoc * 0.1) {
        batterySOC  = RAWsoc * 0.1;
        screenUpdate = true;
    }
    // parse SOC_HVmax
    idx = BTResponse.indexOf("F1 21");
    Astring = BTResponse.substring(idx + 6, idx + 8) + BTResponse.substring(idx + 9, idx + 11);
    long RAWsochvmax = hstol(Astring);
    if (batterySOCHVMax != RAWsochvmax * 0.1) {
        batterySOCHVMax  = RAWsochvmax * 0.1;
        screenUpdate = true;
    }
    // parse SOC_HVmin
    Astring = BTResponse.substring(idx + 12, idx + 14) + BTResponse.substring(idx + 15, idx + 17);
    long RAWsochvmin = hstol(Astring);
    if (batterySOCHVMin != RAWsochvmin * 0.1) {
        batterySOCHVMin  = RAWsochvmin * 0.1;
        screenUpdate = true;
    }
    if (screenUpdate == true) {
        showScreen();
    }

    //get cell voltages (min)
    startMillis = millis();
    screenUpdate = false;
    sendATCommand("22DDBF");        // byte 0,1 = cell voltage min (/1000) ; byte 2,3 cell voltage max (/1000)
    idx = BTResponse.indexOf("F1 10");
    stopMillis = millis();
    Astring = BTResponse.substring(idx + 18, idx + 20) + BTResponse.substring(idx + 21, idx + 23);
    long RAWcvmin = hstol(Astring);
    if (batteryCellVoltageMin != RAWcvmin * 0.001) {
        batteryCellVoltageMin  = RAWcvmin * 0.001;
        screenUpdate = true;
    }
    //get cell voltage (max)
    //Astring = BTResponse.substring(idx + 35, idx + 37) + BTResponse.substring(idx + 38, idx + 40);
    idx = BTResponse.indexOf("F1 21");
    Astring = BTResponse.substring(idx + 6, idx + 8) + BTResponse.substring(idx + 9, idx + 11);
    long RAWcvmax = hstol(Astring);
    if (batteryCellVoltageMax != RAWcvmax * 0.001) {
        batteryCellVoltageMax  = RAWcvmax * 0.001;
        screenUpdate = true;
    }
    // calculate battery balance (max - min) in mV
    batteryBalance = (batteryCellVoltageMax - batteryCellVoltageMin) * 1000;
    if (screenUpdate == true) {
        showScreen();
    }

    //get battery temps
    startMillis = millis();
    screenUpdate = false;
    sendATCommand("22DDC0");        // byte 0,1 = Batt temp min (/100) ; byte 2,3 = Batt temp max (/100) ; byte 4,5 = Batt temp_avg (/100)
    idx = BTResponse.indexOf("F1 10");
    stopMillis = millis();
    Astring = BTResponse.substring(idx + 18, idx + 20) + BTResponse.substring(idx + 21, idx + 23);
    long RAWctmin = hstol(Astring);
    if (batteryTempMin != RAWctmin * 0.01) {
        batteryTempMin  = RAWctmin * 0.01;
        screenUpdate = true;
    }
    // battery temp max
    idx = BTResponse.indexOf("F1 21");
    Astring = BTResponse.substring(idx + 6, idx + 8) + BTResponse.substring(idx + 9, idx + 11);
    long RAWctmax = hstol(Astring);
    if (batteryTempMax != RAWctmax * 0.01) {
        batteryTempMax  = RAWctmax * 0.01;
        screenUpdate = true;
    }
    //battery temp avg
    Astring = BTResponse.substring(idx + 12, idx + 14) + BTResponse.substring(idx + 15, idx + 17);
    long RAWctavg = hstol(Astring);
    if (batteryTempAverage != RAWctavg * 0.01) {
        batteryTempAverage  = RAWctavg * 0.01;
        screenUpdate = true;
    }
    if (screenUpdate == true) {
        showScreen();
    }

    //get SOC_HV
    startMillis = millis();
    screenUpdate = false;
    sendATCommand("22DDC4");        // byte 0,1 : SOC_HV (/100)
    idx = BTResponse.indexOf("F1 06");
    stopMillis = millis();
    Astring = BTResponse.substring(idx + 15, idx + 17) + BTResponse.substring(idx + 18, idx + 20);
    long RAWsochv = hstol(Astring);
    if (batterySOCHV != RAWsochv * 0.01) {
        batterySOCHV  = RAWsochv * 0.01;
        showScreen();
    }

    // battery capacity
    startMillis = millis();
    screenUpdate = false;
    sendATCommand("22DFA0");        // byte 4,5 battery capacity (/100), byte 10,11 ?= cell voltage average (/10000)
    idx = BTResponse.indexOf("F1 10");
    stopMillis = millis();
    Astring = BTResponse.substring(idx + 18, idx + 20) + BTResponse.substring(idx + 21, idx + 23);
    //DEBUG_PORT.println(Astring);
    long RAWcap = hstol(Astring);
    if (batteryCapacity != RAWcap * 0.01) {
        batteryCapacity  = RAWcap * 0.01;
        screenUpdate = true;
    }
    //batteryCapacity = 0; // blokeren ivm onbetrouwbaar
    idx = BTResponse.indexOf("F1 22");
    Astring = BTResponse.substring(idx + 12, idx + 14) + BTResponse.substring(idx + 15, idx + 17);
    long RAWcvavg = hstol(Astring);
    if (batteryCellVoltage != RAWcvavg * 0.0001) {
        batteryCellVoltage  = RAWcvavg * 0.0001;
        screenUpdate = true;
    }
    if (screenUpdate == true) {
        showScreen();
    }
    //additional calculations
    if (batterySOC < 80 && batteryPower > 0) {
        timeToEightyPercent =  ((85 - batterySOC) * 0.01 * 96.2 * 3.6 * 96 * 0.001 * 60 ) / batteryPower;
    }
    else {
        timeToEightyPercent = 0;
    }
}

void getPIDForRangeSpeed() {
    int idx = 0;
    String Astring;
    int screenUpdate = false;

    // get range from module 660
    sendATCommand("22D111");        //  range
    idx = BTResponse.indexOf("F1 21");                      // 660 F1 03 7F 22 31 : is always 7F 22 31. no change after or during a drive
    Astring = BTResponse.substring(idx + 12, idx + 14) + BTResponse.substring(idx + 15, idx + 17);
    long RAWrange = hstol(Astring);
    if (carRange != RAWrange * 0.1 or screenUpdate == true) {
        carRange  = RAWrange * 0.1;
        showScreen();
    }

    sendATCommand("22D107");        // speed
    idx = BTResponse.indexOf("F1 05");                      // 660 F1 05 62 D1 07 00 00 ? 00 00 = 0000 = 0 kmh / also 0029 / 0108 / 0076 / 01DF / 00DE / 01C9
    Astring = BTResponse.substring(idx + 15, idx + 17 ) + BTResponse.substring(idx + 18, idx + 20);
    long RAWspeed = hstol(Astring);
    if (carSpeed != RAWspeed * 0.1 or screenUpdate == true) {
        carSpeed  = RAWspeed * 0.1;
        showScreen();
    }

    sendATCommand("22D112");        // outside Temp
    idx = BTResponse.indexOf("F1 05");                      // 660 F1 05 62 D1 12 6C 6C / also 6A 6C / 6B 6C : 6C = 108 / 2 - 40 = 54 - 40 = 14.5 / byte 1 is changing (not byte 0) / other time 5A 59 => 5B 5A could be 2 sensors ?? (or fast and average ??)
    Astring = BTResponse.substring(idx + 15, idx + 17 ); // + BTResponse.substring(idx + 21, idx + 23);
    long RAWtemp = hstol(Astring);
    if (carOutsideTemp != RAWtemp / 2 - 40 or screenUpdate == true) {
        carOutsideTemp  = RAWtemp / 2 - 40;
        showScreen();
    }

}

void logToChargeGraph(float soc, float battpower, float batttemp) {
    if (abs(battpower) > chargingPowerSOC[int(soc)]) {
        chargingPowerSOC[int(soc)] = abs(battpower);
    }
    if (abs(batttemp) > chargingTempSOC[int(soc)]) {
        chargingTempSOC[int(soc)] = abs(batttemp);
    }
}

void demoValues() {
    if (batteryVoltage < 320) batteryVoltage = 320;
    float Resistor = 0.16; //20/125;
    float chargeVoltage = batteryVoltage + 125 * Resistor;
    if (chargeVoltage > 400) chargeVoltage = 400;
    float demoValue = batterySOC * 0.01; //0.000005 * millis();
    batterySOH      = 100 * demoValue;              // 0-100%
    batteryVoltage  = batteryVoltage + batteryCurrent / 1000; //300 + 100 * sqrt(demoValue);        // 300-400 Volt
    if (batteryVoltage > 400) batteryVoltage = 400;
    if (batteryTempAverage < 20) {
        batteryCurrent  = batteryTempAverage * 0.05 * (chargeVoltage - batteryVoltage) / Resistor; //125; //-350 + (350 + 125) * demoValue; // max current withdrawal = 135kW/400V = 337A (set to 350A) max current charging = 125 => -350..+125
    } else {
        batteryCurrent  = (chargeVoltage - batteryVoltage) / Resistor;
    }
    if (batteryCurrent > 125) batteryCurrent = 125;
    batteryPower    = batteryVoltage * batteryCurrent / 1000;
    batterySOC      = 1.25 * (batteryVoltage - 320); //100 * demoValue;
    batterySOCHVMax = 90 + 10 * demoValue;
    batterySOCHVMin = 5 + 10 * demoValue;
    batteryCellVoltage = batteryVoltage / 96;
    batteryCellVoltageMin = batteryCellVoltage - 0.05 * demoValue;
    batteryCellVoltageMax = batteryCellVoltage + 0.05 * demoValue;
    batteryBalance = (batteryCellVoltageMax - batteryCellVoltageMin) * 1000;
    batteryTempAverage = batteryTempAverage + batteryCurrent * 0.0005 + 0.002 * (20 - batteryTempAverage);   // temperature rises with current and rises or falls with outside temp (20C)
    batteryTempMin = batteryTempAverage - 5 * demoValue;
    batteryTempMax = batteryTempAverage + 5 * demoValue;
    batterySOCHV = 100 * demoValue;
    batteryCapacity = 100 * demoValue;
    twelveV = String(9 + 6 * demoValue, 1);
    carRange = 250 - demoValue * 250;
    carSpeed = demoValue * 150;
    carOutsideTemp = -20 + demoValue * 100;
    if (batterySOC < 80 && batteryPower > 0) {
        timeToEightyPercent =  ((85 - batterySOC) * 0.01 * 96.2 * 3.6 * 96 * 0.001 * 60 ) / batteryPower;   // minutes/hour (60) * 96.2 Ah * 3.6 (avg cell volt) * 96 cells * 0.01
    }
    else {
        timeToEightyPercent = 0;
    }
}

void sendATCommand(String command) {
    BTResponse = "";
    ELM_PORT.print(command);
    ELM_PORT.print("\r");       // do not use println and do not use \r\n only \r
    // wait for response
    while (ELM_PORT.available() == 0) {               //  wait for a response
        // maybe add a time-out sequence
        checkTouchScreen();
        checkSerial(); // check touchscreen for possible download
    }
    while (1)   //(ELM_PORT.available())  // read the port until the > and the following \r
    {
        if (ELM_PORT.available()) {
            char c = ELM_PORT.read();
            BTResponse += c;
            if (c == '>') {
                ELM_PORT.read();  // also clear the \r in the response
                break;            // exit the while loop
            }
        }
    }
    if (screenNumber != 0) {
        if (BTResponse.indexOf("DATA") >= 0 or BTResponse.indexOf("ERROR") >= 0) {   // detect NO DATA or CAN ERROR MESSAGE
            tft.setTextFont(4);
            tft.drawString(command + " " + BTResponse              , TFT_WIDTH / 2         , 445 );
            delay(1000);
            goToSleep();
            BTResponse = "";                    // otherwise weird values appear
        }
        else {
            tft.drawString("                             "         , TFT_WIDTH / 2         , 445 );
        }
    }
    if (screenNumber == 3 && BTResponse.indexOf("OK>") < 0 && command.indexOf("AT") < 0 && BTResponse.indexOf("660") >= 0 && command == "22D111") {
        tft.print(command);
        tft.print(" ");
        tft.println(BTResponse);
    }
}



long hstol(String recv) {
    if (recv != "") {
        char c[recv.length() + 1];
        recv.toCharArray(c, recv.length() + 1);
        return strtol(c, NULL, 16);
    }
    else {
        return 0;
    }
}

void checkTouchScreen() {
    TS_Point p = ts.getPoint();
    p.x = map(p.x, TS_MINX, TS_MAXX, 0, 320);
    p.y = map(p.y, TS_MINY, TS_MAXY, 480, 0);

    if (screenNumber == 0) {
        if (p.x > 30 && p.x < 130 && p.y > 360 && p.y < 440 ) {                    // on startup screen, bottom left button : demo mode
            tft.fillScreen(TFT_BLACK);
            demoMode = 1;
            screenNumber = 1;
            choiceMade = 1;
        }
        if (p.x > 190 && p.x < 290 && p.y > 360 && p.y < 440 ) {                    // on startup screen, bottom right button : Live mode
            tft.fillScreen(TFT_BLACK);
            demoMode = 0;
            screenNumber = 1;
            choiceMade = 1;
        }
        if (p.x > 30 && p.x < 130 && p.y > 10 && p.y < 90 ) {                        // top Left corner : go to sleep
            choiceMade = 1;
            goToSleep();
        }
    }
    else {
        if (screenNumber == 1 && p.x > 160 && p.x < 320 && p.y > 0 && p.y < 100 && DEBUG == true) {  // upper right corner when in main screen, go to debug screen
            tft.fillScreen(TFT_BLACK);
            tft.setCursor(0, 5);
            tft.setTextFont(1);
            tft.setRotation(1);
            tft.setTextWrap(1);
            screenNumber = 3;
        }
        if (screenNumber == 2 && p.x > 160 && p.x < 320 && p.y > 0 && p.y < 100) {  // upper right corner when in charging screen
            memset(chargingPowerSOC, 0, sizeof(chargingPowerSOC));                  // clear the charging power array
            memset(chargingTempSOC, 0, sizeof(chargingTempSOC));                    // clear the charging power array
            chargeStartTime = millis();                                                    // set chargeStartTime to current millis()
            tft.fillScreen(TFT_BLACK);
            screenNumber = 2;
        }
        if (p.x > 160 && p.x < 320 && p.y > 360 && p.y < 480 ) {                    // bottom right corner : next screen
            tft.fillScreen(TFT_BLACK);
            screenNumber = 2;
        }
        if (p.x > 0 && p.x < 160 && p.y > 360 && p.y < 480 ) {                     // bottom Left corner : previous screen
            tft.fillScreen(TFT_BLACK);
            screenNumber = 1;
        }
        if (p.x > 0 && p.x < 130 && p.y > 0 && p.y < 100 ) {                        // top Left corner : go to sleep
            goToSleep();
        }
        if (DEBUG == 1) {
            if (p.x > 160 && p.x < 320 && p.y > 200 && p.y < 280 ) if (TFT_OFFWHITE != 0xFFFF)  TFT_OFFWHITE = TFT_OFFWHITE + 1;    // make the white whiter with middle right "button"
            if (p.x > 0 && p.x < 160 && p.y > 200 && p.y < 280 ) TFT_OFFWHITE = TFT_OFFWHITE - 1;                                   // make the white less white with middle left button
        }
    }
    while (!ts.bufferEmpty()) {
        TS_Point p = ts.getPoint();
    }
}

void logToSD() {
    String header = "SECONDS,SOC,VOLTAGE,CURRENT,POWER,TEMPERATURE,CELLVOLTAGE,BALANCE,HEALTH,CAPACITY,SOCHV,TWELVEVOLT,TEMPMIN,TEMPMAX,SOCHVMIN,SOCHVMAX,CELLVOLTMIN,CELLVOLTMAX";
    if (SD.begin(SD_CS)) {
        String dataString = String(millis() * 0.001, 0) + "," + String(batterySOC, 1) + "," + String(batteryVoltage, 2) + "," + String(batteryCurrent, 2) + "," + String(batteryPower, 2) + "," + String(batteryTempAverage, 1);
        dataString = dataString + "," + String(batteryCellVoltage, 3) + "," + String(batteryBalance, 0) + "," + String(batterySOH, 0) + "," + String(batteryCapacity, 1) + "," + String(batterySOCHV, 1) + "," + twelveV; // misc
        dataString = dataString + "," + String(batteryTempMin, 1) + "," + String(batteryTempMax, 1) + "," + String(batterySOCHVMin, 1) + "," + String(batterySOCHVMax, 1) + "," + String(batteryCellVoltageMin, 3) + "," + String(batteryCellVoltageMax, 3);
        LogFile = SD.open("/MINIlog.txt", FILE_APPEND);
        if (LogFile)
        {
            if (headerLogged == 0) {
                LogFile.println(header);
                headerLogged = true;
            }
            LogFile.println(dataString);
            LogFile.close();
        }
        else
        {
            Serial.println("error opening DATAlog.txt");
        }
    }
    else Serial.print("No SD");
    SD.end();
}

void checkSerial() {
    if (Serial.available() > 0) {
        int inByte = Serial.read();
        if (inByte == 'd') {
            if (!SD.begin(SD_CS)) {
                Serial.println("SD initialization failed!");
            }
            LogFile = SD.open("/MINIlog.txt", FILE_READ);
            if (LogFile)             {
                while (LogFile.available()) {
                    Serial.write(LogFile.read());
                }
                LogFile.close();
            }
            else {
                Serial.println("error opening MINIlog.txt");
            }
        }
        else if (inByte == 'e') {
            LogFile = SD.open("/MINIlog.txt", FILE_WRITE);
            if (LogFile) {
                LogFile.close();
                Serial.println("DATA Log erased");
            }
        }
        else if (inByte == 10) {
            // do nothing (Cariage return)
        }
        else
        {
            Serial.println("send 'd' for uploading DATA Log");
            Serial.println("send 'e' for erasing DATA Log");
        }
    }
}

void goToSleep() {
    while (!ts.bufferEmpty()) {                     // empty the touchscreen buffer
        TS_Point p = ts.getPoint();
    }
    tft.fillScreen(TFT_BLACK);                      // blank the screen
    tft.setFreeFont(MEDIUMFONT);
    tft.setTextColor(TFT_OFFWHITE, TFT_BLACK);         // set the text to white on a blck background
    tft.drawString("GOING TO SLEEP", 160, 240);     // show text
    delay(1000);                                    // show the text for 1 second
    ledcWrite(0, 0);                                // disable the backlight
    ts.writeRegister8(STMPE_INT_EN,   0x01);        // set touchscreen interrupt enable
    ts.writeRegister8(STMPE_INT_CTRL, 0x01);        // set interrut control register to active low, level interrupt, global interrupt
    ts.writeRegister8(STMPE_INT_STA,  0xFF);        // clear any existing interrupts
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_27, 0);   // wake up the ESP32 (reset) when 0V detected on GPIO 27 to which the INT output of the STMPE610 is connected
    esp_deep_sleep_start();                         // go to deep sleep (and wait for a touch on the screen to wake up
}
