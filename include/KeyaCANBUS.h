// AIO_GUI is copyright 2025 by the AOG Group
// AiO_GUI is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
// AiO_GUI is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
// You should have received a copy of the GNU General Public License along with Foobar. If not, see <https://www.gnu.org/licenses/>.
// Like most Arduino code, portions of this are based on other open source Arduino code with a compatiable license.

#ifndef KEYACANBUS_H_
#define KEYACANBUS_H_
// KeyaCANBUS
/*
"Barrowed" Keya code from Matt Elias @ https://github.com/m-elias/AgOpenGPS_Boards/tree/575R-Keya/TeensyModules/V4.1"
*/

#define lowByte(w) ((uint8_t)((w) & 0xFF))
#define highByte(w) ((uint8_t)((w) >> 8))

// templates for commands with matching responses, only need first 4 bytes
uint8_t keyaDisableCommand[] = {0x23, 0x0C, 0x20, 0x01};
uint8_t keyaDisableResponse[] = {0x60, 0x0C, 0x20, 0x00};

uint8_t keyaEnableCommand[] = {0x23, 0x0D, 0x20, 0x01};
uint8_t keyaEnableResponse[] = {0x60, 0x0D, 0x20, 0x00};

uint8_t keyaSpeedCommand[] = {0x23, 0x00, 0x20, 0x01};
uint8_t keyaSpeedResponse[] = {0x60, 0x00, 0x20, 0x00};

uint8_t keyaCurrentQuery[] = {0x40, 0x00, 0x21, 0x01};
uint8_t keyaCurrentResponse[] = {0x60, 0x00, 0x21, 0x01};

uint8_t keyaFaultQuery[] = {0x40, 0x12, 0x21, 0x01};
uint8_t keyaFaultResponse[] = {0x60, 0x12, 0x21, 0x01};

uint8_t keyaVoltageQuery[] = {0x40, 0x0D, 0x21, 0x02};
uint8_t keyaVoltageResponse[] = {0x60, 0x0D, 0x21, 0x02};

uint8_t keyaTemperatureQuery[] = {0x40, 0x0F, 0x21, 0x01};
uint8_t keyaTemperatureResponse[] = {0x60, 0x0F, 0x21, 0x01};

uint8_t keyaVersionQuery[] = {0x40, 0x01, 0x11, 0x11};
uint8_t keyaVersionResponse[] = {0x60, 0x01, 0x11, 0x11};

uint64_t KeyaID = 0x06000001; // 0x01 is default ID

const bool debugKeya = false;
bool lnNeeded = false;
uint32_t hbTime;
uint32_t keyaTime;
elapsedMillis keyaCurrentUpdateTimer = 0;
bool keyaMotorStatus = false;

void CAN_Setup()
{
  Serial.println("\r\nBegin CAN 3 setup for Keya");
  Keya_Bus.begin();
  Keya_Bus.setBaudRate(250000); // for official Keya/jnky motor
  // Keya_Bus.setBaudRate(500000);  // for identical motor from JinanLanJiu store https://www.aliexpress.com/item/1005005364248561.html
  delay(100);
  Serial.print("Initialised Keya CANBUS @ ");
  Serial.print(Keya_Bus.getBaudRate());
  Serial.println("bps");
}

bool isPatternMatch(const CAN_message_t &message, const uint8_t *pattern, size_t patternSize)
{
  return memcmp(message.buf, pattern, patternSize) == 0;
}

void printIdAndReply(uint32_t id, uint8_t buf[8])
{
  Serial.print(id, HEX);
  Serial.print(" <> ");
  for (byte i = 0; i < 8; i++)
  {
    if (buf[i] < 16)
      Serial.print("0");
    Serial.print(buf[i], HEX);
    if (i < 7)
      Serial.print(":");
  }
  lnNeeded = true;
}

