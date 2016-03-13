# ESPMorseCodeTransceiver
A web enabled internet of things morse code sending key with a receiving speaker using the ESP8266.

See a video about this project here:

I used a J-38 WW2 era telegraph key, and implemented a websocket interface, web server, and morse code decoder, and encoder / sounder.

It can work as a training device, or with a bit of work, send commands to home automation systems, twitter, etc.

![alt text](https://github.com/evanmj/ESPMorseCodeTransceiver/blob/master/Photos/Closed.jpg "Closed")
![alt text](https://github.com/evanmj/ESPMorseCodeTransceiver/blob/master/Photos/Open%201.jpg "Open")

Setup Instructions:

Install arduino
get esp arduino with board manager from github link
get arduinoWebSockets-master library
get/install ESP8266fs for the arduino ide.  This lets you send the web content to the eeprom.
Install wifimanager library by tzapu from library manager in arduino ide.
Be sure to restart the arduino IDE

esp will start and bring up an AP, connect to it with password "password"

go to http://esp/, or 192.168.4.1.

Configure wifi.

http://<your esp ip/

To use the key, you'll first need to set the base timing.  Once you hear the "connected" sound, hold the key down for the lenth of a 'dot'.  That will become your base timing, as well as the transmit timing.

You can use the web interface to send ascii text to the receiver, or optionally: http://<esp ip/write?val=SOS to have the ESP sound out or "receive" data.

Here is the electrical schematic you'll need to wire.  It holds the EN / CH_PD pin down when the key is closed so little or no power is used.  Open the key to power up!

![alt text](https://github.com/evanmj/ESPMorseCodeTransceiver/blob/master/Diagrams/Wiring%20Schematic.png "Schematic")

Pull requests are welcome, there are lots of "TODO" items to make eveything work better, and some are features I may or may not get around to adding.

If you plan to modify or contribute to the key input sequence, this diagram might be helpful:

![alt text](https://github.com/evanmj/ESPMorseCodeTransceiver/blob/master/Diagrams/Key%20Timing%20State%20Machine%20Flowchart.png "Schematic")
