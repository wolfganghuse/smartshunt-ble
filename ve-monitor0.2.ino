/**
 * Victron Energy Smart Shut Bluetooth Monitor / LoRa Gateway
 * Wolfgang Huse wollems@googlemail.com
 * 
 * ToDo: SD-Card Logwriter
 */

/*
 * Please set compiler directives to your used hardware
 * You can disable LoRa Module and use just M5Stack as Bluetooth Display
 * If disabling M5_Platform you can run the code on nearly any ESP32 DevKit, Data will be written to Serial
 */
#define M5_Platform // Use M5Stack Platform
//#define LoRa // Use M5 LoRa Module


#ifdef LoRa 

#endif

#ifdef M5_Platform 
  #include <M5Stack.h>
  #include <Free_Fonts.h>
#endif

#include "BLEDevice.h"
//#include "BLEScan.h"


/**
 * BLE Configuration Data
 */

static BLEClient*  pClient;
static BLERemoteCharacteristic* pRemoteVoltage;
static BLERemoteCharacteristic* pRemoteCurrent;
static BLERemoteCharacteristic* pRemoteSOC;
static BLERemoteCharacteristic* pRemoteConsumedAh;
static BLERemoteCharacteristic* pRemoteKeepAlive;

static BLEAdvertisedDevice* myDevice;

static BLEUUID serviceUUID("65970000-4bda-4c1e-af4b-551c4cf74769"); // Service UUID

static BLEUUID    VoltageUUID("6597ed8d-4bda-4c1e-af4b-551c4cf74769"); // Voltage Characteristics
static BLEUUID    CurrentUUID("6597ed8c-4bda-4c1e-af4b-551c4cf74769"); // Current Characteristics
static BLEUUID    ConsumedAhUUID("6597eeff-4bda-4c1e-af4b-551c4cf74769"); // Consumed AH Characteristics
static BLEUUID    SOCUUID("65970fff-4bda-4c1e-af4b-551c4cf74769"); // State-of-Charge Characteristics
static BLEUUID    KeepAliveUUID("6597ffff-4bda-4c1e-af4b-551c4cf74769"); // Keep-Alive Characteristics

char ManufacturerData[] = {0xe1,0x02,0x10,0xff,0x89,0xa3}; // Byte 0-2: Vendor Byte 3: internal (will be masked) Byte 4-5 ProductID

uint32_t PIN = 123456 ; // PIN

/**
 * Global Variables
 */
 
static boolean doConnect = false;
static boolean doScan = false;
boolean connected = false;
double pVoltage, pCurrent, pConsumedAh, pSOC;
int voltage, current, consumedah, soc;

   
unsigned long previousMillis = 0; // this timer is used to update tft display and data send frequence
unsigned long interval = 1000;  
unsigned long counter  = 0;
int tft_counter=0; // screen off counter 
bool tft_backlight = true; // tft backlight - on/off button 1 - on/off after 1 minute


