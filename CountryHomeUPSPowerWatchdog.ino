/*

LED error codes:

2 blinks - relay power error
3 blinks - relay switch error


*/

#define MY_OTA_FIRMWARE_FEATURE
//#define MY_OTA_FLASH_SS   8     // EEprom CS pin
//#define MY_OTA_FLASH_JDECID 0x1F65

#define MY_RADIO_NRF24
#define MY_RF24_CHANNEL	86
#define MY_NODE_ID 91
//#define MY_DEBUG // Enables debug messages in the serial log
//#define MY_BAUD_RATE 115200

#include <MySensors.h>  
#include <SPI.h>
#include <DallasTemperature.h>
#include <OneWire.h>
#include <avr/wdt.h>
#include <SimpleTimer.h>

 //#define NDEBUG                        // enable local debugging information

#define SKETCH_NAME "UPS Watchdog OTA"
#define SKETCH_MAJOR_VER "1"
#define SKETCH_MINOR_VER "1"


#define TEMPERATURE_PIN 3
#define RELAY_PIN A4
#define RELAY_STATUS_PIN 4
#define RELAY_POWER_PIN 5
#define STATUS_LED_PIN A0




#define RELAY_TEMP_CHILD_ID 					 60
#define WATCHDOG_STATUS_CHILD_ID 				 90
#define RELAY_POWER_STATUS_CHILD_ID 			 91
#define RELAY_STATUS_CHILD_ID 				     92 
#define WATCHDOG_MESSAGE_CHILD_ID 				 99
#define WATCHDOGENABLED_STATE_CHILD_ID 		     93

#define LASTRECEIVED_MESSAGE_CHILD_ID 			120

#define REBOOT_CHILD_ID                       100
#define RECHECK_SENSOR_VALUES                 101 
#define INITIALIZE_RESET	                  110 
#define STOP_WATCHDOG		                  111 



/*****************************************************************************************************/
/*                               				Common settings									      */
/******************************************************************************************************/
#define RADIO_RESET_DELAY_TIME 50 //Задержка между сообщениями
#define MESSAGE_ACK_RETRY_COUNT 5  //количество попыток отсылки сообщения с запросом подтверждения
#define DATASEND_DELAY  10

boolean gotAck=false; //подтверждение от гейта о получении сообщения 
int iCount = MESSAGE_ACK_RETRY_COUNT;

boolean boolRecheckSensorValues = false;

boolean bWatchDogStopped=false;

//unsigned long currentMillis = millis();

unsigned long ulLastReceivedMark=0;
SimpleTimer timerCheckLastMark;

#define WATCHDOGTIME 3600000 //2400000  //7200000
#define RELAYOPENTIME 300000


#define ONE_WIRE_BUS              3      // Pin where dallase sensor is connected 
#define ALARM_RELAY_TEMP 70

OneWire oneWire(ONE_WIRE_BUS);        // Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
DallasTemperature sensors(&oneWire);  // Pass the oneWire reference to Dallas Temperature. 
float lastTemp1 = 0;

boolean watchdogStatus = false; 
boolean boolRelayStatus = false;
boolean boolRelayPower = false;


boolean bInitReset = false;

//MySensor sensor_node;
MyMessage msgWatchdogState(WATCHDOG_STATUS_CHILD_ID, V_STATUS);
MyMessage msgWatchdogEnabledState(WATCHDOGENABLED_STATE_CHILD_ID, V_STATUS);
MyMessage msgRelayPowerState(RELAY_POWER_STATUS_CHILD_ID, V_STATUS);
MyMessage msgRelayState(RELAY_STATUS_CHILD_ID, V_STATUS);
MyMessage msgRelayTemp(RELAY_TEMP_CHILD_ID, V_TEMP);
MyMessage msgLastReceivedKeepAlive(LASTRECEIVED_MESSAGE_CHILD_ID, V_VAR1);


void before() 
{
 // This will execute before MySensors starts up 

  // Setup the Siren Pin HIGH
  pinMode(TEMPERATURE_PIN, INPUT);

  pinMode(RELAY_STATUS_PIN, INPUT_PULLUP);

  pinMode(RELAY_POWER_PIN, INPUT_PULLUP);


  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);  

  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, HIGH); 

}

