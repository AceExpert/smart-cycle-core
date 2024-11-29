#include<Wire.h>
#include<time.h>

#define ABS(x) ((x > 0) ? (x) : -(x))

int16_t final_res(uint8_t a, uint8_t a1) {
  return (a >> 7) ? ~(((int16_t)a << 8) | (uint16_t)a1) : (((int16_t)a << 8) | (uint16_t)a1);
}

float z_rec[100]{0};
time_t tap_time = 0;

float arr_sum(float* arr, size_t size) {
  float sum = 0;
  for(int i = 0; i < size; i++) sum += *(arr+i);
  return sum;
}

void setup() {
  pinMode(2, 1);
  pinMode(16, 1);
  digitalWrite(2, 1);
  digitalWrite(16, 0);
  Serial.begin(115200);
  Serial.println("\n\nSTART\n");
  Wire.begin();
  Wire.beginTransmission(0x68);
  Wire.write(0x6B);
  Wire.write(0);
  //Wire.endTransmission(true);
  Wire.beginTransmission(0x68);
  Wire.write(0x1C);
  Wire.write(0b00010000);
  Wire.endTransmission(true);
}

void loop() {
  Wire.beginTransmission(0x68);
  Wire.write(0x3B);
  Wire.endTransmission(false);
  Wire.requestFrom(0x68, 6, true);
  float x_a = final_res(Wire.read(), Wire.read()) * 10/ 4096.00;
  float y_a = final_res(Wire.read(), Wire.read()) * 10/ 4096.00;
  float z_a = final_res(Wire.read(), Wire.read()) * 10/ 4096.00;
  /*if(tap_time && time(NULL) - tap_time >= 1) {
    digitalWrite(2, 1);
    tap_time = 0;
  };*/
  if(z_rec[0] <= 5) {
    z_rec[(int)(++(z_rec[0]))] = z_a;
  } else {
    float change = ABS((arr_sum(z_rec + 1, 5) / 5) - 10.5);
    if(change > 0.3 && change <= 1.0 /* && !tap_time*/ && 0) {
      digitalWrite(2, 0);
      //tap_time = time(NULL);
      delay(1000);
      digitalWrite(2, 1);
    }
    z_rec[0] = 0;
  }
  Serial.printf("%f %f %f\n", x_a, y_a, z_a);
}