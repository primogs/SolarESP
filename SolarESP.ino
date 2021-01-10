/////////////////////////////////////////////////////////////////////////////////////////
//    This file is part of SolarESP.
//
//    Copyright (C) 2021 Matthias Hund
//    
//    This program is free software; you can redistribute it and/or
//    modify it under the terms of the GNU General Public License
//    as published by the Free Software Foundation; either version 2
//    of the License, or (at your option) any later version.
//    
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//    
//    You should have received a copy of the GNU General Public License
//    along with this program; if not, write to the Free Software
//    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
/////////////////////////////////////////////////////////////////////////////////////////
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

const uint8_t gcCrcByteSize     = 2u;
const uint8_t gcPageByteSize    = 2u;
const uint16_t gcEepromPageSize = 64u;
const uint16_t gcEepromPages    = 512u;

// WiFi credentials.
const char* WIFI_SSID = "SSID";
const char* WIFI_PASS = "secret";

enum err {NONE=0,NO_WIFI,NO_UDP_SOCKET,NO_UDP_BEGIN_PCK,NO_UDP_SEND,INVALID_DATA};
err gError=NONE;

WiFiUDP Udp;

bool WlanConnect() 
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long wifiConnectStart = millis();

  while (WiFi.status() != WL_CONNECTED) 
  {
    if (millis() - wifiConnectStart > 10000) 
    {
      return false;
    }
    delay(10);
  }
  return true;
}


void WlanDisconnect() 
{
  WiFi.disconnect();
}

bool SendToServer(uint8_t *pageData,uint16_t bufferSize)
{
  bool res = true; 
  unsigned int port = 7647;

  if(Udp.begin(port)==0)
  {
    gError = NO_UDP_SOCKET;
    res=false;
  }
    
  if(Udp.beginPacket("URL", port)==0)
  {
    gError = NO_UDP_BEGIN_PCK;
    res=false;
  }
  
  Udp.write(pageData,bufferSize);
  if(Udp.endPacket()==0)
  {
    gError = NO_UDP_SEND;
    res=false;
  }
  
  delay(200);
  char packet[3];
  int packetSize = Udp.parsePacket();
  if(packetSize) 
  {
    Udp.read(packet, 3);
    if(strncmp(packet,"ACK",sizeof(packet))!=0)
      res=false;
  }
  Udp.stop();

  return res;
}

void SerialFlushInput()
{
  while(Serial.available())
  {
    Serial.read();
  }
}

uint16_t GetPagesWritten()
{
  uint16_t n[]={0xFFFF,0xFFF0};
  int i = 0;
  while(n[0] != n[1])
  {
    n[(i%2)] = GetPagesWrittenOnce();
    i++;
    if(i>5)
    {
      return 0;
    }
    delay(10);
  }
  return n[0];
}

uint16_t GetPagesWrittenOnce()
{
  uint16_t      nRead=0;
  char          qtyStr[4];
  unsigned long timeout = millis()+100u;
  SerialFlushInput();
  Serial.println("QTY"); // request page quantity
  while(nRead<3 and millis()<timeout)
  {
    if(Serial.available())
    {
      int character = Serial.read();
      if(character != -1)
      {
        qtyStr[nRead] = (uint8_t) character;
        nRead++;
      }
    }
  }
  qtyStr[nRead] ='\0';
  int pagesQty = atoi(qtyStr);
  if(pagesQty>0 and pagesQty<=gcEepromPages)
  {
    return  (uint16_t)pagesQty;
  }
  return 0;
}



uint16_t GetBlock(const uint16_t pageNum, uint8_t *pageData,const uint16_t bufferSize)
{
  SerialFlushInput();
  Serial.print("GET");
  Serial.println(pageNum);
  uint16_t nRead=0;
  while(nRead<bufferSize)
  {
    if(Serial.available())
    {
      int character = Serial.read();
      if(character != -1)
      {
        pageData[nRead] = (uint8_t) character;
        nRead++;
      }
    }
  }
  return nRead;
}

void Upload(const uint16_t pagesQty)
{
  if(!WlanConnect())
  {
    gError = NO_WIFI;
  }
  else
  {
    uint8_t pageData[gcEepromPageSize+gcPageByteSize+gcCrcByteSize];  // transmit pages
    memset(pageData,0,sizeof(pageData));
    uint8_t retries   = 0;
    uint16_t pageNum  = pagesQty;
    while(pageNum != 0)
    {
      pageNum--;
      
      GetBlock(pageNum,pageData,sizeof(pageData));
 
      // server upload  
      if(SendToServer(pageData,sizeof(pageData))==true)
      {
        retries=0;
      }
      else if(retries<2u)
      {
        pageNum++;
        retries++;
        delay(10);
      }
      else
      {
        gError = INVALID_DATA;
        retries=0; 
      }
    }
    WlanDisconnect();
  }
}

void setup() 
{ 
  Serial.pins(1,3);
  Serial.begin(19200);
  delay(10);
  
  Serial.println("");
  uint16_t pagesQty = GetPagesWritten();
  if(pagesQty>0u)
  {
    Upload(pagesQty);
  }
  
  if(gError!=NONE)
  {
    Serial.print("ERR");
    Serial.println((uint8_t)gError);
  }
  else
  {
    Serial.println("END");
  }
}

void loop() 
{
  ESP.deepSleepMax();
}
