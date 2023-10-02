#include <Arduino.h>
//This code is for the Atlas Scientific wifi pool kit that uses the Adafruit huzzah32 as its computer.

#include <iot_cmd.h>
#include <WiFi.h>                                                //include wifi library 
#include <sequencer4.h>                                          //imports a 4 function sequencer 
#include <sequencer1.h>                                          //imports a 1 function sequencer 
#include <Ezo_i2c_util.h>                                        //brings in common print statements
#include <Ezo_i2c.h> //include the EZO I2C library from https://github.com/Atlas-Scientific/Ezo_I2c_lib
#include <Wire.h>    //include arduinos i2c library
#include "SimpleWeb/DataController.cpp"
#include "SimpleWeb/Router.h"
#include "SimpleWeb/IController.h"

WiFiClient client;                                              //declare that this device connects to a Wi-Fi network,create a connection to a specified internet IP address
// Set web server port number to 80
WiFiServer server(80);

const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;

Ezo_board PH = Ezo_board(99, "PH");           //create a PH circuit object, who's address is 99 and name is "PH"
Ezo_board ORP = Ezo_board(98, "ORP");         //create an ORP circuit object who's address is 98 and name is "ORP"
Ezo_board RTD = Ezo_board(102, "RTD");        //create an RTD circuit object who's address is 102 and name is "RTD"
Ezo_board PMPL = Ezo_board(109, "PMPL");      //create an PMPL circuit object who's address is 109 and name is "PMPL"

Ezo_board device_list[] = {   //an array of boards used for sending commands to all or specific boards
  PH,
  ORP,
  RTD,
  PMPL
};

bool process_coms(const String &string_buffer) ;
void print_help();

Ezo_board* default_board = &device_list[0]; //used to store the board were talking to

//gets the length of the array automatically so we dont have to change the number every time we add new boards
const uint8_t device_list_len = sizeof(device_list) / sizeof(device_list[0]);

//enable pins for each circuit
const int EN_PH = 12;
const int EN_ORP = 27;
const int EN_RTD = 15;
const int EN_AUX = 33;

const unsigned long reading_delay = 1000;                 //how long we wait to receive a response, in milliseconds
const unsigned long thingspeak_delay = 15000;             //how long we wait to send values to thingspeak, in milliseconds

unsigned int poll_delay = 2000 - reading_delay * 2 - 300; //how long to wait between polls after accounting for the times it takes to send readings

//parameters for setting the pump output
#define PUMP_BOARD        PMPL      //the pump that will do the output (if theres more than one)
#define PUMP_DOSE         10        //the dose that the pump will dispense in  milliliters
#define EZO_BOARD         PH        //the circuit that will be the target of comparison
#define IS_GREATER_THAN   true      //true means the circuit's reading has to be greater than the comparison value, false mean it has to be less than
#define COMPARISON_VALUE  7         //the threshold above or below which the pump is activated

float k_val = 0;                                          //holds the k value for determining what to print in the help menu

bool polling  = true;                                     //variable to determine whether or not were polling the circuits
bool send_to_thingspeak = true;                           //variable to determine whether or not were sending data to thingspeak
TaskHandle_t webSiteTask;
SimpleWeb::DataController *dataController = new SimpleWeb::DataController(PH, ORP, RTD);

bool wifi_isconnected() {                           //function to check if wifi is connected
  return (WiFi.status() == WL_CONNECTED);
}

void WebsiteTaskHandler(void * pvParameters)
{
  Serial.println("Website task running on core ");
  Serial.println(xPortGetCoreID());
  SimpleWeb::Router router = SimpleWeb::Router(server);
  Serial.println("Router setup ");
  
  //Controllers must be placed in the order in which they should check the header
  router.AddController(dataController);
  
  Serial.println("Router done ");

  while(true)
  {
    router.Check();
    //With out the delay it crashes???? idk
    delay(50);
  }
}

void reconnect_wifi() {                                   //function to reconnect wifi if its not connected
  if (!wifi_isconnected()) 
  {
    // Connect to Wi-Fi network with SSID and password
    Serial.print("Connecting to ");
    Serial.println(ssid);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) 
    {
      delay(500);
      Serial.print(".");
    }
    // Print local IP address and start web server
    Serial.println("");
    Serial.println("WiFi connected.");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
    
    server.begin(); 
  }
}

void thingspeak_send() {
  if (send_to_thingspeak == true) {                                                    //if we're datalogging
    if (wifi_isconnected()) {
      /*
      int return_code = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);
      if (return_code == 200) {                                                          //code for successful transmission
        Serial.println("sent to thingspeak");
      } else {
        Serial.println("couldnt send to thingspeak");
      }
      */
    }
  }
}

void step1();      //forward declarations of functions to use them in the sequencer before defining them
void step2();
void step3();
void step4();
Sequencer4 Seq(&step1, reading_delay,   //calls the steps in sequence with time in between them
               &step2, 300,
               &step3, reading_delay,
               &step4, poll_delay);

Sequencer1 Wifi_Seq(&reconnect_wifi, 10000);  //calls the wifi reconnect function every 10 seconds

Sequencer1 Thingspeak_seq(&thingspeak_send, thingspeak_delay); //sends data to thingspeak with the time determined by thingspeak delay

