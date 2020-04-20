#include "MIDIUSB.h"
#include <HCSR04.h>
#include <EEPROM.h>
#include <Wire.h> 
#include <Adafruit_NeoPixel.h>
#include <Mouse.h>
#include <Encoder.h>

// TODO:
// - Use encoder button to trigger mode changes

/*****************************************************/
/**********      ADJUSTABLE PARAMETERS       *********/
/*****************************************************/
const int NUM_PAGES = 3;  // Must erase EEPROM once when changing this.
const int EXP_PEDAL_SENSITIVITY = 1; // Linear factor.
const long DEBOUNCE_DELAY = 10;
const long MODE_CHANGE_DELAY = 5000; // Must hold button for 5 seconds to switch between momentary and toggle mode


/*****************************************************/
/***************      CONSTANTS       ***************/
/*****************************************************/
int MIDI_CONTROL_NUMBERS[] = {0x0E,0x0F,0x10,0x11,
                              0x12,0x13,0x14,0x15,
                              0x16,0x17,0x18,0x19,
                              0x1A,0x1B,0x1C,0x1D}; // Must be enough numbers to equal NUM_GEN_BTNS * NUM_PAGES

const bool ON = 1;
const bool OFF = 0;

const int NUM_GEN_BTNS = 4;
const int LED_COUNT = 6;
const int NUM_BTNS_WITH_LEDS = 5;
const int NUM_LEDS_PER_BUTTON = 1;
const int NUM_LEDS_BTWEEN_BUTTONS = 0;


/*****************************************************/
/***************    PIN ASSIGNMENTS    ***************/
/*****************************************************/
// Led Index Map
//    0   1   2   3   4
int LED_PIN = 5;
int BTN_LED_INDEXES[] = {1, 2, 3, 4};
int PAGE_BTN_INDEX = 0;
int EXP_PEDAL_LED_INDEX = 5;

// Button/Pin Map
//    4   5   6   7   8
int GEN_BTN_PINS[] = {10, 16, 14, 15};
int BTN_PAGE_UP_PIN = A0;
int BTN_EXP_PEDAL_PIN = 4;
int GND_PIN_PULL_DOWN = 9;
int ENCODER_PIN_A = 3;
int ENCODER_PIN_B = 2;

/*****************************************************/
/***************    STATE VARIABLES    ***************/
/*****************************************************/
bool btn_states[NUM_GEN_BTNS * NUM_PAGES];
bool toggle_btn_states[NUM_GEN_BTNS * NUM_PAGES];
bool btn_toggle_modes[NUM_GEN_BTNS * NUM_PAGES];
bool btn_colours[NUM_BTNS_WITH_LEDS * NUM_PAGES];
long btn_last_change_times[NUM_GEN_BTNS];
long btn_long_hold_start_times[NUM_GEN_BTNS];
int roller_values[NUM_PAGES+1];

bool btn_page_up_state = 0;
long btn_page_up_last_change_time = 0;

int current_page = 0; // Midi channel used corresponds to current page. This ranges from 0 to NUM_PAGES-1.

int exp_pedal_in = 0;
int prev_exp_pedal_in = 0;

//Mouse
long mouse_delay_timer = 0;
long mouse_delay = 100;
int mouse_sensitivity = 1; // Inverse


// LED Setup
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN);
Encoder myEnc(ENCODER_PIN_A, ENCODER_PIN_B);

uint32_t GREENISHWHITE = strip.Color(0, 64, 0, 64);
uint32_t RED = strip.Color(255, 1, 1);
uint32_t GREEN = strip.Color(1, 255, 1);
uint32_t BLUE = strip.Color(1, 1, 255);
uint32_t WHITE = strip.Color(0, 0, 0, 64);
uint32_t BLACK = strip.Color(0, 0, 0, 0);
uint32_t PURPLE = strip.Color(64, 0, 64);
uint32_t COLOUR_CHOOSER_COLOURS[] = {GREENISHWHITE,
                                  RED,
                                  GREEN,
                                  BLUE,
                                  WHITE,
                                  PURPLE};

// uint32_t RED_H = strip.gamma32(strip.ColorHSV(35000));
uint32_t TURQOISE_H = 38000;
uint32_t PURPLE_H = 20000;
uint32_t BLUE_H = 5000;
uint32_t WHITE_H = strip.ColorHSV(50000);
uint32_t GREEN_H = strip.gamma32(strip.ColorHSV(60000));
uint32_t PAGE_HUES[] = {  TURQOISE_H,
                          PURPLE_H,
                          BLUE_H};
int NUM_COLOUR_OPTIONS = 6;
int currentColourChooserIndex = 0;
int brightness = 10; //0 - 255

//
// EEPROM (nonvolatile) settings
//
int btn_mode_addr[NUM_GEN_BTNS * NUM_PAGES];
int btn_colour_addr[NUM_BTNS_WITH_LEDS * NUM_PAGES];
int btn_tog_state_addr[NUM_GEN_BTNS * NUM_PAGES];




