/* GSM_IR with timestamp
  
  Each PIR input is counted and the time recorded.
  */

#define DEBUG  1 // Set 1 for serial debug messages or 0 to silence
#define SMS_ON 1 // Set 1 to send SMSs or 0 to ignore sending

#include "TimerOne.h" // http://playground.arduino.cc/Code/Timer1
#include <GSM.h>
#include <avr/pgmspace.h>

#define PIR_VCC 6
#define PIR_SIG 5
#define PIR_GND 4
#define LED 8

// APN data
#define GPRS_APN       "MY_APN" // replace your GPRS APN
#define GPRS_LOGIN     "LOGIN"    // replace with your GPRS login
#define GPRS_PASSWORD  "PASSWORD" // replace with your GPRS password
#define PINNUMBER "" // if the SIM has a pin code set

GSMClient client;
GPRS gprs;
GSM gsmAccess;
GSM_SMS sms;

// api format is http://<server>/api/<function>/<uuid> with a GET request
char server[] = "example.com"; // replace with your server domain
char uuid[] = "devicePhoneNum"; // replace with a UUID e.g. the phone number of the sim on the GSM shield
int port = 80;

char remoteNum[20] = "+44myphonenumber"; // replace with your mobile number (use the international format)

//flags
#if DEBUG == 1
volatile boolean pirSignal = false;
#endif
volatile boolean lastSignal = false;
volatile boolean sendNotifications = false;
volatile boolean sendRepeat = false;
boolean sendNotify = false;

//variables
volatile unsigned long lastPir;
unsigned long lastNotify;
unsigned long lastPing;
unsigned long allowedTimeGap = 5; //minutes
const unsigned long notifyRepeatTime PROGMEM = 10; //minutes

//SMS strings in progmem
prog_char noMove[] PROGMEM = "I've seen no movement for ";
prog_char waitStr[] PROGMEM = " minutes before alert.";
prog_char waitStr2[] PROGMEM = "Last move was ";

PROGMEM const char *string_table[] = {
  noMove,
  waitStr,
  waitStr2
};

// Timer interrupt to check PIR sensor status
// and reset flags as necessary
void check_pir() {
  boolean currentSignal = digitalRead(PIR_SIG);
  digitalWrite(LED, currentSignal);
  
  if (lastSignal == false && currentSignal == true) {
#if DEBUG > 0
    pirSignal = true;
#endif
    sendRepeat = false;
    lastPir = millis();
  }
  
  lastSignal = currentSignal;
}


void setup() {
#if DEBUG > 0
  Serial.begin(9600);
  Serial.println(F("GSM_PIR"));
  Serial.println(F("======="));
#endif
  
  //Setup the pins for the PIR and LED
  pinMode(PIR_VCC, OUTPUT);
  digitalWrite(PIR_VCC, HIGH);
  pinMode(PIR_GND, OUTPUT);
  digitalWrite(PIR_GND, LOW);
  pinMode(PIR_SIG, INPUT);
  pinMode(LED, OUTPUT);
  digitalWrite(LED, LOW);
  
  // Set the regular pir_check counter
  Timer1.initialize(500000UL);
  Timer1.attachInterrupt(check_pir);

  //Setup the RTC
  pinMode(A3, OUTPUT);
  digitalWrite(A3, HIGH);
  pinMode(A2, OUTPUT);
  digitalWrite(A2, LOW);
  
  //connect to GSM
  boolean notConnected = true;
  while(notConnected)
  {
    if( (gsmAccess.begin(PINNUMBER)==GSM_READY) & 
      (gprs.attachGPRS(GPRS_APN, GPRS_LOGIN, GPRS_PASSWORD)==GPRS_READY) ) 
    {
      notConnected = false;
    } else {
#if DEBUG==1
      Serial.println(F("Not connected"));
#endif
      delay(1000);
    }  
  }
#if DEBUG==1
  Serial.println(F("GSM initialized"));
#endif
  
  // send a power-up ping
  send_GET("ping");
  
  //zero the last's from the current time
  lastPing = millis();
  lastPir = millis();
  lastNotify = millis();
  
  free_RAM();
}



void loop() {
#if DEBUG == 1
  //Report lastPir 
  if (pirSignal == true) {
    pirSignal = false;

    Serial.print(F("Last PIR: "));
    Serial.println(lastPir);
    free_RAM();
  }
#endif
  
  //Has motion been seen in the last x minutes?
  if ( sendRepeat == false 
    && millis() - lastPir > allowedTimeGap*60UL*1000UL ) 
  {
#if DEBUG == 1
    Serial.println(F("Send first notify"));
#endif
#if SMS_ON == 1
    int smsError = send_alert_SMS((millis() - lastPir) / (60UL*1000UL));
#endif
    lastNotify = millis();
    sendNotify = true;
    sendRepeat = true;
    free_RAM();
  }
  
  //Has no motion been seen, and we've reached the repeat time.
  if ( sendRepeat == true 
    && millis() - lastNotify > notifyRepeatTime*60UL*1000UL ) 
  {
#if DEBUG == 1
    Serial.println(F("Send repeat notify"));
#endif
#if SMS_ON == 1
    int smsError = send_alert_SMS((millis() - lastPir) / (60UL*1000UL));
#endif
    lastNotify = millis();
    sendNotify = true;
    free_RAM();
  }
  
  //Send the notify GET
  if (sendNotify == true) {
    int getError = send_GET("notify");
    sendNotify = false;
    free_RAM();
  }

  //Parse incoming any incoming SMS
  if (sms.available()) {
#if DEBUG == 1
    Serial.println(F("Parsing incoming SMS"));
#endif
    int smsError = parse_SMS();
    if (smsError == 1) send_response_SMS(allowedTimeGap);
    if (smsError > 0 ) send_status_SMS((millis() - lastPir) / (60UL*1000UL));
  }
  
  //Send keepalive ping every 30 minutes
  if (millis() - lastPing > (30UL*60UL*1000UL)) {
    send_GET("ping");
    free_RAM();
  } 
}


