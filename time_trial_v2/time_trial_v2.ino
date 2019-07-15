/*
Code to get time from the NTP server every hour and check it against the on board RTC.
If RTC time is off by 2 or more seconds, synchronize RTC and Arduino time with NTP time.
Record data in the SD card every minute. 
*/
#include <TimeLib.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <UbidotsEthernet.h>
#include <SPI.h>
#include <SD.h>

const int chipSelect = BUILTIN_SDCARD;
char const * TOKEN = "BBFF-JhRLxkt9gpZxFyL5Y57xuyStrAJg12"; // Assign your Ubidots TOKEN
char const * VARIABLE_LABEL_1 = "random_ip"; // Assign the unique variable label to send the data


byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED }; 
// NTP Servers:
char timeServer[] = "time.nist.gov";
const int timeZone = -5;  // Eastern Standard Time (USA)

EthernetUDP Udp;
unsigned int localPort = 8888;  // local port to listen for UDP packets

Ubidots client(TOKEN);

void setup()  {
	
  Serial.begin(9600);
  client.setDebug(true);// uncomment this line to visualize the debug message
  client.setDeviceLabel("manu-teensy");// uncomment this line to change the defaul label
  while (!Serial);  // Wait for Arduino Serial Monitor to open
  //delay(100);
  
  //SD
  
  Serial.print("Initializing SD card..."); 
  // see if the card is present and can be initialized:
  if (!SD.begin(chipSelect)) {
    Serial.println("Card failed, or not present");
    // don't do anything more:
    return;
  }
  Serial.println("card initialized.");
  
  //Ethernet
  
  Ethernet.init(20);
  if (Ethernet.begin(mac) == 0) {
    // no point in carrying on, so do nothing forevermore:
    while (1) {
      Serial.println("Failed to configure Ethernet using DHCP");
      delay(10000);
    }
  }
  //Serial.print("IP number assigned by DHCP is ");
  //Serial.println(Ethernet.localIP());
  Udp.begin(localPort);
  Serial.println("waiting for sync");
  
  //RTC
  
  // set the Time library to use Teensy 3.0's RTC to keep time
  setSyncProvider(getTeensy3Time);

  if (timeStatus()!= timeSet) {
    Serial.println("Unable to sync with the RTC");
  } else {
    Serial.println("RTC has set the system time");
  }
}

time_t getTeensy3Time()
{
  return Teensy3Clock.get();
}

time_t startNtpWait;  //start of time period between pinging NTP server
int trailingSecond = 59; //To display the time ONCE at the start of every minute
String timestamp;
long data;
String dataString;

void loop() {
  // make a string for assembling the data to log:
  dataString = "";
  data = random(0,10);
  time_t currentTime = now(); //# of seconds elapsed since 01/01/1970
//  currentTime = currentTime - 1; //Use to test online time sync
  if (currentTime-startNtpWait >= 3600) { //StartNTPWait is 0 to begin with => Definitely enters this loop
   time_t t = getNtpTime(); 
   //if (t!=0 && now() - t > 1) {
   if (t!=0 && abs(currentTime - t) > 2) { // Synce online time if Arduino time is off by greater than 2 seconds
     Serial.print("RTC has been synced with online time\n");
     Teensy3Clock.set(t); // set the RTC
     setTime(t);         
    }
     startNtpWait = currentTime; //Restart the wait to ping the NTP server again after making one try
    //Serial.print(startNtpWait);
  }
   
  if ((second(currentTime)- trailingSecond) == -59) { 
    timestamp = String(day(currentTime)) + "/" + String(month(currentTime)) + "/" + String(year(currentTime)) + " " + String(hour(currentTime)) + ":" + String(minute(currentTime)) + ":" + String(second(currentTime)); 
    digitalClockDisplay(); //Print time as soon as seconds change from 59 to 0 ONCE without using Delay()
	  dataString = timestamp + " " + data;
	  writeToSD(dataString);

    /* Sending values to Ubidots */
    client.add(VARIABLE_LABEL_1, data);
    client.sendAll();
  }
  trailingSecond = second(currentTime);
  
}

void writeToSD(String dataString) {
  File dataFile = SD.open("datalog.txt", FILE_WRITE);

  // if the file is available, write to it:
  if (dataFile) {
    dataFile.println(dataString);
    dataFile.close();
    // print to the serial port too:
    //Serial.println(dataString);
  }  
  // if the file isn't open, pop up an error:
  else {
    Serial.println("error opening datalog.txt");
  } 
}

void digitalClockDisplay(){
  // digital clock display of the time
  Serial.print(hour());
  printDigits(minute());
  printDigits(second());
  Serial.print(" ");
  Serial.print(day());
  Serial.print(" ");
  Serial.print(month());
  Serial.print(" ");
  Serial.print(year()); 
  Serial.println(); 
}

void printDigits(int digits){
  // utility for digital clock display: prints preceding colon and leading 0
  Serial.print(":");
  if(digits < 10)
    Serial.print('0');
  Serial.print(digits);
}


/*-------- NTP code ----------*/

const int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets

time_t getNtpTime()
{
  while (Udp.parsePacket() > 0) ; // discard any previously received packets
  Serial.println("Transmit NTP Request");
  sendNTPpacket(timeServer);
  uint32_t beginWait = millis();
  while (millis() - beginWait < 1500) {
    int size = Udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      Serial.println("Receive NTP Response");
      Udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
      unsigned long secsSince1900;
      // convert four bytes starting at location 40 to a long integer
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      return secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR;
    }
  }
  Serial.println("No NTP Response :-(");
  return 0; // return 0 if unable to get the time
}

// send an NTP request to the time server at the given address
//void sendNTPpacket(IPAddress &address)
void sendNTPpacket(char * address)
{
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:                 
  Udp.beginPacket(address, 123); //NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}
  
