#include<Wire.h>

#define ABS(x) ((x > 0) ? (x) : -(x))

//!!! Approx slope of accelerometer graph = change / x, x = .70 -> .73, .71 preferrable  (slope as per plotter) Baudrate = 115200

int16_t final_res(uint8_t a, uint8_t a1) {
  return (a >> 7) ? ~(((int16_t)a << 8) | (uint16_t)a1) : (((int16_t)a << 8) | (uint16_t)a1);
}

unsigned long tap_start[2][3];
unsigned long last_tap_time[2][3];
unsigned long last_spike[2][3];

bool tap_sink[2][3];
bool tsk[2][3];
bool flat_spike[2][3];

float last_change[2][3];
float abs_last_change[2][3];
float last_z[2][3];
float spike_max[2][3]; 
float before_spike[2][3];
float abs_last_slope_change[2][3];
float inf_avg[2][3];

void reset_records(unsigned long rec[][3]) {
  for(int i = 0; i < 2; i++) 
    for(int j = 0; j < 3; j++) 
      rec[i][j] = 0;
};

void reset_records(bool rec[][3]) {
  for(int i = 0; i < 2; i++) 
    for(int j = 0; j < 3; j++) 
      rec[i][j] = false;
};

void reset_records(float rec[][3]) {
  for(int i = 0; i < 2; i++) 
    for(int j = 0; j < 3; j++) 
      rec[i][j] = 0.0;
};

struct tap_info {
  unsigned long time[10];
  int len = 0;
  float intensity;
};

struct tap_reg {
  tap_info left[3];
  tap_info right[3];
} reg;

float arr_sum(float* arr, size_t size) {
  float sum = 0;
  for(int i = 0; i < size; i++) sum += *(arr+i);
  return sum;
}

void get_accel_raw(float* accel, uint8_t address) {
  Wire.beginTransmission(address);
  Wire.write(0x3B);
  Wire.endTransmission(false);
  Wire.requestFrom(address, 6, true);
  accel[0] = final_res(Wire.read(), Wire.read()) * 10/ 8192.00;
  accel[1] = final_res(Wire.read(), Wire.read()) * 10/ 8192.00;
  accel[2] = final_res(Wire.read(), Wire.read()) * 10/ 8192.00;
};

void tap_callback(uint8_t axis, int ind, float intensity) {
  Serial.printf("tap axis: %d  intensity: %f\n", axis, intensity);
}

void detect_tap(uint8_t axis, int ind, float value) {
  
  float change = value - last_z[ind][axis];
  float abs_change = ABS(change);

  float abs_slope_change = ABS(abs_change - abs_last_change[ind][axis]) / .71;
  
  float d2 = ABS(abs_slope_change - abs_last_slope_change[ind][axis]);

  if(tap_start[ind][axis]) {
    if(tap_sink[ind][axis]) {
      if(value < spike_max[ind][axis]) spike_max[ind][axis] = value;
    } else {
      if(value > spike_max[ind][axis]) spike_max[ind][axis] = value;
    }
  }

  if(tap_start[ind][axis] && millis() - tap_start[ind][axis] > 300) { //500 -> 200 -> 50 
      tap_start[ind][axis] = 0;
      spike_max[ind][axis] = 0;
      before_spike[ind][axis] = 0; 
      tsk[ind][axis] = false;
  }

  if(tsk[ind][axis]) {
    if(tap_sink[ind][axis]) {
      if(value < last_z[ind][axis] && value > spike_max[ind][axis] && false) {
          tap_start[ind][axis] = 0;
          last_tap_time[ind][axis] = millis();
          spike_max[ind][axis] = 0;
          before_spike[ind][axis] = 0;
          tsk[ind][axis] = false;
      } else if (value >= before_spike[ind][axis] || ABS(value - before_spike[ind][axis]) < 0.300) { //0.200 -> 0.300
        if(ind) {
          reg.right[axis].time[reg.right[axis].len++] = millis();
          reg.right[axis].intensity = ABS(spike_max[ind][axis] - before_spike[ind][axis]);
        } else {
          reg.left[axis].time[reg.left[axis].len++] = millis();
          reg.left[axis].intensity = ABS(spike_max[ind][axis] - before_spike[ind][axis]);
        };
        tap_start[ind][axis] = 0;
        last_tap_time[ind][axis] = millis();
        spike_max[ind][axis] = 0;
        before_spike[ind][axis] = 0;
        tsk[ind][axis] = false;
      }
    } else {
      if(value > last_z[ind][axis] && value < spike_max[ind][axis] && false) {
        tap_start[ind][axis] = 0;
        last_tap_time[ind][axis] = millis();
        spike_max[ind][axis] = 0;
        tsk[ind][axis] = false;
        before_spike[ind][axis] = 0;
      } else if (value <= before_spike[ind][axis] || ABS(value - before_spike[ind][axis]) < 0.300) {
        if(ind) {
          reg.right[axis].time[reg.right[axis].len++] = millis();
          reg.right[axis].intensity = ABS(spike_max[ind][axis] - before_spike[ind][axis]);
        } else {
          reg.left[axis].time[reg.left[axis].len++] = millis();
          reg.left[axis].intensity = ABS(spike_max[ind][axis] - before_spike[ind][axis]);
        };
        tap_start[ind][axis] = 0;
        last_tap_time[ind][axis] = millis();
        spike_max[ind][axis] = 0;
        before_spike[ind][axis] = 0;
        tsk[ind][axis] = false;
      }
    }
  }
  if(flat_spike[ind][axis]) {
    if(tap_sink[ind][axis]) {
      if(value < last_z[ind][axis]) {
        tap_start[ind][axis] = 0;
        last_tap_time[ind][axis] = millis();
        spike_max[ind][axis] = 0;
        before_spike[ind][axis] = 0;
        last_spike[ind][axis] = millis();
        tsk[ind][axis] = false;
        flat_spike[ind][axis] = false;
      }
    } else {
      if(value > last_z[ind][axis]) {
        tap_start[ind][axis] = 0;
        last_tap_time[ind][axis] = millis();
        spike_max[ind][axis] = 0;
        before_spike[ind][axis] = 0;
        last_spike[ind][axis] = millis();
        tsk[ind][axis] = false;
        flat_spike[ind][axis] = false;
      }    
    }
  }

  if(tap_start[ind][axis] && abs_slope_change > 0.20) { //abs_slope_change before was 0.45
    if(ABS(spike_max[ind][axis] - before_spike[ind][axis]) > 2.3) { //before was 2.1
      tap_start[ind][axis] = 0;
      last_tap_time[ind][axis] = millis();
      spike_max[ind][axis] = 0;
      before_spike[ind][axis] = 0;
      last_spike[ind][axis] = millis();
      tsk[ind][axis] = false;
    }
    else if(((tap_sink[ind][axis] && change > 0 /*&& value >= before_spike[ind][axis]*/) || (!tap_sink[ind][axis] && change < 0 /*&& value <= before_spike[ind][axis]*/))) {
      /*digitalWrite(2, 0);
      delay(1000);
      digitalWrite(2, 1);*/
      tsk[ind][axis] = true;
    }
  }
//.5 , .8
  if((abs_slope_change > .20 && abs_slope_change < .5 && abs_change < .75) || (abs_slope_change > 1.3 && d2 > 1 && abs_change) && !tap_start[ind][axis]) {
    if((millis() - last_tap_time[ind][axis] > 500 || 1) && (millis() - last_spike[ind][axis]) > 700) {
      if (change > 0) {
        tap_sink[ind][axis] = false;
      }
      else { 
        tap_sink[ind][axis] = true;
      };
      spike_max[ind][axis] = value;
      before_spike[ind][axis] = last_z[ind][axis];
      tap_start[ind][axis] = millis();
      last_tap_time[ind][axis] = millis();
      tsk[ind][axis] = false;
      flat_spike[ind][axis] = false;
      if(abs_slope_change < .61) {
        //flat_spike[ind][axis] = true;
      }
    } 
  }

  last_z[ind][axis] = value;
  last_change[ind][axis] = change;
  abs_last_change[ind][axis] = abs_change;
  abs_last_slope_change[ind][axis] = abs_slope_change;
  //Serial.printf("%f %f %f\n", *accel, accel[1], accel[2]);
}

