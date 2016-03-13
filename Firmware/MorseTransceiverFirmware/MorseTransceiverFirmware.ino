#include <WiFiManager.h>


 /*
 * MorseTransceiver.ino
 *
 *  Created on: Mar01 2013
 *
 *  By: evanmj@gmail.com
 *  
 *  Based morse code/timing from: http://morsecode.scphillips.com/morse2.html
 *  
 *  TODO: Consider beeping while button down? Mayb as an option.
 *  TODO: Not all supported characters handled properly.  Need added to array.
 *  TODO: Prosigns not supported, SOS being one of them as 3 chars it no char space.
 *  TODO: full stop (period char) should newline on websocket.
 *  TODO: If any websockets connected, avoid timeout!
 *  TODO: REST output on some signal, maybe full stop, but probably something less used like "!"?
 *  TODO: Handle delete key to clear screen or backspace. "........"
 *  TODO: Different noise if too high or too low
 *  TODO: Shutting lever for a while needs to kill it even when being transmitted to.
 *  
 */

#include <Arduino.h>

#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <WebSocketsServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <Hash.h>
#include <Timer.h>


#include <FS.h>

#define USE_SERIAL Serial

// Setup automatic magic wifi selection system.
ESP8266WiFiMulti WiFiMulti;

// Instantiate server objects
ESP8266WebServer server = ESP8266WebServer(80);
WebSocketsServer webSocket = WebSocketsServer(81);

/* CONSTANTS */


//static unsigned long KEY_DEBOUNCE_TIME = 6000; // Units: microseconds. Per: http://www.qsl.net/sv8gxc/blog/100905-03.jpg, 1.2 sec per pulse = 1 WPM, 140WPM is super fast, so .006 seconds (6ms) seems legit for debounce (max 200WPM detectable).  
static unsigned long KEY_DEBOUNCE_TIME = 9000; // Units: microseconds. Make a bit softer.  A capacitor will probably help but there is not one within reach.
static float PULSE_DURATION_THRESHOLD = 0.50; // How close does a duration need to be to a previous duration to match that it is a dot or a dash? (% of time).  Higher is easier on the user, but don't go over 50% of you'll lose the ability to distingusih a dot from a dash!
static float DASH_TO_DOT_TIME_RATIO = 3.0; // This is standard, the time a dot is is one time unit, one dash is 3 time units.
static float TIME_BETWEEN_CHARS = 3.0; // This is standard, three non transmitting time constant between characters,
static float TIME_BETWEEN_WORDS = 7.0; // This is standard, the non transmitting time constant between words, typically 7 dot time constants.
static float DEFAULT_BASE_TIME_MICROS = 100000; // Default Base time, receive sounds at this rate until user keys in a new rate.  Number in microseconds per 'dot' time constant.
static unsigned long RECIEVE_SOUND_FREQ = 1000; // Frequency to play incoming data (Hz) 600-1000Hz typical.
static unsigned long NO_INPUT_TIMEOUT = 60000; // 30 seconds to shut down with no input.
static unsigned long KEY_HELD_TIMEOUT = 5000; // 5 seconds to shut down with key held down.


/* LOCAL VARIABLES */

int iState; // State machine state.
int iLastState; // Previous State machine state.
bool bFirstScanInState; // True for one time through when state is first entered.
bool bCharEndTimerElapsed; // True when CharEnd timer expires, user lets off for 3 key durations.

bool bWD_Done;  // Watchdog timer elapsed.

// Time keeping of input key pushes
volatile unsigned long iKeyStateChangedTime = 0; // Value in microseconds.
volatile unsigned long iKeyInStateForDeltaTime = 0;  // How long was the key in state for?
bool bKeyPressed; // We will need to keep track of if the button is pressed or not, and the duration.  False = not pressed, True = pressed.
unsigned long iBaseKeyDuration = DEFAULT_BASE_TIME_MICROS; // Scratch space for storing previous key lengths.  Start with a number in case we sound out a message before user inputs one to set base rate.