// only issue one query at a time, wait for respone
void keyaCommand(uint8_t command[])
{
  if (keyaDetected)
  {
    CAN_message_t KeyaBusSendData;
    KeyaBusSendData.id = KeyaID;
    KeyaBusSendData.flags.extended = true;
    KeyaBusSendData.len = 8;
    memcpy(KeyaBusSendData.buf, command, 4);
    Keya_Bus.write(KeyaBusSendData);
  }
}

void SteerKeya(int steerSpeed)
{
  if (keyaCurrentUpdateTimer > 99)
    keyaCommand(keyaCurrentQuery); // for motors with slow HB
  if (steerSpeed == 0)
  {
    keyaCommand(keyaDisableCommand);
    if (debugKeya)
      Serial.println("steerSpeed zero - disabling");
    return; // don't need to go any further, if we're disabling, we're disabling
  }

  if (keyaDetected)
  {
    int actualSpeed = map(steerSpeed, -255, 255, -995, 995);
    if (debugKeya)
      Serial.println("told to steer, with " + String(steerSpeed) + " so....");
    if (debugKeya)
      Serial.println("I converted that to speed " + String(actualSpeed));

    CAN_message_t KeyaBusSendData;
    KeyaBusSendData.id = KeyaID;
    KeyaBusSendData.flags.extended = true;
    KeyaBusSendData.len = 8;
    memcpy(KeyaBusSendData.buf, keyaSpeedCommand, 4);
    if (steerSpeed < 0)
    {
      KeyaBusSendData.buf[4] = highByte(actualSpeed);
      KeyaBusSendData.buf[5] = lowByte(actualSpeed);
      KeyaBusSendData.buf[6] = 0xff;
      KeyaBusSendData.buf[7] = 0xff;
      if (debugKeya)
        Serial.println("pwmDrive < zero - clockwise - steerSpeed " + String(steerSpeed));
    }
    else
    {
      KeyaBusSendData.buf[4] = highByte(actualSpeed);
      KeyaBusSendData.buf[5] = lowByte(actualSpeed);
      KeyaBusSendData.buf[6] = 0x00;
      KeyaBusSendData.buf[7] = 0x00;
      if (debugKeya)
        Serial.println("pwmDrive > zero - anticlock-clockwise - steerSpeed " + String(steerSpeed));
    }
    Keya_Bus.write(KeyaBusSendData);
    keyaCommand(keyaEnableCommand);
  }
}

