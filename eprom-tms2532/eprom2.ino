// EEPROM Programmer - code for an Arduino Mega 2560
//
// Written by K Adcock.
//       Jan 2016 - Initial release
//       Dec 2017 - Slide code tartups, to remove compiler errors for new Arduino IDE (1.8.5).
//   7th Dec 2017 - Updates from Dave Curran of Tynemouth Software, adding commands to enable/disable SDP.
//  10th Dec 2017 - Fixed one-byte EEPROM corruption (always byte 0) when unprotecting an EEPROM
//                  (doesn't matter if you write a ROM immediately after, but does matter if you use -unprotect in isolation)
//                - refactored code a bit (split loop() into different functions)
//                - properly looked at timings on the Atmel datasheet, and worked out that my delays
//                  during reads and writes were about 10,000 times too big!
//                  Reading and writing is now orders-of-magnitude quicker.
//  21st Feb 2018 - P. Sieg
//                  static const long int k_uTime_WriteDelay_uS = 500; // delay between byte writes - needed for at28c16
//                  delayMicroseconds(k_uTime_WritePulse_uS);
//  06th Oct 2018 - P. Sieg
//                - corrected SDP (un)protect adresses & k_uTime_WriteDelay_uS
//                - Set parameters -A=28C16; -B=28C64; -C=28C256
//  29th Jan 2019 - P. Sieg
//                - Introduced + and - to alter k_uTime_WritePulse_uS
//
//
// Distributed under an acknowledgement licence, because I'm a shallow, attention-seeking tart. :)
//
// http://danceswithferrets.org/geekblog/?page_id=903
//
// This software presents a 9600-8N1 serial port.
//
// R[hex address]                         - reads 16 bytes of data from the EEPROM
// W[hex address]:[data in two-char hex]  - writes up to 16 bytes of data to the EEPROM
// P                                      - set write-protection bit (Atmels only, AFAIK)
// U                                      - clear write-protection bit (ditto)
// V                                      - prints the version string
// A                                      - set parameters for 28C16
// B                                      - set parameters for 28C64
// C                                      - set parameters for 28C256
// +                                      - k_uTime_WritePulse_uS + 50
// -                                      - k_uTime_WritePulse_uS - 25
//
// Any data read from the EEPROM will have a CRC checksum appended to it (separated by a comma).
// If a string of data is sent with an optional checksum, then this will be checked
// before anything is written.
//

#include <avr/pgmspace.h>

const char hex[] =
{
  '0', '1', '2', '3', '4', '5', '6', '7',
  '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
};

const char version_string[] = {"EPROM Version=0.01"};
const char version_string_TMS2516[] = {"-TMS2516"};
const char version_string_TMS2532[] = {"-TMS2532"};

static const int kPin_Addr0  = 16;
static const int kPin_Addr1  = 17;
static const int kPin_Addr2  = 18;
static const int kPin_Addr3  = 19;
static const int kPin_Addr4  = 20;
static const int kPin_Addr5  = 21;
static const int kPin_Addr6  = 22;
static const int kPin_Addr7  = 23;
static const int kPin_Addr8  = 24;
static const int kPin_Addr9  = 25;
static const int kPin_Addr10 = 26;
static const int kPin_Addr11 = 27;

static const int kPin_Data0  = 8;
static const int kPin_Data1  = 9;
static const int kPin_Data2  = 10;
static const int kPin_Data3  = 11;
static const int kPin_Data4  = 12;
static const int kPin_Data5  = 13;
static const int kPin_Data6  = 14;
static const int kPin_Data7  = 15;

static const int kPin_Program = 29;
static int kPin_PGM = 27;
static int kPin_nCS = 28;


byte g_cmd[80]; // strings received from the controller will go in here
static const int kMaxBufferSize = 16;
byte buffer[kMaxBufferSize];

static const long int k_uTime_WritePulse_mS = 50; 
static const long int k_uTime_ReadPulse_uS = 1;
             long int k_uTime_WriteDelay_mS = 1; // delay between byte writes

// the setup function runs once when you press reset or power the board

typedef enum {TMS2516, TMS2532} TMS_VER;

TMS_VER tmsVer = TMS2516;