// Declare timer
Timer Watchdog_tmr;
int Watchdog_tmrEventID;

Timer CharEndTimer;
int CharEnd_tmrEventID;




//In progress character
String DotDashString;  //  dot-dash character array.
char DecodedChar; // ascii char decoded from user input

//In progress String, pre send to REST, etc.  Terminated (eventually) by full stop (period).
String CurrentDecodedString;
char NewestDecodedCharacter;

const char latinAlphabet[30] = { 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', '.', ',' };
// NOTE: Does not handle lower case.  Convert all inputs to upper before comparing.

const String morseAlphabet[30] = { ".-", "-...", "-.-.", "-..", ".", "..-.", "--.", "....", "..", ".---", "-.-", ".-..", "--", "-.", "---", ".--.", "--.-", ".-.", "...", "-", "..-", "...-", ".--", "-..-", "-.--", "--..", ".-.-.-", "--..--" };

String DecodedStringToTransmit;  // String in morse to send to the speaker.
String PayloadAsString;  // Uppercase String in morse to send to the speaker.

/* END LOCAL VARIABLES */


void morse_to_sound(String inputstring, unsigned freq);
void string_to_morse(String &dest, String inputstring); 

/* WEB FUNCTIONS */

// This function runs when a new websocket event arrives.
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {

    switch(type) {
        case WStype_DISCONNECTED:
            USE_SERIAL.printf("[%u] Disconnected!\n", num);
            break;
        case WStype_CONNECTED: {
            IPAddress ip = webSocket.remoteIP(num);
            USE_SERIAL.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);

            // send message to client
            webSocket.sendTXT(num, "Connected! This message is from the ESP");
        }
            break;
        case WStype_TEXT:
        
            USE_SERIAL.printf("[%u] get Text: %s\n", num, payload);
            Serial.println();

            // Javascript prepends a 't' to let us know to transmit.  Maybe S = settings, etc.  
            if(payload[0] == 't') {
                // we got command data that we need to transmit.

                PayloadAsString = (const char *) &payload[1];

                // Make uppercase so string_to_morse likes it.
                PayloadAsString.toUpperCase();

                // Trim leading character, we are done with it ("t" in this case)
                //PayloadAsString.substring(1);

                // Null this so we don't concat like string_to_morse does.
                DecodedStringToTransmit = "";
                
                // Convert it to dots and dashes.
                string_to_morse(DecodedStringToTransmit,PayloadAsString);
                
                
                Serial.println("Sounding morse payload: " + DecodedStringToTransmit);

                morse_to_sound(DecodedStringToTransmit, RECIEVE_SOUND_FREQ);

            }


            break;

         case WStype_BIN:
            USE_SERIAL.printf("[%u] get binary length: %u\n", num, length);
            
            break;
    }
    

}

// Unused, needs implemented instead of other lame 404
/*void handleNotFound(){
  digitalWrite(led, 1);
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i=0; i<server.args(); i++){
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
  digitalWrite(led, 0);
}*/

String getContentType(String filename){
  if(server.hasArg("download")) return "application/octet-stream";
  else if(filename.endsWith(".htm")) return "text/html";
  else if(filename.endsWith(".html")) return "text/html";
  else if(filename.endsWith(".css")) return "text/css";
  else if(filename.endsWith(".js")) return "application/javascript";
  else if(filename.endsWith(".png")) return "image/png";
  else if(filename.endsWith(".gif")) return "image/gif";
  else if(filename.endsWith(".jpg")) return "image/jpeg";
  else if(filename.endsWith(".ico")) return "image/x-icon";
  else if(filename.endsWith(".xml")) return "text/xml";
  else if(filename.endsWith(".pdf")) return "application/x-pdf";
  else if(filename.endsWith(".zip")) return "application/x-zip";
  else if(filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}

bool handleFileRead(String path){
  USE_SERIAL.println("handleFileRead: " + path);
  if(path.endsWith("/")) path += "index.html";
  String contentType = getContentType(path);
  String pathWithGz = path + ".gz";
  if(SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)){
    if(SPIFFS.exists(pathWithGz))
      path += ".gz";
    File file = SPIFFS.open(path, "r");
    size_t sent = server.streamFile(file, contentType);
    file.close();
    return true;
  }
  else
  {
    USE_SERIAL.println("File does not exist: " + path);
  }
  return false;
}