#ifdef LoRa 
  /*   
   * TTN ABP access data 
   */
  
  char* NwkSKey="E8F99AE5EA52B34B59FCDDD9DD6564A4";
  char* AppSKey="A9F4C439E25E44D3BBC7610B33FD5D74";   
  char* DevAddr="260B1FDB";
  
  int TTNCounter=0; // ttn counter send frequence
  
  
  /*-------------------------------------------------------------------------------*/
  /* Function void ATCommand(char cmd[],char date[], uint32_t timeout = 50)        */
  /*                                                                               */
  /* TASK    : send AT commands to the M5Stack COM.LoRaWAN Module                  */
  /* UPDATE  : 24.01.2021                                                          */
  /*-------------------------------------------------------------------------------*/
  void ATCommand(char cmd[],char date[], uint32_t timeout = 50)
  {
    char buf[256] = {0};
    if(date == NULL)
    {
      sprintf(buf,"AT+%s",cmd);
    }
    else 
    {
      sprintf(buf,"AT+%s=%s",cmd,date); 
    }
    Serial2.write(buf);
    delay(200);
    ReceiveAT(timeout);
  }
  
  /*-------------------------------------------------------------------------------*/
  /* Function bool ReceiveAT(uint32_t timeout)                                     */
  /*                                                                               */
  /* TASK    : receive AT msg's from the M5Stack COM.LoRaWAN Module                */
  /* UPDATE  : 24.01.2021                                                          */
  /*-------------------------------------------------------------------------------*/
  bool ReceiveAT(uint32_t timeout)
  {
    uint32_t nowtime = millis();
    while(millis() - nowtime < timeout){
      if (Serial2.available() !=0) {
        String str = Serial2.readString();
        if (str.indexOf("+OK") != -1 || str.indexOf("+ERROR") != -1) {
          Serial.println(str);
          return true;
        }else {
          Serial.println("[!] Syntax Error");
          break;
        }
      }
    }
    Serial.println("[!] Timeout");
    return false;
  }
  
  /*-------------------------------------------------------------------------------*/
  /* Function void array_to_string(byte array[], unsigned int len, char buffer[])  */
  /*                                                                               */
  /* TASK    : build string out of payload data                                    */
  /* UPDATE  : 24.01.2021                                                          */
  /*-------------------------------------------------------------------------------*/
  void array_to_string(byte array[], unsigned int len, char buffer[])
  {
    for (unsigned int i = 0; i < len; i++)
    {
      byte nib1 = (array[i] >> 4) & 0x0F;
      byte nib2 = (array[i] >> 0) & 0x0F;
      buffer[i*2+0] = nib1  < 0xA ? '0' + nib1  : 'A' + nib1  - 0xA;
      buffer[i*2+1] = nib2  < 0xA ? '0' + nib2  : 'A' + nib2  - 0xA;
    }
    buffer[len*2] = '\0';
  }
  
  
  /*-------------------------------------------------------------------------------*/
  /* Function void send_to_TTN(void)                                               */
  /*                                                                               */
  /* TASK    : send sensor data to TTN                                             */
  /* UPDATE  : 25.01.2021                                                          */
  /*-------------------------------------------------------------------------------*/
  void send_to_TTN(void) 
  {  
    Serial.println(F("[!] LORAWAN=?"));   // activate communication
    ATCommand("LORAWAN", "?");
    delay(500);
  
    /* TTN V3 payload Uplink decoder
     *
     * 
     
      function decodeUplink(input) {
        var data = {};
        var voltage = (input.bytes[0] << 8) + input.bytes[1];
        var currenta = (input.bytes[2] << 8) + input.bytes[3];
        var consumedah = (input.bytes[4] << 8) + input.bytes[5];
        var soc = (input.bytes[6] << 8) + input.bytes[7];
        
        data.voltage     = voltage/100;
        data.currenta    = currenta/1000;
        data.consumedah  = consumedah/10;
        data.soc         = soc/100;
        var warnings = [];
        
        return {
          data: data,
          warnings: warnings
        };
      }
  
    *
    *
    */
   
    byte payload [8];
    
    payload[0] = voltage >> 8;
    payload[1] = voltage;
    
    payload[2] = current >> 8;
    payload[3] = current;
    
    payload[4] = consumedah >> 8;
    payload[5] = consumedah;
    
    payload[6] = soc >> 8;
    payload[7] = soc;
    
    Serial.print(F("[x] actual TTN payload  --> "));
    char str[32] = "";
    array_to_string(payload, 8, str);
    Serial.println(str);
  
    // now send all to TTN
    ATCommand("SendHex", str); 
  }
  
#endif

/**
 * BLE Notify Callback Functions
 */
 
static void notifyVoltageCallback(
  BLERemoteCharacteristic* pRemoteVoltage,
  uint8_t* pData,
  size_t length,
  bool isNotify) {
    voltage = (pData[1] << 8) | pData[0];
    pVoltage = (double) (voltage) / 100.0;
}
  
