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

        //enable pins for each circuit
        const int EN_PH = 12;
        const int EN_ORP = 27;
        const int EN_RTD = 15;
        const int EN_AUX = 33;

        //an array of boards used for sending commands to all or specific boards
        Ezo_board device_list[3] ;    

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

        bool process_coms(const String &string_buffer) 
        {      //function to process commands that manipulate global variables and are specifc to certain kits
            if (string_buffer == "HELP") {
                print_help();
                return true;
            }
            return false;                         //return false if the command is not in the list, so we can scan the other list or pass it to the circuit
        }    

        public:
        DataController(Ezo_board &ph, Ezo_board &orp, Ezo_board &rtd): 
            PH(ph),
            ORP(orp),
            RTD(rtd),
            device_list {PH, ORP, RTD},
            deviceLength(sizeof(device_list)/sizeof(device_list[0]))
        {

        }

        bool Handler(WiFiClient& client, const String& header)
        {
            if(header.indexOf("POST /CMD HTTP/1.1") >= 0)
            {
                String cmd;
                Ezo_board* default_board = &PH;

                StaticJsonDocument<256> doc;
                String s = client.readStringUntil('\n');              
                DeserializationError error = deserializeJson(doc, s.c_str());

                cmd = doc.as<String>();
                cmd.toUpperCase();                     //turn the command to uppercase for easier comparisions
                cmd.trim();
                Serial.printf("Received command=%s\n", cmd);

                process_command(cmd, device_list, deviceLength, default_board);    //then if its not kit specific, pass the cmd to the IOT command processing function

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
                    Serial.printf("device=%s\n", device_list[i].get_name());
                }
                    
                // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
                // and a content-type so the client knows what's coming, then a blank line:
                client.println("HTTP/1.1 200 OK");
                client.println("Content-type:text/json");
                client.println("Connection: close");
                client.println();

                serializeJson(doc, client); 
                return true; 
            }
            else if(header.indexOf("POST /data HTTP/1.1") >= 0)
            {
                StaticJsonDocument<256> doc;
                String s = client.readStringUntil('\n');              
                DeserializationError error = deserializeJson(doc, s.c_str());   

                if (error) 
                {
                    Serial.print(F("deserializeJson() failed: "));
                    Serial.println(error.f_str());

                    doc["success"] = false;
                    doc["message"] = error.f_str();                

                    // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
                    // and a content-type so the client knows what's coming, then a blank line:
                    client.println("HTTP/1.1 500 Internal Server Error");
                    client.println("Content-type:text/json");
                    client.println("Connection: close");
                    client.println();
                }
                else
                {
                    if(doc["state"].as<bool>())
                        digitalWrite(doc["gpio"].as<int>(), HIGH);
                    else
                        digitalWrite(doc["gpio"].as<int>(), LOW);

                    // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
                    // and a content-type so the client knows what's coming, then a blank line:
                    client.println("HTTP/1.1 200 OK");
                    client.println("Content-type:text/json");
                    client.println("Connection: close");
                    client.println();
                }

                serializeJson(doc, client);  

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

                Serial.printf("Getting %s\n", RTD.get_name());
                RTD.send_read_cmd();
                receive_and_print_reading(RTD); 
                if ((RTD.get_error() == Ezo_board::SUCCESS) && (RTD.get_last_received_reading() > -1000.0)) 
                { 
                    //if the temperature reading has been received and it is valid
                    PH.send_cmd_with_num("T,", RTD.get_last_received_reading());
                    doc["rtd"] = RTD.get_last_received_reading();
                    //ThingSpeak.setField(3, String(RTD.get_last_received_reading(), 2));                 //assign temperature readings to the third column of thingspeak channel
                } 
                else 
                {                                                                                      //if the temperature reading is invalid
                    PH.send_cmd_with_num("T,", 25.0);                                                           //send default temp = 25 deg C to PH sensor
                    doc["rtd"] = 25;
                    //ThingSpeak.setField(3, String(25.0, 2));                 //assign temperature readings to the third column of thingspeak channel
                }

                //send a read command. we use this command instead of PH.send_cmd("R");
                //to let the library know to parse the reading
                PH.send_read_cmd();
                ORP.send_read_cmd();

                Serial.printf("Getting %s\n", PH.get_name());
                receive_and_print_reading(PH);             //get the reading from the PH circuit
                if (PH.get_error() == Ezo_board::SUCCESS) 
                {                                          //if the PH reading was successful (back in step 1)
                    doc["ph"] = PH.get_last_received_reading();
                    //ThingSpeak.setField(1, String(PH.get_last_received_reading(), 2));                 //assign PH readings to the first column of thingspeak channel
                }
                else
                    doc["ph"] = PH.get_error();

                Serial.printf("Getting %s\n", ORP.get_name());
                receive_and_print_reading(ORP);             //get the reading from the ORP circuit
                if (ORP.get_error() == Ezo_board::SUCCESS) 
                {                                          //if the ORP reading was successful (back in step 1)
                    doc["orp"] = ORP.get_last_received_reading();
                    //ThingSpeak.setField(2, String(ORP.get_last_received_reading(), 0));                 //assign ORP readings to the second column of thingspeak channel
                }
                else
                    doc["orp"] = ORP.get_error();

                
/*
               Serial.printf("Getting %s\n", RTD.get_name());
                print_device_info(RTD);
                receive_and_print_reading(RTD);                             
                doc["rtd"] = RTD.get_last_received_reading();              
                
                Serial.printf("Getting %s\n", PH.get_name());
                receive_and_print_reading(PH);  
                print_device_info(PH);
                doc["ph"] = PH.get_last_received_reading(); 

                Serial.printf("Getting %s\n", ORP.get_name());   
                receive_and_print_reading(ORP);
                print_device_info(ORP);
                doc["orp"] = ORP.get_last_received_reading(); 

  */              
                
                serializeJson(doc, client);

                return true;
            }

            return false;
        }
    };

}