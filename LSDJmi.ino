//
// LSDJmi. Minimal support for the MIDI out mode of "Arduinoboy" version of LSDJ.
// Copyright (C) 2018, Aleh Dzenisiuk.
//
// See the actual "Arduinoboy" project at https://github.com/trash80/Arduinoboy 
// which supports other Gameboy programs and modes of operation.
//

#include <a21.hpp>
#include <avr/eeprom.h>

using namespace a21;

/** 
 * Our main code: reads messages from LSDJ and generates corresponding MIDI events.
 */
template<typename pinCLK, typename pinOUT, typename pinMIDI, typename led>
class LSDJmi {
  
private:

  /** CC messages can be treated differently depending on the settings of the current channel. */
  enum ChannelCCMode : uint8_t {
    
    /** In this mode there is a single  CC associated with a Gameboy channel. 
     * The value of the X command will be scaled from 00-6F to 00-7F range of the control's value. */
    ChannelCCModeSingle = 0,

    /** In this mode the first nibble of the X command argument selects one of 7 CCs associated with a Gameboy.
     * The second nibble is scaled from 0-F range to 00-7F range of the control's value. */
    ChannelCCModeScaled
  };

  /** Settings per Gameboy channel adjustable at run time by the user. */
  struct ChannelConfig {
    
    // MIDI channel that should be used for all MIDI messages trigerred by LSDJ commands in this Gameboy channel.
    uint8_t midiChannel;

    // How to treat CC messages for this channel.
    ChannelCCMode ccMode;

    // In the 'single' mode only the first element is used as a CC number for any Xnn command.
    // In the 'scaled' CC mode however these are MIDI CC numbers to associate with the high nibble in Xnn command.
    uint8_t ccNumbers[7];

    // Default MIDI velocity for the notes in this channel.
    uint8_t velocity;
  };
  
  // User-adjustable settings per Gameboy channel, like what MIDI channel to use, etc. 
  ChannelConfig channels[4];

  /** In case we receive Gameboy channel config changes, then we need to keep track of what's coming next; these are the possibilities. */
  enum ChannelConfigState : uint8_t {
    // Not in the channel config mode at the moment.
    ChannelConfigStateIdle = 0,
    // Just got a channel config start command, waiting for the first CC mode and MIDI channel command.
    ChannelConfigStateCCModeAndMIDIChannel,
    // Expect one or more definitions of MIDI CCs.
    ChannelConfigStateCC1,
    ChannelConfigStateCC2,
    ChannelConfigStateCC3,
    ChannelConfigStateCC4,
    ChannelConfigStateCC5,
    ChannelConfigStateCC6,
    ChannelConfigStateCC7,
    // Expecting the default note velocity.
    ChannelConfigStateVelocity
  };

  /** State info we store per channel. */
  struct ChannelState {
    
    // The most recent note we've triggered from this channel.
    uint8_t currentNote;    

    // In case the user has just started configuring the channel, then here we keep track of the commands to expect.
    ChannelConfigState configState;

    // The total number of CC numbers we expect to receive.
    uint8_t configStateTotalCCs;
  };

  // Our own per channel runtime state.
  ChannelState channelStates[4];
  
  static bool readByte(uint8_t& b) {
  
    b = 0;
  
    const int d1 = 80;
    delayMicroseconds(d1);
    pinCLK::setLow();
    delayMicroseconds(d1);
  
    const int d2 = 2;
    pinCLK::setHigh();
    delayMicroseconds(d2);
    
    if (!pinOUT::read())
      return false;
  
    for (uint8_t i = 7; i > 0; i--) {  
      pinCLK::setLow();
      delayMicroseconds(d2);
      pinCLK::setHigh();
      delayMicroseconds(d2);
      b <<= 1;
      if (pinOUT::read())
        b |= 1;
    }
    
    return true;
  }
  