static void notifyCurrentCallback(
  BLERemoteCharacteristic* pRemoteCurrent,
  uint8_t* pData,
  size_t length,
  bool isNotify) {
    current = (pData[1] << 8) | pData[0];
    pCurrent = (double) (current) / 10.0;
}

static void notifyConsumedAhCallback(
  BLERemoteCharacteristic* pRemoteConsumedAh,
  uint8_t* pData,
  size_t length,
  bool isNotify) {
    consumedah = (pData[1] << 8) | pData[0];
    pConsumedAh = (double) (consumedah) / 10.0;
}

static void notifySOCCallback(
  BLERemoteCharacteristic* pRemoteSOC,
  uint8_t* pData,
  size_t length,
  bool isNotify) {
    soc = (pData[1] << 8) | pData[0];
    pSOC = (double) (soc) / 100.0;
}

class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient* pclient) {
    Serial.println("onConnect");
    status_message("Connected");
  }

  void onDisconnect(BLEClient* pclient) {
    connected = false;
    Serial.println("onDisconnect");
  }
};

class MySecurity : public BLESecurityCallbacks
{
    uint32_t onPassKeyRequest()     // pairing password PIN
    {
      Serial.printf("Pairing password: %d \r\n", PIN); 
      return PIN;                   // Authentication / Security
    }
    void onPassKeyNotify(uint32_t pass_key)  // key
    {
    }
   
    bool onConfirmPIN(uint32_t pass_key)
    {
      return true;
    }
 
    bool onSecurityRequest() {       
      Serial.printf("Security Request\r\n");
      return true;
    }
    void onAuthenticationComplete(esp_ble_auth_cmpl_t auth_cmpl) 
    {
      if (auth_cmpl.success)        // Bluetooth Authentication erfolgreich
      {
      }
      else
      {
       Serial.println("Authentication failes. Wrong PIN ?");
       doConnect = false;
      }
    }
};

