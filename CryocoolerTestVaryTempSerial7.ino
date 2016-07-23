#include <SoftwareSerial.h>
#include <SPI.h>  // Include SPI if you're using SPI
#include <SFE_MicroOLED.h>  // Include the SFE_MicroOLED library
#include "Adafruit_MAX31855.h"

#define TEMP_QUERY "<TP OP=\"GT\" LC=\"MS\"/>\r\n"
#define POW_QUERY "<PW OP=\"GT\" LC=\"MS\"/>\r\n"
#define TURNOFF_QUERY "<TP OP=\"ST\" LC=\"SM\">1 4</TP>\r\n"
#define TURNAUTO_QUERY "<TP OP=\"ST\" LC=\"SM\">0 4</TP>\r\n"
#define MAX_QRET_LEN 80 // Maximum length of a the serial returned to the Arduino

#define SOFTSERIAL_TX 2
#define SOFTSERIAL_RX 3

//////////////////////////
// MicroOLED Definition //
//////////////////////////
#define PIN_RESET 9  // Connect RST to pin 9 (req. for SPI and I2C)
#define PIN_DC    8  // Connect DC to pin 8 (required for SPI)
#define CS_DISPLAY    10 // Connect CS to pin 10 (required for SPI)

#define TEMP_OUT_PIN 5 //PWM output pin
#define TEMP_OUT_FINE_PIN 6 //fine PWM output pin

//Definitions for Thermocouple

#define CS_THERMO   7
Adafruit_MAX31855 thermocouple(CS_THERMO); //High speed SPI assumes pin 12 for MISO and pin 13 for clock
#define DC_JUMPER 0

#define CRYO_TOGGLE_PIN 4 //Cryocooler on/off toggle switch pin

int CryoToggle = 0; //Cryocooler switch status, 0 = off, 3 = on
int CryoStatus = 0; //Actual cryocooler status, 0 = off, 1 = on
int SwitchStatus = 0;

//Constants for the cryocooler data processing
const float Tcold_offset = 5.814;
const float Tcold_gain = -0.01559;
const float Trej_offset = 12.537;
const float Trej_gain = -0.0344;
const float Power_Scaling = 0.0075;

char QueryReturn[MAX_QRET_LEN];

char *ReturnString;

int PWMOutput = -1;  //PWM output value


//Calibration constants for thermocouple. The raw MAX31855 output isn't accurate when it gets down to liquid nitrogen temperatures, these correct for it.
const float CalA = -0.0027257;
const float CalB =  2.4560566;
const float CalC = - 203.8678445;

float CorrT = 0;
float Tval = 0;
int TempData = 0;
float CryoTemp = 0;


const float R1 = 12.21; //Resistor in series with optoisolator
const float R2 = 12.4; //Resistor between Vin and Gnd

//Calibration slope for temperature measurement as function of resistance used by cryocooler. Use to estimate output for original PWM output before cryocooler connects.
// Temp (K) = TcalSlope * Resistance + TcalIntercept
const float TcalSlope = -46.915;
const float TcalIntercept = 563.9;

//global iterator variable
int i = 0;

//////////////////////////////////
// MicroOLED Object Declaration //
//////////////////////////////////
// Declare a MicroOLED object. The parameters include:
// 1 - Reset pin: Any digital pin
// 2 - D/C pin: Any digital pin (SPI mode only)
// 3 - CS pin: Any digital pin (SPI mode only, 10 recommended)
MicroOLED oled(PIN_RESET, PIN_DC, CS_DISPLAY);

SoftwareSerial mySerial(SOFTSERIAL_RX, SOFTSERIAL_TX); // RX, TX

void CryoQuery(char QueryString[40], int ReturnIndex){
  mySerial.write(QueryString);
  delay(100);
 
  i = 0;

  while (mySerial.available() > 0) {

    char inByte = mySerial.read();
    if (i<MAX_QRET_LEN-2){
      QueryReturn[i] = inByte;
      i++;
      QueryReturn[i] = '\0';
    }
    //Serial.write(inByte);

  }

  //overrwrite the rest of the query return variable with null
  while (i < MAX_QRET_LEN) {
    i++;
    QueryReturn[i] = '\0';
  }

  //parse output. First, get entire bit between brackets, then get each element (which are separated by spaces) and select the one we want.
  char *DataStart;
  char *pch;
  char *DataOut[8];

  //Serial.println(QueryReturn);

  //DataStart cuts off the query section at the beginning, the data we want starts at the first close bracket
  DataStart = strchr(QueryReturn, '>');

  // use strtok to parse the rest of the string
  pch = strtok (DataStart, " <>");
  i = 0;
  while (pch != NULL)
  {
    //Serial.print(i);
    //Serial.print(":");
    if (i<4){
      DataOut[i] = pch;
      
      //Serial.println(DataOut[i]);
    }
    
    i++;

    pch = strtok (NULL, " <>");

    
  }
  //Serial.println(DataOut[ReturnIndex]);
  ReturnString = DataOut[ReturnIndex];

}


