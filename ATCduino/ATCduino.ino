/* This program is based on misan dcservo https://github.com/misan/dcservo 
 *  the original structrure of the program is not changed nut it's adapted 
 *  to the needs of my application.
 *  Some extra commands added to serial communication, and some are deleted
 *  
 */
 
/* This one is not using any PinChangeInterrupt library */

/*
   This program uses an Arduino for a closed-loop control of a DC-motor. 
   Motor motion is detected by a quadrature encoder.
   Two inputs named STEP and DIR allow changing the target position.
   Serial port prints current position and target position every second.
   Serial input can be used to feed a new location for the servo (no CR LF).
   
   Pins used:
   Digital inputs 2 & 8 are connected to the two encoder signals (AB).
   Digital input 3 is the STEP input.
   Analog input 0 is the DIR input.
   Digital outputs 9 & 10 control the PWM outputs for the motor (I am using half L298 here).


   Please note PID gains kp, ki, kd need to be tuned to each different setup. 
*/
#include <EEPROM.h>
#include <PID_v1.h>
#define encoder0PinA  2   // PD2; 
#define encoder0PinB  8   // PC0;
#define homeSensor    4   //
#define enableRunPin  5   //
#define M1            9   // motor's PWM outputs
#define M2            10  // motor's PWM outputs
#define piston        7
#define stepsPerTurn  8384
#define maxStations   8
#define inPostionLimit 10
#define firmware "0.1" // firmware version
byte pos[500]; int p=0;

double kp=37.0,ki=32.0,kd=0.57;
double home_offset = 4000,rspeed = 255, hspeed =200,hoffsetspeed =200;
double input=0, output=0, setpoint=0;
PID myPID(&input, &output, &setpoint,kp,ki,kd, DIRECT);
volatile long encoder0Pos = 0;
boolean auto1=false, auto2=false,counting=false;
long previousMillis = 0;        // will store last time LED was updated

long target1=0,station =0; // destination location at any moment
bool homing =false;
bool home_invert_read= true;
bool searching =false;
bool searching1 = false;
//for motor control ramps 1.4
bool newStep = false;
bool oldStep = false;
bool dir = false;
bool parsing = false;
bool pistonCmd = false;
byte skip=0;
double Stations[maxStations];
long foo=0;
bool state,laststate;
// Install Pin change interrupt for a pin, can be called multiple times
void pciSetup(byte pin) 
{
    *digitalPinToPCMSK(pin) |= bit (digitalPinToPCMSKbit(pin));  // enable pin
    PCIFR  |= bit (digitalPinToPCICRbit(pin)); // clear any outstanding interrupt
    PCICR  |= bit (digitalPinToPCICRbit(pin)); // enable interrupt for the group 
}

void setup() { 
  pinMode(encoder0PinA, INPUT_PULLUP); 
  pinMode(encoder0PinB, INPUT_PULLUP);
  pinMode(homeSensor, INPUT);
  pinMode(enableRunPin, INPUT);
  pinMode (piston,OUTPUT);  
  pciSetup(encoder0PinB);
  attachInterrupt(0, encoderInt, CHANGE);  // encoder pin on interrupt 0 - pin 2
  //attachInterrupt(1, countStep      , RISING);  // step  input on interrupt 1 - pin 3
  TCCR1B = TCCR1B & 0b11111000 | 1; // set 31Kh PWM
  Serial.begin (115200);
  //help();
  recoverPIDfromEEPROM();
  //Setup the pid 
  myPID.SetMode(AUTOMATIC);
  myPID.SetSampleTime(1);
  myPID.SetOutputLimits(-rspeed,rspeed);

} 

void loop(){
    if (pistonCmd) digitalWrite (piston,LOW);else digitalWrite (piston,HIGH); 
    input = encoder0Pos; 
    setpoint=target1;
    if(Serial.available()) process_line(); // it may induce a glitch to move motion, so use it sparingly 
    if (!homing) {if(digitalRead (enableRunPin)== HIGH ) {myPID.SetMode(AUTOMATIC);myPID.Compute();} else { myPID.SetMode(MANUAL);myPID.ComputeM(0);};};
    //while(myPID.Compute()){ // wait till PID is actually computed
    pwmOut(output); 
    //if(auto1) if(millis() % 3000 == 0) target1=random(2000); // that was for self test with no input from main controller
    if(auto2) if(millis() % 1000 == 0) printPos();
    //if(counting && abs(input-target1)<15) counting=false; 
    if (homing) Homing(home_offset);
    if(counting &&  (skip++ % 5)==0 ) {pos[p]=encoder0Pos; if(p<499) p++; else counting=false;}
}