bool connectToServer() {
    Serial.print("[X] connectToServer");
    Serial.print(" - Forming a connection to ");
    Serial.println(myDevice->getAddress().toString().c_str());
    status_message ("Connecting..."); 

    BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT);
    BLEDevice::setSecurityCallbacks(new MySecurity());
  
    BLESecurity *pSecurity = new BLESecurity();
    pSecurity->setAuthenticationMode(ESP_LE_AUTH_BOND); //
    pSecurity->setCapability(ESP_IO_CAP_KBDISP);
    pSecurity->setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

    pClient  = BLEDevice::createClient();
    Serial.println(" - Created client");

    pClient->setClientCallbacks(new MyClientCallback());

    // Connect to the remove BLE Server.
    Serial.println(" - call connect");
    connected = pClient->connect(myDevice);
    if (connected) {
      Serial.println(" - connected");  
    } else {
      Serial.println(" - connecting error, disconnect");
      pClient->disconnect();
      return false;
    }
    
    // Obtain a reference to the service we are after in the remote BLE server.
    Serial.println(" - get Service");
    BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
    if (pRemoteService == nullptr) {
      Serial.print(" - Failed to find our service UUID: ");
      Serial.println(serviceUUID.toString().c_str());
      pClient->disconnect();
      return false;
    }
    Serial.println(" - Found our service");


    // Obtain a reference to the Voltage Characteristic
    pRemoteVoltage = pRemoteService->getCharacteristic(VoltageUUID);
    if (pRemoteVoltage == nullptr) {
      Serial.print(" - Failed to find our characteristic UUID: ");
      Serial.println(VoltageUUID.toString().c_str());
      pClient->disconnect();
      return false;
    }
    Serial.println(" - Found Voltage");

    // Read the value of the characteristic.
    if(pRemoteVoltage->canRead()) {
      std::string value = pRemoteVoltage->readValue();
    }

    if(pRemoteVoltage->canNotify())
      pRemoteVoltage->registerForNotify(notifyVoltageCallback);

    // Obtain a reference to the Current Characteristic
    pRemoteCurrent = pRemoteService->getCharacteristic(CurrentUUID);
    if (pRemoteCurrent == nullptr) {
      Serial.print(" - Failed to find our characteristic UUID: ");
      Serial.println(CurrentUUID.toString().c_str());
      pClient->disconnect();
      return false;
    }
    Serial.println(" - Found Current");

    // Read the value of the characteristic.
    if(pRemoteCurrent->canRead()) {
      std::string value = pRemoteCurrent->readValue();
    }

    if(pRemoteCurrent->canNotify())
      pRemoteCurrent->registerForNotify(notifyCurrentCallback);

    // Obtain a reference to the ConsumedAh Characteristic      
    pRemoteConsumedAh = pRemoteService->getCharacteristic(ConsumedAhUUID);
     if (pRemoteConsumedAh == nullptr) {
      Serial.print("- Failed to find our characteristic UUID: ");
      Serial.println(ConsumedAhUUID.toString().c_str());
      pClient->disconnect();
      return false;
    }
    Serial.println(" - Found ConsumedAh");

    // Read the value of the characteristic.
    if(pRemoteConsumedAh->canRead()) {
      std::string value = pRemoteConsumedAh->readValue();
    }

    if(pRemoteConsumedAh->canNotify())
      pRemoteConsumedAh->registerForNotify(notifyConsumedAhCallback);

    // Obtain a reference to the SOC Characteristic         
   pRemoteSOC = pRemoteService->getCharacteristic(SOCUUID);
    if (pRemoteSOC == nullptr) {
      Serial.print(" - Failed to find our characteristic UUID: ");
      Serial.println(SOCUUID.toString().c_str());
      pClient->disconnect();
      return false;
    }
    Serial.println(" - Found SOC");

    // Read the value of the characteristic.
    if(pRemoteSOC->canRead()) {
      std::string value = pRemoteSOC->readValue();
    }

    if(pRemoteSOC->canNotify())
      pRemoteSOC->registerForNotify(notifySOCCallback);
    

    // Obtain a reference to the KeepAlive Characteristic         
   pRemoteKeepAlive = pRemoteService->getCharacteristic(KeepAliveUUID);
    if (pRemoteKeepAlive == nullptr) {
      Serial.print("- - Failed to find our characteristic UUID: ");
      Serial.println(KeepAliveUUID.toString().c_str());
      pClient->disconnect();
      return false;
    }
    Serial.println(" - Found KeepAlive");

    if(pRemoteKeepAlive->canWrite()) {
      pRemoteSOC->writeValue((uint8_t)5);
      Serial.println(" - Value Written");
    }

    connected = true;
    return true;
}

/**
 * Scan for BLE servers and find matching Vendor/ProductID in ManufacturerData
 */
 
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
 /**
   * Called for each advertising BLE server.
   */
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    Serial.print("BLE Advertised Device found: ");
    Serial.println(advertisedDevice.toString().c_str());

    if (advertisedDevice.haveManufacturerData()) 
    {
      uint8_t* MFRdata;
      MFRdata = (uint8_t*)advertisedDevice.getManufacturerData().data();
      int len = advertisedDevice.getManufacturerData().length();
      if (len == 6)
      {
        MFRdata[3] = {0xff}; //Masking Byte 3
        if (memcmp (MFRdata, ManufacturerData, len)==0)
        {
          Serial.println("tag");
          BLEDevice::getScan()->stop();
          myDevice = new BLEAdvertisedDevice(advertisedDevice);
          doConnect = true;
          doScan = true;
        } // Found our server 
      } //correct length
    } //hasManufacturerData
  } // onResult
}; // MyAdvertisedDeviceCallbacks


