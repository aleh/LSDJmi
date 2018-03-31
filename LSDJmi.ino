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
    
    /** In this mode there is only one control defined for this channel. 
     * The full value from the correspponding LSDJ command will be used with this control.*/
    ChannelCCModeSingle = 0,
    ChannelCCModeScaled
  };

  /** Runtime settings per Gameboy channel. */
  struct ChannelConfig {
    
    // MIDI channel used for all commands in this Gameboy channel.
    uint8_t midiChannel;

    // How to treat CC messages for this channel.
    ChannelCCMode ccMode;

    // If ccMode is 'single', then this is the single CC number to use for all the CC commands in this channel.
    uint8_t ccNumber;

    // The most recent note we've triggered from this channel.
    uint8_t currentNote;    
  };

  // Info for all our channels.
  ChannelConfig channels[4];

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

    ChannelConfig& channelConfig = channels[channel];
    
    if (channelConfig.currentNote) {              
      
      midiOut.write(0x80 | channelConfig.midiChannel);
      midiOut.write(channelConfig.currentNote);
      midiOut.write(0x40);
      
      channelConfig.currentNote = 0;
    }
  }

  void stopAllNotes() {
    for (uint8_t channel = LSDJChannelPU1; channel <= LSDJChannelNOI; channel++) {
      stopCurrentNote((LSDJChannel)channel);
    }
  }

public:

  LSDJmi() {
    for (uint8_t channel = LSDJChannelPU1; channel <= LSDJChannelNOI; channel++) {
      ChannelConfig& channelConfig = channels[channel];
      channelConfig.midiChannel = 0;
      channelConfig.ccMode = ChannelCCModeSingle;
      channelConfig.ccNumber = 43;
    }
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

      switch (command) {
        
        case LSDJCommandN:
        
          // Note on/off.

          //~ Serial.print("note ");
          //~ Serial.println(data);    

          stopCurrentNote(channel);

          if (data != 0) {
            
            channelConfig.currentNote = data;
            
            midiOut.write(0x90 | channelConfig.midiChannel);
            midiOut.write(data);
            midiOut.write(0x7F);            
          }
          break;
        
        case LSDJCommandX:
    
          // Control Change (CC)          
          uint8_t value;
          uint8_t ccNumber;
          
          switch (channelConfig.ccMode) {
          case ChannelCCModeSingle:
            value = data * (int)0x7F / 0x6F;
            ccNumber = channelConfig.ccNumber;
            break;
          }
          
          midiOut.write(0xB0 | channelConfig.midiChannel);
          midiOut.write(channelConfig.ccNumber);
          midiOut.write(value);
          
          break;
        
        case LSDJCommandY:
    
          // Program Change
          midiOut.write(0xC0 | channelConfig.midiChannel);
          midiOut.write(data);
          break;
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
  OSCCAL = 0xEF;
    
  mi.begin();
}

void loop() {
  led::check();
  mi.check();
  delayMicroseconds(10);
}

