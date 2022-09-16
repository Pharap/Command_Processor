//Version 2 - change the variable pkversion too. version control started jan 20
//changed baud rate to 19200 Jan 20


#include <SPI.h>

#include <RF24.h>

//for nrf to work, pin 10 must be high if it is not used as an nrf connecton
constexpr uint8_t pin10 = 10;

RF24 radio(7, 8); // CE, CSN

// pin definitions for shutter relays

// These data pins link to  Relay board pins IN1, IN2, IN3 and IN4
constexpr uint8_t open_shutter_pin = 30;      // arduino  pin 46 corresponds with same pin number on the shutter arduino
constexpr uint8_t close_shutter_pin = 34;      // these 3 pins are used to ' lay off' the open close and status commands to the shutter arduino
constexpr uint8_t shutter_status_pin = 48;      // to prevent the shutter status command being blocked and causing radio timeout



const byte thisaddress[6]       = "shutt";   // "shutt" - the address of this arduino board/ transmitter
const byte masterNodeaddress[6] = "mastr";

const int channel    = 115;

char message[10]     = "CLOSED";
String receivedData;
String pkversion     = "2.0";
String MovementState = "";
bool shutterstatus   = true;
bool Tx_sent         = false;

void setup()
{

  pinMode(pin10,                  OUTPUT);                     // this is an NRF24L01 requirement if pin 10 is not used
  pinMode(open_shutter_pin,       OUTPUT);
  pinMode(close_shutter_pin,      OUTPUT);
  pinMode(shutter_status_pin,     INPUT_PULLUP);  //input on this arduino and OUTPUT on the shutter arduino

  digitalWrite(open_shutter_pin,  HIGH);       //open and close pins are used as active low, so initialise to high
  digitalWrite(close_shutter_pin, HIGH);

  SPI.begin();

  Serial.begin(19200);                         //used only for debug writes to sermon

  ConfigureRadio();

  radio.startListening();                   // listen for commands from the master radio which itself receives from the c# dome driver


}  // end setup

//========================================================================================================================================
//========================================================================================================================================


uint32_t configTimer =  millis();


void loop()
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
        ConfigureRadio();                         // reconfigure the radio
        radio.startListening();
        Serial.println("Radio available failure detected");
        break;
      }
      radio.read(&text, sizeof(text));

    }



    if (text[0] == 'C' && text[1] == 'S' && text[2] == '#') // close shutter command
    {
      //Serial.print ("received CS");
      MovementState = "CLOSING";
      close_shutter();

    }


    if (text[0] == 'O' && text[1] == 'S' && text[2] == '#') // open shutter command
    {
      MovementState = "OPENING";
      open_shutter();

    }

    if (text[0] == 'S' && text[1] == 'S' && text[2] == '#') //  shutter status command
    {

      TestforlostRadioConfiguration() ;

      radio.stopListening();

      //check for timeout / send failure

      Tx_sent = false;

      while (Tx_sent == false)
      {
        Tx_sent = radio.write(&message, sizeof(message));   // true if the tx was successful
        // test for timeout after tx
        if (Tx_sent == false)
        {
          ConfigureRadio();    // if the Tx wasn't successful, restart the radio
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


  CreateStatusMessage();             // this sets message to OPEN or OPENING, CLOSING or CLOSED


} // end void loop

void close_shutter()
{
  // commands to close shutters
  digitalWrite (open_shutter_pin , HIGH);             // activate the open shutter routine on the shutter arduino
  digitalWrite (close_shutter_pin, LOW);             // 50 millisec delay, then high again


} // end  CS


void open_shutter()
{
  digitalWrite (close_shutter_pin, HIGH);
  digitalWrite (open_shutter_pin, LOW);               // activate the open shutter routine on the shutter arduino

}// end  OS

void ConfigureRadio()
{

  radio.begin();
  radio.setChannel(channel);
  radio.setDataRate(RF24_250KBPS);           // set RF datarate

  // enable ack payload - slaves can reply with data using this feature if needed in future
  radio.enableAckPayload();

  radio.setPALevel(RF24_PA_LOW);            // this is one step up from MIN and provides approx 15 feet range - so fine in observatory
  radio.enableDynamicPayloads();
  radio.setRetries(15, 15);                 // 15 retries at 15ms intervals

  radio.openReadingPipe(1, thisaddress);    // the 1st parameter can be any number 1 to 5 the master routine uses 1
  radio.openWritingPipe(masterNodeaddress);
}

void TestforlostRadioConfiguration()   // this tests for the radio losing its configuration - one of the known failure modes for the NRF24l01+
{

  if (millis() - configTimer > 5000)
  {
    configTimer = millis();
    if (radio.getChannel() != 115)   // first possible radio error - the configuration has been lost. This can be checked
      // by testing whether a non default setting has returned to the default - for channel the default is 76
    {
      radio.failureDetected = true;
      Serial.print("Radio configuration error detected");
      ConfigureRadio();

    }
  }

}


void CreateStatusMessage()
{


  shutterstatus = digitalRead(shutter_status_pin);   // the status pin is set in shutter arduino true = closed

if ( (MovementState=="OPENING" ) && (shutterstatus==true) )
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
 
if ( (MovementState=="OPENING" ) && (shutterstatus==false) )
  {

    message [0] = 'O';
    message [1] = 'P';
    message [2] = 'E';
    message [3] = 'N';
    message [4] = 0;
    message [5] = 0;
    message [6] = 0;
    message [7] = 0;
    digitalWrite (open_shutter_pin, HIGH); // the status is 'open', so set the open activation pin back to high
  }

if ( (MovementState=="CLOSING" ) && (shutterstatus==false) )

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

if ( (MovementState=="CLOSING" ) && (shutterstatus==true) )
{
    message [0] = 'C';
    message [1] = 'L';
    message [2] = 'O';
    message [3] = 'S';
    message [4] = 'E';
    message [5] = 'D';
    message [6] = 0;
    message [7] = 0;
    digitalWrite (close_shutter_pin, HIGH);   // the status is 'closed', so set the close activation pin back to high
  }


}
//
//