/* MORSE FUNCTIONS */

// Let the compiler know about our interrupt functions or it will complain about one calling before the other exists, etc.
extern void risingedge();
extern void fallingedge();


// Convert letters in a string to a string of morse code dots and dashes.
void string_to_morse(String &dest, String inputstring) 
{

  for (unsigned i = 0; i<inputstring.length(); ++i)
    {
        for (int counter = 0; counter < 30; counter++)
        {
                if (inputstring.charAt(i) == latinAlphabet[counter])
                {
                    dest += morseAlphabet[counter];
                    //break;  <= force first char, let user decide instead.
                    
                }
        }
    }

}

// Convert a character to a string of morse code dots and dashes.
void char_to_morse(String &dest, char inputstring) 
{
  
      for (int counter = 0; counter < 30; counter++)
        {
                if (inputstring == latinAlphabet[counter])
                {
                    dest = morseAlphabet[counter];
                                       
                }
        }

}

// Convert morese to an ascii character
char morse_to_char(String inputstring) 
{
    for (unsigned i = 0; i<inputstring.length(); ++i)
    {

      for (int counter = 0; counter < 30; counter++)
        {
                if (inputstring == morseAlphabet[counter])
                {
                    return latinAlphabet[counter];
                                       
                }
        }
      
    }


    
      

}

// Dwell one 'between character' length of time.
void dwell_char()
{
  delay(2*iBaseKeyDuration/1000);  // Delay one 'next character', or 'between characters' pause.  This is typically 3 durations, but morse_to_sound appends one duration already.
}

// Sound out a dot-dash string of morse code, i.e. "..---"
void morse_to_sound(String inputstring, unsigned freq)
{

  for (unsigned i = 0; i<inputstring.length(); ++i)
    {

      if (inputstring.charAt(i) == '.') 
      {
        // Sound DOT  
          analogWriteFreq(RECIEVE_SOUND_FREQ);
          analogWrite(D1, 512);
          delay(iBaseKeyDuration/1000);            // on time.  1 duration.
          analogWrite(D1, 0);
          delay(iBaseKeyDuration/1000);            // gap before next pulse. 1 duration.
      }
      else if (inputstring.charAt(i) == '-') 
      {
        // Sound DASH
          analogWriteFreq(RECIEVE_SOUND_FREQ);
          analogWrite(D1, 512);
          delay(3*iBaseKeyDuration/1000);            // on time.  3 durations.
          analogWrite(D1, 0);
          delay(iBaseKeyDuration/1000);            // gap before next pulse. 1 durations.
      }
      else if (inputstring.charAt(i) == ' ') 
      {
        // Sound SPACE (next word)          
          delay(7*iBaseKeyDuration/1000);            // gap before next pulse. 7 durations.
      }     
    }
  
}

/* INTERRUPT KEY IN FUNCTIONS */

void risingedge() {
    // Start with debounce
    if ((micros() - iKeyStateChangedTime) > KEY_DEBOUNCE_TIME)
    {
      iKeyInStateForDeltaTime = micros() - iKeyStateChangedTime;  // NOTE: BUG HERE.  When this rolls over
      iKeyStateChangedTime = micros();  // Mark for next time.
      //Serial.printf("Rising Edge, Key Pressed Going to 0, was on for %d micros", iKeyInStateForDeltaTime);
      //Serial.println();
      attachInterrupt(digitalPinToInterrupt(D7), fallingedge, FALLING);  // Now set to interrupt on falling edge.
      bKeyPressed = 0;
    }
    else
    {
      //Serial.println("Nulled some rising fuzz");
    }

}
 
