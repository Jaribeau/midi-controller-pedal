#include "MIDIUSB.h"
#include <HCSR04.h>
#include <EEPROM.h>

// TODO:
// - Add LEDs
// - Add LCD
// - Assign button pins
// - Decide if expression pedal will get a channel change button (and LEDs)
// - Calibrate exp pedal

/*****************************************************/
/*************** PROPOSED PEDAL LAYOUT ***************/
/*****************************************************/
/*
   8 footswitches: general purpose buttons
   1 footswitch: page toggle for general purpose buttons
   1 expression/volume pedal
   1 LCD readout
   1 LED strip (10+ LEDS)
   1 footswitch: expression pedal channel toggle?
   3 knobs?
*/


//IDEA: page N = toggle/momentary config page, LEDs on each button color to indicate mode?


//
// Pin assignments
//
int BTN_PAGE_UP_PIN = 8;
int GEN_BTN_PINS[] = {6, 7, 9, 2, 3, 4, 5};
int CONTROL_NUMBER[] = {0x0E,0x0F,0x10,0x11,
                        0x12,0x13,0x14,0x15,
                        0x16,0x17,0x18,0x19,
                        0x1A,0x1B,0x1C,0x1D}; // Must be at least NUM_GEN_BTNS * NUM_PAGES

int LEDS_CONTROL_PIN = 15;
int ULTRASONIC_TRIG_PIN = 14;
int ULTRASONIC_ECHO_PIN = 16;
UltraSonicDistanceSensor distanceSensor(ULTRASONIC_TRIG_PIN, ULTRASONIC_ECHO_PIN);


//
// State variables
//
bool btn_states[] = {0, 0, 0, 0, 0, 0, 0, 0};
bool toggle_btn_states[] = {0, 0, 0, 0, 0, 0, 0, 0};
long btn_last_change_times[] = {0, 0, 0, 0, 0, 0, 0, 0};
long btn_long_hold_start_times[] = {0, 0, 0, 0, 0, 0, 0, 0};
bool btn_page_up_state = 0;
long btn_page_up_last_change_time = 0;
long last_pedal_read_time = 0;
int current_page = 0; // Channel used corresponds to current page
double rolling_distance_avg = 0;


//
// Constants
//
const int NUM_GEN_BTNS = 2;
const int NUM_PAGES = 2;
const long DEBOUNCE_DELAY = 50;
const long MODE_CHANGE_DELAY = 5000; // Must hold button for 5 seconds to switch between momentary and toggle mode
const double EXP_PEDAL_SMOOTHING = 20;
const long PEDAL_DELAY = 100;

//
// EEPROM (nonvolatile) settings
//
int btn_mode_addr[NUM_GEN_BTNS * NUM_PAGES];



// First parameter is the event type (0x09 = note on, 0x08 = note off).
// Second parameter is note-on/note-off, combined with the channel.
// Channel: 0-15 (1-16 in live)
// Pitch: 48 = middle C
// Velocity: 64 = normal, 127 - fastest
void noteOn(byte channel, byte pitch, byte velocity) {
  midiEventPacket_t noteOn = {0x09, 0x90 | channel, pitch, velocity};
  MidiUSB.sendMIDI(noteOn);
  MidiUSB.flush();
}



void noteOff(byte channel, byte pitch, byte velocity) {
  midiEventPacket_t noteOff = {0x08, 0x80 | channel, pitch, velocity};
  MidiUSB.sendMIDI(noteOff);
  MidiUSB.flush();
}



// CONTROL CHANGE
// First parameter is the event type (0x0B = control change).
// Second parameter is the event type, combined with the channel.
// Third parameter is the control number number (0-119).
// Fourth parameter is the control value (0-127).
void controlChange(byte channel, byte control, byte value) {
  midiEventPacket_t event = {0x0B, 0xB0 | channel, control, value};
  MidiUSB.sendMIDI(event);
  MidiUSB.flush();
}



void messageReceivedCallback(byte channel, byte pitch, byte velocity)
{
  // Do some stuff with NoteOn here
  // TODO: Display BPM on LCD AND/OR a Blinking LED
}



void switchBtnMode(int button){
  if(isBtnInToggleMode(button))
    EEPROM.write(btn_mode_addr[button + (NUM_GEN_BTNS * current_page)], 0); 
  else
    EEPROM.write(btn_mode_addr[button + (NUM_GEN_BTNS * current_page)], 1); 

  Serial.print("BtnAddr:");
  Serial.print(button + (NUM_GEN_BTNS * current_page));
  Serial.print(" TMode:");
  Serial.println(isBtnInToggleMode(button));
}



