/*
*
*
*/

#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>

#include <iostream>
#include <fstream>
#include <vector>
#include <string>

using namespace std;

int ifd = -1;
char data[1024];

void cleanRx();
bool sendData(const string& d);
bool readRx(string& rxData);
bool readEprom(int a);
bool hexToInt(char a, uint8_t& val);
bool parseRx(const string& inD, int& addr, vector<uint8_t>& d, uint8_t& ckSum);
bool getVersion();
bool writeEprom(int a);

ifstream ifs;
ofstream ofs;

static const int REC_SIZE = 16;
static const int ROM_SIZE = 1024*2
;
typedef enum {CMD_NA, CMD_VER, CMD_RX, CMD_TX} CMD;

int main(int argc, char** argv) {
     
    CMD cmd = CMD_NA;

    if(argc < 2) {
      cerr << "ERROR: missing arguments" << endl;
      return -2;
    }
    
    if(string("-r") == argv[1]) {
	if(argc < 3) {
           cout << "Usage: epromClient -r <out filename>" << endl;
           return -3;
        }
        ofs.open(argv[2]);
        if(!ofs.is_open()) {
           cerr << "ERROR: cannot open output filename: " << argv[2] << endl;
           return -4;
	}
        cmd = CMD_RX;
    } else
    if(string("-w") == argv[1]) {
	if(argc < 3) {
           cout << "Usage: epromClient -w <input filename>" << endl;
           return -5;
        }
        ifs.open(argv[2]);
        if(!ifs.is_open()) {
           cerr << "ERROR: cannot open output filename: " << argv[2] << endl;
           return -6;
	}
        cmd = CMD_TX;
    } else
    if(string("-v") == argv[1]) {
        cmd = CMD_VER;
    }
    else {
        cerr << "ERROR: wrong arguments" << endl;
        return -100;
    }

    struct termios tio;

    memset(&tio,0,sizeof(tio));
    tio.c_iflag=0;
    tio.c_oflag=0;
    tio.c_cflag=CS8|CREAD|CLOCAL;           // 8n1, see termios.h for more information
    tio.c_lflag=0;
    tio.c_cc[VMIN]=1;
    tio.c_cc[VTIME]=5;

    cfsetospeed(&tio, B9600); // 115200 baud
    cfsetispeed(&tio, B9600); // 115200 baud

    ifd = open("/dev/ttyUSB0", O_RDWR | O_NONBLOCK);

    if(ifd < 0) {
       cout << "ERROR: cannot open serial port" << endl;
    }

    tcsetattr(ifd, TCSANOW,&tio);

    usleep(10000);

    cleanRx();

    switch(cmd) {
	case CMD_VER:
	    getVersion();
   	break;
	case CMD_RX:
	    for(int a=0; a < ROM_SIZE / REC_SIZE; a++) {
		readEprom(a * REC_SIZE);
	    }
	    ofs.close();
   	break;
	case CMD_TX:
	    for(int a=0; a < ROM_SIZE / REC_SIZE; a++) {
		writeEprom(a * REC_SIZE);
	    }
	    ifs.close();
   	break;
    }
  
    close(ifd);

    return 0;
}

bool getVersion() {

    sendData("V\n");
    string rxData;
    readRx(rxData);
    cout << endl <<  "VERSION: " << rxData << endl << endl;
    
     return true;
}

bool readEprom(int a) {
  char buf[20];
  sprintf(buf, "R%4.4x\n", a);
  sendData(buf);

  string rxData;
//  cout << "readEprom 1" << endl ;
  readRx(rxData);

  int addr = 0;
  vector<uint8_t> parsedData;
  uint8_t rxCkSum = 0;
  parseRx(rxData, addr, parsedData, rxCkSum);

  printf("<<< ADDR: %4.4x : ", addr);
  printf("DATA: ");

  int ckSum = 0;
  for(auto val: parsedData) {
      ckSum = ckSum ^ (val & 0xff);
      printf("%2.2x ", val);
  }
  ckSum = ckSum & 0xff;
  printf(" : CHK, received %2.2x, calculated: %2.2x\n", rxCkSum, ckSum);
  if(rxCkSum != ckSum) {
     cerr << "ERROR: Check sum missmatch" << endl;
     return false;
  }

//  cout << "readEprom 2" << endl ;

  readRx(rxData);
  if(rxData != "OK") {
     cerr << "ERROR, Got response: " << rxData << endl;
     return false;
  }

  ofs.write((char*)parsedData.data(), parsedData.size());
 
  return true;
}