void setup()
{
  pinMode(kPin_Program, OUTPUT); digitalWrite(kPin_Program, LOW); // not programming

  Serial.begin(9600);
//  Serial.println(version_string);
  
  // address lines are ALWAYS outputs
  pinMode(kPin_Addr0,  OUTPUT);
  pinMode(kPin_Addr1,  OUTPUT);
  pinMode(kPin_Addr2,  OUTPUT);
  pinMode(kPin_Addr3,  OUTPUT);
  pinMode(kPin_Addr4,  OUTPUT);
  pinMode(kPin_Addr5,  OUTPUT);
  pinMode(kPin_Addr6,  OUTPUT);
  pinMode(kPin_Addr7,  OUTPUT);
  pinMode(kPin_Addr8,  OUTPUT);
  pinMode(kPin_Addr9,  OUTPUT);
  pinMode(kPin_Addr10, OUTPUT);
  pinMode(kPin_Addr11, OUTPUT);

  SetDataLinesAsInputs();
  SetAddress(0);

  // control lines are ALWAYS outputs
  if(tmsVer == TMS2516) {
    kPin_PGM = 27;
    kPin_nCS = 28;
    pinMode(kPin_PGM, OUTPUT); digitalWrite(kPin_PGM, LOW);
    pinMode(kPin_nCS, OUTPUT); digitalWrite(kPin_nCS, HIGH);
  }
  else
  if(tmsVer == TMS2532) {
    kPin_PGM = 28;
    kPin_nCS = 28;
    pinMode(kPin_PGM, OUTPUT); digitalWrite(kPin_PGM, HIGH);
  }
}

void loop()
{
  while (true)
  {
    ReadString();
    
    switch (g_cmd[0])
    {
      case 'V': 
        Serial.print(version_string); 
          if(tmsVer == TMS2516) Serial.println(version_string_TMS2516); 
        else
          if(tmsVer == TMS2532) Serial.println(version_string_TMS2532); 
        else
          Serial.println("");
      break;
      case 'R': ReadEEPROM(); break;
      case 'W': WriteEEPROM(); break;
      case 0: break; // empty string. Don't mind ignoring this.
      default: Serial.print("ERR Unrecognised command: ");
               Serial.println(g_cmd[0]);
      break;
    }

//    if(Serial.available()) {
//      char c = Serial.read();
//      delay(1);
//      Serial.write(c);
//    }
//    else {
//      Serial.write('H');
//      delay(1000);
//    }
  }
}

void ReadEEPROM() // R<address>  - read kMaxBufferSize bytes from EEPROM, beginning at <address> (in hex)
{
  if (g_cmd[1] == 0)
  {
    Serial.println("ERR");
    return;
  }

  // decode ASCII representation of address (in hex) into an actual value
  int addr = 0;
  int x = 1;
  while (x < 5 && g_cmd[x] != 0)
  {
    addr = addr << 4;
    addr |= HexToVal(g_cmd[x++]);
  }     

  SetDataLinesAsInputs();
  delayMicroseconds(1);
      
  ReadEEPROMIntoBuffer(addr, kMaxBufferSize);

  // now print the results, starting with the address as hex ...
  Serial.print(hex[ (addr & 0xF000) >> 12 ]);
  Serial.print(hex[ (addr & 0x0F00) >> 8  ]);
  Serial.print(hex[ (addr & 0x00F0) >> 4  ]);
  Serial.print(hex[ (addr & 0x000F)       ]);
  Serial.print(":");
  PrintBuffer(kMaxBufferSize);

  Serial.println("OK");
}

