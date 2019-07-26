#include "MIDIUSB.h"
#include <HCSR04.h>
#include <EEPROM.h>
#include <Wire.h> 
#include <LiquidCrystal_I2C.h>
#include <Adafruit_NeoPixel.h>
#include <Mouse.h>



// TODO:
// - Add LEDs
// - Plan config interaction (mode selection, setting LEDs, setting toggle/momentary, saving presets)
// - Expression pedal interface (page changes channel, LEDs inditicate direction to setpoint, changes apply after hitting setpoint)
// - Explore sending info from Live to Pedal (https://julienbayle.studio/PythonLiveAPI_documentation/Live10.0.2.xml)
// - Saving presets (preset mode, new, delete, save, load, name)

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


//
// Pin assignments
//
int BTN_PAGE_UP_PIN = 8;
int BTN_MODE_PIN = 16;
int EXP_PEDAL_PIN = A0;
int GEN_BTN_PINS[] = {4, 5, 6, 7, 9, 15, 14, 10};
int CONTROL_NUMBER[] = {0x0E,0x0F,0x10,0x11,
                        0x12,0x13,0x14,0x15,
                        0x16,0x17,0x18,0x19,
                        0x1A,0x1B,0x1C,0x1D}; // Must be at least NUM_GEN_BTNS * NUM_PAGES

int LEDS_CONTROL_PIN = 18;

LiquidCrystal_I2C lcd(0x27,20,4);  // set the LCD address to 0x27 for a 16 chars and 2 line display

//
// State variables
//
bool btn_states[] = {0, 0, 0, 0, 0, 0, 0, 0};
bool toggle_btn_states[] = {0, 0, 0, 0, 0, 0, 0, 0};
long btn_last_change_times[] = {0, 0, 0, 0, 0, 0, 0, 0};
long btn_long_hold_start_times[] = {0, 0, 0, 0, 0, 0, 0, 0};
bool btn_page_up_state = 1;
long btn_page_up_last_change_time = 0;
int current_page = 0; // Channel used corresponds to current page
int exp_pedal_in = 0;
int prev_exp_pedal_in = 0;

//Mouse
int prev_scroller_in = 0;
long mouse_delay_timer = 0;
long mouse_delay = 100;


//
// Constants
//
const int NUM_GEN_BTNS = 8;
const int NUM_PAGES = 3;
const long DEBOUNCE_DELAY = 50;
const long MODE_CHANGE_DELAY = 5000; // Must hold button for 5 seconds to switch between momentary and toggle mode

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


  lcd.setCursor(0,1);
  lcd.print("                ");
  lcd.setCursor(0,1);
  lcd.print("B");
  lcd.print(button);
  if(isBtnInToggleMode(button))
    lcd.print(" mode: togg");
  else
    lcd.print(" mode: moment");
  delay(2000);
  lcd.setCursor(0,1);
  lcd.print("                ");
  
//  Serial.print("BtnAddr:");
//  Serial.print(button + (NUM_GEN_BTNS * current_page));
//  Serial.print(" TMode:");
//  Serial.println(isBtnInToggleMode(button));
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

  // Setup LCD
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0,0);
  lcd.print("Hello!");
  lcd.setCursor(0,1);
  lcd.print("Midi Pedal v0.1");
  delay(2000);
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("PAGE ");
  lcd.print(current_page);
  
  // TODO: Setup NeoPixel strip

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
        
        lcd.setCursor(0,1);
        lcd.print("BTN ");
        lcd.print(btn);
        lcd.print("|");
        lcd.print(!btn_states[btn]);
      }
      else if(isBtnInToggleMode(btn) && !btn_states[btn]){
        // Flip toggle only on falling edge
        toggle_btn_states[btn] = !toggle_btn_states[btn];
        controlChange(current_page, CONTROL_NUMBER[current_page*NUM_GEN_BTNS + btn], (int)toggle_btn_states[btn]*127);
        
        lcd.setCursor(0,1);
        lcd.print("tBTN");
        lcd.print(btn);
        lcd.print("|");
        lcd.print(!toggle_btn_states[btn]);
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

      // Update LCD Page Number
      lcd.setCursor(0,0);
      lcd.print("PAGE ");
      lcd.print(current_page);
      lcd.setCursor(0,1);
      lcd.print("                ");
    }
  }
  

  ///////////////////
  // EXPRESSION PEDAL
  ///////////////////
  exp_pedal_in = analogRead(EXP_PEDAL_PIN);
  if(exp_pedal_in != prev_exp_pedal_in && current_page != 2){
    controlChange(0, 0x0B, exp_pedal_in/4);
    lcd.setCursor(15-4,1);
    lcd.print("xp");
    lcd.print(exp_pedal_in/4 *100/255);
    if(exp_pedal_in/4*100/255 < 100) {lcd.print(" ");}
    prev_exp_pedal_in = exp_pedal_in;
  }


  ///////////////////
  // MOUSE SCROLL WHEEL
  ///////////////////
  if(millis() - mouse_delay_timer > mouse_delay && current_page == 2){
    mouse_delay_timer = millis();
    Mouse.move(0,0, -(char)(prev_scroller_in - exp_pedal_in/8));
    prev_scroller_in = exp_pedal_in/8;
  }
}
