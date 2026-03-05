#include <ArduinoBLE.h>
#define DUTY_CYCLE 127 //50% duty cycle (i/255)*100%
//Pin connected to vibration motor
const int motorPin = D0;
unsigned long motorStartTime = 0;
bool isVibrating = false;
bool lastMotorState = false;

// BLE Service and Characteristic
BLEService dogService("19B10000-E8F2-537E-4F6C-D104768A1214");
BLEByteCharacteristic alertCharacteristic("19B10001-E8F2-537E-4F6C-D104768A1214", BLERead | BLEWrite);

void setup() {
 
  //Set the type of signal at pin D0
  pinMode(motorPin, OUTPUT);
  digitalWrite(motorPin, LOW);

  //Set up Serial terminal
  Serial.begin(9600);
  // REMOVED: while (!Serial); 
  // This line prevents the code from running unless the Serial Monitor is open.

  //Halt program if BLE hardware fails
  if(!BLE.begin()) {
    while(1);
  }

  BLE.setLocalName("Dog Device Receiver");
  BLE.setAdvertisedService(dogService);
  dogService.addCharacteristic(alertCharacteristic);
  BLE.addService(dogService);
  BLE.advertise();

}

void loop() {
  BLEDevice humanDevice = BLE.central();

  if(humanDevice) {
    while(humanDevice.connected()) {
      //PART 1: Receive - sees if the central device has sent a message
      if (alertCharacteristic.written()) {
          if (alertCharacteristic.value() > 0) {
            analogWrite(motorPin, DUTY_CYCLE); // Drive motor
            isVibrating = true;           // Flag set to true to trigger Timer and Debugging
            motorStartTime = millis();    // Save the start time
          }
        }
      //PART 2: Timer - ensure that vibration motor only lasts 10 seconds
      if(isVibrating && (millis() - motorStartTime >= 10000)){
        digitalWrite(motorPin, LOW); //shut off motor
        isVibrating = false; //turn off the flag and wait for new signal
        alertCharacteristic.writeValue(0); // Clear the characteristic
        }

      //PART 3: Debugging and Monitoring - prints the state of the motor whenever it changes
      if(isVibrating != lastMotorState){
        if(isVibrating){
          Serial.println("Motor On");
        }
        else{
          Serial.println("Motor Off");
        }
        lastMotorState = isVibrating;
      }

      }
    }
}