void WriteEEPROM() // W<four byte hex address>:<data in hex, two characters per byte, max of 16 bytes per line>
{
  if (g_cmd[1] == 0)
  {
    Serial.println("ERR");
    return;
  }

  int addr = 0;
  int x = 1;
  while (g_cmd[x] != ':' && g_cmd[x] != 0)
  {
    addr = addr << 4;
    addr |= HexToVal(g_cmd[x]);
    ++x;
  }

  // g_cmd[x] should now be a :
  if (g_cmd[x] != ':')
  {
    Serial.println("ERR");
    return;
  }
  
  x++; // now points to beginning of data
  uint8_t iBufferUsed = 0;
  while (g_cmd[x] && g_cmd[x+1] && iBufferUsed < kMaxBufferSize && g_cmd[x] != ',')
  {
    uint8_t c = (HexToVal(g_cmd[x]) << 4) | HexToVal(g_cmd[x+1]);
    buffer[iBufferUsed++] = c;
    x += 2;
  }

  // if we're pointing to a comma, then the optional checksum has been provided!
  if (g_cmd[x] == ',' && g_cmd[x+1] && g_cmd[x+2])
  {
    byte checksum = (HexToVal(g_cmd[x+1]) << 4) | HexToVal(g_cmd[x+2]);

    byte our_checksum = CalcBufferChecksum(iBufferUsed);

    if (our_checksum != checksum)
    {
      // checksum fail!
      iBufferUsed = -1;
      Serial.print("ERR ");
      Serial.print(checksum, HEX);
      Serial.print(" ");
      Serial.print(our_checksum, HEX);
      Serial.println("");
      return;
    }
  }

  // buffer should now contains some data
  if (iBufferUsed > 0)
  {
    WriteBufferToEEPROM(addr, iBufferUsed);
  }

  if (iBufferUsed > -1)
  {
    Serial.println("OK");
  }
}

// ----------------------------------------------------------------------------------------

void ReadEEPROMIntoBuffer(int addr, int size)
{
  SetDataLinesAsInputs();
  
  for (int x = 0; x < size; ++x)
  {
    buffer[x] = ReadByteFrom(addr + x);
  }
}

void WriteBufferToEEPROM(int addr, int size)
{
  SetDataLinesAsOutputs();

  digitalWrite(kPin_Program, HIGH);
  
  for (uint8_t x = 0; x < size; ++x)
  {
    WriteByteTo(addr + x, buffer[x]);
    delay(k_uTime_WriteDelay_mS);    
  }

  digitalWrite(kPin_Program, LOW);
}

// ----------------------------------------------------------------------------------------

// this function assumes that data lines have already been set as INPUTS
byte ReadByteFrom(int addr)
{
  SetAddress(addr);
  digitalWrite(kPin_nCS, LOW);
  delayMicroseconds(k_uTime_ReadPulse_uS);
  byte b = ReadData();
  digitalWrite(kPin_nCS, HIGH);

  return b;
}

// this function assumes that data lines have already been set as OUTPUTS, and that
// Program is set HIGH.
void WriteByteTo(int addr, byte b)
{
  SetAddress(addr);
  SetData(b);

  delayMicroseconds(2);
  
  if(tmsVer == TMS2516) {
    digitalWrite(kPin_nCS, HIGH);
    digitalWrite(kPin_PGM, HIGH); // enable write  

    delay(k_uTime_WritePulse_mS);
  
    digitalWrite(kPin_PGM, LOW); // disable write
    digitalWrite(kPin_nCS, HIGH); 
  } else
  if(tmsVer == TMS2532) {
    digitalWrite(kPin_PGM, LOW); // enable write  

    delay(k_uTime_WritePulse_mS);

    digitalWrite(kPin_PGM, HIGH); // disable write
  }
}

// ----------------------------------------------------------------------------------------

void SetDataLinesAsInputs()
{
  pinMode(kPin_Data0, INPUT);
  pinMode(kPin_Data1, INPUT);
  pinMode(kPin_Data2, INPUT);
  pinMode(kPin_Data3, INPUT);
  pinMode(kPin_Data4, INPUT);
  pinMode(kPin_Data5, INPUT);
  pinMode(kPin_Data6, INPUT);
  pinMode(kPin_Data7, INPUT);
}

void SetDataLinesAsOutputs()
{
  pinMode(kPin_Data0, OUTPUT);
  pinMode(kPin_Data1, OUTPUT);
  pinMode(kPin_Data2, OUTPUT);
  pinMode(kPin_Data3, OUTPUT);
  pinMode(kPin_Data4, OUTPUT);
  pinMode(kPin_Data5, OUTPUT);
  pinMode(kPin_Data6, OUTPUT);
  pinMode(kPin_Data7, OUTPUT);
}