/*-------------------------------------------------------------------------------*/
/* Function void setup()                                                         */
/*                                                                               */
/* TASK    : setup all needed requirements                                       */
/* UPDATE  : 24.01.2021                                                          */
/*-------------------------------------------------------------------------------*/
void setup() {
  // initialize the M5Stack object

  status_message("Booting...");
  #ifdef M5_Platform
    M5.begin();
    M5.Power.begin(); //Init Power module. 
    M5.Lcd.setBrightness(255); 
    M5.Lcd.setTextColor(TFT_BLACK, TFT_WHITE);
    M5.Lcd.fillScreen(TFT_WHITE);
    M5.Lcd.setFreeFont(FSSB12);
    M5.Lcd.setTextDatum(MC_DATUM);
    M5.Lcd.drawString(F("VE Smart Shunt Monitor"), 160, 15, GFXFF);
    M5.Lcd.setTextDatum(TL_DATUM);
    displayInit();
  #else
    Serial.begin(115200);
  #endif
  

  #ifdef LoRa 
    // Connect to Serial Lorawan Modul
    status_message("Connect to LoRa");
    Serial2.begin(115200, SERIAL_8N1, 16, 17);
      // Send something to wake-up the module
    ATCommand("LORAWAN", "?");
    delay(200);
    // Enable LoraWAN Mode
    ATCommand("LORAWAN", "1");
    delay(200);
    // Switch to ABP Mode
    ATCommand("OTAA", "0");
    delay(200);
    // Disable TXconfirm
    ATCommand("IsTxConfirmed", "0");
    delay(200);
    // your TTN access data
    ATCommand("NwkSKey", NwkSKey);
    delay(200);
    // always the same for all devices
    ATCommand("AppSKey", AppSKey);
    delay(200);
    ATCommand("DevAddr", DevAddr);
    delay(200);
    // Join the Network
    ATCommand("Join", "1");
    delay(200);
#endif

  status_message("Connect to Bluetooth");
  
  BLEDevice::init("");

  // Retrieve a Scanner and set the callback we want to use to be informed when we
  // have detected a new device.  Specify that we want active scanning and start the
  // scan to run for 5 seconds.
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  //pBLEScan->setInterval(1349);
  //pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);
  status_message("Scan for Device...");
  pBLEScan->start(5, false);
  
} // End of setup.

#ifdef M5_Platform
  
  void buttons_test() {
  
    if  (M5.BtnA.isPressed()) 
    {
      tft_backlight = true;
      if (tft_backlight == true)  { M5.Lcd.wakeup(); M5.Lcd.setBrightness(255); }
    }
    if (M5.BtnA.wasReleasefor(700))
    {
      tft_backlight = true;
      if (tft_backlight == true)  { M5.Lcd.wakeup(); M5.Lcd.setBrightness(255); }
      // first run - we have to get and programm some parameters 
      
    }
    if  (M5.BtnB.isPressed()) 
    {
      tft_backlight = true;
      if (tft_backlight == true)  { M5.Lcd.wakeup(); M5.Lcd.setBrightness(255); }
    }
    if (M5.BtnB.wasReleasefor(700)) 
    {
      Serial.println("[x] Button B was pressed - Send to TTN");
      // Turning on the LCD backlight
      tft_backlight = true;
      if (tft_backlight == true)  { M5.Lcd.wakeup(); M5.Lcd.setBrightness(255); }
      delay(200); 
  #ifdef LoRa         
      send_to_TTN();   
      delay(200);   
  #endif
    }
    if  (M5.BtnC.isPressed()) 
    {
      tft_backlight = true;
      if (tft_backlight == true)  { M5.Lcd.wakeup(); M5.Lcd.setBrightness(255); }
    }
    if (M5.BtnC.wasReleasefor(700))
    {  // Button C Long Press for Dis-/Connect
      Serial.println("[x] Button C was pressed - Toogle BLE");
  
      if (connected){
        pClient->disconnect();
        status_message("Disconnected");
        doScan = false;
        doConnect = false;
      } else {
        doScan = true;
        status_message("Searching...");
      }
      delay(200);
    }
  }
  
  void displayInit() {
    M5.Lcd.fillRect(5, 60, 150, 80, TFT_BLUE);
    M5.Lcd.fillRect(165, 60, 150, 80, TFT_BLUE);
    M5.Lcd.fillRect(5, 150, 150, 80, TFT_BLUE);
    M5.Lcd.fillRect(165,150, 150, 80, TFT_BLUE);
   
    M5.Lcd.setFreeFont(FSS9);
    M5.Lcd.setTextColor(TFT_BLACK, TFT_BLUE);
    M5.Lcd.drawString(F("Voltage: "), 10, 65, GFXFF);
    M5.Lcd.drawString(F("Current: "), 175, 65, GFXFF);
    M5.Lcd.drawString(F("Consumed AH: "), 10, 155, GFXFF);
    M5.Lcd.drawString(F("SOC: "), 175, 155, GFXFF);  
  }
