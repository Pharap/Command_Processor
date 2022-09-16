//Version 2 - change the variable pkVersion too. version control started jan 20
//changed baud rate to 19200 Jan 20
//July 10th 21 I did a quick compare with the file in c:Arduino_Projects and this one seems to be the same, it just has the routines forward referenced
// so it looks like I amde a start to convert it with VSCode
// On that basis I placed it in Projects\Live 

#include <Arduino.h>
#include <SPI.h>

#include <RF24.h>

constexpr char pkVersion[] = "2.0";

// For nrf to work, pin 10 must be high if it is not used as an nrf connecton
constexpr uint8_t pin10 = 10;

// CE, CSN
RF24 radio(7, 8);

// Pin definitions for shutter relays
// These data pins link to  Relay board pins IN1, IN2, IN3 and IN4
// Arduino pin 46 corresponds with same pin number on the shutter arduino.
// These 3 pins are used to ' lay off' the open close and status commands to the shutter Arduino,
// to prevent the shutter status command being blocked and causing radio timeout.
constexpr uint8_t openShutterPin = 30;
constexpr uint8_t closeShutterPin = 34;
constexpr uint8_t shutterStatusPin = 48;

// The address of this Arduino board/transmitter
constexpr uint8_t thisAddress[] = "shutt";

// The address of the master radio?
constexpr uint8_t masterNodeAddress[] = "mastr";

constexpr uint8_t channel = 115;

enum class MovementState : uint8_t
{
	Initial,
	Opening,
	Closing,
};

MovementState movementState = MovementState::Initial;

char message[10] = "CLOSED";

void closeShutter()
{
  // Activate the open shutter routine on the shutter arduino
  digitalWrite(openShutterPin , HIGH);

  // 50 millisecond delay, then high again
  digitalWrite(closeShutterPin, LOW);
}

void openShutter()
{
  digitalWrite (closeShutterPin, HIGH);

  // Activate the open shutter routine on the shutter arduino
  digitalWrite (openShutterPin, LOW);
}

void configureRadio()
{
  radio.begin();

  radio.setChannel(channel);

  // Set RF datarate
  radio.setDataRate(RF24_250KBPS);

  // Enable ack payload - slaves can reply with data using this feature if needed in future
  radio.enableAckPayload();

  // This is one step up from MIN and provides approx 15 feet range - so fine in observatory
  radio.setPALevel(RF24_PA_LOW);

  radio.enableDynamicPayloads();

  // 15 retries at 15ms intervals
  radio.setRetries(15, 15);

  // The 1st parameter can be any number from 1 to 5, the master routine uses 1
  radio.openReadingPipe(1, thisAddress);
  radio.openWritingPipe(masterNodeAddress);
}

// The last time the radio configuration was tested.
uint32_t configTimer =  millis();

// This tests for the radio losing its configuration - one of the known failure modes for the NRF24l01+
void testForlostRadioConfiguration()
{
  // Cache the current time
  const uint32_t now = millis();

  // Calculate the time elapsed since the last check
  const uint32_t elapsed = (now - configTimer);

  // If less than 5000ms has elapsed
  if (elapsed < 5000)
    // Do nothing - exit early.
    return;

  // Update the config timer
  configTimer = millis();

  // First possible radio error - the configuration has been lost.
  // This can be checked by testing whether a non default setting has returned to the default.
  // For channel the default is 76.
  if (radio.getChannel() != 115)
  {
    radio.failureDetected = true;
    Serial.print(F("Radio configuration error detected"));
    configureRadio();
  }
}

constexpr char openingString[] PROGMEM = "opening";
constexpr char openString[] PROGMEM = "OPEN";
constexpr char closingString[] PROGMEM = "closing";
constexpr char closedString[] PROGMEM = "CLOSED";

template<size_t size> void writeMessageStringP(const char (&messageString)[size])
{
  static_assert(sizeof(message) > size, "Provided message is larger than the message buffer");
  strncpy_P(&message[0], &messageString[0], size);
}

void createStatusMessage()
{
  // The status pin is set in shutter arduino true = closed
  bool shutterStatus = (digitalRead(shutterStatusPin) != 0);

  switch(movementState)
  {
    case MovementState::Initial:
      break;
    
    case MovementState::Opening:
    {
      if(shutterStatus)
      {
        writeMessageStringP(openingString);
      }
      else
      {
        writeMessageStringP(openString);

        // The status is 'open', so set the open activation pin back to high
        digitalWrite (openShutterPin, HIGH);
      }
    }
    break;
    
    case MovementState::Closing:
    {
      if(shutterStatus)
      {
        writeMessageStringP(closingString);
      }
      else
      {
        writeMessageStringP(closedString);

        // The status is 'closed', so set the close activation pin back to high
        digitalWrite (closeShutterPin, HIGH);
      }
    }
    break;
  }
}

void setup()
{
  // this is an NRF24L01 requirement if pin 10 is not used
  pinMode(pin10,                  OUTPUT);
  pinMode(openShutterPin,       OUTPUT);
  pinMode(closeShutterPin,      OUTPUT);
  // Input on this Arduino and OUTPUT on the shutter Arduino
  pinMode(shutterStatusPin,     INPUT_PULLUP);

  // Ppen and close pins are used as active low, so initialise to high
  digitalWrite(openShutterPin,  HIGH);
  digitalWrite(closeShutterPin, HIGH);

  SPI.begin();

  // Used only for debug writes to sermon
  Serial.begin(19200);

  configureRadio();

  // Listen for commands from the master radio, which itself receives commands from the C# dome driver
  radio.startListening();
}

constexpr char closeShutterCommand[] PROGMEM = "CS#";
constexpr char openShutterCommand[] PROGMEM = "OS#";
constexpr char shutterStatusCommand[] PROGMEM = "SS#";

void loop()
{
  if (radio.available())
  {
    // Used to store what the master node sent e.g AZ , SA SS
    char text[32] {};

    // Error detection for radio always avaiable below

    uint32_t failTimer = millis();

    while (radio.available())
    {
      const uint32_t now = millis();
      const uint32_t elapsed = (now - failTimer);

      // If available always returns true, there is a problem
      if (elapsed > 250)
      {
        radio.failureDetected = true;
        // Reconfigure the radio
        configureRadio();
        radio.startListening();
        Serial.println(F("Radio available failure detected"));
        break;
      }

      radio.read(&text, sizeof(text));
    }

    // Close shutter command
    if (strcmp_P(text, closeShutterCommand) == 0)
    {
      movementState = MovementState::Closing;
      closeShutter();
    }

    // Open shutter command
    if (strcmp_P(text, openShutterCommand) == 0)
    {
      movementState = MovementState::Opening;
      openShutter();
    }

    // Shutter status command
    if (strcmp_P(text, shutterStatusCommand) == 0)
    {
      testForlostRadioConfiguration();

      radio.stopListening();

      // Check for timeout / send failure

      bool txSent = false;

      while (!txSent)
      {
        // true if the tx was successful
        txSent = radio.write(&message, sizeof(message));

        // test for timeout after tx
        if (!txSent)
        {
          // If the Tx wasn't successful, restart the radio
          configureRadio();
          radio.stopListening();
          Serial.println(F("tx_sent failure "));
        }
        Serial.print(F("radio wrote "));
        Serial.println(message);
      }
      
      // Straight away after write to master, in case another message is sent
      radio.startListening();
    }
  }

  // This sets message to OPEN or OPENING, CLOSING or CLOSED
  createStatusMessage();
}