bool writeEprom(int addr) {
  char buf[20];
  uint8_t data[REC_SIZE];
  sprintf(buf, "W%4.4x", addr);
  string writeStr(buf);
  writeStr.append(":");
  
  ifs.read((char*)&data[0], REC_SIZE);
  if(ifs.eof() || ifs.fail())
     return true;

  int ckSum = 0;
  for(auto val: data) {
      ckSum = ckSum ^ (val);
      sprintf(buf, "%2.2x", val);
      writeStr.append(buf);
  }
  ckSum = ckSum & 0xff;

  sprintf(buf, ",%2.2x\n", ckSum);
  writeStr.append(buf);

  printf(">>> ADDR: %4.4x : ", addr);
  printf("DATA: %s", writeStr.data());

  sendData(writeStr);

  string rxData;
  readRx(rxData);

  cout << "writeEprom: " << rxData << endl;

  if(rxData != "OK") {
     cerr << "ERROR, Got response: " << rxData << endl;
     return false;
  }
 
  return true;
}


bool sendData(const string& d) {

  int rc = write(ifd, d.data(), d.size());
//  printf("rc: %d, tx: %s\n", rc, d.data());

  return true;
}

void cleanRx() {
 
   char c;
   while(read(ifd, &c, 1) > 0)
      printf("clean rx: %2.2x\n", (unsigned int)c);
}

bool readRx(string& rxData) {
   rxData.clear();
   char c;
   do {
     while(read(ifd, &c, 1) < 0) {
       usleep(1000);
     }
//     printf("read rx: %2.2x\n", (unsigned int)c);

     if(c == 0x0d)
	continue;

     if(c == 0x0a)
	break;
 
     rxData.append(&c, 1);

   } while (1);

//    cout << "read rx data: " << rxData << endl;

   return true;
}

bool parseRx(const string& inD, int& addr, vector<uint8_t>& d, uint8_t& ckSum) {

   int idx = 0;;
   enum {NA, HEADER, DATA, CKSUM} state;

   uint8_t val, tmpVal = 0;
   auto inItr = inD.begin();
   state = HEADER;
   while(inItr != inD.end()) {
     switch(state) {
       case HEADER:
          if(*inItr == ':') {
            state = DATA;
            break;
          }
          if(*inItr != 'H') {
             val = 0;
             if(!hexToInt(*inItr, val)) 
                return false;
//             cout << "parse rx addr :" << hex << val << dec << endl;
             addr = (addr << 4) | val;
          }
       break;
       case DATA:
          if(*inItr == ',') {
            state = CKSUM;
            break;
           }
          val = 0;
          if(!hexToInt(*inItr, val)) 
             return false;
          tmpVal = (tmpVal << 4) | val;
          
//	  cout << "parse rx data :" << hex << val << ", " << tmpVal << dec << ", idx " << idx << endl;
          
	  if(++idx == 2) {
             d.push_back(tmpVal);;
             tmpVal = 0;
             idx = 0;
          }
       break;
       case CKSUM:
          val = 0;
          if(!hexToInt(*inItr, val)) 
             return false;
          tmpVal = (tmpVal << 4) | val;

          if(++idx == 2) {
             ckSum = tmpVal;
          }
       break;
     }
     ++inItr;
   }

   return true;
}

bool hexToInt(char a, uint8_t& val) {
   switch (a) {
     case '0':
     case '1':
     case '2':
     case '3':
     case '4':
     case '5':
     case '6':
     case '7':
     case '8':
     case '9':
        val = a - 0x30;
     break;
     case 'a':
     case 'b':
     case 'c':
     case 'd':
     case 'e':
     case 'f':
        val = (unsigned int)a - 'a' + 10;
     break;
     default:
       cerr << "ERROR: cannot convert hex value" << endl;
       return false;
     break;
   }

   return true;
}