  /** 
   * State of our protocol parser.
   * 
   * The protocol is nice and simple. A byte stream is coming from the Gameboy (we poll it for the next byte) 
   * where bit 7 of all the incoming bytes is set to 1, so effectively we deal with 7 bit values. 
   * 
   * The following 1 byte general messages can appear in this stream:
   * 
   * 1111 1111 — Clock tick. Not used in this project.
   * 1111 1110 - Stop. The user has stopped playback of a pattern/chain/song.
   * 1111 1101 - Start. The user has started playback of a pattern/chain/song.
   * 1111 1100 - Not used.
   * 
   * The are also 2 byte messages corresponding to special LSDJ commands (cc is Gameboy channel number 
   * the command was played in):
   * 
   * 1111 00cc 1ddd dddd - Note on/off. N or Q command in LSDJ. The data bits correspond to a MIDI note with 0 meaning "off".
   * 1111 01cc 1ddd dddd - Control Change. X command in LSDJ. The data bits define both the control and its value (depends on settings).
   * 1111 10cc 1ddd dddd - Program Change. Y command in LSDJ. The data bits define the program number.
   * 
   * That's it!
   */
  enum ReceiverState : uint8_t {
    
    /** The next byte received is expected to be a command. */
    ReceiverStateCommand,
    
    /** The current command expects a data byte. */
    ReceiverStateData
    
  } state = ReceiverStateCommand;
  
  /** Codes of LSDJ's special MIDI commands. */
  enum LSDJCommand : uint8_t {
    /** N or Q command. Note on/off. */
    LSDJCommandN = 0,
    /** X command. Control Change. */
    LSDJCommandX = 1,
    /** Y command. Program Change. */
    LSDJCommandY = 2
  } command;
  
  /** Gameboy's channel the current LSDJ command applies to. */
  enum LSDJChannel : uint8_t {
    LSDJChannelPU1 = 0,
    LSDJChannelPU2 = 1,
    LSDJChannelWAV = 2,
    LSDJChannelNOI = 3
  } channel;
  
  /** The data byte for the current command. */
  uint8_t data;
  
  /** True, if we've seen the start event. Refuse to send notes otherwise. */
  bool started = false;

  /** Our MIDI OUT pin, software serial here. */
  SerialTx<pinMIDI, 31250> midiOut;  

  void stopCurrentNote(LSDJChannel channel) {

    ChannelState& channelState = channelStates[channel];
    
    if (channelState.currentNote) {              
      
      ChannelConfig& channelConfig = channels[channel];
      midiOut.write(0x80 | channelConfig.midiChannel);
      midiOut.write(channelState.currentNote);
      midiOut.write(0x40);
      
      channelState.currentNote = 0;
    }
  }

  void resetConfigState(LSDJChannel channel) {
    ChannelState& channelState = channelStates[channel];
    if (channelState.configState != ChannelConfigStateIdle) {
      channelState.configState = ChannelConfigStateIdle;
      led::clear();
    }
  }

  void stopAllNotes() {
    
    for (uint8_t channel = LSDJChannelPU1; channel <= LSDJChannelNOI; channel++) {
      stopCurrentNote((LSDJChannel)channel);
      resetConfigState((LSDJChannel)channel);
    }
  }

public:

  // Invalid MIDI CC number to use 'no value' instead of 0 (since 0 can be the actual CC number).
  const uint8_t NoCC = 0xFF;
  
  LSDJmi()
    : channels({
      {
        .midiChannel = 0,
        .ccMode = ChannelCCModeSingle,
        .ccNumbers = { NoCC, NoCC, NoCC, NoCC, NoCC, NoCC, NoCC },
        .velocity = 0x3F
      },
      {
        .midiChannel = 0,
        .ccMode = ChannelCCModeSingle, 
        .ccNumbers = { NoCC, NoCC, NoCC, NoCC, NoCC, NoCC, NoCC },
        .velocity = 0x3F
      },
      {
        .midiChannel = 0,
        .ccMode = ChannelCCModeSingle, 
        .ccNumbers = { NoCC, NoCC, NoCC, NoCC, NoCC, NoCC, NoCC },
        .velocity = 0x3F
      },
      {
        .midiChannel = 0,
        .ccMode = ChannelCCModeSingle, 
        .ccNumbers = { NoCC, NoCC, NoCC, NoCC, NoCC, NoCC, NoCC },
        .velocity = 0x3F
      },
    }),
    channelStates({{0},{0},{0},{0}})
  {
  }

  void begin() {

    led::begin();
    
    pinCLK::setOutput();
    pinCLK::setHigh();
    
    pinOUT::setInput(false);

    midiOut.begin();
  }