/*****************************************************/
/***************    MIDI FUNCTIONS    ***************/
/*****************************************************/
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




/*****************************************************/
/***************    CONFIG FUNCTIONS    ***************/
/*****************************************************/
void switchBtnMode(int button){
  btn_toggle_modes[button + (NUM_GEN_BTNS * current_page)] = !isBtnInToggleMode(button);
  EEPROM.write(btn_mode_addr[button + (NUM_GEN_BTNS * current_page)], isBtnInToggleMode(button)); 
}

int getToggBtnState(int button){
  return toggle_btn_states[current_page*NUM_GEN_BTNS + button];
}

int getBtnState(int button){
  return btn_states[current_page*NUM_GEN_BTNS + button];
}

void setToggBtnState(int button, bool state){
  toggle_btn_states[current_page*NUM_GEN_BTNS + button] = state;
  EEPROM.write(btn_tog_state_addr[button + (NUM_GEN_BTNS * current_page)], getToggBtnState(button));
}

void setBtnState(int button, bool state){
  btn_states[current_page*NUM_GEN_BTNS + button] = state;
}



/*******************************************************/
/***************    HELPER FUNCTIONS    ***************/
/*******************************************************/

void setBtnLEDs(int btn, uint32_t colour){
  // Button oder: Page change, Gen buttons, roller
  strip.setPixelColor(BTN_LED_INDEXES[btn], colour);
}

void refreshLEDs(){
  // Set Page Button LED
  strip.setPixelColor(PAGE_BTN_INDEX, getButtonColour(0));

  // Set Page Button LED
  int c = 20 + roller_values[current_page] * 1.5;  // From 0-127, down to 0-25

  // Roller indicator   R, B, G
  strip.setPixelColor(EXP_PEDAL_LED_INDEX, strip.Color(c, c, c)); 

  for(int btn = 0; btn < NUM_GEN_BTNS; btn++){
    if(isBtnInToggleMode(btn) && getToggBtnState(btn)){
      setBtnLEDs(btn, getButtonColour(btn));
    }
    else if(isBtnInToggleMode(btn) && !getToggBtnState(btn)){
      setBtnLEDs(btn, BLACK);
    }
    else if(!isBtnInToggleMode(btn) && !getBtnState(btn)){
      setBtnLEDs(btn, getButtonColour(btn));
    }
    else if(!isBtnInToggleMode(btn) && getBtnState(btn)){
      setBtnLEDs(btn, BLACK);
    }
  }
  strip.show();
}

int getButtonColour(int button){
  return strip.gamma32(strip.ColorHSV(PAGE_HUES[current_page],255,255)) ;
}

void saveButtonColour(int button, int colour){
  btn_colours[button + (NUM_BTNS_WITH_LEDS * current_page)] = colour;
  EEPROM.write(btn_colour_addr[button + (NUM_BTNS_WITH_LEDS * current_page)], colour);
}

bool isBtnInToggleMode(int button){
  return btn_toggle_modes[button + (NUM_GEN_BTNS * current_page)];
}


/*******************************************************/
/***************  INPUT TRIGGER FUNCTIONS  ***************/
/*******************************************************/

void generalButtonPress(int btn){
  btn_last_change_times[btn] = millis();

  // Publish Midi message
  if(!isBtnInToggleMode(btn)){
    controlChange(current_page+1, MIDI_CONTROL_NUMBERS[current_page*NUM_GEN_BTNS + btn], (int)!getBtnState(btn)*127); // Control change on or off
  }
  else {
    setToggBtnState(btn, !getToggBtnState(btn));
    controlChange(current_page+1, MIDI_CONTROL_NUMBERS[current_page*NUM_GEN_BTNS + btn], (int)getToggBtnState(btn)*127);
  }
}


void generalButtonRelease(int btn){
  btn_last_change_times[btn] = millis();

  if(!isBtnInToggleMode(btn)){
    controlChange(current_page+1, MIDI_CONTROL_NUMBERS[current_page*NUM_GEN_BTNS + btn], (int)(!getBtnState(btn)*127)); // Control change on or off
  }
}

void pageUpButtonPress(){    
    btn_page_up_state = false;
    btn_page_up_last_change_time = millis();
    myEnc.write(0);
    if(current_page == NUM_PAGES-1)
      current_page = 0;
    else
      current_page++;
}

void pageUpButtonRelease(){
    btn_page_up_state = true;
    btn_page_up_last_change_time = millis();
}