void SetAddress(int a)
{
  digitalWrite(kPin_Addr0,  (a&1)?HIGH:LOW    );
  digitalWrite(kPin_Addr1,  (a&2)?HIGH:LOW    );
  digitalWrite(kPin_Addr2,  (a&4)?HIGH:LOW    );
  digitalWrite(kPin_Addr3,  (a&8)?HIGH:LOW    );
  digitalWrite(kPin_Addr4,  (a&16)?HIGH:LOW   );
  digitalWrite(kPin_Addr5,  (a&32)?HIGH:LOW   );
  digitalWrite(kPin_Addr6,  (a&64)?HIGH:LOW   );
  digitalWrite(kPin_Addr7,  (a&128)?HIGH:LOW  );
  digitalWrite(kPin_Addr8,  (a&256)?HIGH:LOW  );
  digitalWrite(kPin_Addr9,  (a&512)?HIGH:LOW  );
  digitalWrite(kPin_Addr10, (a&1024)?HIGH:LOW );
  if(tmsVer == TMS2532) {
    digitalWrite(kPin_Addr11, (a&2048)?HIGH:LOW );
  }
}

// this function assumes that data lines have already been set as OUTPUTS.
void SetData(byte b)
{
  digitalWrite(kPin_Data0, (b&1)?HIGH:LOW  );
  digitalWrite(kPin_Data1, (b&2)?HIGH:LOW  );
  digitalWrite(kPin_Data2, (b&4)?HIGH:LOW  );
  digitalWrite(kPin_Data3, (b&8)?HIGH:LOW  );
  digitalWrite(kPin_Data4, (b&16)?HIGH:LOW );
  digitalWrite(kPin_Data5, (b&32)?HIGH:LOW );
  digitalWrite(kPin_Data6, (b&64)?HIGH:LOW );
  digitalWrite(kPin_Data7, (b&128)?HIGH:LOW);
}

// this function assumes that data lines have already been set as INPUTS.
byte ReadData()
{
  byte b = 0;

  if (digitalRead(kPin_Data0) == HIGH) b |= 1;
  if (digitalRead(kPin_Data1) == HIGH) b |= 2;
  if (digitalRead(kPin_Data2) == HIGH) b |= 4;
  if (digitalRead(kPin_Data3) == HIGH) b |= 8;
  if (digitalRead(kPin_Data4) == HIGH) b |= 16;
  if (digitalRead(kPin_Data5) == HIGH) b |= 32;
  if (digitalRead(kPin_Data6) == HIGH) b |= 64;
  if (digitalRead(kPin_Data7) == HIGH) b |= 128;

  return(b);
}

// ----------------------------------------------------------------------------------------

void PrintBuffer(int size)
{
  uint8_t chk = 0;

  for (uint8_t x = 0; x < size; ++x)
  {
    Serial.print(hex[ (buffer[x] & 0xF0) >> 4 ]);
    Serial.print(hex[ (buffer[x] & 0x0F)      ]);

    chk = chk ^ buffer[x];
  }

  Serial.print(",");
  Serial.print(hex[ (chk & 0xF0) >> 4 ]);
  Serial.print(hex[ (chk & 0x0F)      ]);
  Serial.println("");
}

void ReadString()
{
  int i = 0;
  byte c;

  g_cmd[0] = 0;
  do
  {
    if (Serial.available())
    {
      c = Serial.read();
//      Serial.write(c);
      if (c > 31)
      {
        g_cmd[i++] = c;
        g_cmd[i] = 0;

        if(i>78)
          break;
      }
    }
  } 
  while (c != 0x0a);
}

uint8_t CalcBufferChecksum(uint8_t size)
{
  uint8_t chk = 0;

  for (uint8_t x = 0; x < size; ++x)
  {
    chk = chk ^  buffer[x];
  }

  return(chk);
}

// converts one character of a HEX value into its absolute value (nibble)
byte HexToVal(byte b)
{
  if (b >= '0' && b <= '9') return(b - '0');
  if (b >= 'A' && b <= 'F') return((b - 'A') + 10);
  if (b >= 'a' && b <= 'f') return((b - 'a') + 10);
  return(0);
}
