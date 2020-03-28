# midi-controller-pedal
A custom MIDI controller pedalboard to control a DAW, for fun!

## TODO
* Change button mode interaction
    * Current: Hold button for 5 seconds.
    * Desired: Hold config button (maybe press roller inwards), then press switch
* Save roller states to EEPROM
* Save current page to EEPROM
* Add toggle switch for mouse? 
* Set roller LED colours to match page colour

## Planned pedal layout
* 4 footswitches: general purpose buttons
* 1 footswitches: page cycle
* 1 LED strip (10+ LEDS)
* 3 general purpose knobs (?)

## Pin Allocations
See PIN ASSIGNMENTS in `midi-controller-pedal-simple.ino`  

## LED Plans
* 1x for page indicator
* 4x for footswitches
* 1 for roller position

## Configure Pins
* Long button hold (10s) changes pin between toggle and momentary mode

## Momentary vs Toggle Buttons
The physical buttons on the controller are momentary swtiches; however, for some uses of midi control messages a toggle is required (i.e. activating/deactivating an effects processor). To switch the configuration of one of the general purpose switches, hold the switch for ~7 seconds (TODO: Add feedback via LCD and LEDs). The configuration will persist even after power is disconnected.  

https://help.ableton.com/hc/en-us/articles/209774945--Momentary-and-Toggle-MIDI-functions


## MIDI Control Change Messages
https://www.midi.org/specifications-old/item/table-3-control-change-messages-data-bytes-2
