# midi-controller-pedal
A custom MIDI controller pedalboard to control a DAW, for fun!

## Planned pedal layout
* 8 footswitches: general purpose buttons
* 1 footswitch: page toggle for general purpose buttons
* 1 expression/volume pedal
* 1 LCD readout (show page number and other messages)
* 1 LED strip (10+ LEDS)
* 1 footswitch: expression pedal channel toggle (?)
* 3 general purpose knobs (?)

## Momentary vs Toggle Buttons
The physical buttons on the controller are momentary swtiches; however, for some uses of midi control messages a toggle is required (i.e. activating/deactivating an effects processor). To switch the configuration of one of the general purpose switches, hold the switch for ~7 seconds (TODO: Add feedback via LCD and LEDs). The configuration will persist even after power is disconnected.
