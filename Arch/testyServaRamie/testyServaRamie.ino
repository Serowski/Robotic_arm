#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>

Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver();

#define SERVOMIN  150 // This is the 'minimum' pulse length count (out of 4096)
#define SERVOMAX  600 // This is the 'maximum' pulse length count (out of 4096)
#define USMIN  600 // This is the rounded 'minimum' microsecond length based on the minimum pulse of 150
#define USMAX  2400 // This is the rounded 'maximum' microsecond length based on the maximum pulse of 600
#define SERVO_FREQ 50 // Analog servos run at ~50 Hz updates

#define SERVO_0 0
#define SERVO_1 1
#define SERVO_2 2

#define HOME_ANG 0

uint8_t rev_vel = 1; //the lower the faster servo turns
uint8_t angle = 0;
uint16_t prev_angle = 0;
uint8_t servo_num = 0;
uint16_t microsec;

void setup() {
  Serial.begin(9600);
  pwm.begin();
  pwm.setOscillatorFrequency(27000000);
  pwm.setPWMFreq(SERVO_FREQ);  // Analog servos run at ~50 Hz updates
  home();
  delay(10);
}

void move_servo(uint8_t servo_number, uint8_t angle){
  
  if(angle != 0){
    microsec = map(angle, 0, 180, USMIN, USMAX);
    if(microsec > prev_angle){
      for( int counter = prev_angle*rev_vel; counter <= microsec*rev_vel; counter++){
        pwm.writeMicroseconds(servo_number, counter/rev_vel);
        pwm.writeMicroseconds(servo_number+1, counter/rev_vel);
        pwm.writeMicroseconds(servo_number+2, counter/rev_vel);
        //delay(5);
      }
    }
    if(microsec < prev_angle){
      for( int counter = prev_angle*rev_vel; counter >= microsec*rev_vel;counter--){
        pwm.writeMicroseconds(servo_number, counter/rev_vel);
        pwm.writeMicroseconds(servo_number+1, counter/rev_vel);
        pwm.writeMicroseconds(servo_number+2, counter/rev_vel);
        //delay(5);
      }
    }
    prev_angle = microsec;
    Serial.println(angle);
    //Serial.println(microsec);
  }
}

void home(){
  pwm.writeMicroseconds(servo_num, (USMIN+USMAX)/2);
  for(int counter = rev_vel*(USMIN+USMAX)/2; counter >= USMIN*rev_vel; counter--){
    pwm.writeMicroseconds(servo_num, counter/rev_vel);
    pwm.writeMicroseconds(servo_num+1, counter/rev_vel);
    pwm.writeMicroseconds(servo_num+2, counter/rev_vel);
  }
}

void loop() {

  if(Serial.available() > 0){
    angle = Serial.parseInt();
    //move_servo(SERVO_0, angle);
    move_servo(SERVO_0, angle);
  }
  delay(200);
}