void rollerChange(int amount){
  if(current_page != NUM_PAGES-1){
    // Limit to max and min values
    if(amount == 0 || 
    (roller_values[current_page] + amount * EXP_PEDAL_SENSITIVITY < 0 ) || 
    (roller_values[current_page] + amount * EXP_PEDAL_SENSITIVITY > 128 ))
    return;

    // Update roller values and publish MIDI control change
    roller_values[current_page]+= amount * EXP_PEDAL_SENSITIVITY;
    controlChange(current_page+1, 0x0B, roller_values[current_page]);
  }
  else{  
    // MOUSE SCROLL WHEEL
    // if(millis() - mouse_delay_timer > mouse_delay){
    //   mouse_delay_timer = millis();
      Mouse.move(0,0, (char)(amount * mouse_sensitivity));
    // }
  }
}



/*******************************************************/
/***************   SETUP AND MAIN LOOP   ***************/
/*******************************************************/
void setup() {
  // RESET EEPROM
  // for (int i = 0 ; i < EEPROM.length() ; i++) {
  //   EEPROM.write(i, 255);
  // }

  //
  // POPULATE EEPROM
  //
  // Gen Button Modes
  int mem_btn_mode_start = 0;
  int mem_btn_mode_size = NUM_GEN_BTNS * NUM_PAGES;
  for(int i = mem_btn_mode_start; i < mem_btn_mode_size; i++){
    btn_mode_addr[i] = i;

    // Initialize values to 1 if they have never been written
    if(EEPROM.read(btn_mode_addr[i]) != 1 && EEPROM.read(btn_mode_addr[i]) != 0)
      EEPROM.write(btn_mode_addr[i], 1);
    else
      btn_toggle_modes[i] = EEPROM.read(btn_mode_addr[i]);
  }

  // Button Colours
  int mem_btn_colour_start = mem_btn_mode_start + mem_btn_mode_size;
  int mem_btn_colour_size = NUM_BTNS_WITH_LEDS * NUM_PAGES;
  for(int i = 0; i < mem_btn_colour_size; i++){
    btn_colour_addr[i] = i + mem_btn_colour_start;

    // Initialize values to 0 if they have never been written
    if(EEPROM.read(btn_colour_addr[i]) >= NUM_COLOUR_OPTIONS)
      EEPROM.write(btn_colour_addr[i], 0);
    else
      btn_colours[i] = EEPROM.read(btn_colour_addr[i]);
  }

  // Gen Button States
  int mem_tog_btn_state_start = mem_btn_colour_start+mem_btn_colour_size;
  int mem_tog_btn_state_size = NUM_GEN_BTNS * NUM_PAGES;
  for(int i = 0; i < mem_tog_btn_state_size; i++){
    btn_tog_state_addr[i] = i + mem_tog_btn_state_start;

    // Read in the state values from last shutdown, initializing them to 0 (off) if they have never been written
    if(EEPROM.read(btn_tog_state_addr[i]) != 1 && EEPROM.read(btn_tog_state_addr[i]) != 0)
      EEPROM.write(btn_tog_state_addr[i], 0);
    else
      toggle_btn_states[i] = EEPROM.read(btn_tog_state_addr[i]);
  }

  // Roller values
  for(int i = 0; i < NUM_PAGES; i++){
    roller_values[i] = 0;
  }
  

  // Setup MIDI over USB serial
  // Serial.begin(115200);
  Serial.begin(9600);

  // Setup pins
  pinMode(GND_PIN_PULL_DOWN, OUTPUT);
  digitalWrite(GND_PIN_PULL_DOWN, LOW);
  pinMode(BTN_PAGE_UP_PIN, INPUT_PULLUP);
  for(int i = 0; i < NUM_GEN_BTNS; i++){
    pinMode(GEN_BTN_PINS[i], INPUT_PULLUP);
  }
  
  // Setup NeoPixel strip
  strip.begin();
  // strip.fill(GREEN, 0, 18);
  strip.setBrightness(brightness);
  strip.show(); // Initialize all pixels to 'off'

  myEnc.write(0);

  // Setup Mouse Scroll Control
  Mouse.begin();
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

  //  GENERAL BUTTONS
  for(int btn = 0; btn < NUM_GEN_BTNS; btn++){
    // Check for press (and debounce input)
    if(digitalRead(GEN_BTN_PINS[btn]) != getBtnState(btn) && (millis() - btn_last_change_times[btn]) > DEBOUNCE_DELAY){
      setBtnState(btn, digitalRead(GEN_BTN_PINS[btn]));

      if(getBtnState(btn))
        generalButtonRelease(btn);
      else
        generalButtonPress(btn);
    } 
  }

  // PAGE UP BUTTON
  if(digitalRead(BTN_PAGE_UP_PIN) != btn_page_up_state && (millis() - btn_page_up_last_change_time) > DEBOUNCE_DELAY*2){
    if(btn_page_up_state && !digitalRead(BTN_PAGE_UP_PIN))
      pageUpButtonPress();
    else if(!btn_page_up_state && digitalRead(BTN_PAGE_UP_PIN))
      pageUpButtonRelease();
  }

  // EXPRESSION PEDAL
  rollerChange(-(int)myEnc.read());
  myEnc.write(0);

  refreshLEDs();
}
