#include "MIDIUSB.h"
#include <HCSR04.h>
#include <EEPROM.h>
#include <Wire.h> 
#include <Adafruit_NeoPixel.h>
#include <Mouse.h>
#include <Encoder.h>


// TODO:
// Refactor to use buttonPress(), buttonRelease(), rollerChange() (only write debouncing once in the DI loop).
// Add switch statement in buttonPress() that calls genBtnPress(), pageUp/DownPressed(), modePressed()

// MIDI Mode
  // changeMode(MIDI_MODE)
  // btnPress()
      // sendMIDI()
      // IF togg: toggleLED()
      // IF momentary: LED on until buttonRelease()
  // rollerChange()
      // Update roller state
      // sendMIDI() 
      // setLed / screen #
      // OR scrollMouse()


// - Expression pedal interface (page changes channel, LEDs inditicate direction to setpoint, changes apply after hitting setpoint)

/*****************************************************/
/*************** PROPOSED PEDAL LAYOUT ***************/
/*****************************************************/
/*
   Pin Allocations
   0    None (RX pin)
   1    None (TX pin)
   2    LCD Data
   3    LCD Clock
   4-9  Buttons 1-6
   10   Button 7
   16   Button 8
   14   Button 9
   15   Button 10
   18   Expression Pedal Analog In
   19   
   20   LEDs
   21   
*/

//IDEA: page N = toggle/momentary config page, LEDs on each button color to indicate mode?

/*****************************************************/
/***************      CONSTANTS       ***************/
/*****************************************************/
const int NUM_GEN_BTNS = 4;
const long DEBOUNCE_DELAY = 100;
const long MODE_CHANGE_DELAY = 5000; // Must hold button for 5 seconds to switch between momentary and toggle mode
const int NUM_PAGES = 3;

int MIDI_CONTROL_NUMBERS[] = {0x0E,0x0F,0x10,0x11,
                              0x12,0x13,0x14,0x15,
                              0x16,0x17,0x18,0x19,
                              0x1A,0x1B,0x1C,0x1D}; // Must be enough numbers to equal NUM_GEN_BTNS * NUM_PAGES

const bool ON = 1;
const bool OFF = 0;

const int LED_COUNT = 18;
const int NUM_BTNS_WITH_LEDS = 11;
const int NUM_LEDS_PER_BUTTON = 1;
const int NUM_LEDS_BTWEEN_BUTTONS = 0;


/*****************************************************/
/***************    PIN ASSIGNMENTS    ***************/
/*****************************************************/
// Button/Pin Map
//    4   5   6   7   8
//    9   15  14  10  16
// Led Index Map
//    0   1   2   3   4
//    11  10  9   8   7
int GEN_BTN_PINS[] =    {4, 5, 6, 7};
int BTN_LED_INDEXES[] = {0, 1, 2, 3, 4};
int EXP_PEDAL_PIN = A0;
int LED_PIN = 19;
int BTN_PAGE_UP_PIN = 8;

/*****************************************************/
/***************    STATE VARIABLES    ***************/
/*****************************************************/
bool btn_states[NUM_GEN_BTNS * NUM_PAGES];
bool toggle_btn_states[NUM_GEN_BTNS * NUM_PAGES];
bool btn_toggle_modes[NUM_GEN_BTNS * NUM_PAGES];
bool btn_colours[NUM_BTNS_WITH_LEDS * NUM_PAGES];

long btn_last_change_times[] = {0, 0, 0, 0, 0, 0, 0};
long btn_long_hold_start_times[] = {0, 0, 0, 0, 0, 0, 0};
bool btn_change_ISR_flag[] = {0, 0, 0, 0, 0, 0, 0};
int roller_values[] = {0, 0, 0, 0, 0, 0, 0};

bool btn_page_up_state = 0;
long btn_page_up_last_change_time = 0;

int current_page = 1; // Midi channel used corresponds to current page. This ranges from 1 to NUM_PAGES.

int exp_pedal_in = 0;
int prev_exp_pedal_in = 0;
long oldPosition  = -999;

// int selected_button = 0;

//Mouse
int prev_scroller_in = 0;
long mouse_delay_timer = 0;
long mouse_delay = 100;


// LED Setup
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ400);
Encoder myEnc(3, 2);

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
uint32_t TURQOISE_H = strip.gamma32(strip.ColorHSV(38000));
uint32_t PURPLE_H = strip.gamma32(strip.ColorHSV(20000));
uint32_t BLUE_H = strip.gamma32(strip.ColorHSV(5000));
uint32_t WHITE_H = strip.ColorHSV(50000);
uint32_t GREEN_H = strip.gamma32(strip.ColorHSV(60000));
uint32_t PAGE_COLOURS[] = {       TURQOISE_H,
                                  PURPLE_H,
                                  BLUE_H};
int NUM_COLOUR_OPTIONS = 6;
int currentColourChooserIndex = 0;
int brightness = 125; //0 - 255

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
  // Button oder: Gen buttons, page Up, page Down, mode, roller
  strip.setPixelColor(BTN_LED_INDEXES[btn], colour);
}

void refreshLEDs(){
  setBtnLEDs(4, getButtonColour(4));
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
  return PAGE_COLOURS[current_page-1];
  // return COLOUR_CHOOSER_COLOURS[btn_colours[button + (NUM_BTNS_WITH_LEDS * current_page)]];
}