  void check() {
    
    uint8_t b;
    if (!readByte(b))
      return;
      
    if (b >= 0x70) {
      
      // No matter the current state, if we see a byte that looks like a command, then we start checking it.
        
      switch (b) {
                
        case 0x7D:
        
          // The user has started playing the current pattern/chain/song.
          if (!started) {
            //~ Serial.println("Start");
            started = true;
          }
          break;
          
        case 0x7E:
        
          // The user has stopped playing the current pattern/chain/song.
          if (started) {
            //~ Serial.println("Stop");
            started = false;
            stopAllNotes();
          }
          
          break;
  
        case 0x7C:
        case 0x7F:
          // Unused.
          break;        
          
        default:
          channel = (LSDJChannel)(b & 0x3);
          command = (LSDJCommand)((b >> 2) & 0x3);
          state = ReceiverStateData;
          break;
      }
      
    } else if (state == ReceiverStateData) {
  
      data = b;
      state = ReceiverStateCommand; 

      if (!started) {
        return;
      }

      led::set();      

      ChannelConfig& channelConfig = channels[channel];
      ChannelState& channelState = channelStates[channel];

      switch (command) {
        
        case LSDJCommandN:
        
          // Note on/off.

          resetConfigState(channel);

          stopCurrentNote(channel);

          if (data != 0) {
            
            channelState.currentNote = data;
            
            midiOut.write(0x90 | channelConfig.midiChannel);
            midiOut.write(data);
            midiOut.write((uint16_t)channelConfig.velocity * 0x7F / 0x6F);
          }
          break;
        
        case LSDJCommandX:

          // Control Change (CC)

          switch (channelState.configState) {

            // Not in the channel config mode, just a normal CC.
            case ChannelConfigStateIdle:
              {
                uint8_t value;
                uint8_t ccNumber;
                
                switch (channelConfig.ccMode) {
                case ChannelCCModeSingle:
                  value = (uint16_t)data * 0x7F / 0x6F;
                  ccNumber = channelConfig.ccNumbers[0];
                  break;
                case ChannelCCModeScaled:
                  value = (uint16_t)(data & 0x0F) * 0x7F / 0xF;
                  ccNumber = channelConfig.ccNumbers[(data >> 4) & 0x0F];
                  break;
                }
      
                if (ccNumber != NoCC) {
                  midiOut.write(0xB0 | channelConfig.midiChannel);
                  midiOut.write(ccNumber);
                  midiOut.write(value);
                }          
              }
              break;

            case ChannelConfigStateCCModeAndMIDIChannel:            
              {
                uint8_t newMIDIChannel = (data & 0xF);
                if (channelConfig.midiChannel != newMIDIChannel) {
                  // Let's make sure we have everything stopped in the old MIDI channel if it's changing.
                  stopCurrentNote(channel);
                  channelConfig.midiChannel = newMIDIChannel;
                }
                
                // Number of CC configStateTotalCCs to expect.
                channelState.configStateTotalCCs = (data >> 4) & 0xF;

                // Reset the current CC map so any commands still referring them won't produce unexpected results.
                for (uint8_t i = 0; i < 7; i++) {
                  channelConfig.ccNumbers[i] = NoCC;
                }

                if (channelState.configStateTotalCCs > 0) {
                  channelConfig.ccMode = (channelState.configStateTotalCCs == 1) ? ChannelCCModeSingle : ChannelCCModeScaled;
                  channelState.configState = (ChannelConfigState)(ChannelConfigStateCC1 + channelState.configStateTotalCCs - 1);
                } else {
                  // If no CCs is expected, then jump into the next state whatever it is.
                  channelState.configState = (ChannelConfigState)(ChannelConfigStateCC7 + 1);
                }
              }
              break;
              
              case ChannelConfigStateCC1:
              case ChannelConfigStateCC2:
              case ChannelConfigStateCC3:
              case ChannelConfigStateCC4:
              case ChannelConfigStateCC5:
              case ChannelConfigStateCC6:
              case ChannelConfigStateCC7:
                {
                  uint8_t left = channelState.configState - ChannelConfigStateCC1;
                  channelConfig.ccNumbers[channelState.configStateTotalCCs - 1 - left] = data;
                  if (left > 0) {
                    channelState.configState = (ChannelConfigState)(channelState.configState - 1);
                  } else {
                    channelState.configState = ChannelConfigStateVelocity;
                  }
                }
              break;
            
            case ChannelConfigStateVelocity:
              channelConfig.velocity = data;
              resetConfigState(channel);
              break;
          }
    
          break;
        
        case LSDJCommandY:

          resetConfigState(channel);

          if (data == 0x6F) {

            // Special patch number, treating it as "enter channel configuration mode".
            channelState.configState = ChannelConfigStateCCModeAndMIDIChannel;
            led::set();
            
          } else {
            
            // Program Change
            midiOut.write(0xC0 | channelConfig.midiChannel);
            midiOut.write(data);
            break;
          }
      }
        
      led::clear();
    }
  }
};