void presentation() 
{

  //sensor_node.begin(incomingMessage, NODE_ID);

  // Send the sketch version information to the gateway and Controller
sendSketchInfo(SKETCH_NAME, SKETCH_MAJOR_VER"."SKETCH_MINOR_VER);
wait(RADIO_RESET_DELAY_TIME);

present(WATCHDOG_STATUS_CHILD_ID, S_BINARY);
wait(RADIO_RESET_DELAY_TIME);


present(RELAY_POWER_STATUS_CHILD_ID, S_BINARY);
wait(RADIO_RESET_DELAY_TIME);

present(RELAY_STATUS_CHILD_ID, S_BINARY);
wait(RADIO_RESET_DELAY_TIME);


present(RELAY_TEMP_CHILD_ID, S_TEMP);
wait(RADIO_RESET_DELAY_TIME);


present(WATCHDOG_MESSAGE_CHILD_ID, S_BINARY);
wait(RADIO_RESET_DELAY_TIME);

present(LASTRECEIVED_MESSAGE_CHILD_ID, S_CUSTOM);
wait(RADIO_RESET_DELAY_TIME);

present(WATCHDOGENABLED_STATE_CHILD_ID, S_BINARY);
wait(RADIO_RESET_DELAY_TIME);

  //reboot sensor command
present(REBOOT_CHILD_ID, S_BINARY); //, "Reboot node sensor", true); 
wait(RADIO_RESET_DELAY_TIME);

  //reget sensor values
present(RECHECK_SENSOR_VALUES, S_LIGHT); 
wait(RADIO_RESET_DELAY_TIME);  

present(INITIALIZE_RESET, S_BINARY); 
wait(RADIO_RESET_DELAY_TIME);  

present(STOP_WATCHDOG, S_BINARY); 
wait(RADIO_RESET_DELAY_TIME);  


  	//Enable watchdog timer
  	wdt_enable(WDTO_8S);  


ledStartupBlink();


}

void setup()
{

	  timerCheckLastMark.setInterval(60000, checkMark); //check every hour 3600000
	  timerCheckLastMark.setInterval(30000, checkRelayTemp); //check temp every 30 sec
	  timerCheckLastMark.setInterval(20000, checkRelayPower); //check relay power every 20 sec
	  timerCheckLastMark.setInterval(300000, reportRelayStatus); //report relay status every 5 min


wait(500);
byte bRelayStatus = digitalRead(RELAY_STATUS_PIN);

wait(500);
byte bRelayPower = digitalRead(RELAY_POWER_PIN);

if (bRelayStatus == 1)
{
	boolRelayStatus = false;
}
else
{
	boolRelayStatus = true;
}


if (bRelayPower == 1)
{
	boolRelayPower = false;
}
else
{
	boolRelayPower = true;
}


reportRelayStatus();


	if( bRelayStatus == 1 && bRelayPower == 0 )
	{
		watchdogStatus = true;

	}	  

					//report status
				    iCount = MESSAGE_ACK_RETRY_COUNT;

                    while( !gotAck && iCount > 0 )
                      {
            
                        send(msgWatchdogState.set(watchdogStatus?"1":"0"), true);
                         wait(RADIO_RESET_DELAY_TIME);
                        iCount--;
                       }

                      gotAck = false; 

//blink status LED

}

void loop()
{

	timerCheckLastMark.run();


if ( boolRecheckSensorValues )
{
	boolRecheckSensorValues = false;

	checkRelayTemp();
	reportRelayStatus();
} 

  //reset watchdog timer
  wdt_reset();   

}

