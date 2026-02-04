//NodeMCU-32S
//https://www.kincony.com/how-to-programming.html
#include <Wire.h>
#include <PCF8574.h> // Change library LATENCY | https://github.com/xreef/PCF8574_library/tree/master
#include <LiquidCrystal_I2C.h>
//#include <HardwareSerial.h>  //Library from Arduino
//#include <RCSwitch.h> //https://github.com/sui77/rc-switch

int DeviceNumber = 1;
String DeviceName = "ASPI";
float creditAmount = 0.0;
unsigned long RemainingCredit = 0;
unsigned long previousTime = 0;
int SelectedProgram = 1;
bool ProgramSTART = false;
const unsigned long programStartDelay = 3000; // 3 seconds
unsigned long programStartTime = 0;

PCF8574 pcf8574_in1(0x22, 4, 5);   //input channel X1-8 (PCF8574(uint8_t address, uint8_t sda, uint8_t scl);)
PCF8574 pcf8574_out1(0x24, 4, 5);  //output channel Y1-8

// CREDITS
const unsigned long CREDIT_DECREMENT_INTERVAL = 2000;                             // Interval in milliseconds between 2 decrement
const float CREDIT_DECREMENT_AMOUNT[] = { 1.2, 1.2, 0, 0, 0, 100, 0, 0 };  // Amount to decrement in cents every second
long CreditValue[] = { 50, 100, 200, 400 };                                      // Value of credit for each inputs

// COMBINAISONS DES SORTIES RELAIS Y1-8 => NOT IN USE FOR ASPI
int relay_out_sequence[8][8] = {
  { 0, 0, 0, 0, 0, 0, 0, 0 },  // Combination
  { 0, 0, 0, 0, 0, 0, 0, 0 },  // Combination
  { 0, 0, 0, 0, 0, 0, 0, 0 },  // Combination
  { 1, 0, 0, 0, 0, 0, 0, 0 },  // Combination
  { 0, 1, 0, 0, 0, 0, 0, 0 },  // Combination
  { 0, 0, 0, 0, 0, 0, 0, 0 },  // Combination
  { 0, 0, 0, 0, 0, 0, 0, 0 },  // Combination
  { 0, 0, 0, 0, 0, 0, 0, 0 }   // Combination
};

String InputDef[] =     { "COIN", "COIN", "COIN", "BUTTON", "BUTTON", "STOP", "NA", "NA" }; // Inputs function
String ProgDisplay[] =  { "NA",  "NA",  "NA",  "<<<--- ASPI",  "AIR --->>>",  " STOP ",   "NA", "NA",};  // Prog to display on LCD

int TimePreStart = 3; // Pre Activate the system for X seconds before program starts

int Standby_Output[] =   { 0, 0, 0, 0, 0, 0, 0, 1 }; // Combination Standby    Output Y1-8
int START_Output[] =     { 1, 0, 0, 0, 0, 0, 0, 0 }; // Combination START    Output Y1-8
int allOFF_Output[] =    { 0, 0, 0, 0, 0, 0, 0, 0 }; // Combination All OFF    Output Y1-8

// Initialize the LCD screen
LiquidCrystal_I2C lcd(0x27, 16, 2);
char msgBuffer[32];  // Enough space for the string
char line2Buffer[32]; // or larger if needed
String lastLcdLine1 = "";
String lastLcdLine2 = "";
bool lcdRefreshPending = false;
unsigned long lcdRefreshAt = 0;
unsigned long lastLcdRefresh = 0;
const unsigned long LCD_REFRESH_DELAY_MS = 1000;
const unsigned long LCD_PERIODIC_REFRESH_MS = 10000;

// ======================================== SETUP ======================================== //
void setup() {
  Serial.begin(115200);
  Serial.println("\n Starting");
  Wire.begin();
  Wire.setClock(100000);  // standard mode = 100 kHz

  pinMode(0, INPUT);  // Button DOWNLOAD used to reset 3sec

  // Set all pins of PCF8574 instances as inputs or outputs
  for (int i = 0; i < 8; i++) {
      pcf8574_in1.pinMode(i, INPUT);
      pcf8574_out1.pinMode(i, OUTPUT);
  }
  // Check if all PCF8574 instances are initialized successfully
  if (pcf8574_in1.begin() && pcf8574_out1.begin()) {
    Serial.println("All PCF8574 instances initialized successfully.");
  } else {
    Serial.println("Failed to initialize one or more PCF8574 instances.");
  }
  lcd.init();
  lcd.backlight();  // initialize the lcd
  delay(50);                     // Ensure LCD is ready
  lcd.clear();
  displayMessage("      READY     ", String(DeviceName) + " " + String(DeviceNumber) + "    ", 1);
  delay(100);
  activateRelays(Standby_Output,-1);
  delay(100);
}

