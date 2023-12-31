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

// void GetStackSize()
// {
//     TaskStatus_t *pxTaskStatusArray[2];
//     volatile UBaseType_t uxArraySize, x;
//     uint32_t ulTotalRunTime, ulStatsAsPercentage;

//    /* Make sure the write buffer does not contain a string. */
//    //*pcWriteBuffer = 0x00;

//    /* Take a snapshot of the number of tasks in case it changes while this
//    function is executing. */
//    uxArraySize = uxTaskGetNumberOfTasks();

//    /* Allocate a TaskStatus_t structure for each task.  An array could be
//    allocated statically at compile time. */
//    //pxTaskStatusArray = pvPortMalloc( uxArraySize * sizeof( TaskStatus_t ) );

//    if( pxTaskStatusArray != NULL )
//    {
//       /* Generate raw status information about each task. */
//       uxArraySize = uxTaskGetSystemState( &pxTaskStatusArray,
//                                  uxArraySize,
//                                  &ulTotalRunTime );

//       /* For percentage calculations. */
//       ulTotalRunTime /= 100UL;

//       /* Avoid divide by zero errors. */
//       if( ulTotalRunTime > 0 )
//       {
//          /* For each populated position in the pxTaskStatusArray array,
//          format the raw data as human readable ASCII data. */
//          for( x = 0; x < uxArraySize; x++ )
//          {
//             /* What percentage of the total run time has the task used?
//             This will always be rounded down to the nearest integer.
//             ulTotalRunTimeDiv100 has already been divided by 100. */
//             ulStatsAsPercentage =
//                   pxTaskStatusArray[ x ].ulRunTimeCounter / ulTotalRunTime;

//             if( ulStatsAsPercentage > 0UL )
//             {
//                Serial.printf("%stt%lutt%lu%%rn",
//                                  pxTaskStatusArray[ x ].pcTaskName,
//                                  pxTaskStatusArray[ x ].ulRunTimeCounter,
//                                  ulStatsAsPercentage );
//             }
//             else
//             {
//                /* If the percentage is zero here then the task has
//                consumed less than 1% of the total run time. */
//                Serial.printf("%stt%lutt<1%%rn",
//                                  pxTaskStatusArray[ x ].pcTaskName,
//                                  pxTaskStatusArray[ x ].ulRunTimeCounter );
//             }

//             //pcWriteBuffer += strlen( ( char * ) pcWriteBuffer );
//          }
//       }

//       /* The array is no longer needed, free the memory it consumes. */
//       vPortFree( pxTaskStatusArray );
//    }
// }

bool wifi_isconnected() 
{                           //function to check if wifi is connected
    return (WiFi.status() == WL_CONNECTED);
}

void reconnect_wifi() 
{                                   //function to reconnect wifi if its not connected
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


void WebsiteTaskHandler(void * pvParameters)
{
  
  //set ESP32 mode as a station to be connected to wifi network
  WiFi.mode(WIFI_STA);   
  wifi_isconnected();

  Serial.println("Website task running on core ");
  Serial.println(xPortGetCoreID());
  SimpleWeb::Router router = SimpleWeb::Router(server);
  Serial.println("Router setup ");
  
  //Controllers must be placed in the order in which they should check the header
  router.AddController(dataController);  
  Serial.println("Router done ");

  while(true)
  {
    //GetStackSize();

    reconnect_wifi();
    router.Check();
    //With out the delay it crashes???? idk
    delay(500);     
  }
}

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

   
  xTaskCreatePinnedToCore(
        WebsiteTaskHandler,   /* Task function. */
        "Website Task",     /* name of task. */
        20000,       /* Stack size of task */
        NULL,        /* parameter of the task */
        1,           /* priority of the task */
        &webSiteTask,      /* Task handle to keep track of created task */
        1);          /* pin task to core 1 setup and loop run on core 0*/  
}

void loop() { 
    Serial.println("\nGoing to read");  
    dataController->ReadData();
    delay(10000);
}