void checkMark()
{

byte value=2;

  unsigned long currentMillis = millis();



//send last received mark
				    iCount = MESSAGE_ACK_RETRY_COUNT;

                    while( !gotAck && iCount > 0 )
                      {
            
                        send(msgLastReceivedKeepAlive.set(((currentMillis-ulLastReceivedMark)/1000/60), true));
                         wait(RADIO_RESET_DELAY_TIME);
                        iCount--;
                       }

                      gotAck = false; 




  if( (( (currentMillis - ulLastReceivedMark > WATCHDOGTIME) && (currentMillis - ulLastReceivedMark < (WATCHDOGTIME+300000) ) || bInitReset) && !bWatchDogStopped )
  {


  	value = digitalRead(RELAY_STATUS_PIN);

  		if (value == 1)
		{
    		// switch relay to on
      		digitalWrite(RELAY_PIN, HIGH); 
      		wait(500);
  			value = digitalRead(RELAY_STATUS_PIN);

			if ( value != 0 )
			{

				reportRelayMaifunction();
				boolRelayStatus = false;

			} else
			{
				watchdogStatus = true;
				boolRelayStatus = true;
    			//run timer to switch off after 5 minutes    
    			timerCheckLastMark.setTimeout(RELAYOPENTIME, closeRelay);
    			digitalWrite(STATUS_LED_PIN, 1);
    			if (!bInitReset)
    			{
    				ulLastReceivedMark = currentMillis - (WATCHDOGTIME/2 - 300000); 
    			}

    		}

      	}
      	else
      	{

			reportRelayMaifunction();
			boolRelayStatus = false;

      	}


					//report relay status
				    iCount = MESSAGE_ACK_RETRY_COUNT;

                    while( !gotAck && iCount > 0 )
                      {
            
                        send(msgRelayState.set(boolRelayStatus?"1":"0"), true);
                         wait(RADIO_RESET_DELAY_TIME);
                        iCount--;
                       }

                      gotAck = false; 

  	bInitReset = false;

  }




}


void closeRelay()
{
	byte value=2;

	//close relay

	value = digitalRead(RELAY_STATUS_PIN);

	if ( value == 0 )
	{
		digitalWrite(RELAY_PIN, LOW);
		wait(500);
    	value = digitalRead(RELAY_STATUS_PIN);

		if ( value !=1 )
		{
			boolRelayStatus = true;
			reportRelayMaifunction();
		}
		else
		{
			boolRelayStatus = false;
			watchdogStatus = true;
		    digitalWrite(STATUS_LED_PIN, 0);			
		}    
	}
	else
	{
		reportRelayMaifunction();
		boolRelayStatus = false;
	}

					//report relay status
				    iCount = MESSAGE_ACK_RETRY_COUNT;

                    while( !gotAck && iCount > 0 )
                      {
            
                        send(msgRelayState.set(boolRelayStatus?"1":"0"), true);
                         wait(RADIO_RESET_DELAY_TIME);
                        iCount--;
                       }

                      gotAck = false; 


}


void checkRelayTemp()
{

byte value =2;
// Fetch temperatures from Dallas sensors
  sensors.requestTemperatures();

  // query conversion time and sleep until conversion completed
  int16_t conversionTime = sensors.millisToWaitForConversion(sensors.getResolution());
  // sleep() call can be replaced by wait() call if node need to process incoming messages (or if node is repeater)

       wait(conversionTime+5);


 float temperature = static_cast<float>(static_cast<int>(sensors.getTempCByIndex(0) * 10.)) / 10.;

          		#ifdef NDEBUG                
                Serial.print ("Temp: ");
          	    Serial.println (temperature); 
          	    #endif

         if (temperature != lastTemp1 && temperature != -127.00 && temperature != 85.00 ) {



					//report relay status
				    iCount = MESSAGE_ACK_RETRY_COUNT;

                    while( !gotAck && iCount > 0 )
                      {
            
                        send(msgRelayTemp.set(temperature,1), true);
                         wait(RADIO_RESET_DELAY_TIME);
                        iCount--;
                       }

                      gotAck = false; 

            lastTemp1 = temperature;
        	} 


        	if ( temperature >= ALARM_RELAY_TEMP && temperature != -127.00 && temperature != 85.00 )
        	{
        		reportRelayMaifunction();

          		#ifdef NDEBUG                
          	    Serial.println ("temperatureMailfunction"); 
          	    #endif

        		//try to switch off relay
        		if (digitalRead(RELAY_STATUS_PIN) == 0)
				{
					digitalWrite(RELAY_PIN, LOW);
					wait(500);
    				value = digitalRead(RELAY_STATUS_PIN);

					if (value == 1)
					{
						boolRelayStatus = false;
					}
					else
					{
						boolRelayStatus = true;
					}

					//report relay status
				    iCount = MESSAGE_ACK_RETRY_COUNT;

                    while( !gotAck && iCount > 0 )
                      {
            
                        send(msgRelayState.set(boolRelayStatus?"1":"0"), true);
                         wait(RADIO_RESET_DELAY_TIME);
                        iCount--;
                       }

                      gotAck = false; 
				}

        	}
        	else
        	{
        		watchdogStatus = true;
        	}
      

}


void checkRelayPower()
{

byte value =1;
value = digitalRead(RELAY_POWER_PIN);

    	   	    #ifdef NDEBUG      
      			Serial.println("Relay power: ");
      			Serial.println(value);
   	  			#endif  

if (value != 0)
{
	watchdogStatus = false;
	boolRelayPower = false;
					//report relay mailfunction
				    iCount = MESSAGE_ACK_RETRY_COUNT;

                    while( !gotAck && iCount > 0 )
                      {
            
                        send(msgRelayPowerState.set("0"), true);
                         wait(RADIO_RESET_DELAY_TIME);
                        iCount--;
                       }

                      gotAck = false; 

}
else
{
	watchdogStatus = true;

}

}




void reportRelayMaifunction()
{
    	   	    #ifdef NDEBUG      
      			Serial.println("reportRelayMaifunction");
   	  			#endif 

					watchdogStatus = false;

					//report relay mailfunction
				    iCount = MESSAGE_ACK_RETRY_COUNT;

                    while( !gotAck && iCount > 0 )
                      {
            
                        send(msgWatchdogState.set("0"), true);
                         wait(RADIO_RESET_DELAY_TIME);
                        iCount--;
                       }

                      gotAck = false; 

}

void reportRelayStatus()
{
    	   	    #ifdef NDEBUG      
      			Serial.println("reportRelayStatus");
   	  			#endif 

					//report power status
				    iCount = MESSAGE_ACK_RETRY_COUNT;

                    while( !gotAck && iCount > 0 )
                      {
            
                        send(msgRelayPowerState.set(boolRelayPower?"1":"0"), true);
                         wait(RADIO_RESET_DELAY_TIME);
                        iCount--;
                       }

                      gotAck = false; 


					//report relay status
				    iCount = MESSAGE_ACK_RETRY_COUNT;

                    while( !gotAck && iCount > 0 )
                      {
            
                        send(msgRelayState.set(boolRelayStatus?"1":"0"), true);
                         wait(RADIO_RESET_DELAY_TIME);
                        iCount--;
                       }

                      gotAck = false; 

					//report sensor status
				    iCount = MESSAGE_ACK_RETRY_COUNT;

                    while( !gotAck && iCount > 0 )
                      {
            
                        send(msgWatchdogState.set(watchdogStatus?"1":"0"), true);
                         wait(RADIO_RESET_DELAY_TIME);
                        iCount--;
                       }

                      gotAck = false; 


                      reportWatchDogRunningState();

}



void reportWatchDogRunningState()
{

					//report watchdog operational status
				    iCount = MESSAGE_ACK_RETRY_COUNT;

                    while( !gotAck && iCount > 0 )
                      {
            
                        send(msgWatchdogEnabledState.set(bWatchDogStopped?"1":"0"), true);
                         wait(RADIO_RESET_DELAY_TIME);
                        iCount--;
                       }

                      gotAck = false; 

}


void ledStartupBlink()
{

	for ( int i=0; i<15; i++)
	{
		digitalWrite(STATUS_LED_PIN, 1);
		wait(100);
		digitalWrite(STATUS_LED_PIN, 0);
		wait(100);		
	}
}




void receive(const MyMessage &message) 
{
 // Handle incoming message 

  if (message.isAck())
  {
    gotAck = true;
    return;
  }

    if ( message.sensor == REBOOT_CHILD_ID && message.getBool() == true && strlen(message.getString())>0 ) {
    	   	  #ifdef NDEBUG      
      			Serial.println("Received reboot message");
   	  			#endif    
             //wdt_enable(WDTO_30MS);
              while(1) {};

     }
     


    if ( message.sensor == RECHECK_SENSOR_VALUES && strlen(message.getString())>0 ) {
         
         if (message.getBool() == true)
         {
            boolRecheckSensorValues = true;


         }

     }

    if ( message.sensor == STOP_WATCHDOG && strlen(message.getString())>0 ) {
         
         if (message.getBool() == true)
         {
            bWatchDogStopped = true;

            if ( boolRelayStatus )
            {
                  digitalWrite(RELAY_PIN, LOW); 
                  boolRelayStatus = false;
            }
  			
         }
         else
         {

         	bWatchDogStopped = false;
         }

		reportWatchDogRunningState();
     }

    if ( message.sensor == INITIALIZE_RESET && strlen(message.getString())>0 ) {
         
         if (message.getBool() == true)
         {
            bInitReset = true;
            checkMark();


         }

     }
     
    if ( message.sensor == WATCHDOG_MESSAGE_CHILD_ID && strlen(message.getString())>0 ) {
         
    	ulLastReceivedMark = millis();

    	if (!boolRelayStatus)
    	{
    		digitalWrite(STATUS_LED_PIN, 1);
			wait(100);
			digitalWrite(STATUS_LED_PIN, 0);

    	 }  	 
    	   	    #ifdef NDEBUG      
      			Serial.println("Received keepalive message");
   	  			#endif  
     }

        return;      



}

