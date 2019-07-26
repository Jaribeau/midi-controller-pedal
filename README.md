# midi-controller-pedal
A custom MIDI controller pedalboard to control a DAW, for fun!

## Planned pedal layout
* 6 footswitches: general purpose buttons
* 2 footswitches: page up/down
* 1 footswitch: mode change (set LEDs, set toggle/momentary)
* 1 footswitch: ??? (maybe leave it out to make space for LCD?)
* 1 LCD readout (show page number, mode)
* 1 LED strip (10+ LEDS)
* 3 general purpose knobs (?)

## LED Plans
* 8x for footswitches
* 1 for BPM? (Can this info be received from Ableton?)
* 1 for 
* Roller position?

## Functions / LCD Info
* Current bank page/name
* Roller position?
* Configure toggle/momentary
* Configure LED colours
* Rename each page?
* "Assign tap tempo pin"?

## Momentary vs Toggle Buttons
The physical buttons on the controller are momentary swtiches; however, for some uses of midi control messages a toggle is required (i.e. activating/deactivating an effects processor). To switch the configuration of one of the general purpose switches, hold the switch for ~7 seconds (TODO: Add feedback via LCD and LEDs). The configuration will persist even after power is disconnected.  

https://help.ableton.com/hc/en-us/articles/209774945--Momentary-and-Toggle-MIDI-functions


## MIDI Control Change Messages
https://www.midi.org/specifications-old/item/table-3-control-change-messages-data-bytes-2