void setup() {
  pinMode(TEMP_OUT_PIN, OUTPUT);
  pinMode(CRYO_TOGGLE_PIN, INPUT);

  oled.begin();
  // clear(ALL) will clear out the OLED's graphic memory.
  // clear(PAGE) will clear the Arduino's display buffer.
  oled.clear(ALL);  // Clear the display's memory (gets rid of artifacts)
  // To actually draw anything on the display, you must call the
  // display() function. 
  oled.display();   
  delay(1000);     // Delay 1000 ms
  oled.clear(PAGE); // Clear the buffer.
  oled.setFontType(1);
  
  Serial.begin(9600);
  Serial.println("Timestamp,Raw measured,corrected,PWM output,cryocooler temp,Cryocooler power,Cryocooler status");

  while (!Serial) {
    delay(1);
  }
  mySerial.begin(19200);

}


void loop() {
  //Read the cryocooler toggle pin over three loops. If the status changes, switch the cryocooler on or off.

  SwitchStatus = digitalRead(CRYO_TOGGLE_PIN);

  CryoToggle = CryoToggle + SwitchStatus * 2 - 1; //If Switch is off, it'll be zero. If it's on, it'll be one. This will increment CryoToggle if it's on and decrement it if it's off. Wait three cycles before switching status.
  
  if ((CryoToggle == 0) && (CryoStatus == 1)){
    mySerial.write(TURNOFF_QUERY);
    CryoStatus = 0;
    delay(100);
    while (mySerial.available() > 0) {
      mySerial.read();
    }
  }

  if ((CryoToggle == 3) && (CryoStatus == 0)){
    //turn off cryocooler
    mySerial.write(TURNAUTO_QUERY);
    CryoStatus = 1;
    delay(100);
    while (mySerial.available() > 0) {
      mySerial.read();
    }
  }
  
  if (CryoToggle > 3){
    CryoToggle = 3;
  }

  if (CryoToggle < 0){
    CryoToggle = 0;
  }

  
    
  if (!isnan(Tval)) { //If the temperature reading is unreliable, don't change the PWM output

    //If we don't have any reading from the cryocooler, convert temperature to desired resistance using the approximate formula below
    if (PWMOutput < 0){
      PWMOutput = R1 * (TcalSlope / (thermocouple.readCelsius() + 273.15 - TcalIntercept) - 1 / R2) * 4095;
    } else {
      //otherwise, step the PWM output by one to make the cryocooler reported temperature match the thermocouple reading. It only moves one point per loop, but will get close enough eventually.
      if (CryoTemp < CorrT){
        PWMOutput = PWMOutput + (PWMOutput-50)/50 + 1;
        
        
      } else {
        PWMOutput = PWMOutput-(PWMOutput-50)/50-1;
        
      }
    }
    //Maximum PWM output
    if (PWMOutput>4095){
      PWMOutput = 4095;
    }
    //Minimum PWM output to keep reading to ~86K
    if (PWMOutput<80){
      PWMOutput=80;
    }
    
    //write coarse optoisolator output
    analogWrite(TEMP_OUT_PIN, PWMOutput/16);

    //write fine optoisolator output (taking the PWM output mod 16 and squaring it to approximately linearize it)
    analogWrite(TEMP_OUT_FINE_PIN,map((PWMOutput % 16)*(PWMOutput % 16), 0,256,5,255));
  }  
  delay(1000);

  // Read the temperature in Kelvin
  Tval = 0;
  for (int x = 0; x < 10; x++) {
    Tval = Tval + thermocouple.readCelsius();
    delay(100);
  }

  Tval = Tval / 10;

  Tval = Tval + 273.15;

  //Use the calibration constants to correct the raw reading. It gets a bit off at really low temperatures and needs to be corrected.
  CorrT = CalA * Tval * Tval + CalB * Tval + CalC; 

  //query the cryocooler, see what temperature it thinks it is
  //This function changes the ReturnString global variable to the string returned from the query
  CryoQuery(TEMP_QUERY, 1);

  TempData = strtol(ReturnString, NULL, 16);
  //Convert raw reading to temperature
  if (TempData>0) {
    CryoTemp = (5.0 * TempData / 32768 - Tcold_offset) / Tcold_gain;
  } else {
    CryoTemp = 0;
  }
  //Query the cryocooler to get the power output, then convert the query return to the power output
  CryoQuery(POW_QUERY, 0);

  float CryoPower = strtol(ReturnString, NULL, 16);
  if (CryoPower>0){
    CryoPower = pow(5.0*CryoPower/32768,2)/0.0075;
  } else {
    CryoPower = 0;
  }
  
  //print everything to the OLED
  oled.clear(PAGE);
  oled.setCursor(0, 0);
  oled.print("T:" + String(CorrT, 1));
  oled.setCursor(0, 17);
  oled.print("C:" + String(CryoTemp, 1));
  oled.setCursor(0, 33);
  oled.print("P:" + String(CryoPower, 1));
  oled.display();

  
  Serial.print(millis());
  Serial.print(",");
  Serial.print(Tval);
  Serial.print(",");
  Serial.print(CorrT);
  Serial.print(",");
  Serial.print(PWMOutput);
  Serial.print(",");
  Serial.print(CryoTemp);
  Serial.print(",");
  Serial.print(CryoPower);
  Serial.print(",");
  Serial.print(CryoStatus);
  
  Serial.println();

}
 