bool isBtnInToggleMode(int button){
  return EEPROM.read(btn_mode_addr[button + (NUM_GEN_BTNS * current_page)]);
}



void setup() {
  // Populate EEPROM address list
  for(int i = 0; i < (NUM_GEN_BTNS * NUM_PAGES); i++){
    btn_mode_addr[i] = i;

    // Initialize values to 0 if they have never been written
    if(EEPROM.read(btn_mode_addr[i]) == 255)
      EEPROM.write(btn_mode_addr[i], 0);
  }
  
  // Setup MIDI over USB serial
  Serial.begin(115200);

  // Setup pins
  pinMode(BTN_PAGE_UP_PIN, INPUT_PULLUP);
  for(int i = 0; i < NUM_GEN_BTNS; i++){
    pinMode(GEN_BTN_PINS[i], INPUT_PULLUP);
  }

  // TODO: Setup NeoPixel strip
  // TODO: Setup I2C for LCD
}



void loop() {
  ///////////////////
  //  BUTTON MODE CHANGES
  ///////////////////
  for(int i = 0; i < NUM_GEN_BTNS; i++){
    if(!digitalRead(GEN_BTN_PINS[i])){
      // Start timer if not already started
      if(!btn_long_hold_start_times[i])
        btn_long_hold_start_times[i] = millis();
      else if ((millis() - btn_long_hold_start_times[i]) > MODE_CHANGE_DELAY){
        switchBtnMode(i);
        btn_long_hold_start_times[i] = 0;
      }
    }
    else
      btn_long_hold_start_times[i] = 0;
  }
  
  
  ///////////////////
  //  GENERAL BUTTONS
  ///////////////////
  for(int btn = 0; btn < NUM_GEN_BTNS; btn++){
    
    // Check for press (and debounce input)
    if(digitalRead(GEN_BTN_PINS[btn]) != btn_states[btn] && (millis() - btn_last_change_times[btn]) > DEBOUNCE_DELAY){
      btn_last_change_times[btn] = millis();
      btn_states[btn] = digitalRead(GEN_BTN_PINS[btn]);

      // Publish Midi message
      if(!isBtnInToggleMode(btn)){
        controlChange(current_page, CONTROL_NUMBER[current_page*NUM_GEN_BTNS + btn], (int)(!btn_states[btn])*127); // Control change on or off
        //Print button state
        Serial.print("Btn");
        Serial.print(btn);
        Serial.print(":");
        Serial.println(!btn_states[btn]);
      }
      else if(isBtnInToggleMode(btn) && !btn_states[btn]){
        // Flip toggle only on falling edge
        toggle_btn_states[btn] = !toggle_btn_states[btn];
        controlChange(current_page, CONTROL_NUMBER[current_page*NUM_GEN_BTNS + btn], (int)toggle_btn_states[btn]*127);
        Serial.print("TBtn");
        Serial.print(btn);
        Serial.print(":");
        Serial.println(!toggle_btn_states[btn]);
      }
    } 
  }


  ///////////////////
  // PAGE UP BUTTON
  ///////////////////
  
  if(digitalRead(BTN_PAGE_UP_PIN) != btn_page_up_state && (millis() - btn_page_up_last_change_time) > DEBOUNCE_DELAY){
    btn_page_up_last_change_time = millis();
    btn_page_up_state = !btn_page_up_state;
    
    // Change page only on rising edge
    if(btn_page_up_state){
      current_page++;
      if(current_page >= NUM_PAGES){
        current_page = 0;
      }
      Serial.print("Pg:");
      Serial.println(current_page);
    }
  }
  

  ///////////////////
  // EXPRESSION PEDAL
  ///////////////////
  // Every 500 miliseconds, do a measurement using the sensor and print the distance in centimeters.
  if(millis() - last_pedal_read_time > PEDAL_DELAY){
    double distance = distanceSensor.measureDistanceCm();
    double prev_distance = rolling_distance_avg;
    rolling_distance_avg = (rolling_distance_avg * EXP_PEDAL_SMOOTHING + distance) / (EXP_PEDAL_SMOOTHING + 1); 
    
    if(rolling_distance_avg != prev_distance)
      controlChange(0, 0x0B, rolling_distance_avg*10);
  }


}
