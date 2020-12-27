/*  This sketch asks relevant battery data from the ODB canbus of a Mini Cooper SE Electric (or BMW i3)
    Hardware needed :
    a VGATE ICAR 4.0 BT OBD adapter to get to the data wirelessly
    an ESP32 board made by adafruit : Huzzah32 Feather
    a 3.5" LCD made by adafruit : 3.5 TFT Featherwing
    a USB to microUSB cable to supply power to the board/LCD (or a 12V to microUSB)
    A housing for the feather (ESP32) and featherwing (LCD) : thingiverse search "featherwing 3.5"

    soldering 2 wires and a resistor is needed for full functionality :
    1 wire from the INT (touchscreen interrupt) pad on the LCD to GPIO_27 pad, also on the LCD. This enables the wake-up when sleeping by touching the touchscreen (anywhere)
    1 wire from the LITE (LCD background LED) pad on the LCD to GPIO_21. This enables backlight dimming control and a much shorter white screen on startup
    1 resistor of 3.3 kOhm (max!) from GND to GPIO-21. This prevents a solid white screen on power-up (because the uC is not active yet)
    If the first wire is not connected then the only way to power-up the unit from after pressing the sleep button is to cycle the power
    The latter 2 modifications prevent or limit the "solid white screen" on power-up of the unit
    Not doing any of the above modifications still ensure normal functionality after start-up

    libraries to be installed :
    TFT_eSPI for the LCD
    BluetoothSerial.h for the interaction with the Bluetooth OBD module
    Adafruit_STMPE610 for the touchscreen

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
    v1.2 touchscreen used to send the unit to sleep AND to wake it up (needs 2 wires and 1 resistor
    v1.3 reduced flickering by not refreshing the screen when no value is altered.
    v1.4 logging to SD card (incl downloading through serial monitor)
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
#define BENCH_TEST 0                        // set 0 for normal operation, set to 1 for bench testing the display (no BT)
#define TFT_GRAY 0x7BEF
#define LARGEFONT &FreeSans24pt7b
#define MEDIUMFONT &FreeSans18pt7b
#define SMALLFONT &FreeSans12pt7b
#define XSMALLFONT &FreeSans9pt7b

//#define REMOVE_BONDED_DEVICES 0

unsigned long lastTimeData = 0;
unsigned long getDataInterval = 10000;       //get data every 10 seconds (8 data strings, about every second a new value is passed to the bus, gives 2 seconds "rest")|
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
float batteryRange = 0;
float vehicleSpeed = 0;
float outsideTemp = 0;
String twelveV = "-.-";
float chargingPowerSOC[101];
float chargingTempSOC[101];
int FONT = 1;
int LCDbrightness = 128;
int screenNumber = 0;
int padding = 0;
bool headerLogged = 0;


void setup()
{
    if (BENCH_TEST == 1) getDataInterval = 1000;
    ledcSetup(0, 1000, 8);   // backlight PWM on pin 21
    ledcAttachPin(21, 0);
    ledcWrite(0, 0);        // blank the backlight ASAP

    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);
    pinMode(GPIO_NUM_27, INPUT_PULLUP);  // interrupt from touchscreen

    DEBUG_PORT.begin(115200);
    //ELM_PORT.setPin("1234");
    ELM_PORT.begin("ESP32test", true);

    tft.begin();
    tft.fillScreen(TFT_BLACK);
    tft.setRotation(2);
    tft.setTextSize(1);                                                             // Set the fontsize (see library for more fonts)
    tft.setTextWrap(0);                                                             // Set the text wrap of the TFT (0 = no text wrap)
    tft.setTextWrap(false, false);
    tft.setTextDatum(MC_DATUM);                     // text alignment middle center
    tft.setSwapBytes(true);
    //tft.pushImage(TFT_WIDTH/2 - 200 / 2, TFT_HEIGHT/2 - 182 / 2, 200, 182, ZeroLogo);
    //delay(1000);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    ledcWrite(0, LCDbrightness);                               // set Backlight to "on" using the brightness setting
    drawMiniCooperSE();
    tft.setCursor(0, 10);
    if (!ts.begin()) {
        tft.println("Couldn't start touchscreen controller");
        while (1);
    }
    tft.println("Touchscreen started");
    tft.println("Attempting to connect to VGATE");
    dumpLogFromSDToSerial();                // check the serial monitor if download is to be started
    if (BENCH_TEST == 0) {
        if (!ELM_PORT.connect("Android-Vlink"))    // VGATE
        {
            tft.println("Couldn't connect to VGATE");
            tft.println("restart arduino to try again");
            //DEBUG_PORT.println("Couldn't connect to OBD scanner");
            while (1);
        }
        tft.println("Connected to VGATE");
        //DEBUG_PORT.println("Connected to Vgate");
        if (BENCH_TEST != 1)     {
            initVGate();
            tft.println("VGATE initilisation complete");
            initForSME();
            tft.println("Ready to read from SME");
        }
    }
    tft.fillScreen(TFT_BLACK);
    lastTimeData = -getDataInterval; // make sure the loop starts right away
}


void loop() {
    if (millis() > lastTimeData + getDataInterval) {
        lastTimeData = millis();
        if (BENCH_TEST == 1) {
            demoValues();
        } else {
            getPIDfromSME();
            logToSD();
        }
        logToChargeGraph(batterySOC, batteryPower, batteryTempAverage);
        showScreen();
    }
    checkTouchScreen();
    dumpLogFromSDToSerial();
}

void showScreen() {
    switch (screenNumber) {
        case 0: // main screen
            mainscreenFixed();
            mainscreenVariables();
            break;
        case 1: // charging graphscreen
            chargeScreenFixed();
            chargeScreenVariables();
            break;
        default:
            screenNumber = 0;
            break;
    }
    checkTouchScreen();
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

    drawMiniCooperSE();
}

void mainscreenVariables() {
    // **** VARIABLES *************************************************
    // line 1
    String draw_String = "";
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
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
    else tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(String(batteryTempAverage, 1)  , TFT_WIDTH / 2    , 185 );
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(String(batterySOCHV, 1)        , TFT_WIDTH * 5 / 6, 185 );

    // line 4
    // **** BALANCE , CAPACITY , HEALTH ********************************
    padding = tft.textWidth("99.99") + 1;
    tft.setTextPadding(padding);
    tft.drawString(String(batteryBalance, 0)      , TFT_WIDTH / 6     , 300 );
    tft.drawString(String(batteryCapacity, 1)     , TFT_WIDTH / 2     , 300 );
    tft.drawString(String(batterySOH, 0)          , TFT_WIDTH * 5 / 6 , 300 );

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
    else tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(String(batteryTempMax, 1)  , TFT_WIDTH / 2     , 215 );
    if (batteryTempMin < 10 | batteryTempMin > 40) {
        tft.setTextColor(TFT_RED, TFT_BLACK);
    }
    else tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(String(batteryTempMin, 1)  , TFT_WIDTH / 2     , 235 );
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    padding = tft.textWidth("999.9") + 2;
    tft.setTextPadding(padding);
    tft.drawString(String(batterySOCHVMax, 1)  , TFT_WIDTH * 5 / 6, 215 );
    tft.drawString(String(batterySOCHVMin, 1)  , TFT_WIDTH * 5 / 6, 235 );

    // responsetime
    tft.setTextFont(2);
    tft.setTextColor(TFT_GRAY, TFT_BLACK);
    tft.drawString(String(stopMillis - startMillis) + " ms", 30, 465);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
}

void chargeScreenFixed() {
    // draw the grpah box
    tft.setTextColor(TFT_WHITE);
    tft.drawFastVLine(20, 0, 250, TFT_WHITE);
    tft.drawFastHLine(20, 250, 320, TFT_WHITE);
    tft.drawFastHLine(20, 0, 320, TFT_WHITE);
    tft.drawFastVLine(319, 0, 250, TFT_WHITE);

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
        tft.drawLine(17, i, 19, i, TFT_WHITE);
    }
    //draw the unit numbers (y-axis)
    for (int i = 250; i >= 0; i = i - 25) {
        tft.drawString(String((250 - i) / 5), 8, i, 2);
    }

    for (int i = 20; i <= 320; i = i + 30) {
        tft.drawLine(i, 251, i, 253, TFT_WHITE);
    }
    for (int i = 20; i <= 320; i = i + 30) {
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
    tft.drawLine(300, 10, 310, 10, TFT_WHITE);
    tft.drawLine(302, 11, 302, 20, TFT_WHITE);
    tft.drawLine(308, 11, 308, 20, TFT_WHITE);
    tft.drawLine(302, 20, 308, 20, TFT_WHITE);
    tft.drawLine(304, 13, 304, 18, TFT_WHITE);
    tft.drawLine(306, 13, 306, 18, TFT_WHITE);
    tft.drawLine(303, 8, 307, 8, TFT_WHITE);
    tft.drawLine(303, 8, 303, 10, TFT_WHITE);
    tft.drawLine(307, 8, 307, 10, TFT_WHITE);

    drawMiniCooperSE();
}

void drawMiniCooperSE() {
    tft.setTextPadding(0);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("Mini Cooper SE"  , TFT_WIDTH / 2, 467, 4);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString("SE"  , TFT_WIDTH / 2 + 72, 467, 4);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
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
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setFreeFont(MEDIUMFONT);
    padding = tft.textWidth("888.8");
    tft.setTextPadding(padding);
    tft.setTextDatum(MC_DATUM);                     // text alignment middle center
    // batterySOC
    if (batterySOC < 99.5)  draw_String = String(batterySOC, 1) + "%";
    else                    draw_String = String(batterySOC, 0);
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
    else tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(String(batteryTempAverage, 1)  , TFT_WIDTH / 2    , 425 );
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(String(batteryBalance, 0)        , TFT_WIDTH * 5 / 6, 425 );

    tft.setTextFont(2);
    tft.setTextColor(TFT_GRAY, TFT_BLACK);
    tft.drawString(String(stopMillis - startMillis) + " ms", 30, 465);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
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

void initForSME() {
    sendATCommand("ATCRA607");      // set can receive address to 607
    sendATCommand("ATCEA07");       // use can extended address 07
    sendATCommand("ATFCSH6F1");     // flow control set the header to 6F1
    sendATCommand("ATFCSD07300800");// flow control set data to 07 30 08 00
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
    if (batteryVoltage < 300) batteryVoltage = 300;
    float Resistor = 0.16; //20/125;
    float chargeVoltage = batteryVoltage + 125 * Resistor;
    if (chargeVoltage > 400) chargeVoltage = 400;
    float demoValue = 0.000005 * millis();    // make a value between 0 and 1 (4 decimals)
    batterySOH      = 100 * demoValue;              // 0-100%
    batteryVoltage  = batteryVoltage + batteryCurrent / 1000; //300 + 100 * sqrt(demoValue);        // 300-400 Volt
    if (batteryVoltage > 400) batteryVoltage = 400;
    batteryCurrent  = (chargeVoltage - batteryVoltage) / Resistor; //125; //-350 + (350 + 125) * demoValue; // max current withdrawal = 135kW/400V = 337A (set to 350A) max current charging = 125 => -350..+125
    if (batteryCurrent > 125) batteryCurrent = 125;
    batteryPower    = batteryVoltage * batteryCurrent / 1000;
    batterySOC      = batteryVoltage - 300; //100 * demoValue;
    batterySOCHVMax = 90 + 10 * demoValue;
    batterySOCHVMin = 5 + 10 * demoValue;
    batteryCellVoltage = 3 + 1.2 * demoValue;
    batteryCellVoltageMin = batteryCellVoltage - 0.1 * demoValue;
    batteryCellVoltageMax = batteryCellVoltage + 0.1 * demoValue;
    batteryBalance = (batteryCellVoltageMax - batteryCellVoltageMin) * 1000;
    batteryTempAverage = -10 + 60 * demoValue;
    batteryTempMin = batteryTempAverage - 5 * demoValue;
    batteryTempMax = batteryTempAverage + 5 * demoValue;
    batterySOCHV = 100 * demoValue;
    batteryCapacity = 100 * demoValue;
    twelveV = String(9 + 6 * demoValue, 1);
}

void sendATCommand(String command) {
    BTResponse = "";
    ELM_PORT.print(command);
    ELM_PORT.print("\r");       // do not use println and do not use \r\n only \r
    // wait for response
    while (ELM_PORT.available() == 0) {               //  wait for a response
        // maybe add a time-out sequence
        checkTouchScreen();
        dumpLogFromSDToSerial(); // check touchscreen for possible download
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
    if (screenNumber == 0) {
        if (BTResponse.indexOf("DATA") >= 0 or BTResponse.indexOf("ERROR") >= 0) {   // detect NO DATA or CAN ERROR MESSAGE
            tft.drawString(command + " " + BTResponse              , TFT_WIDTH / 2         , 445 );
            delay(1000);
        }
        else {
            tft.drawString("                             "         , TFT_WIDTH / 2         , 445 );
        }
    }
}

long hstol(String recv) {
    char c[recv.length() + 1];
    recv.toCharArray(c, recv.length() + 1);
    return strtol(c, NULL, 16);
}

void checkTouchScreen() {
    //p.x = 0;
    //p.y = 0;
    TS_Point p = ts.getPoint();
    p.x = map(p.x, TS_MINX, TS_MAXX, 0, 320);
    p.y = map(p.y, TS_MINY, TS_MAXY, 480, 0);
    if (p.x > 160 && p.x < 320 && p.y > 0 && p.y < 100 && screenNumber == 1) {  // upper right corner when in charging screen
        memset(chargingPowerSOC, 0, sizeof(chargingPowerSOC));                  // clear the charging power array
        memset(chargingTempSOC, 0, sizeof(chargingTempSOC));                    // clear the charging power array
        chargeStartTime = millis();                                                    // set chargeStartTime to current millis()
        tft.fillScreen(TFT_BLACK);
        screenNumber = 1;
    }
    if (p.x > 160 && p.x < 320 && p.y > 380 && p.y < 480 ) {                    // bottom right corner : next screen
        tft.fillScreen(TFT_BLACK);
        screenNumber = 1;
    }
    if (p.x > 0 && p.x < 160 && p.y > 380 && p.y < 480 ) {                     // bottom Left corner : previous screen
        tft.fillScreen(TFT_BLACK);
        screenNumber = 0;
    }
    if (p.x > 0 && p.x < 100 && p.y > 0 && p.y < 100 ) {                        // top Left corner : go to sleep
        goToSleep();
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
        dataString = dataString + "," + String(batteryTempMin, 1) + "," + String(batteryTempMax, 1) + "," + String(batterySOCHVMin, 1) + "," + String(batterySOCHVMax, 1) + String(batteryCellVoltageMin, 3) + "," + String(batteryCellVoltageMax, 3);

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

void dumpLogFromSDToSerial() {
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
            // removed because deleting this log is not the purpose
        }
        else if (inByte == 10) {
            // do nothing (Cariage return)
        }
        else
        {
            Serial.println("send 'd' for uploading DATA Log");
            Serial.println("send 'e' for erasing GPSLog");
        }
    }
}

void goToSleep() {
    while (!ts.bufferEmpty()) {                     // empty the touchscreen buffer
        TS_Point p = ts.getPoint();
    }
    tft.fillScreen(TFT_BLACK);                      // blank the screen
    tft.setFreeFont(MEDIUMFONT);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);         // set the text to white on a blck background
    tft.drawString("GOING TO SLEEP", 160, 240);     // show text
    delay(1000);                                    // show the text for 1 second
    ledcWrite(0, 0);                                // disable the backlight
    ts.writeRegister8(STMPE_INT_EN,   0x01);        // set touchscreen interrupt enable
    ts.writeRegister8(STMPE_INT_CTRL, 0x01);        // set interrut control register to active low, level interrupt, global interrupt
    ts.writeRegister8(STMPE_INT_STA,  0xFF);        // clear any existing interrupts
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_27, 0);   // wake up the ESP32 (reset) when 0V detected on GPIO 27 to which the INT output of the STMPE610 is connected
    esp_deep_sleep_start();                         // go to deep sleep
}