void fallingedge() {

    // Start with debounce
    if ((micros() - iKeyStateChangedTime) > KEY_DEBOUNCE_TIME)
    {    
      iKeyInStateForDeltaTime = micros() - iKeyStateChangedTime;  // NOTE: BUG HERE.  When this rolls over
      iKeyStateChangedTime = micros();  // Mark for next time.
      //Serial.printf("Falling Edge, Key Pressed Going to 1, was on for %d micros", iKeyInStateForDeltaTime);
      //Serial.println();
      attachInterrupt(digitalPinToInterrupt(D7), risingedge, RISING);  // Now set to interrupt on rising edge.
      bKeyPressed = 1;
      iKeyStateChangedTime = micros();
    }
        else
    {
      //Serial.println("Nulled some falling fuzz");
    }
 
}

void WDTimerElapsed()
{
  Serial.println("WDTimerElapsed() Running.");
  bWD_Done = 1;
}

void CharEndTimerElapsed()
{
  bCharEndTimerElapsed = 1;
}



void setup() {

    // Set GPIO2 high for reset pin... This will keep things on until we are done.  must have 1k or bigger to CH_PD/EN pin.
    pinMode(D4, OUTPUT);     // Initialize pin as an output
    digitalWrite(D4,HIGH);

    pinMode(D7, INPUT);     // Initialize pin as an input, input from key
   
    Serial.begin(115200);

    Serial.setDebugOutput(true);

    Serial.println();
    Serial.println();
    Serial.println();
  
    //WiFiManager
    //Local intialization. Once its business is done, there is no need to keep it around
    WiFiManager wifiManager;

    //reset settings - for testing
    //wifiManager.resetSettings();

	
	  //tries to connect to last known settings
	  //if it does not connect it starts an access point with the specified name
	  //here  "AutoConnectAP" with password "password"
	  //and goes into a blocking loop awaiting configuration
	  if (!wifiManager.autoConnect("AutoConnectAP", "password")) {
		Serial.println("failed to connect, we should reset as see if it connects");
		delay(3000);
		ESP.reset();
		delay(5000);
	  }
  
   
    USE_SERIAL.println("Bringing up SPIFFS");
    SPIFFS.begin();


    // start webSocket server
    webSocket.begin();
    webSocket.onEvent(webSocketEvent);

    if(MDNS.begin("arenabot")) {
        USE_SERIAL.println("MDNS responder started");
    }

    // handle notfound by pulling files from eeprom if they exist.
    //called when the url is not defined here
    //use it to load content from SPIFFS
    server.onNotFound([](){
      if(!handleFileRead(server.uri()))  /// if reding from eeprom fails, send 404
        server.send(404, "text/plain", "FileNotFound");
    });

    server.begin();

    // Add service to MDNS
    MDNS.addService("http", "tcp", 80);
    MDNS.addService("ws", "tcp", 81);

      
    // Init status of pressed bit:
    if (digitalRead(D7),0)
    {
      bKeyPressed = 1;
      //Serial.println("Key was pressed at start");
      // when pin D7 goes high, call the rising function
      attachInterrupt(digitalPinToInterrupt(D7), risingedge, RISING);
    }
    else
    {
      bKeyPressed = 0;
      //Serial.println("Key was not pressed at start");
      // when pin D7 goes high, call the rising function
      attachInterrupt(digitalPinToInterrupt(D7), fallingedge, FALLING);
    } 

    // Beep so the user knows we are online and connected to wifi.
    analogWriteFreq(1400);
    analogWrite(D1, 512);
    delay(200);
    analogWrite(D1, 0);
    //delay(200);
    analogWriteFreq(1800);
    analogWrite(D1, 512);
    delay(100);
    analogWrite(D1, 0);
    //delay(200);
    analogWriteFreq(2200);
    analogWrite(D1, 512);
    delay(400);
    analogWrite(D1, 0);
    //delay(500);    

    //Debug
    /*delay(2000); 
    String MySOS = "SOS";
    String output;
    letters_to_morse(output,MySOS);
    Serial.println(output);
    morse_to_sound(output, RECIEVE_SOUND_FREQ);
    delay(2000);
    morse_to_sound(output, RECIEVE_SOUND_FREQ);
    delay(2000);
    morse_to_sound(output, RECIEVE_SOUND_FREQ);*/
    //End Debug

    // Handle printing morse code from URL Request
    server.on("/write", []() {

        // TODO: Does not handle spaces properly, repeats a character or somethjing.
      
        String val=server.arg("val");

        // Force Uppercase
        val.toUpperCase();
        
        String val_as_morse; // i.e. "..----...--" version of the string the user typed.
        String val_char_as_morse; // Just one character worth of dots and dashes at a time to morse since some contain other subchars in dot dash land... :-/
        
        string_to_morse(val_as_morse,val); // Convert to morse code string, we won't use 'val_as_morse' except to send it to the user, we need it a character at a time so we can delay properly on send.
        Serial.println("Got value from URL to sound out: " + val);
        server.send(200, "text/plain", "Sounding String " + val + ": " + val_as_morse);
        
        for (unsigned i = 0; i<val.length(); ++i)
          {
             // Send char to "sound card".
             char_to_morse(val_char_as_morse,val.charAt(i)); 
             
             morse_to_sound(val_char_as_morse, RECIEVE_SOUND_FREQ);
             
             dwell_char();// End character
          }

         // TODO: Send end of transmission code.
        
    }); // End /write url handler


} // End setup function