// Send alert to server if no movement seen after time limit
int send_GET(char function[7]) {
  if (client.connect(server, port)) {
#if DEBUG == 1
    Serial.println("connected");
#endif
    // Make a HTTP request:
    client.print(F("GET /api/"));
    client.print(function);
    client.print(F("/"));
    client.print(uuid);
    client.println(F(" HTTP/1.1"));
    client.print(F("Host: "));
    client.println(server);
    client.println(F("Connection: close"));
    client.println();
#if DEBUG == 1
    Serial.print(F("GET /api/"));
    Serial.print(function);
    Serial.print(F("/"));
    Serial.print(uuid);
    Serial.println(F(" HTTP/1.1"));
    Serial.print(F("Host: "));
    Serial.println(server);
    Serial.println(F("Connection: close"));
    Serial.println();
#endif
  } else { 
#if DEBUG == 1
    Serial.println(F("connection failed"));
#endif
    return 0; 
  }
  
  boolean waiting = true;
  while (waiting == true) {
    if (client.available()) {
      char c = client.read();
#if DEBUG == 1
      Serial.print(c);
#endif
    }
      
  
    // if the server's disconnected, stop the client:
    if (!client.available() && !client.connected()) {
#if DEBUG == 1
      Serial.println();
      Serial.println(F("disconnecting."));
#endif
      client.stop();
      
      waiting = false;
    }
  }
  return 1;
}


// Read incoming SMS and adjust default settings
int parse_SMS() {
  char messageBuffer[31];
  int pointer;
#if DEBUG==1
  Serial.println(F("parse_for_allowed_time"));
  free_RAM();
#endif
  while(sms.peek() && pointer < 30) {
    messageBuffer[pointer] = sms.read();
    pointer++;
  }
  sms.flush();
  messageBuffer[pointer] = '\0';
#if DEBUG ==1
  Serial.println(messageBuffer);
#endif
  //if there's a W or w at the start, assume it's a "wait for" command
  if (messageBuffer[0] == 'W' || messageBuffer[0] == 'w') {
    unsigned int value = 0;
    pointer = 0;
    int n = 0;
    while (n < 30) {
      if (messageBuffer[n] >= '0' && messageBuffer[n] <= '9') {
        pointer = n;
        n = 31;
      }
      n++;
    }
    while (messageBuffer[pointer] >='0' && messageBuffer[pointer] <='9') {
      value = value * 10 + messageBuffer[pointer] - '0';
      pointer++;
    }
    allowedTimeGap = value;
    return 1;
  } else if (messageBuffer[0] == 'S' || messageBuffer[0] == 's') {
    return 2;
  } else { 
    return 0;
  }
}


// Send SMS alert to user if no movement seen after time limit
int send_alert_SMS(unsigned long gap) {
  char txtMsg[35];
  char buffer[30];
  char buf[11];
  strcpy_P(buffer, (char*)pgm_read_word(&(string_table[0])));
  strcpy(txtMsg, buffer);
  sprintf(buf, "%lu", gap);
  strcat(txtMsg, buf);
  strcat(txtMsg, " minutes");
  
#if DEBUG==1
  Serial.print(F("SENDING: "));
  Serial.println(txtMsg);
#endif
  
  // send the message
#if SMS_ON==1
  int test = sms.beginSMS(remoteNum);
  if (test == 1) {
    sms.print(txtMsg);
    sms.endSMS(); 
  } else {
    return 0;
  }
#endif
  free_RAM();
#if DEBUG==1
  Serial.println(F("COMPLETE"));
#endif
  return 1;
}


// Send SMS response to changed settings
int send_response_SMS(unsigned long gap) {
  char txtMsg[35];
  char buffer[30];
  char buf[11];
  strcpy(txtMsg, "Waiting ");
  sprintf(buf, "%lu", gap);
  strcat(txtMsg, buf);
  strcpy_P(buffer, (char*)pgm_read_word(&(string_table[1])));
  strcat(txtMsg, buffer);

#if DEBUG==1
  Serial.print(F("SENDING: "));
  Serial.println(txtMsg);
#endif
  
  // send the message
#if SMS_ON==1
  int test = sms.beginSMS(remoteNum);
  if (test == 1) {
    sms.print(txtMsg);
    sms.endSMS(); 
  } else {
    return 0;
  }
#endif
  free_RAM();
#if DEBUG==1
  Serial.println(F("COMPLETE"));
#endif
  return 1;
}

// Send SMS response to changed settings
int send_status_SMS(unsigned long gap) {
  char txtMsg[35];
  char buffer[30];
  char buf[11];
  strcpy_P(buffer, (char*)pgm_read_word(&(string_table[2])));
  strcpy(txtMsg, buffer);
  sprintf(buf, "%lu", gap);
  strcat(txtMsg, buf);
  strcat(txtMsg, " minutes ago.");

#if DEBUG==1
  Serial.print(F("SENDING: "));
  Serial.println(txtMsg);
#endif
  
  // send the message
#if SMS_ON==1
  int test = sms.beginSMS(remoteNum);
  if (test == 1) {
    sms.print(txtMsg);
    sms.endSMS(); 
  } else {
    return 0;
  }
#endif
  free_RAM();
#if DEBUG==1
  Serial.println(F("COMPLETE"));
#endif
  return 1;
}

int free_RAM() {
#if DEBUG==1
  extern int __heap_start, *__brkval; 
  int v; 
  Serial.print(F("Free RAM: "));
  Serial.println((int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval)); 
#endif
}

