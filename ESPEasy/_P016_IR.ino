//#######################################################################################################
//#################################### Plugin 016: Input IR #############################################
//#######################################################################################################

#include <IRremoteESP8266.h>
IRrecv *irReceiver;
decode_results results;

#define PLUGIN_016
#define PLUGIN_ID_016         16
#define PLUGIN_NAME_016       "Infrared input - TSOP4838"
#define PLUGIN_VALUENAME1_016 "IR"

boolean Plugin_016(byte function, struct EventStruct *event, String& string)
{
  boolean success = false;

  switch (function)
  {
    case PLUGIN_DEVICE_ADD:
      {
        Device[++deviceCount].Number = PLUGIN_ID_016;
        Device[deviceCount].Type = DEVICE_TYPE_SINGLE;
        Device[deviceCount].VType = SENSOR_TYPE_SINGLE;
        Device[deviceCount].Ports = 0;
        Device[deviceCount].PullUpOption = true;
        Device[deviceCount].InverseLogicOption = true;
        Device[deviceCount].FormulaOption = false;
        Device[deviceCount].ValueCount = 1;
        break;
      }

    case PLUGIN_GET_DEVICENAME:
      {
        string = F(PLUGIN_NAME_016);
        break;
      }

    case PLUGIN_GET_DEVICEVALUENAMES:
      {
        strcpy_P(ExtraTaskSettings.TaskDeviceValueNames[0], PSTR(PLUGIN_VALUENAME1_016));
        break;
      }

    case PLUGIN_INIT:
      {
        int irPin = Settings.TaskDevicePin1[event->TaskIndex];
        if (irReceiver == 0 && irPin != -1)
        {
          Serial.println("IR Init");
          irReceiver= new IRrecv(irPin);
          irReceiver->enableIRIn(); // Start the receiver
        }
        if (irReceiver != 0 && irPin == -1)
        {
          Serial.println("IR Removed");
          irReceiver->disableIRIn();
          delete irReceiver;
          irReceiver=0;
        }
        success = true;
        break;
      }

    case PLUGIN_TEN_PER_SECOND:
      {
        if (irReceiver->decode(&results))
        {
          unsigned long IRcode = results.value;
          irReceiver->resume();
          UserVar[event->BaseVarIndex] = IRcode;
          String log = F("IR   : Code ");
          log += String(IRcode, HEX);
          addLog(LOG_LEVEL_INFO, log);
          sendData(event);
        }
        success = true;
        break;
      }
  }
  return success;
}