/** 
 * The LED is not necessary in this project, see below on how to disable it. 
 * We use it only to blink occasionally to not spend much power on the LED but still to allow 
 * the user to see when the device is turned on.
 * We also toggle it when sending MIDI messages, so it is possible to see that they at least leave the device.
 */
template<typename pinLED, uint32_t onTime = 10, uint32_t offTime = 2000>
class LED {

  typedef LED<pinLED, onTime, offTime> Self;
  static inline Self& getSelf() { static Self s; return s; }

  uint32_t nextBlinkTime;
  bool blinkState;
  uint8_t toggleState;

  static void update() {
    Self& self = getSelf();
    pinLED::write((self.toggleState > 0) ^ self.blinkState);
  }  

public:

  static void begin() {
    
    pinLED::setOutput();
    pinLED::setLow();

    Self& self = getSelf();
    self.nextBlinkTime = millis();
    self.blinkState = false;
    self.toggleState = 0;
    check();
  }
  
  static void check() {
    Self& self = getSelf();
    if (millis() >= self.nextBlinkTime) {
      self.blinkState = !self.blinkState;
      self.nextBlinkTime = millis() + (self.blinkState ? onTime : offTime);
      update();
    }
  }

  static void set() {
    getSelf().toggleState++;
    update();
  }
  
  static void clear() {
    getSelf().toggleState--;
    update();
  }
};

/**
 * When a dummy pin is used for a LED, then we don't need to do anything at all. 
 * I.e. passing UnusedPin<> should eliminate any LED related code.
 */
template<>
class LED< UnusedPin<> > {
public:
  static void begin() {}
  static void check() {}
  static void set() {}
  static void clear() {}
};

//
//
//

// Change the pin to match your board.
typedef LED< FastPin<2> > led;

//! If you don't want to use a LED, then use this definition instead (will save ~200 bytes):
//! typedef LED< UnusedPin<> > led;

// Our receiver of LSDJ messages. The pins are Gameboy's CLK, Gameboy's OUT (our input) and a MIDI out.
//
// Gameboy's Link Port pin-out (looking at the Gameboy):
//             ___
//     ______/     \______
//   /                     \
//  |  5 CLK   3 IN  1 +5V  |
//  |  6 GND   4 ?   2 OUT  |
//  |_______________________|
// 
// One half of the link cable that I use (the part with a small plug) is missing both Vcc and GND wires. To make it work: 
//  - IN is tied to the GND of our board;
//  - OUT is pulled up to the board's Vcc via a 1K resistor.
//
LSDJmi< FastPin<4>, FastPin<3>, FastPin<1>, led > mi;

bool readCalibrationByte(uint8_t& b) {
  b = eeprom_read_byte((uint8_t*)0);
  return b == (uint8_t)(~eeprom_read_byte((uint8_t*)1));
}

void setup() {

  // If you use your microcontroler without a crystal, like I do with an ATtiny85, then you'll need to use your calibration byte.
  // Here I assume it is stored in EEPROM at address 0 by a calibration f/w, but this can be different in your setup.
  uint8_t calibrationByte;
  if (readCalibrationByte(calibrationByte)) {
    OSCCAL = calibrationByte;
  } else {
    // Alternatively you can manually find a suitable value and hardcode it.
    OSCCAL = 0xEF;
  }
    
  mi.begin();
}

void loop() {
  led::check();
  mi.check();
  delayMicroseconds(10);
}

