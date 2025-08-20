#define dirPin 7
#define stepPin 8
#define stepsPerRevolution 200
#define maxSpeed 1100 //minimalny czas pomiędzy krokami w us

uint16_t speed = 0; //dodatkowy czas pomiędzy krokami w us
int prev_angle = 0;
int angle = 0;

void setup() {
  // Declare pins as output:
  Serial.begin(9600);
  pinMode(stepPin, OUTPUT);
  pinMode(dirPin, OUTPUT);
}

void move_stepper(int angle){
  uint8_t steps = round(abs(angle - prev_angle) / 1.8);
  float real_steps = abs(angle - prev_angle) / 1.8;
  if ((angle - prev_angle) < 0){
    digitalWrite(dirPin, LOW);
  }
  else{
    digitalWrite(dirPin, HIGH);
  }
  for(int i = 0; i < steps; i++){
    digitalWrite(stepPin, HIGH);
    delayMicroseconds(maxSpeed + speed);
    digitalWrite(stepPin, LOW);
    delayMicroseconds(maxSpeed + speed);
  }
  prev_angle = angle;
  Serial.println(steps);
  Serial.println(real_steps);
  
}


void loop() {
  // Set the spinning direction clockwise:
  if(Serial.available() > 0){
    angle = Serial.parseInt();
   
    //move_servo(SERVO_0, angle);
    if(angle != 0){
      move_stepper(angle);
    }
  }
  delay(200);
}


/*    SZYBKI OBRÓT
  // Set the spinning direction counterclockwise:
  digitalWrite(dirPin, LOW);

  // Spin the stepper motor 1 revolution quickly:
  for (int i = 0; i < stepsPerRevolution; i++) {
    // These four lines result in 1 step:
    digitalWrite(stepPin, HIGH);
    delayMicroseconds(1000);
    digitalWrite(stepPin, LOW);
    delayMicroseconds(1000);
  }

  //delay(1000);

  // Set the spinning direction clockwise:
  //digitalWrite(dirPin, HIGH);
*/