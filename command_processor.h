//Version 2 - change the variable pkVersion too. version control started jan 20
//changed baud rate to 19200 Jan 20
//July 10th 21 I did a quick compare with the file in c:Arduino_Projects and this one seems to be the same, it just has the routines forward referenced
// so it looks like I amde a start to convert it with VSCode
// On that basis I placed it in Projects\Live 

#include <Arduino.h>
#include <SPI.h>

#include <RF24.h>

//for nrf to work, pin 10 must be high if it is not used as an nrf connecton
constexpr uint8_t pin10 = 10;

RF24 radio(7, 8); // CE, CSN

// pin definitions for shutter relays

// These data pins link to  Relay board pins IN1, IN2, IN3 and IN4
constexpr uint8_t openShutterPin = 30;      // arduino  pin 46 corresponds with same pin number on the shutter arduino
constexpr uint8_t closeShutterPin = 34;      // these 3 pins are used to ' lay off' the open close and status commands to the shutter arduino
constexpr uint8_t shutterStatusPin = 48;      // to prevent the shutter status command being blocked and causing radio timeout

enum class MovementState : uint8_t
{
	Initial,
	Opening,
	Closing,
};

constexpr uint8_t thisAddress[]       = "shutt";   // "shutt" - the address of this arduino board/ transmitter
constexpr uint8_t masterNodeAddress[] = "mastr";

constexpr uint8_t channel    = 115;

char message[10]     = "CLOSED";
constexpr char pkVersion[]     = "2.0";
MovementState movementState = MovementState::Initial;

//========================================================================================================================================
//========================================================================================================================================


uint32_t configTimer =  millis();

inline void closeShutter()
{
  // commands to close shutters
  digitalWrite (openShutterPin , HIGH);             // activate the open shutter routine on the shutter arduino
  digitalWrite (closeShutterPin, LOW);             // 50 millisec delay, then high again


} // end  CS


inline void openShutter()
{
  digitalWrite (closeShutterPin, HIGH);
  digitalWrite (openShutterPin, LOW);               // activate the open shutter routine on the shutter arduino

}// end  OS

inline void configureRadio()
{

  radio.begin();
  radio.setChannel(channel);
  radio.setDataRate(RF24_250KBPS);           // set RF datarate

  // enable ack payload - slaves can reply with data using this feature if needed in future
  radio.enableAckPayload();

  radio.setPALevel(RF24_PA_LOW);            // this is one step up from MIN and provides approx 15 feet range - so fine in observatory
  radio.enableDynamicPayloads();
  radio.setRetries(15, 15);                 // 15 retries at 15ms intervals

  radio.openReadingPipe(1, thisAddress);    // the 1st parameter can be any number 1 to 5 the master routine uses 1
  radio.openWritingPipe(masterNodeAddress);
}

inline void testForlostRadioConfiguration()   // this tests for the radio losing its configuration - one of the known failure modes for the NRF24l01+
{

  if (millis() - configTimer > 5000)
  {
    configTimer = millis();
    if (radio.getChannel() != 115)   // first possible radio error - the configuration has been lost. This can be checked
      // by testing whether a non default setting has returned to the default - for channel the default is 76
    {
      radio.failureDetected = true;
      Serial.print("Radio configuration error detected");
      configureRadio();

    }
  }

}

constexpr char openingString[] PROGMEM = "opening";
constexpr char openString[] PROGMEM = "OPEN";
constexpr char closingString[] PROGMEM = "closing";
constexpr char closedString[] PROGMEM = "CLOSED";

inline void createStatusMessage()
{


  bool shutterStatus = (digitalRead(shutterStatusPin) != 0);   // the status pin is set in shutter arduino true = closed

if ( (movementState==MovementState::Opening ) && shutterStatus )
{
    message [0] = 'o';
    message [1] = 'p';
    message [2] = 'e';
    message [3] = 'n';
    message [4] = 'i';
    message [5] = 'n';
    message [6] = 'g';
    message [7] = 0;
}
 
if ( (movementState==MovementState::Opening ) && !shutterStatus )
  {

    message [0] = 'O';
    message [1] = 'P';
    message [2] = 'E';
    message [3] = 'N';
    message [4] = 0;
    message [5] = 0;
    message [6] = 0;
    message [7] = 0;
    digitalWrite (openShutterPin, HIGH); // the status is 'open', so set the open activation pin back to high
  }

if ( (movementState==MovementState::Closing ) && !shutterStatus )

 {
    message [0] = 'c';
    message [1] = 'l';
    message [2] = 'o';
    message [3] = 's';
    message [4] = 'i';
    message [5] = 'n';
    message [6] = 'g';
    message [7] = 0;
    
  }

if ( (movementState==MovementState::Closing ) && shutterStatus )
{
    message [0] = 'C';
    message [1] = 'L';
    message [2] = 'O';
    message [3] = 'S';
    message [4] = 'E';
    message [5] = 'D';
    message [6] = 0;
    message [7] = 0;
    digitalWrite (closeShutterPin, HIGH);   // the status is 'closed', so set the close activation pin back to high
  }


}
//
//

inline void setupProcessor()
{

  pinMode(pin10,                  OUTPUT);                     // this is an NRF24L01 requirement if pin 10 is not used
  pinMode(openShutterPin,       OUTPUT);
  pinMode(closeShutterPin,      OUTPUT);
  pinMode(shutterStatusPin,     INPUT_PULLUP);  //input on this arduino and OUTPUT on the shutter arduino

  digitalWrite(openShutterPin,  HIGH);       //open and close pins are used as active low, so initialise to high
  digitalWrite(closeShutterPin, HIGH);

  SPI.begin();

  Serial.begin(19200);                         //used only for debug writes to sermon

  configureRadio();

  radio.startListening();                   // listen for commands from the master radio which itself receives from the c# dome driver


}  // end setup


inline void updateProcessor()
{

  if (radio.available())
  {
    char text[32] = "";             // used to store what the master node sent e.g AZ , SA SS


    //error detection for radio always avaiable below
    //

    uint32_t failTimer = millis();

    while (radio.available())
    { //If available always returns true, there is a problem
      if (millis() - failTimer > 250)
      {
        radio.failureDetected = true;
        configureRadio();                         // reconfigure the radio
        radio.startListening();
        Serial.println("Radio available failure detected");
        break;
      }
      radio.read(&text, sizeof(text));

    }



    if (text[0] == 'C' && text[1] == 'S' && text[2] == '#') // close shutter command
    {
      //Serial.print ("received CS");
      movementState = MovementState::Closing;
      closeShutter();

    }


    if (text[0] == 'O' && text[1] == 'S' && text[2] == '#') // open shutter command
    {
      movementState = MovementState::Opening;
      openShutter();

    }

    if (text[0] == 'S' && text[1] == 'S' && text[2] == '#') //  shutter status command
    {

      testForlostRadioConfiguration() ;

      radio.stopListening();

      //check for timeout / send failure

      bool txSent = false;

      while (!txSent)
      {
        txSent = radio.write(&message, sizeof(message));   // true if the tx was successful
        // test for timeout after tx
        if (!txSent)
        {
          configureRadio();    // if the Tx wasn't successful, restart the radio
          radio.stopListening();
          Serial.println("tx_sent failure ");
        }
        Serial.print("radio wrote ");
        Serial.println(message);
      }

      radio.startListening();                               // straight away after write to master, in case another message is sent

  
    }   //endif SS

    text[0] = 0;   // set to null character
    text[1] = 0;
    text[2] = 0;

  } //endif radio available


  createStatusMessage();             // this sets message to OPEN or OPENING, CLOSING or CLOSED


} // end void loop