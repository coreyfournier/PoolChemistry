#pragma once
#include <ArduinoJson.h>
#include "Router.h"
#include "IController.h"
#include <Ezo_i2c_util.h>                                        //brings in common print statements
#include <Ezo_i2c.h> //include the EZO I2C library from https://github.com/Atlas-Scientific/Ezo_I2c_lib
#include <iot_cmd.h>

using namespace std;


namespace SimpleWeb
{
    class DataController: public IController
    {
        private:        
        Ezo_board PH;
        Ezo_board ORP;
        Ezo_board RTD;
        int deviceLength;
        bool okToGetData;

        //enable pins for each circuit
        const int EN_PH = 12;
        const int EN_ORP = 27;
        const int EN_RTD = 15;
        const int EN_AUX = 33;

        //an array of boards used for sending commands to all or specific boards
        Ezo_board* devicePointers[3];
 
        Ezo_board* findDevice(string name)
        {
            for (uint8_t i = 0; i < deviceLength; i++) 
            {
                if (name == devicePointers[i]->get_name()) 
                    return devicePointers[i]; 
            }

            return nullptr;
        }
        

        public:
        DataController(Ezo_board &ph, Ezo_board &orp, Ezo_board &rtd): 
            PH(ph),
            ORP(orp),
            RTD(rtd),
            devicePointers {&PH, &ORP, &RTD},
            deviceLength(sizeof(devicePointers)/sizeof(devicePointers[0])),
            okToGetData(true)
        {

        }

  

        /*
            Reads the data from the sensors and stores them
        */
        void ReadData()
        {
            float rtd;
            float ph;
            float orp;

            if(okToGetData)
            {
                Serial.printf("\nGetting %s\n", RTD.get_name());
                    
                RTD.send_read_cmd();
                delay(1000);
                receive_and_print_reading(RTD); 
                if ((RTD.get_error() == Ezo_board::SUCCESS) && (RTD.get_last_received_reading() > -1000.0)) 
                { 
                    //if the temperature reading has been received and it is valid
                    PH.send_cmd_with_num("T,", RTD.get_last_received_reading());
                    delay(1000);
                    rtd = RTD.get_last_received_reading();
                } 
                else 
                {                                                                                      //if the temperature reading is invalid
                    PH.send_cmd_with_num("T,", 25.0);  
                    delay(1000);                                                         //send default temp = 25 deg C to PH sensor
                    rtd = 25;
                }

                //send a read command. we use this command instead of PH.send_cmd("R");
                //to let the library know to parse the reading
                PH.send_read_cmd();
                
                ORP.send_read_cmd();
                delay(1000);

                Serial.printf("\nGetting %s\n", PH.get_name());
                receive_and_print_reading(PH);             //get the reading from the PH circuit
                if (PH.get_error() == Ezo_board::SUCCESS) 
                    ph = PH.get_last_received_reading();
                else
                    PH.get_error();

                Serial.printf("\nGetting %s\n", ORP.get_name());
                receive_and_print_reading(ORP);             //get the reading from the ORP circuit
                if (ORP.get_error() == Ezo_board::SUCCESS) 
                    orp = ORP.get_last_received_reading();
                else
                    ORP.get_error();
            }
        }

        
        bool Handler(WiFiClient& client, const String& header)
        {
            if(header.indexOf("POST /CMD HTTP/1.1") >= 0)
            {
                okToGetData = false;
                String cmd;
                StaticJsonDocument<256> doc;
                String s = client.readStringUntil('\n');              
                DeserializationError error = deserializeJson(doc, s.c_str());

                okToGetData = false;

                cmd = doc.as<String>();
                
                Ezo_board* board = findDevice(doc["device"]);

                if(board == nullptr)
                {
                    StaticJsonDocument<100> response;
                    response["error"] = "Device not found";
                    
                    serializeJson(response, client);
                }
                else
                {
                    StaticJsonDocument<100> response;
                    
                    cmd.toUpperCase();                     //turn the command to uppercase for easier comparisions
                    cmd.trim();
                    Serial.printf("Received command=%s\n", cmd);
                    
                    board->send_cmd(cmd.c_str());
                    delay(1200);

                     switch (board->get_error()) {             //switch case based on what the response code is.
                        case Ezo_board::SUCCESS:
                            char receive_buffer[32];
                            Serial.println("Receiving response");
                            board->receive_cmd(receive_buffer, 32);
                            response["response"] = receive_buffer;

                            break;
                        case Ezo_board::FAIL:
                            response["error"] = "Device responded with a FAIL";
                            break;

                        case Ezo_board::NOT_READY:
                            response["error"] = "the command has not yet been finished calculating";
                            break;
                        case Ezo_board::NO_DATA:
                            response["error"] = "the sensor has no data to send.";
                            break;
                    }

                    //write out the response
                    serializeJson(response, client);
                }               

                okToGetData = true;

                client.println("HTTP/1.1 200 OK");
                client.println("Content-type:text/json");
                client.println("Connection: close");
                client.println();
                return true;
            }
            else if(header.indexOf("GET /HELP HTTP/1.1") >= 0)
            {
                // compute the required size
                const size_t CAPACITY = JSON_ARRAY_SIZE(8);

                // allocate the memory for the document
                StaticJsonDocument<CAPACITY> doc;

                // create an empty array
                JsonArray array = doc.to<JsonArray>();

                array.add("ph:cal,mid,7     calibrate to pH 7");
                array.add("ph:cal,low,4     calibrate to pH 4");
                array.add("ph:cal,high,10   calibrate to pH 10");
                array.add("ph:cal,clear     clear calibration");
                array.add("orp:cal,225          calibrate orp probe to 225mV");
                array.add("orp:cal,clear        clear calibration");
                array.add("rtd:cal,t            calibrate the temp probe to any temp value");                
                array.add("rtd:cal,clear        clear calibration");

                for(int i=0; i < deviceLength; i++)
                {
                    Serial.printf("device=%s\n", devicePointers[i]->get_name());
                }
                    
                // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
                // and a content-type so the client knows what's coming, then a blank line:
                client.println("HTTP/1.1 200 OK");
                client.println("Content-type:text/json");
                client.println("Connection: close");
                client.println();

                serializeJson(doc, client); 
                doc.garbageCollect();
                return true; 
            }           
            else if(header.indexOf("GET /data HTTP/1.1") >= 0)
            {   
                StaticJsonDocument<200> doc;      
                // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
                // and a content-type so the client knows what's coming, then a blank line:
                client.println("HTTP/1.1 200 OK");
                client.println("Content-type:text/json");
                client.println("Connection: close");
                client.println(); 
                
                Serial.printf("data...\n"); 
                    
                for(int i=0; i< deviceLength; i++)
                    doc[devicePointers[i]->get_name()] = devicePointers[i]->get_last_received_reading();
                
                serializeJson(doc, client);
                doc.garbageCollect();
                return true;
            }

            return false;
        }
    };   
    
}