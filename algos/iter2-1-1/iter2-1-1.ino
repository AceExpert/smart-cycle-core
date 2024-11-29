#include<Wire.h>

#define ABS(x) ((x > 0) ? (x) : -(x))

//!!! Approx slope of accelerometer graph = change / x, x = .70 -> .73, .71 preferrable  (slope as per plotter) Baudrate = 115200

int16_t final_res(uint8_t a, uint8_t a1) {
  return (a >> 7) ? ~(((int16_t)a << 8) | (uint16_t)a1) : (((int16_t)a << 8) | (uint16_t)a1);
}

unsigned long tap_start = 0;
unsigned long last_tap_time = 0;
unsigned long last_spike = 0;

bool tap_sink = false;
bool tsk = false;
bool flat_spike = false;

float last_change = 0;
float abs_last_change = 0;
float last_z = 0;
float spike_max = 0; 
float before_spike = 0;

float arr_sum(float* arr, size_t size) {
  float sum = 0;
  for(int i = 0; i < size; i++) sum += *(arr+i);
  return sum;
}

void get_accel_raw(float* accel) {
  Wire.beginTransmission(0x68);
  Wire.write(0x3B);
  Wire.endTransmission(false);
  Wire.requestFrom(0x68, 6, true);
  accel[0] = final_res(Wire.read(), Wire.read()) * 10/ 4096.00;
  accel[1] = final_res(Wire.read(), Wire.read()) * 10/ 4096.00;
  accel[2] = final_res(Wire.read(), Wire.read()) * 10/ 4096.00;
};

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
  Wire.endTransmission(true);
  Wire.beginTransmission(0x68);
  Wire.write(0x1C);
  Wire.write(0b00010000);
  Wire.endTransmission(true);
  float accel[3];
  get_accel_raw(accel);
  last_z = accel[2];
  get_accel_raw(accel);
  last_change = accel[2] - last_z;
  last_z = accel[2];
  abs_last_change = ABS(last_change);
}

void loop() {
  float accel[3];
  get_accel_raw(accel);
  float change = accel[2] - last_z;
  float abs_change = ABS(change);

  float abs_slope_change = ABS(abs_change - abs_last_change) / .71;

  if(tap_start) {
    if(tap_sink) {
      if(accel[2] < spike_max) spike_max = accel[2];
    } else {
      if(accel[2] > spike_max) spike_max = accel[2];
    }
  }

  if(tap_start && millis() - tap_start > 50) { //500 -> 200 -> 50 
      tap_start = 0;
      spike_max = 0;
      before_spike = 0; 
      tsk = false;
  }

  if(tsk) {
    if(tap_sink) {
      if(accel[2] < last_z && accel[2] > spike_max) {
          tap_start = 0;
          last_tap_time = millis();
          spike_max = 0;
          before_spike = 0;
          tsk = false;
      } else if (accel[2] >= before_spike || ABS(accel[2] - before_spike) < 0.201) {
        digitalWrite(2, 0);
        delay(1000);
        digitalWrite(2, 1);
            tap_start = 0;
            last_tap_time = millis();
            spike_max = 0;
            before_spike = 0;
            tsk = false;
      }
    } else {
      if(accel[2] > last_z && accel[2] < spike_max) {
        tap_start = 0;
        last_tap_time = millis();
        spike_max = 0;
        tsk = false;
        before_spike = 0;
      } else if (accel[2] <= before_spike || ABS(accel[2] - before_spike) < 0.201) {
        digitalWrite(2, 0);
        delay(1000);
        digitalWrite(2, 1);
        tap_start = 0;
        last_tap_time = millis();
        spike_max = 0;
        before_spike = 0;
        tsk = false;
      }
    }
  }
  if(flat_spike) {
    if(tap_sink) {
      if(accel[2] < last_z) {
        tap_start = 0;
        last_tap_time = millis();
        spike_max = 0;
        before_spike = 0;
        last_spike = millis();
        tsk = false;
        flat_spike = false;
      }
    } else {
      if(accel[2] > last_z) {
        tap_start = 0;
        last_tap_time = millis();
        spike_max = 0;
        before_spike = 0;
        last_spike = millis();
        tsk = false;
        flat_spike = false;
      }    
    }
  }

  if(tap_start && abs_slope_change > 0.2) { //abs_slope_change before was 0.45
    if(ABS(spike_max - before_spike) > 1.0) { //before was 2.1
      tap_start = 0;
      last_tap_time = millis();
      spike_max = 0;
      before_spike = 0;
      last_spike = millis();
      tsk = false;
    }
    else if(((tap_sink && change > 0 /*&& accel[2] >= before_spike*/) || (!tap_sink && change < 0 /*&& accel[2] <= before_spike*/))) {
      /*digitalWrite(2, 0);
      delay(1000);
      digitalWrite(2, 1);*/
      tsk = true;
    }
  }

  if(abs_slope_change > .2 && !tap_start) {
    if((millis() - last_tap_time > 500 || 1) && millis() - last_spike > 500) {
      if (change > 0) {
        tap_sink = false;
      }
      else { 
        tap_sink = true;
      };
      spike_max = accel[2];
      before_spike = last_z;
      tap_start = millis();
      last_tap_time = millis();
      tsk = false;
      flat_spike = false;
      if(abs_slope_change < .61) {
        //flat_spike = true;
      }
    } 
  }

  last_z = accel[2];
  last_change = change;
  abs_last_change = abs_change;
  Serial.printf("%f %f %f\n", *accel, accel[1], accel[2]);
}