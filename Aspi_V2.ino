//NodeMCU-32S
//https://www.kincony.com/how-to-programming.html
#include <PCF8574.h> // Change library LATENCY | https://github.com/xreef/PCF8574_library/tree/master
#include <LiquidCrystal_I2C.h>
#include <HardwareSerial.h>  //Library from Arduino
//#include <RCSwitch.h> //https://github.com/sui77/rc-switch
#include <WiFiManager.h> // https://urldefense.com/v3/__https://github.com/tzapu/WiFiManager__;!!Dl6pPzL6!f1NLhxYDNph0NM5BrmR5ROBmmrHJFT7AaXGXELbBxjAcaQ2hg9YNwBSDHwp0HUFtbU0-WcsGZG57yM4I0ZK6aQ$ 
WiFiManager wm;
WiFiManagerParameter custom_field; // global param ( for non blocking w params )
#include <Preferences.h>
Preferences preferences;

int DeviceNumber = 0;
String DeviceName = "ASPI";
float creditAmount = 0.0;
unsigned long RemainingCredit = 0;
unsigned long previousTime = 0;
bool ProgramStarted = false;
int SelectedProgram = 0;

PCF8574 pcf8574_in1(0x22, 4, 5);   //input channel X1-8 (PCF8574(uint8_t address, uint8_t sda, uint8_t scl);)
PCF8574 pcf8574_out1(0x24, 4, 5);  //output channel Y1-8

// CREDITS
const unsigned long CREDIT_DECREMENT_INTERVAL = 2000;                             // Interval in milliseconds between 2 decrement
const float CREDIT_DECREMENT_AMOUNT[] = { 2.4, 1.0, 0, 0, 0, 0, 0, 0 };  // Amount to decrement in cents every second
long CreditValue[] = { 50, 100, 200, 400 };                                      // Value of credit for each inputs

// COMBINAISONS DES SORTIES RELAIS Y1-8
int relay_out_sequence[8][8] = {
  { 0, 0, 0, 0, 0, 0, 0, 0 },  // Combination
  { 0, 0, 0, 0, 0, 0, 0, 0 },  // Combination
  { 0, 0, 0, 0, 0, 0, 0, 0 },  // Combination
  { 0, 0, 0, 0, 0, 0, 0, 0 },  // Combination
  { 1, 0, 0, 0, 0, 0, 0, 0 },  // Combination Button1
  { 0, 0, 0, 0, 0, 0, 0, 0 },  // Combination STOP
  { 0, 0, 0, 0, 0, 0, 0, 0 },  // Combination
  { 0, 0, 0, 0, 0, 0, 0, 0 }   // Combination
};

String InputDef[] =     { "COIN", "COIN", "COIN", "COIN", "BUTTON", "BUTTON", "NA", "NA" }; // Inputs function
String ProgDisplay[] =  { "NA",  "NA",  "NA",  "NA",  " ASPI ",  " STOP ",   "NA", "NA",};  // Prog to display on LCD

int TimePreStart = 3; // Pre Activate the system for X seconds before program starts

int Standby_Output[] =   { 0, 0, 0, 0, 0, 0, 0, 1 }; // Combination Standby    Output Y1-16
int allOFF_Output[] =    { 0, 0, 0, 0, 0, 0, 0, 0 }; // Combination All OFF    Output Y1-16

// Initialize the LCD screen
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ======================================== SETUP ======================================== //
void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP 
  // Load the last saved value from Preferences (or default if not set)
  preferences.begin("my-app", true);
  DeviceNumber = preferences.getInt("customValue", 0);
  Serial.print("DeviceNumber : ");Serial.println(DeviceNumber);
  preferences.end();
  delay(DeviceNumber*1500);

  Serial.println("\n Starting");
  // add a custom input field
  //int customFieldLength = 40;
  const char* custom_radio_str = "<br/><label for='customfieldid'>Choisir ASPI</label><br><br><input type='radio' name='customfieldid' value='1' checked> 1<br><input type='radio' name='customfieldid' value='2'> 2";
  new (&custom_field) WiFiManagerParameter(custom_radio_str); // custom html input
  wm.addParameter(&custom_field);
  wm.setSaveParamsCallback(saveParamCallback);
  std::vector<const char *> menu = {"wifi","info","sep","param","sep","restart","exit"};
  wm.setMenu(menu);
  wm.setClass("invert"); // set dark theme
  wm.setConfigPortalTimeout(20); // auto close configportal after n seconds
    bool res;
    char WiFiName[32];
    sprintf(WiFiName, "ASPI_%d", DeviceNumber);
    res = wm.autoConnect(WiFiName);
    if(!res) {Serial.println("Failed to connect");} 
    else {Serial.println("connected...yeey :)");}

  pinMode(0, INPUT);  // Button DOWNLOAD used to reset 3sec

  // Set all pins of PCF8574 instances as inputs or outputs
  for (int i = 0; i < 8; i++) {
      pcf8574_in1.pinMode(i, INPUT);
      pcf8574_out1.pinMode(i - 8, OUTPUT);
  }
  // Check if all PCF8574 instances are initialized successfully
  if (pcf8574_in1.begin() && pcf8574_out1.begin()) {
    Serial.println("All PCF8574 instances initialized successfully.");
  } else {
    Serial.println("Failed to initialize one or more PCF8574 instances.");
  }

  lcd.init();
  lcd.backlight();  // initialize the lcd
  displayMessage("     LOADING    ", "                ", 1);
  // Display message on Serial port, LCD line 1, clear LCD before displaying
  displayMessage("      READY     ", String(DeviceName) + " " + String(DeviceNumber) + "    ", true);
  activateRelays(Standby_Output,-1);
  delay(3000);
}

