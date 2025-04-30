#include<stdio.h>

struct user_cache {
    char* name;
    char* value;
};

char* copy_content(FILE* file, int len);
void setup_user_info();
void set_field(const char* field, const void* data, int data_len);
int get_field(const char* field, uint8_t** data);
uint8_t update_field(const char* field, uint8_t* value, size_t value_len);
void save_user_info();
struct user_cache* get_user_cache();
struct user_cache* get_all_field();
int get_force_thresh();
char* get_cache_field(const char* name);
uint8_t get_alarm();
uint8_t get_force_active();
float get_g();