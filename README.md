# LSDJmi

This is the firmware for a little dongle providing MIDI out interface for the "Arduinoboy" version of LSDJ.

The actual [Arduinoboy](https://github.com/trash80/Arduinoboy) by **trash80** supports other Gameboy programs and more modes of operation. Check it out.

Similar to Arduinoboy the LSDJmi dongle allows to control other synthesizers using familiar tracker interface of LSDJ. You can play a drum machine or a bass synth together with your chiptune or you can can use your Gameboy solely as an external sequencer, which sometimes can be more powerful than a built-in sequencer of an external synth.

Unlike Arduinoboy LSDJmi is configurable directly from LSDJ which allows to have different setup for different songs. 

![Overview Image](./overview.jpg)

## How does it work?

The "Arduinoboy" build of LSDJ (you can download it where you got your copy of LSDJ) supports the following extra commands (you can find them after the standard **Z** command):

 - **N**`xx` — sends an arbitrary MIDI note `xx`.
 - **Q**`xx` — sends a note corresponding to the pitch in the current Gameboy channel transposed by `xx` semitones.
 - **X**`xx` — sends a Control Change message with the value `xx` to a control associated with the current channel (can also work with multiple controls if enabled, see below); this allows to control knobs of your external gear. 
 - **Y**`xx` — sends a Program Change message which allows you to change the current patch of your synth.

These commands are received by the dongle from the Gameboy Link port and the corresponding MIDI messages are generated on the MIDI out port of the dongle.

(Note that because of the communication protocol the `xx` argument in all the commands above should be in the range 00-6F.)

## Configuration

Each Gameboy channel has the following settings associated with it on the dongle side:

 - the MIDI channel to use for notes and CC or PC messages coming from LSDJ on this Gameboy channel;
 - the velocity for every note being sent;
 - the mode of interpretation of CC commands (**X**`xx`):
   - in the **single** mode there is only one control associated with the Gameboy channel, so the value from any **X**`xx` command will be translated from the command's 00-6F range into MIDI's 00-7F and sent to the associated MIDI CC;
   - in the **scaled** mode there can be up to 7 controls associated with the Gameboy channel; the first nibble of the argument of the **X**`xx` command is interpreted as an index of the control (0-6) and the second one as a value, which will be scaled from 0-F range into MIDI's 00-7F.

As mentioned above, Arduinoboy allows to change its configuration via special messages on its MIDI in sent by a dedicated tool. LSDJmi however can be configured from the Gameboy itself as follows:

The `Y6F` command is not treated as "Program Change to 111" but instructs LSDJmi to treat the following **X**`xx` commands as channel configuration changes:

**X**`mc` goes first and defines how many CCs will be associated with the current Gameboy channel (`m`), as well as the MIDI channel to use for all the notes, CC and PC messages on this Gameboy channel. 
 
When `m` is:

- 0 — it means that no changes in the current CC config will follow;
- 1 — it selects the 'single' CC mode with the following single **X**`xx` command defining the MIDI CC number;
- 2-6 — it selects the 'scaled' CC mode, and the following `m` **X**`xx` commands define the MIDI CC numbers to use for as targets corresponding to the high nibble of the argument of the regular (i.e. outside of the config change sequence) **X** command.

The last **X**`vv` command defines the default velocity of every note on this Gameboy channel (will be scaled from the commands 00-6F range to the MIDI's 00-7F).

For example, you want to associate MIDI channel 3 with Gameboy's PU2 channel, treat all X commands on this channel as changing controller 43 (cutoff filter on KORG monologue) and use velocity 90 instead of 127 for all the notes you send, then you should execute the following sequence in the beginning of your song on PU2 channel:

- `Y6F` — to enter the config mode;
- `X12` — to use a single CC and MIDI channel 3 (2 in the command because we use zero-based numbers here);
- `X2B` — the single CC to use (2B is 43 in hex);
- `X4F` — the velocity we want (90 is from MIDI's 0-127 range, while we have 0-6F range here, so 0x4F will become 90 after scaling, i.e. 0x4F * 0x7F / 0x6F = 90).

The configuration sequence ends after that. In case the sequence is incomplete, i.e. some more **X** commands are expected, then you'll see the LED being lit. If any other **Y** or note commands are received before the the config sequence is complete, then configuration stops.

## Schematics

I am using a bare ATtiny85 for this project, but any Arduino-compatible board should work just fine, you'll need only 3 pins:

 - CLK pin of the Gameboy;
 - OUT pin of the Gameboy;
 - a pin for the MIDI out.

Here is my schematics for the completeness. 

![Schematics](./schematics.png)

Note that I am using a pull-up on the `OUT` pin (`R1`) because the end of the link cable I have is missing a `GND` wire. You should not need the pull-up if your cable is OK.

Also note that the LED is really optional. I've added it later so I don't forget to switch the power off. You might want to pick different value for its current limiting resistor.

Speaking of power, `VCC` is tied directly to a 3.7V LiPo battery, one of those used with drones.

---