// ======================================== LOOP ======================================== //
void loop() {
  static unsigned long lastCheck = 0;
  const unsigned long checkInterval = 10000; // Check every 10 seconds
  int InputIndex = getInputIndexINPUTSTATUS();
  if (lcdRefreshPending && (long)(millis() - lcdRefreshAt) >= 0) {
    lcdRefreshPending = false;
    refreshLcd();
  }
  if (millis() - lastLcdRefresh >= LCD_PERIODIC_REFRESH_MS) {
    refreshLcd();
  }
  // _______________________ FUNCTION 1 : CHECK INPUT STATUS _______________________ //
  if (!digitalRead(0)) {  // User press RESET button
    delay(2000);
    displayMessage("   RESET        ", "   3 sec        ", 1);
    delay(3000);
    if (!digitalRead(0)) {
      displayMessage("  RESET ALL     ", "", 1);
      delay(500);
      ESP.restart();
    }
  } else if (InputIndex > 0 && InputDef[InputIndex - 1] == "COIN" && creditAmount == 0) {  // COIN inputs
      creditAmount += CreditValue[InputIndex - 1];
      activateRelays(allOFF_Output,-1);
      //displayMessage("", "CREDIT : " + String(float(creditAmount / 100)) + " E  ", 1);
      snprintf(msgBuffer, sizeof(msgBuffer), "CREDIT : %.2f E", creditAmount / 100.0);
      displayMessage(msgBuffer, "", false);
          for (int i = 3; i > 0; i--) {
            displayMessage(" PRET  " + String(i) + "        ", "", 0);
            delay(500);
          }
  } else if (InputIndex > 0 && InputDef[InputIndex - 1] == "STOP" && creditAmount > 0) {  // STOP inputs
      creditAmount = 0;
      displayMessage("     STOP       ", "", true);
      activateRelays(allOFF_Output,-1);
      delay(3000);
  } else if (InputIndex > 0 && InputDef[InputIndex - 1] == "BUTTON" && creditAmount > 0 && InputIndex != SelectedProgram) {  // Program change
      SelectedProgram = InputIndex;
      if (SelectedProgram > 0 && SelectedProgram <= 8) {
          displayMessage(String(ProgDisplay[SelectedProgram - 1]), "DEPART PROGRAMME", 0);
          activateRelays(allOFF_Output, -1);
          ProgramSTART = true;
          programStartTime = millis();         
      } else {
          displayMessage("DEPART PROGRAMME", "Invalid Program", 0); // Handle error or set a default message
      }
  } else if (creditAmount > 0 && ProgramSTART) {  // First PROGRAM start
      if (millis() - programStartTime >= programStartDelay) {
            // Delay has elapsed
            ProgramSTART = false;
        }
  } else if (creditAmount > 0 && ProgramSTART == false) {  // PROGRAM selected and not STOP
      activateRelays(relay_out_sequence[SelectedProgram - 1],-1);
      unsigned long currentTime = millis(); // Get the current time
      // Check if the interval has elapsed
      if (currentTime - previousTime >= CREDIT_DECREMENT_INTERVAL) {
          previousTime = currentTime; // Update the previous time
          // Decrement the credit
        float thisDecrement = CREDIT_DECREMENT_AMOUNT[SelectedProgram - 1];
          if (creditAmount >= thisDecrement) {
              creditAmount -= thisDecrement;
              int wholePart = int(creditAmount/100); // Get whole part
              int fractionalPart = int((creditAmount/100 - wholePart) * 100); // Get fractional part
              //displayMessage(String(ProgDisplay[SelectedProgram - 1]),"CREDIT : " + String(wholePart) + "." + (fractionalPart < 10 ? "0" : "") + String(fractionalPart) + " E  ",0);
              snprintf(line2Buffer, sizeof(line2Buffer), "CREDIT : %d.%02d E", wholePart, fractionalPart);
              displayMessage(ProgDisplay[SelectedProgram - 1], line2Buffer, false);
          } else {
              creditAmount = 0;
              InputIndex = -1;
          }
      }
  }else if(creditAmount <= 0) {  // STANDBY mode when Credit = 0
    activateRelays(Standby_Output,-1);
    InputIndex = -1;
    creditAmount = 0;
      if (millis() - lastCheck > checkInterval) {
        lastCheck = millis();
        displayMessage("BONJOUR         ", "INSEREZ PIECE   ", 0);
    }
  }
}

// ======================================== FUNCTIONS ======================================== //
// ---- ACTIVATE RELAYS ---- //
void activateRelays(int* outputStatus, int ForceLow) {
  for (int i = 0; i < 8; i++) {
      if (outputStatus[i] == 1 && i+1 != ForceLow) {
        pcf8574_out1.digitalWrite(i, LOW); // Relay ON
      } else if (outputStatus[i] == 0) {
        pcf8574_out1.digitalWrite(i, HIGH); // Relay OFF
      }
  }
  delay(500);
  requestLcdRefresh(LCD_REFRESH_DELAY_MS);
}
// ---- SEND MESSAGE TO LCD & SERIAL ---- //
void displayMessage(const String& messageL1, const String& messageL2, bool clearLCD) {
  // Truncate strings to 16 characters
  String line1 = messageL1.substring(0, 16);
  String line2 = messageL2.substring(0, 16);
  lastLcdLine1 = line1;
  lastLcdLine2 = line2;

  // Display message on Serial port
  Serial.println(line1 + " & " + line2);

  // Display message on LCD
  if (clearLCD) {
    lcd.clear();
  }
  lcd.setCursor(0, 0);
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print(line2);
  lastLcdRefresh = millis();
}

// ---- LCD REFRESH HELPERS ---- //
void requestLcdRefresh(unsigned long delayMs) {
  lcdRefreshPending = true;
  lcdRefreshAt = millis() + delayMs;
}

void refreshLcd() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(lastLcdLine1);
  lcd.setCursor(0, 1);
  lcd.print(lastLcdLine2);
  lcd.backlight();
  lastLcdRefresh = millis();
}

// ---- GET VALUE INPUT STATUS ---- //
int getInputIndexINPUTSTATUS() {
  for (int i = 0; i < 8; i++) {
    if (pcf8574_in1.digitalRead(i) == LOW) {
      return i+1;  // Return the index of the set bit
    }
  }
  return -1; // Return -1 if no set bit is found
}
// ---- GET VALUE INPUT BUTTON ---- //
int getInputIndexBUTTON() {
  for (int i = 7; i >= 0; i--) {
    if (pcf8574_in1.digitalRead(i) == LOW) {
      return i+1;  // Return the index of the set bit
    }
  }
  return -1; // Return -1 if no set bit is found
}