void setup() {

  pinMode(EN_PH, OUTPUT);                                                         //set enable pins as outputs
  pinMode(EN_ORP, OUTPUT);
  pinMode(EN_RTD, OUTPUT);
  pinMode(EN_AUX, OUTPUT);
  digitalWrite(EN_PH, LOW);                                                       //set enable pins to enable the circuits
  digitalWrite(EN_ORP, LOW);
  digitalWrite(EN_RTD, HIGH);
  digitalWrite(EN_AUX, LOW);

  Wire.begin();                           //start the I2C
  Serial.begin(115200);                    //start the serial communication to the computer

  WiFi.mode(WIFI_STA);                    //set ESP32 mode as a station to be connected to wifi network
  //ThingSpeak.begin(client);               //enable ThingSpeak connection
  Wifi_Seq.reset();                       //initialize the sequencers
  Seq.reset();
  
  wifi_isconnected();

  
  xTaskCreatePinnedToCore(
        WebsiteTaskHandler,   /* Task function. */
        "Task1",     /* name of task. */
        10000,       /* Stack size of task */
        NULL,        /* parameter of the task */
        1,           /* priority of the task */
        &webSiteTask,      /* Task handle to keep track of created task */
        1);          /* pin task to core 0 */  
}

void loop() { 
  reconnect_wifi();
  dataController->ReadData();
  delay(1000);
}

void step1() {
  //send a read command. we use this command instead of RTD.send_cmd("R");
  //to let the library know to parse the reading
  RTD.send_read_cmd();
}

void step2() {
  receive_and_print_reading(RTD);             //get the reading from the RTD circuit

  if ((RTD.get_error() == Ezo_board::SUCCESS) && (RTD.get_last_received_reading() > -1000.0)) 
  { 
    //if the temperature reading has been received and it is valid
    PH.send_cmd_with_num("T,", RTD.get_last_received_reading());
    //ThingSpeak.setField(3, String(RTD.get_last_received_reading(), 2));                 //assign temperature readings to the third column of thingspeak channel
  } 
  else 
  {                                                                                      //if the temperature reading is invalid
    PH.send_cmd_with_num("T,", 25.0);                                                           //send default temp = 25 deg C to PH sensor
    //ThingSpeak.setField(3, String(25.0, 2));                 //assign temperature readings to the third column of thingspeak channel
  }

  Serial.print(" ");
}

void step3() {
  //send a read command. we use this command instead of PH.send_cmd("R");
  //to let the library know to parse the reading
  PH.send_read_cmd();
  ORP.send_read_cmd();
}

void step4() {
  receive_and_print_reading(PH);             //get the reading from the PH circuit
  if (PH.get_error() == Ezo_board::SUCCESS) 
  {                                          //if the PH reading was successful (back in step 1)
    //ThingSpeak.setField(1, String(PH.get_last_received_reading(), 2));                 //assign PH readings to the first column of thingspeak channel
  }
  Serial.print("  ");
  receive_and_print_reading(ORP);             //get the reading from the ORP circuit
  if (ORP.get_error() == Ezo_board::SUCCESS) 
  {                                          //if the ORP reading was successful (back in step 1)
    //ThingSpeak.setField(2, String(ORP.get_last_received_reading(), 0));                 //assign ORP readings to the second column of thingspeak channel
  }
  Serial.println();
  
}

void start_datalogging() {
  polling = true;                                                 //set poll to true to start the polling loop
  send_to_thingspeak = true;
  Thingspeak_seq.reset();
}

bool process_coms(const String &string_buffer) 
{      //function to process commands that manipulate global variables and are specifc to certain kits
  if (string_buffer == "HELP") {
    print_help();
    return true;
  }
  else if (string_buffer.startsWith("DATALOG")) 
  {
    start_datalogging();
    return true;
  }
  else if (string_buffer.startsWith("POLL")) 
  {
    polling = true;
    Seq.reset();

    int16_t index = string_buffer.indexOf(',');                    //check if were passing a polling delay parameter
    if (index != -1) {                                              //if there is a polling delay
      float new_delay = string_buffer.substring(index + 1).toFloat(); //turn it into a float

      float mintime = reading_delay * 2 + 300;
      if (new_delay >= (mintime / 1000.0)) {                                     //make sure its greater than our minimum time
        Seq.set_step4_time((new_delay * 1000.0) - mintime);          //convert to milliseconds and remove the reading delay from our wait
      } else {
        Serial.println("delay too short");                          //print an error if the polling time isnt valid
      }
    }
    return true;
  }
  return false;                         //return false if the command is not in the list, so we can scan the other list or pass it to the circuit
}

void print_help() {
  Serial.println(F("Atlas Scientific I2C pool kit                                              "));
  Serial.println(F("Commands:                                                                  "));
  Serial.println(F("datalog      Takes readings of all sensors every 15 sec send to thingspeak "));
  Serial.println(F("             Entering any commands stops datalog mode.                     "));
  Serial.println(F("poll         Takes readings continuously of all sensors                    "));
  Serial.println(F("                                                                           "));
  Serial.println(F("ph:cal,mid,7     calibrate to pH 7                                         "));
  Serial.println(F("ph:cal,low,4     calibrate to pH 4                                         "));
  Serial.println(F("ph:cal,high,10   calibrate to pH 10                                        "));
  Serial.println(F("ph:cal,clear     clear calibration                                         "));
  Serial.println(F("                                                                           "));
  Serial.println(F("orp:cal,225          calibrate orp probe to 225mV                          "));
  Serial.println(F("orp:cal,clear        clear calibration                                     "));
  Serial.println(F("                                                                           "));
  Serial.println(F("rtd:cal,t            calibrate the temp probe to any temp value            "));
  Serial.println(F("                     t= the temperature you have chosen                    "));
  Serial.println(F("rtd:cal,clear        clear calibration                                     "));
}