void pwmOut(int out) {
  
   if(out<0) { analogWrite(M1,0); analogWrite(M2,abs(out)); }
   else { analogWrite(M2,0); analogWrite(M1,abs(out)); }}
  
  
const int QEM [16] = {0,-1,1,2,1,0,2,-1,-1,2,0,1,2,1,-1,0};               // Quadrature Encoder Matrix
static unsigned char New, Old;
ISR (PCINT0_vect) { // handle pin change interrupt for D8
  Old = New;
  New = (PINB & 1 )+ ((PIND & 4) >> 1); //
  encoder0Pos+= QEM [Old * 4 + New];
}

void encoderInt() { // handle pin change interrupt for D2
  Old = New;
  New = (PINB & 1 )+ ((PIND & 4) >> 1); //
  encoder0Pos+= QEM [Old * 4 + New];
}


void countStep(){ if (PINC&B0000001) target1--;else target1++; } // pin A0 represents direction

void process_line() {
 char cmd = Serial.read();
 long target = 0;
 if(cmd>'Z') cmd-=32;
 switch(cmd) {
  case 'P': kp=Serial.parseFloat(); myPID.SetTunings(kp,ki,kd); break;
  case 'D': kd=Serial.parseFloat(); myPID.SetTunings(kp,ki,kd); break;
  case 'I': ki=Serial.parseFloat(); myPID.SetTunings(kp,ki,kd); break;
  case '?': printPos(); break;
  case 'X': target=Serial.parseInt(); if (!homing) if (0<=target && target<90000)target1 = target ; clearMem(300);Serial.print(abs(target1-encoder0Pos)<inPostionLimit);Serial.print(",");Serial.print(encoder0Pos);Serial.print(",");Serial.print(target1);Serial.print(",");Serial.println(digitalRead (enableRunPin));break;
  //case 'T': auto1 = !auto1; break;
  //case 'A': auto2 = !auto2; break;
  case 'Q': Serial.print(kp); Serial.print(","); Serial.print(ki); Serial.print(",");  Serial.println(kd); break;
  //case 'H': help(); break;
  case 'W': writetoEEPROM(); break;
  case 'K': eedump(); break;
  case 'R': recoverPIDfromEEPROM() ; break;
  case 'S': for(int i=0; i<p; i++) Serial.println(pos[i]); break;
  //case 'L': station = Serial.parseInt();target1= Stations[station-1]; clearMem(300); break;
  case 'B': Reset();break;
  case 'M': home_offset = Serial.parseFloat();homing=true;searching=true;break;
  case 'N': parsing = true;parselocations();break;
  case 'O': if (abs(target1-encoder0Pos)<inPostionLimit) pistonCmd = true;Serial.println ("Piston On");break;
  case 'J': pistonCmd = false ;Serial.println ("Piston Off");break;
  case '!': Serial.println ("ATC");break;
  case 'F': Serial.println (firmware);break;
  case 'T': Serial.println(digitalRead(homeSensor));break;
  case 'U': Serial.print(abs(target1-encoder0Pos)<inPostionLimit);Serial.print(",");Serial.print(encoder0Pos);Serial.print(",");Serial.print(target1);Serial.print(",");Serial.println(digitalRead (enableRunPin));break;
  case 'V': hspeed=Serial.parseInt(); break;
  case 'C': hoffsetspeed=Serial.parseInt(); break;
  case 'Z': rspeed=Serial.parseInt();myPID.SetOutputLimits(-rspeed,rspeed); break;

  //default : Serial.println ("not a command"); break;
 }
 if (!parsing)  while((Serial.read()!=10));parsing = false; // dump extra characters till LF is seen (you can use CRLF or just LF)
}
void parselocations()
{
  // Calculate based on max input size expected for one command
#define INPUT_SIZE 128
// ...
// n1=00000:2=08384:3=16768:4=25152:5=33536:6=41920:7=50304:8=58688
// locations defaullts

// Get next command from Serial (add 1 for final 0)
char input[INPUT_SIZE + 1];
byte size = Serial.readBytes(input, INPUT_SIZE);
// Add the final 0 to end the C string
input[size] = 0;
//Serial.println(size);
// Read each command pair 
char* command = strtok(input, ":");
while (command != 0)
{
    // Split the command in two values
    char* separator = strchr(command, '=');
    if (separator != 0)
    {
        // Actually split the string in 2: replace '=' with 0
        *separator = 0;
        int StationNo = atoi(command);
        ++separator;
        double location = round(atof(separator));
        Stations[StationNo-1]= location;
        // Do something with servoId and position
    }
    // Find the next command in input string
    command = strtok(0, ":");
}
//for(int i=0; i<maxStations; i++) {Serial.println(Stations[i]);}; 

}
void Homing(int HomeOffset)
{
 int home_read = 1;
 if(searching) {
  myPID.SetMode(MANUAL);
  myPID.ComputeM(-hspeed);
  if (home_invert_read) home_read = !digitalRead(homeSensor) ; else home_read= digitalRead(homeSensor);
  if (home_read == HIGH) {myPID.ComputeM(0);delay(1000);searching=false;searching1=true;encoder0Pos=0;target1 = HomeOffset;}}
 if (searching1){
    
    myPID.SetMode(AUTOMATIC);myPID.Compute();
    myPID.SetOutputLimits(-hoffsetspeed,hoffsetspeed);
    
    if (abs(target1-encoder0Pos)<inPostionLimit) {
      Reset();myPID.SetOutputLimits(-rspeed,rspeed);searching1=false;homing=false;}
    }
}
void clearMem(int bytes){p=0; counting=true;for(int i=0; i<bytes; i++) pos[i]=0;}
void Reset() {encoder0Pos=0;target1=0;}
void printPos() {
  Serial.print(F("Position=")); Serial.print(encoder0Pos); Serial.print(F(" PID_output=")); Serial.print(output); Serial.print(F(" Target=")); Serial.println(setpoint);
}
/*
void help() {
 Serial.println(F("\nPID DC motor controller and stepper interface emulator"));
 Serial.println(F("by misan"));
 Serial.println(F("Available serial commands: (lines end with CRLF or LF)"));
 Serial.println(F("P123.34 sets proportional term to 123.34"));
 Serial.println(F("I123.34 sets integral term to 123.34"));
 Serial.println(F("D123.34 sets derivative term to 123.34"));
 Serial.println(F("? prints out current encoder, output and setpoint values"));
 Serial.println(F("X123 sets the target destination for the motor to 123 encoder pulses"));
 Serial.println(F("T will start a sequence of random destinations (between 0 and 2000) every 3 seconds. T again will disable that"));
 Serial.println(F("Q will print out the current values of P, I and D parameters")); 
 Serial.println(F("W will store current values of P, I and D parameters into EEPROM")); 
 Serial.println(F("H will print this help message again")); 
 Serial.println(F("A will toggle on/off showing regulator status every second\n")); 
}
*/
void writetoEEPROM() { // keep PID set values in EEPROM so they are kept when arduino goes off
  eeput(kp,0);
  eeput(ki,4);
  eeput(kd,8);
  double cks=0;
  for(int i=0; i<12; i++) cks+=EEPROM.read(i);
  eeput(cks,12);
  Serial.println("\nPID values stored to EEPROM");
  //Serial.println(cks);
}

void recoverPIDfromEEPROM() {
  double cks=0;
  double cksEE;
  for(int i=0; i<12; i++) cks+=EEPROM.read(i);
  cksEE=eeget(12);
  //Serial.println(cks);
  if(cks==cksEE) {
    Serial.println(F("*** Found PID values on EEPROM"));
    kp=eeget(0);
    ki=eeget(4);
    kd=eeget(8);
    myPID.SetTunings(kp,ki,kd); 
  }
  else Serial.println(F("*** Bad checksum"));
}

void eeput(double value, int dir) { // Snow Leopard keeps me grounded to 1.0.6 Arduino, so I have to do this :-(
  char * addr = (char * ) &value;
  for(int i=dir; i<dir+4; i++)  EEPROM.write(i,addr[i-dir]);
}

double eeget(int dir) { // Snow Leopard keeps me grounded to 1.0.6 Arduino, so I have to do this :-(
  double value;
  char * addr = (char * ) &value;
  for(int i=dir; i<dir+4; i++) addr[i-dir]=EEPROM.read(i);
  return value;
}

void eedump() {
 for(int i=0; i<16; i++) { Serial.print(EEPROM.read(i),HEX); Serial.print(" "); }Serial.println(); 
}