#endif
void displayUpdate() {
  #ifdef M5_Platform
    M5.Lcd.setTextDatum(MC_DATUM);
    M5.Lcd.setFreeFont(FSS18);
    M5.Lcd.setTextColor(TFT_WHITE, TFT_BLUE);
    M5.Lcd.drawString(String(pVoltage,2) + "V", 80, 100, GFXFF);
    M5.Lcd.drawString(String(pCurrent,2) + "A", 235, 100, GFXFF);
    M5.Lcd.drawString(String(pConsumedAh,2) + "Ah", 80, 190, GFXFF);
    M5.Lcd.drawString(String(pSOC,2) + "%", 235, 190, GFXFF);
    M5.Lcd.setTextDatum(TL_DATUM);
  #else
    Serial.println(String(pVoltage,2) + "V");
    Serial.println(String(pCurrent,2) + "A");
    Serial.println(String(pConsumedAh,2) + "Ah");
    Serial.println(String(pSOC,2) + "%");
  #endif
}

void status_message (String message) {
  #ifdef M5_Platform
    M5.Lcd.setFreeFont(FSS9);
    M5.Lcd.setTextColor(TFT_BLACK, TFT_WHITE);
    M5.Lcd.drawString(F("Status:"), 10, 40, GFXFF);
    M5.Lcd.fillRect(70,40,250,15,TFT_WHITE); 
    M5.Lcd.drawString(message, 70, 40, GFXFF);
  #else
    Serial.println(message);
  #endif
}
void loop()
{ 
  #ifdef M5_Platform
    // check if some one has pressed a button
    buttons_test();
  #endif
  
  /* 
   * It is checked whether the time for the transmission interval has already expired
   * If the time difference between the last save and the current time is greater
   * as the interval, the following function is executed.
  */
  if (millis() - previousMillis > interval)
  {
    // Get Bluetooth Data
    if (connected) {

      // Write KeepAlive
      pRemoteKeepAlive->writeValue((uint8_t)1);

    }else if(doScan){
      Serial.println("[X] doScan");
      BLEDevice::getScan()->start(0);
    }

    // correct timer
    previousMillis = millis();

#ifdef LoRa 
    // shall we now publish (5 minutes)
    TTNCounter++;
    displayUpdate();
    if (TTNCounter==300)
    {
      send_to_TTN();
      TTNCounter=0; 
    }
#endif
#ifdef M5_Platform
    // tft got to sleep after 1 minute
    tft_counter++;
    if (tft_counter==60) 
    {
      if (tft_backlight == true) { tft_backlight=false; }   
      // Turning off the LCD backlight
      if (tft_backlight == false) 
      {
        M5.Lcd.sleep();  M5.Lcd.setBrightness(0);
        Serial.println(F("[x] sleeping mode ... "));
      }
      tft_counter=0;     
    }
#endif   
  }
  if (doConnect == true) {
    Serial.println("[X] doConnect");
    if (connectToServer()) {
      Serial.println(" - Connected");
      status_message("Connected");
    } else {
      Serial.println(" - failed to connect to the server; Rescanning...");
      status_message("Cannot connect");
      doScan = true;
    }
    doConnect = false;
  }
  delay(100); 
  #ifdef M5_Platform 
    M5.update();
  #endif
}