// ======================================== LOOP ======================================== //
void loop() {
  uint8_t inputStatus = pcf8574_in1.digitalReadAll();
  int InputIndex = getInputIndexINPUTSTATUS(inputStatus);
  // _______________________ FUNCTION 1 : CHECK INPUT STATUS _______________________ //
  if (!digitalRead(0)) {  // User press RESET button
    delay(2000);
    displayMessage("   RESET        ", "   3 sec        ", 1);
    delay(3000);
    if (!digitalRead(0)) {
      displayMessage("  RESET ALL     ", "", 1);
      delay(500);
      wm.resetSettings();
      ESP.restart();
    }
  } else if (InputIndex > 0 && InputDef[InputIndex - 1] == "COIN" && ProgramStarted == false) {  // COIN inputs
    creditAmount += CreditValue[InputIndex - 1];
    displayMessage("", "CREDIT : " + String(float(creditAmount / 100)) + " E  ", 1);
    delay(1000);// Adjust the delay to avoid reading twice the COIN input
  } else if (creditAmount > 0 && InputDef[InputIndex - 1] == "BUTTON" && ProgramStarted == false) {  // If credit > 0 listen buttons
      SelectedProgram = InputIndex;
      ProgramStarted = true;
  } else if (SelectedProgram > 0 && ProgramStarted == true) {  // PROGRAM selected and not STOP
      activateRelays(relay_out_sequence[SelectedProgram - 1],-1);
      unsigned long currentTime = millis(); // Get the current time
      // Check if the interval has elapsed
      if (currentTime - previousTime >= CREDIT_DECREMENT_INTERVAL) {
          previousTime = currentTime; // Update the previous time
          // Decrement the credit
          if (creditAmount >= CREDIT_DECREMENT_AMOUNT[SelectedProgram - 1]) {
              creditAmount -= CREDIT_DECREMENT_AMOUNT[SelectedProgram - 1];
              int wholePart = int(creditAmount/100); // Get whole part
              int fractionalPart = int((creditAmount/100 - wholePart) * 100); // Get fractional part
              displayMessage("PROG: " + String(ProgDisplay[SelectedProgram - 1]) + "      ","CREDIT : " + String(wholePart) + "." + (fractionalPart < 10 ? "0" : "") + String(fractionalPart) + " E  ",false);
          } else {
              creditAmount = 0;
              SelectedProgram = 0;
              InputIndex = -1;
              ProgramStarted = false;
              activateRelays(Standby_Output,-1);
          }
      }
  }else {  // STANDBY mode when Credit = 0
    activateRelays(Standby_Output,-1);
    InputIndex = -1;
    SelectedProgram = 0;
    ProgramStarted = false;
    displayMessage("BONJOUR         ", "INSEREZ PIECE   ", false);
  }
  delay(10);
}

// ======================================== PARAM WIFI ======================================== //
void saveParamCallback(){
  String customValueStr = getParam("customfieldid");
  // Convert the string to an integer
  int customValueInt = customValueStr.toInt();
  // Save the updated value to Preferences.
  preferences.begin("my-app", false);
  preferences.putInt("customValue", customValueInt);
  preferences.end();
  Serial.println("[CALLBACK] saveParamCallback fired");
  Serial.println("PARAM customfieldid = " + getParam("customfieldid"));
}
String getParam(String name){
  //read parameter from server, for customhmtl input
  String value;
  if(wm.server->hasArg(name)) {
    value = wm.server->arg(name);
  }
  return value;
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
}
// ---- SEND MESSAGE TO LCD & SERIAL ---- //
void displayMessage(const String& messageL1, const String& messageL2, bool clearLCD) {
  // Display message on Serial port
  int indicator = 0;
  int totalLength = 16;
  String messageSerial = messageL1 + " & " + messageL2;
  Serial.println(messageSerial);
  // Display message on LCD
  if (clearLCD) {
    lcd.clear();
    indicator = 2;
  }
  if (messageL1 != "") {
    lcd.setCursor(0, 0);
    lcd.print(messageL1);
  }
  if (messageL2 != "") {
    lcd.setCursor(0, 1);
    lcd.print(messageL2);
  }
}
// ---- GET VALUE INPUT STATUS ---- //
int getInputIndexINPUTSTATUS(uint8_t inputStatus) {
  for (int i = 0; i < 8; i++) {
    if ((inputStatus & (1 << i)) == 0) {

      return i+1;  // Return the index of the set bit
    }
  }
  return -1; // Return -1 if no set bit is found
}
// ---- GET VALUE INPUT BUTTON ---- //
int getInputIndexBUTTON(uint8_t inputStatus) {
  for (int i = 7; i >= 0; i--) {
    if ((inputStatus & (1 << i)) == 0) {
      return i+1;  // Return the index of the set bit
    }
  }
  return -1; // Return -1 if no set bit is found
}