/* START LOOP!!! */

void loop() {

    webSocket.loop();
    server.handleClient();

    // Make sure TCP and stuff has time to run.
    yield();

    

    // Run state machine.
    switch(iState) {

      case 0:  // Initial - Wait on user to press single key to initiate base timing.
     
        // Runs one time on state entry.
        if (bFirstScanInState == 1) 
        {
          DotDashString = ""; // Null dot dash string.
          // Start watchdog timer.
          Watchdog_tmrEventID = Watchdog_tmr.after(KEY_HELD_TIMEOUT, WDTimerElapsed);  // Set Timer
      
        }
      
        // Start
        if (bKeyPressed == 0)
        {
          iState = 5;  
        }
        else
        {
          Serial.println("Key pressed on start, waiting on it to drop before starting.");
          delay(10);
        }

        // Watch for timeout
        if (bWD_Done == 1)
        {
          Serial.println("WD in State" + iState);
          iState = 999;  // Shutdown.
        }

          

      case 5:  // Wait on first key press - This sets base timing for a "dot", and is not an 'E' character!
     
        // Runs one time on state entry.
        if (bFirstScanInState == 1) 
        {
          // Start watchdog timer.
          Watchdog_tmrEventID = Watchdog_tmr.after(NO_INPUT_TIMEOUT, WDTimerElapsed);  // Set Timer      
        }
      
        // User pressed key.  Go!
        if (bKeyPressed == 1)
        {
          iState = 10;  
        }
        
        // Watch for timeout.
        if (bWD_Done == 1)
        {
          Serial.println("WD in State" + iState);
          iState = 999;  // Shutdown.
        }

        break;

      case 10:  // Wait on key to drop for timing initialization sequence.  The amount of time held here is what the user wants to be a "dot".
     
        // Runs one time on state entry.
        if (bFirstScanInState == 1) 
        {
          // Start watchdog timer.
          Watchdog_tmrEventID = Watchdog_tmr.after(KEY_HELD_TIMEOUT, WDTimerElapsed);  // Set Timer
        }

        // Key _just_ released. 
        // Key went off.  We have our timing.
        if (bKeyPressed == 0)
        {
          iState = 20;  
        }

        // Watch for timeout
        if (bWD_Done == 1)
        {
          Serial.println("WD in State" + iState);
          iState = 999;  // Shutdown.
        }

        break;

      case 20: // Key went off, we should have our base timing now for "dot".

        // Store our base timing.
        iBaseKeyDuration = iKeyInStateForDeltaTime;
        
        // Here is where we could send info to connected websockets to let them know what our WPM is.
        // Wikipedia has the milliseconds to WPM formula... TODO.

        delay(1000);  // Delay 1 second, then echo back what they set.

        // Sound buzzer letting user know how long we recieved their button push, and letting them know it is time to key in some dater.
        analogWriteFreq(RECIEVE_SOUND_FREQ);
        analogWrite(D1, 512);
        delay(iBaseKeyDuration/1000);            // on time.  1 duration.
        analogWrite(D1, 0);
          

        iState = 100;

        break;

      case 100:  // Timing decided.  Wait on first key press for first actual character from user.
     
        // Runs one time on state entry.
        if (bFirstScanInState == 1) 
        {
          // Start watchdog timer.
          Watchdog_tmrEventID = Watchdog_tmr.after(NO_INPUT_TIMEOUT, WDTimerElapsed);  // Set Timer
        }
      
        // User pressed key.  Go!
        if (bKeyPressed == 1)
        {
          iState = 110;  
        }
        
        // Watch for timeout.
        if (bWD_Done == 1)
        {
          Serial.println("WD in State" + iState);
          iState = 999;  // Shutdown.
        }

        break;


      case 110:  // Wait on key to drop for reciving dot or dash..  The amount of time held here is what determines a dot or dash.
     
        // Runs one time on state entry.
        if (bFirstScanInState == 1) 
        {
          // Start watchdog timer.
          Watchdog_tmrEventID = Watchdog_tmr.after(KEY_HELD_TIMEOUT, WDTimerElapsed);  // Set Timer
        }

        // Key _just_ released. 
        // Key went off.  We have our dot or dash timing from the user.
        if (bKeyPressed == 0)
        {
          // We have a push and release time now, it is time to determine if the user wanted a dot or a dash.

          // See what sort of pulse we are dealing with.  If we are about 3x, we have a dash after a dot.  
          if ( iKeyInStateForDeltaTime < (iBaseKeyDuration * (1 + PULSE_DURATION_THRESHOLD)) && (iKeyInStateForDeltaTime > (iBaseKeyDuration * (1 - PULSE_DURATION_THRESHOLD))) )  // Are we +/- 50% (depending on constant) from valid timing?
            {
              Serial.println("110: .");
              DotDashString += "."; // Concatenate input we just received.
     
              iState = 120;  
            }
          else if(iKeyInStateForDeltaTime < (iBaseKeyDuration * ((1 + PULSE_DURATION_THRESHOLD) * DASH_TO_DOT_TIME_RATIO)) && (iKeyInStateForDeltaTime > (iBaseKeyDuration * ((1 - PULSE_DURATION_THRESHOLD) * DASH_TO_DOT_TIME_RATIO))) ) // Are we 250%-350%?  That would be a dash.
            {
              Serial.println("110: -");
              DotDashString += "-";
             
              iState = 120;  
            }
          else{
            Serial.printf("Bad Timing: iBaseKeyDuration Was: %d and you entered: %d", iBaseKeyDuration, iKeyInStateForDeltaTime);
            Serial.println();
            iState = 900;  // Bad timing... Buzz, then Make user re-key base timing.
          }

         
        }

        // Watch for timeout
        if (bWD_Done == 1)
        {
          Serial.println("WD in State" + iState);
          iState = 999;  // Shutdown.
        }

        break;

      case 120:  // Time the dead space before the next key.  This accounts for character endings.  
                 // Timing is important, as some characters are long and contain other character's dot dash sequences in them.
     
        // Runs one time on state entry.
        if (bFirstScanInState == 1) 
        {
          // Start watchdog timer.
          Watchdog_tmrEventID = Watchdog_tmr.after(NO_INPUT_TIMEOUT, WDTimerElapsed);  // Set Timer

          // Watch for "end of character" signal.  If greater than 1 duration, we know we are ending a character (at least, maybe a word).
          CharEnd_tmrEventID = CharEndTimer.after((iBaseKeyDuration * (1 + PULSE_DURATION_THRESHOLD)) / 1000, CharEndTimerElapsed); // Set timer for 1 durations (plus threshold).
        }
      
        // User pressed key.  Go!
        if (bKeyPressed == 1)
        {
          iState = 110;  // Back to 110, we have another dot or dash as part of this character (because we didn't time out yet).
        }

        // Watch for user to delay long enough to attempt to end the dot-dash string as a character.
        if (bCharEndTimerElapsed == 1) // It has been 3 durations with no push. Character should be over.
        { 
          iState = 130;  // Head to 130 where we will decide if this is a character end or even a word end.
        }
        
        // Watch for timeout.
        if (bWD_Done == 1)
        {
          Serial.println("WD in State" + iState);
          iState = 999;  // Shutdown.
        }

        break;

      case 130:  // Mark Character, wait to see if it is a word.
     
        // Runs one time on state entry.
        if (bFirstScanInState == 1) 
        {
          // Watch for "end of word" signal.  If greater than 3 durations, we know we are ending a character (at least, maybe a word).
          CharEnd_tmrEventID = CharEndTimer.after((iBaseKeyDuration * TIME_BETWEEN_CHARS * (1 + PULSE_DURATION_THRESHOLD)) / 1000, CharEndTimerElapsed); // Set timer for 3 durations (plus threshold).
          
          Serial.println("State 130 Character found was: " + String(morse_to_char(DotDashString)));
          
          // Mark this character!
          CurrentDecodedString += String(morse_to_char(DotDashString)); // Append to current working string, we have not sent it anywhere yet.
          //TODO: Send char to websocket if enabled.

          webSocket.sendTXT(0, "m" + String(morse_to_char(DotDashString))); // The m is for the "morse" command so the webpage knows this is morse char to display.
          
          Serial.println("State 130 Detected Character End after dot dash string: " + DotDashString);
          

          // Here we can also parse the newest string to see if we need to send to REST or if we want to newline, etc.
          // That value is: String(morse_to_char(DotDashString))
                             
          DotDashString = ""; // Null string for next time.

         
          
        }
      
        // User pressed key.  Go!
        if (bKeyPressed == 1)
        {
          iState = 110;  // Back to 110, we have another dot or dash as part of this character (because we didn't time out yet).
        }

        // Watch for user to delay long enough to attempt to end the dot-dash string as a character.
        if (bCharEndTimerElapsed == 1) // It has been 3 durations with no push. Character should be over.
        { 
          iState = 140;  // Head to 140 where we will decide if this is a character end or even a word end.
        }

        break;


      case 140:  // Character timing exceeded.  If we exceed word timing, print a space.
     
        // Runs one time on state entry.
        if (bFirstScanInState == 1) 
        {
          // Watch for "end of word" signal.  If greater than 3 durations, we know we are ending a character (at least, maybe a word).
          CharEnd_tmrEventID = CharEndTimer.after((iBaseKeyDuration * TIME_BETWEEN_WORDS * (1 + PULSE_DURATION_THRESHOLD)) / 1000, CharEndTimerElapsed); // Set timer for 3 durations (plus threshold).
        }
      
        // User pressed key.  Go!
        if (bKeyPressed == 1)
        {
          iState = 110;  // Back to 110, we have another dot or dash as part of this character (because we didn't time out yet).
        }

        // Watch for user to delay long enough to attempt to end the word.
        if (bCharEndTimerElapsed == 1) // It has been 3 durations with no push. Character should be over.
        { 
          iState = 150;  // Head to 150 where we will apend the space to the word.
        }
 
        break;

      case 150:  // Mark Space to end word, then do next char or timeout if user is done for good.
     
        // Runs one time on state entry.
        if (bFirstScanInState == 1) 
        {
          
          // Mark this character!
          CurrentDecodedString += " "; // Append a space to current working string.
          
          //TODO: Send char to websocket only if connected.
          webSocket.sendTXT(0, "m ");  // The m is for the "morse" command so the webpage knows this is morse char to display.

          
          Serial.println("State 150 Detected End of word.");

          Serial.println("Word received as: " + CurrentDecodedString);
                    
          //Serial.println("CurrentDecodedString: " + CurrentDecodedString);
          DotDashString = ""; // Null string for next time.
          
          // Start watchdog timer.
          Watchdog_tmrEventID = Watchdog_tmr.after(NO_INPUT_TIMEOUT, WDTimerElapsed);  // Set Timer

        }
      
        // User pressed key.  Go!
        if (bKeyPressed == 1)
        {
          iState = 110;  // Back to 110, we have another dot or dash as part of this character (because we didn't time out yet).
        }

        // Watch for timeout.
        if (bWD_Done == 1)
        {
          Serial.println("WD in State" + iState);
          iState = 999;  // Shutdown.
        }

        break;


      case 900: // Bad Timing Detected.  user missed 1x or 3x (depending on constants) timing, start again.

        delay(iBaseKeyDuration / 1000);            // wait hold tight for a sec so they calm down.


        // Sound buzzer letting user know they did not match the specified base timing.
        analogWriteFreq(350);  // Nice and low for a shaming effect.
        analogWrite(D1, 512);
        delay(iBaseKeyDuration / 1000);            // on time.  
        analogWrite(D1, 0);
        Serial.println("Bad Timing, State 900. Resetting to State 5 to reinit base time, clearing DotDashString."); 
        DotDashString = "";
        iState = 5;

        
        break;

        
      case 999: // Timeout.  Time to go night night.

        // Is anyone connecte to the websockets?
        if (true == true) 
        {

          // Debug
          if (bKeyPressed == 0) 
          {
            Serial.println("Inactivity. Goodbye World."); 
          }
          else
          {
            Serial.println("User closed line. Goodbye World."); 
          }
          
          // Die slowly... sounding buzzer (Hz)
          for(int x = 300; x > 20; x--) {
  
            analogWriteFreq(x);
            analogWrite(D1, 512);
            delay(3);
            
          }
  
          analogWrite(D1, 0);
  

          
          digitalWrite(D4,LOW);   // Kill ESP until next user event on the slider.
          
          ESP.deepSleep(999999999*999999999U, WAKE_NO_RFCAL);                         // This is a lame work around for the wiring not working just right.  Line above should kill it but does not :-/  Sleep probably uses more power than holding EN down.
                                                                                      // May not be needed, as digital write does kill the chip to some degree.  Testing needed! TODO.
          delay(1000);           // Wait on death to come.
        }
        else
        {
          // Someone is connected, let's not die, just reset and wait on new timing inputs.
          Serial.println("Inactivity. Would reset, but we will keep it open for now. Reinitting base timing."); 
          iState = 5;
        }
        

        break;

      default:
        Serial.printf("Invalid State: %d",iState);
        Serial.println();
        break;  
       
    }

    // Clever stuff on state transition
    if (iState != iLastState)  // State Change Detected.  Null Timer
    {
      Watchdog_tmr.update();
      Watchdog_tmr.stop(Watchdog_tmrEventID);
      bWD_Done = 0;  // Reset for next time.

      CharEndTimer.update();
      CharEndTimer.stop(Watchdog_tmrEventID);
      bCharEndTimerElapsed = 0; // Reset for next time.
      
      bFirstScanInState = 1;  // Trigger first scan in state flag for a run-once on the next state.
      Serial.printf("State Transition from %d to %d ",iLastState,iState);
      Serial.println(); 
      iLastState = iState; // Update for next time.
    }
    else
    {
      bFirstScanInState = 0;  // Not the first scan anymore.
      Watchdog_tmr.update();
      CharEndTimer.update();
    }

    
    

    
}