void triple_axis_tap_detect() {
  float accel[2][3];
  get_accel_raw(accel[0], 0x68);
  //get_accel_raw(accel[1], 0x69);
  
  detect_tap(2, 0, accel[0][2]);
  //detect_tap(2, 1, accel[1][2]);

  detect_tap(0, 0, accel[0][0]);
  //detect_tap(0, 1, accel[1][0]);

  //Serial.printf("%f %f %f\n", accel[0][0], accel[0][1], accel[0][2]);


  if(reg.left[2].len) {
    //tap_callback(2, 0, reg.left[2].intensity);
    if(millis() - reg.left[2].time[0] >= 300 && !reg.right[2].len) {
      
    }
  } else if (reg.right[2].len) {
    if(millis() - reg.right[2].time[0] >= 300 && !reg.left[2].len) {
      tap_callback(2, 1, 0);
    }
  }

  if(reg.left[0].len) {
    //tap_callback(0, 0, reg.left[0].intensity);
  }
  if(reg.left[0].len && reg.left[2].len)
  Serial.printf("Final Tap: %s DIFF: %f\n\n", (reg.left[0].intensity > reg.left[2].intensity ? "X axis" : "Z axis"), ABS(reg.left[0].intensity - reg.left[2].intensity));

  for(int i = 0; i < reg.left[2].len; i++) {
    if(millis() - reg.left[2].time[i] >= 500) {
      reg.left[2].len--;
      reg.left[2].intensity = 0;
    };
  };
  
  for(int i = 0; i < reg.left[0].len; i++) {
    if(millis() - reg.left[0].time[i] >= 500) {
      reg.left[0].len--;
      reg.left[0].intensity = 0;
    };
  }
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
  Wire.endTransmission(true);
  Wire.beginTransmission(0x68);
  Wire.write(0x1C);
  Wire.write(0b00001000);
  Wire.endTransmission(true);

  reset_records(tap_start);
  reset_records(last_tap_time);
  reset_records(last_spike);
  reset_records(tap_sink);
  reset_records(tsk);
  reset_records(flat_spike);
  reset_records(last_change);
  reset_records(abs_last_change);
  reset_records(last_z);
  reset_records(spike_max);
  reset_records(before_spike);
  reset_records(abs_last_slope_change);

  float accel[2][3];

  for(int i = 0; i < 1; i++) {
    get_accel_raw(accel[i], !i ? 0x68 : 0x69);
    last_z[i][2] = accel[i][2];
  };

  for(int i = 0; i < 1; i++) {
    get_accel_raw(accel[i], !i ? 0x68 : 0x69);
    last_change[i][2] = accel[i][2] - last_z[i][2];
    last_z[i][2] = accel[i][2];
    abs_last_change[i][2] = ABS(last_change[i][2]);
  };
}

void loop() {
  triple_axis_tap_detect();
}