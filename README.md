# MCSE-battery
Mini Cooper SE Electric High Voltage Battery readouts (probably also BMW i3)

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
*/