void saveButtonColour(int button, int colour){
  btn_colours[button + (NUM_BTNS_WITH_LEDS * current_page)] = colour;
  EEPROM.write(btn_colour_addr[button + (NUM_BTNS_WITH_LEDS * current_page)], colour);
}

bool isBtnInToggleMode(int button){
  return btn_toggle_modes[button + (NUM_GEN_BTNS * current_page)];
}

//
// ISR Functions
//
bool page_up_btton_press_ISR_flag = false;
void pageUpButtonPressISR(){  if(millis() - btn_page_up_last_change_time > DEBOUNCE_DELAY){  page_up_btton_press_ISR_flag = true;}}

void generalButtonChangeISR0(){  if(millis() - btn_last_change_times[0] > DEBOUNCE_DELAY){ btn_change_ISR_flag[0] = true;}}

void generalButtonChangeISR3(){  
  // if(millis() - btn_last_change_times[3] > DEBOUNCE_DELAY)
  // { 
    btn_change_ISR_flag[3] = true;
  // }
}


/*******************************************************/
/***************  INPUT TRIGGER FUNCTIONS  ***************/
/*******************************************************/

void generalButtonPress(int btn){
  btn_change_ISR_flag[btn] = false;
  btn_last_change_times[btn] = millis();

  // Publish Midi message
  if(!isBtnInToggleMode(btn)){
    controlChange(current_page, MIDI_CONTROL_NUMBERS[current_page*NUM_GEN_BTNS + btn], (int)!getBtnState(btn)*127); // Control change on or off
  }
  else {
    setToggBtnState(btn, !getToggBtnState(btn));
    controlChange(current_page, MIDI_CONTROL_NUMBERS[current_page*NUM_GEN_BTNS + btn], (int)getToggBtnState(btn)*127);
  }
}


void generalButtonRelease(int btn){
  btn_change_ISR_flag[btn] = false;
  btn_last_change_times[btn] = millis();

  if(!isBtnInToggleMode(btn)){
    controlChange(current_page, MIDI_CONTROL_NUMBERS[current_page*NUM_GEN_BTNS + btn], (int)(!getBtnState(btn)*127)); // Control change on or off
  }
}

void pageUpButtonPress(){    
    btn_page_up_state = true;
    page_up_btton_press_ISR_flag = false;
    btn_page_up_last_change_time = millis();

    if(current_page == NUM_PAGES)
      current_page = 1;
    else
      current_page++;
}

void rollerChange(int amount){
  if(amount == 0 || 
    (amount < 0 && roller_values[current_page] == 0) || 
    (amount > 0 && roller_values[current_page] == 512))
    return;

  if(current_page != 3)
    controlChange(0, 0x0B, roller_values[current_page]/4);
  else{  
    // MOUSE SCROLL WHEEL
    if(millis() - mouse_delay_timer > mouse_delay && current_page == 2){
      mouse_delay_timer = millis();
      // Mouse.move(0,0, -(char)(amount/8));
      // prev_scroller_in = exp_pedal_in/8;
    }
  }
  
  // printToScreen(exp_pedal_in/4 *100/255);
  // if(exp_pedal_in/4*100/255 < 100) {printToScreen(" ");}
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
  

  // Setup MIDI over USB serial
  Serial.begin(115200);

  // Setup pins
  pinMode(BTN_PAGE_UP_PIN, INPUT_PULLUP);
  for(int i = 0; i < NUM_GEN_BTNS; i++){
    pinMode(GEN_BTN_PINS[i], INPUT_PULLUP);
  }
  
  // Setup NeoPixel strip
  strip.begin();
  // strip.fill(GREEN, 0, 18);
  strip.show(); // Initialize all pixels to 'off'

  // Attach interrupt service routines (ISRs)
  // attachInterrupt(digitalPinToInterrupt(BTN_PAGE_UP_PIN), pageUpButtonPressISR, RISING);
  // attachInterrupt(digitalPinToInterrupt(GEN_BTN_PINS[0]), generalButtonPressISR0, RISING);
  // attachInterrupt(digitalPinToInterrupt(GEN_BTN_PINS[0]), generalButtonReleaseISR0, FALLING);

  // attachInterrupt(digitalPinToInterrupt(GEN_BTN_PINS[3]), generalButtonChangeISR3, CHANGE);

  // Setup Mouse Scroll Control
  Mouse.begin();
}



void loop() {
  // Check ISR flags
  // if (page_up_btton_press_ISR_flag){pageUpButtonPress();}
  // if (btn_change_ISR_flag[0]){generalButtonPress(0);}
  // if (btn_release_ISR_flag[0]){generalButtonRelease(0);}

  // if (btn_change_ISR_flag[3]){
  //   if(digitalRead(GEN_BTN_PINS[3]))
  //     generalButtonPress(3);
  //   else
  //     generalButtonRelease(3);
  // }


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
    btn_page_up_state = digitalRead(BTN_PAGE_UP_PIN);

    if(!btn_page_up_state)
      pageUpButtonPress();
  }

  // EXPRESSION PEDAL
  rollerChange(myEnc.read());
  myEnc.write(0);

  refreshLEDs();
  delay(1);
}
