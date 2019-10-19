# midi-controller-pedal
A custom MIDI controller pedalboard to control a DAW, for fun!

## Planned pedal layout
* 4 footswitches: general purpose buttons
* 1 footswitches: page cycle
* 1 LED strip (10+ LEDS)
* 3 general purpose knobs (?)

## LED Plans
* 4x for footswitches
* 1 for roller position

## Configure Pins
* Long button hold (10s) changes pin between toggle and momentary mode

## Momentary vs Toggle Buttons
The physical buttons on the controller are momentary swtiches; however, for some uses of midi control messages a toggle is required (i.e. activating/deactivating an effects processor). To switch the configuration of one of the general purpose switches, hold the switch for ~7 seconds (TODO: Add feedback via LCD and LEDs). The configuration will persist even after power is disconnected.  

https://help.ableton.com/hc/en-us/articles/209774945--Momentary-and-Toggle-MIDI-functions


## MIDI Control Change Messages
https://www.midi.org/specifications-old/item/table-3-control-change-messages-data-bytes-2