void KeyaBus_Receive()
{
  static uint32_t keyaCheckTime;
  uint32_t millisNow = millis();
  if (millisNow < keyaCheckTime) return;   // only need to check for new data every ms, not 100s of times per ms
  //Serial.print((String)"\r\n" + millisNow + " KEYA check " + keyaCheckTime);
  keyaCheckTime = millisNow + 1;     // allow check every ms

  KEYAusage.timeIn();
  CAN_message_t KeyaBusReceiveData;
  if (Keya_Bus.read(KeyaBusReceiveData))
  {
    // parse the different message types

    // heartbeat 00:07:00:00:00:00:00:[ID]
    if (KeyaBusReceiveData.id == 0x07000001)
    {
      if (!keyaDetected)
      {
        Serial.println("\r\nKeya heartbeat detected! Enabling Keya canbus & using reported motor current for disengage");
        keyaDetected = true;
        keyaCommand(keyaVersionQuery);
      }
      if (debugKeya)
      {
        uint32_t time = millis();
        Serial.print(time);
        Serial.print(" ");
        Serial.print(time - hbTime);
        Serial.print(" ");
        hbTime = time;
        printIdAndReply(KeyaBusReceiveData.id, KeyaBusReceiveData.buf);
        Serial.print(" HB ");

        // calc speed
        Serial.print(KeyaBusReceiveData.buf[2]);
        Serial.print(":");
        Serial.print(KeyaBusReceiveData.buf[3]);
        Serial.print("=");
        if (KeyaBusReceiveData.buf[2] == 0xFF)
        {
          Serial.print("-");
          Serial.print(255 - KeyaBusReceiveData.buf[3]);
        }
        else
        {
          Serial.print(KeyaBusReceiveData.buf[3]);
        }
        Serial.print(" ");

        // calc current
        Serial.print(KeyaBusReceiveData.buf[4]);
        Serial.print(":");
        Serial.print(KeyaBusReceiveData.buf[5]);
        Serial.print("=");
        if (KeyaBusReceiveData.buf[4] == 0xFF)
        {
          Serial.print("-");
          Serial.print(255 - KeyaBusReceiveData.buf[5]);
        }
        else
        {
          Serial.print(KeyaBusReceiveData.buf[5]);
        }
        Serial.print(" ");

        // print error status
        Serial.print(KeyaBusReceiveData.buf[6]);
        Serial.print(":");
        Serial.print(KeyaBusReceiveData.buf[7]);
        Serial.print(" "); // Serial.print(KeyaCurrentSensorReading);
        keyaMotorStatus = !bitRead(KeyaBusReceiveData.buf[7], 0);
        Serial.print("\r\nmotor status ");
        Serial.print(keyaMotorStatus);
      }
      // check if there's any motor diag/error data and parse it
      if (KeyaBusReceiveData.buf[7] != 0)
      {

        // motor disabled bit
        if (bitRead(KeyaBusReceiveData.buf[7], 0))
        {
          if (steerConfig.SteerSwitch == 0 && keyaMotorStatus == 1)
          {
            Serial.print("\r\nMotor disabled");
            Serial.print(" - set AS off");
            steerConfig.SteerSwitch = 1; // turn off AS if motor's internal shutdown triggers
            // currentState = 1;
            prevSteerReading = 0;
          }
        }
        else if (bitRead(KeyaBusReceiveData.buf[7], 1))
        {
          Serial.println("\r\nOver voltage");
        }
        else if (bitRead(KeyaBusReceiveData.buf[7], 2))
        {
          Serial.println("\r\nHardware protection");
        }
        else if (bitRead(KeyaBusReceiveData.buf[7], 3))
        {
          Serial.println("\r\nE2PROM");
        }
        else if (bitRead(KeyaBusReceiveData.buf[7], 4))
        {
          Serial.println("\r\nUnder voltage");
        }
        else if (bitRead(KeyaBusReceiveData.buf[7], 5))
        {
          Serial.println("\r\nN/A");
        }
        else if (bitRead(KeyaBusReceiveData.buf[7], 6))
        {
          Serial.println("\r\nOver current");
        }
        else if (bitRead(KeyaBusReceiveData.buf[7], 7))
        {
          Serial.println("\r\nMode failure");
        }
      }

      if (KeyaBusReceiveData.buf[6] != 0)
      {
        if (bitRead(KeyaBusReceiveData.buf[6], 0))
        {
          Serial.print("\r\nLess phase");
        }
        else if (bitRead(KeyaBusReceiveData.buf[6], 1))
        {
          Serial.println("\r\nMotor stall");
        }
        else if (bitRead(KeyaBusReceiveData.buf[6], 2))
        {
          Serial.println("\r\nReserved");
        }
        else if (bitRead(KeyaBusReceiveData.buf[6], 3))
        {
          Serial.println("\r\nHall failure");
        }
        else if (bitRead(KeyaBusReceiveData.buf[6], 4))
        {
          Serial.println("\r\nCurrent sensing");
        }
        else if (bitRead(KeyaBusReceiveData.buf[6], 5))
        {
          Serial.println("\r\n232 disconnected");
        }
        else if (bitRead(KeyaBusReceiveData.buf[6], 6))
        {
          Serial.println("\r\nCAN disconnected");
        }
        else if (bitRead(KeyaBusReceiveData.buf[6], 7))
        {
          Serial.println("\r\nMotor stalled");
        }
      }
    }

    // parse query/command 00:05:08:00:00:00:00:[ID] responses
    if (KeyaBusReceiveData.id == 0x05800001)
    {

      // Disable command response
      if (isPatternMatch(KeyaBusReceiveData, keyaDisableResponse, sizeof(keyaDisableResponse)))
      {
        // printIdAndReply(KeyaBusReceiveData.id, KeyaBusReceiveData.buf);
        // Serial.print(" disable reply ");
      }

      // Enable command response
      else if (isPatternMatch(KeyaBusReceiveData, keyaEnableResponse, sizeof(keyaEnableResponse)))
      {
        // printIdAndReply(KeyaBusReceiveData.id, KeyaBusReceiveData.buf);
        // Serial.print(" enable reply ");
      }

      // Speed command response
      else if (isPatternMatch(KeyaBusReceiveData, keyaSpeedResponse, sizeof(keyaSpeedResponse)))
      {
        // printIdAndReply(KeyaBusReceiveData.id, KeyaBusReceiveData.buf);
        // Serial.print(" speed reply ");
      }

      // Current query response (this is also in heartbeat)
      else if (isPatternMatch(KeyaBusReceiveData, keyaCurrentResponse, sizeof(keyaCurrentResponse)))
      {
        uint32_t time = millis();
        if (debugKeya)
        {
          Serial.print(time);
          Serial.print(" ");
          Serial.print(time - keyaTime);
          Serial.print(" ");
          printIdAndReply(KeyaBusReceiveData.id, KeyaBusReceiveData.buf);
          Serial.print(" current reply ");
          Serial.print(KeyaBusReceiveData.buf[4]);

          Serial.print(" ave ");
          Serial.print(sensorReading / 2.5); // to print ave in "amps"
        }
        keyaTime = time;
        KeyaCurrentSensorReading = KeyaBusReceiveData.buf[4] * 2.5; // so that AoG's display shows "amps"
        keyaCurrentUpdateTimer -= 100;
      }

      // Fault query response
      else if (isPatternMatch(KeyaBusReceiveData, keyaFaultResponse, sizeof(keyaFaultResponse)))
      {
        printIdAndReply(KeyaBusReceiveData.id, KeyaBusReceiveData.buf);
        Serial.print(" fault reply ");
        Serial.print(KeyaBusReceiveData.buf[4]);
        Serial.print(":");
        Serial.print(KeyaBusReceiveData.buf[5]);
      }

      // Voltage query response
      else if (isPatternMatch(KeyaBusReceiveData, keyaVoltageResponse, sizeof(keyaVoltageResponse)))
      {
        printIdAndReply(KeyaBusReceiveData.id, KeyaBusReceiveData.buf);
        Serial.print(" voltage reply ");
        Serial.print(KeyaBusReceiveData.buf[4]);
      }

      // Temperature query response
      else if (isPatternMatch(KeyaBusReceiveData, keyaTemperatureResponse, sizeof(keyaTemperatureResponse)))
      {
        printIdAndReply(KeyaBusReceiveData.id, KeyaBusReceiveData.buf);
        Serial.print(" temperature reply ");
        Serial.print(KeyaBusReceiveData.buf[4]);
      }

      // Version query response
      else if (isPatternMatch(KeyaBusReceiveData, keyaVersionResponse, sizeof(keyaVersionResponse)))
      {
        printIdAndReply(KeyaBusReceiveData.id, KeyaBusReceiveData.buf);
        Serial.print(" version reply ");
        Serial.print(KeyaBusReceiveData.buf[4]);
        Serial.print(":");
        Serial.print(KeyaBusReceiveData.buf[5]);
        Serial.print(":");
        Serial.print(KeyaBusReceiveData.buf[6]);
        Serial.print(":");
        Serial.print(KeyaBusReceiveData.buf[7]);
      }
      else
      {
        printIdAndReply(KeyaBusReceiveData.id, KeyaBusReceiveData.buf);
        Serial.print(" unknown reply ");
      }
    }

    if (lnNeeded)
    {
      Serial.println();
      lnNeeded = false;
    }
  }

  KEYAusage.timeOut();
}

#endif // KEYACANBUS_H_