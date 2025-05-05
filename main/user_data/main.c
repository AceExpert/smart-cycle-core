#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "main.h"

const char* file_format = "user:\ngps_token:\nphone_token:\nspeaker_addr:\nphone_addr:\nforce_sense:400\nalarm:1\nalarm_sense:1\ng:10\nforce:1\n";
static int file_len = 107;

struct user_cache* cached = NULL;
size_t cached_len = 0;

void setup_user_info() {
    FILE* user_info = fopen("/sdcard/userinfo", "r");
    if(user_info == NULL) {
        user_info = fopen("/sdcard/userinfo", "w");
    } else {
        fseek(user_info, 0, SEEK_END);
        if(ftell(user_info) != 0) {
            fclose(user_info);
            return;
        } else {
            fclose(user_info);
            user_info = fopen("/sdcard/userinfo", "w");
        };
    }
    int written = 0;

    if(ftell(user_info) == 0) {

        while(written < file_len) {
            written += fwrite(file_format + written, 1, file_len - written, user_info);
        };

    }
    fclose(user_info);
}

void set_field(const char* field, const void* data, int data_len) {
    FILE* user_info = fopen("/sdcard/userinfo", "r+");
    fseek(user_info, 0, SEEK_END);
    long file_end = ftell(user_info);

    rewind(user_info);

    char rfield[20];
    int field_len = 0;
    while(1) {
        if(feof(user_info)) break;
        char c[1];
        do {
            if(fread(c, 1, 1, user_info)) {
                *(rfield + field_len) = *c;
                field_len++;
            } else {
                break;
            };
        } while (field_len && rfield[field_len - 1] != ':' && !feof(user_info));
        if(rfield[field_len - 1] == ':') {
            rfield[field_len - 1] = 0; 
        } else {
            break;
        }
        if(strcmp(rfield, field) == 0) {
            char c[1] = {0};
            long start_write = ftell(user_info);
            while(*c != '\n' && !feof(user_info)) {
                fread(c, 1, 1, user_info);
            }
            if(!feof(user_info)) {
                int current_seek = ftell(user_info);
                int copied_len = file_end - current_seek;
                rewind(user_info);
                char* copied_before = copy_content(user_info, start_write);
                fseek(user_info, current_seek, SEEK_SET);
                char* copied = copy_content(user_info, copied_len);
                fclose(user_info);
                user_info = fopen("/sdcard/userinfo", "w");
                fwrite(copied_before, 1, start_write, user_info);
                fwrite(data, 1, data_len, user_info);
                fwrite("\n", 1, 1, user_info);
                fwrite(copied, 1, copied_len, user_info);
                free(copied);
                free(copied_before);
                fclose(user_info);
                return;
            };
        } else {
            while(1) {
                char c[1] = {0};
                if(!fread(c, 1, 1, user_info)) {
                    break;
                } else {
                    if(*c == '\n') {
                        break;
                    }
                }
            }
        }
        field_len = 0;
    }
    fclose(user_info);
}

int get_field(const char* field, uint8_t** data) {
    FILE* user_info = fopen("/sdcard/userinfo", "r");

    char rfield[20];
    int field_len = 0;
    while(1) {
        if(feof(user_info)) break;
        char c[1];
        do {
            if(fread(c, 1, 1, user_info)) {
                *(rfield + field_len) = *c;
                field_len++;
            } else {
                break;
            };
        } while (field_len && rfield[field_len - 1] != ':' && !feof(user_info));
        if(rfield[field_len - 1] == ':') {
            rfield[field_len - 1] = 0; 
        } else {
            break;
        }
        if(strcmp(rfield, field) == 0) {
            char* read_data = malloc(0);
            int total_read = 0;
            char c[1] = {0};
            while(*c != '\n' && !feof(user_info)) {
                if(fread(c, 1, 1, user_info)) {
                    read_data = realloc(read_data, total_read + 1);
                    read_data[total_read++] = *c;
                } else {
                    break;
                };
            }
            *data = (uint8_t*)read_data;
            return total_read - 1;
        } else {
            while(1) {
                char c[1] = {0};
                if(!fread(c, 1, 1, user_info)) {    
                    break;
                } else {
                    if(*c == '\n') {
                        break;
                    }
                }
            }
        }
        field_len = 0;
    };
    fclose(user_info);
    return 0;
}

struct user_cache* get_all_field() {
    FILE* user_info = fopen("/sdcard/userinfo", "r");

    if(cached != NULL) free(cached);

    cached = malloc(0);
    cached_len = 0;
    
    char rfield[20];
    int field_len = 0;
    while(1) {
        if(feof(user_info)) break;
        char c[1];
        do {
            if(fread(c, 1, 1, user_info)) {
                *(rfield + field_len) = *c;
                field_len++;
            } else {
                break;
            };
        } while (field_len && rfield[field_len - 1] != ':' && !feof(user_info));
        if(rfield[field_len - 1] == ':') {
            rfield[field_len - 1] = 0; 
        } else {
            break;
        }

        cached = realloc(cached, (cached_len + 1) * sizeof(struct user_cache));
        cached[cached_len].name = malloc(field_len);
        memcpy(cached[cached_len].name, rfield, field_len);

        char* read_data = malloc(0);
        int total_read = 0;
        *c = 0;
        while(*c != '\n' && !feof(user_info)) {
            if(fread(c, 1, 1, user_info)) {
                read_data = realloc(read_data, total_read + 1);
                read_data[total_read++] = *c;
            } else {
                break;
            };
        }
        if(!total_read) {
            cached[cached_len++].value = NULL;
        } else {
            cached[cached_len++].value = read_data;
            read_data[total_read - 1] = 0;
        };
    
        field_len = 0;
    };
    fclose(user_info);
    return cached;
}

uint8_t update_field(const char* field, uint8_t* value, size_t value_len) {
    for(int i = 0; i < cached_len; i++) {
        if(strcmp(cached[i].name, field) == 0) {
            cached[i].value = realloc(cached[i].value, value_len + 1);
            memcpy(cached[i].value, value, value_len);
            cached[i].value[value_len] = 0;
            return 1;
        }
    }
    return 0;
}

void save_user_info() {
    FILE* user_info = fopen("/sdcard/userinfo", "r");
    FILE* user_info_back = fopen("/sdcard/userback", "w");

    char c[1];
    while (fread(c, 1, 1, user_info))
    {
        fwrite(c, 1, 1, user_info_back);
    }
    fclose(user_info_back);
    fclose(user_info);

    user_info = fopen("/sdcard/userinfo", "w");
    for(int i = 0; i < cached_len; i++) {
        fwrite(cached[i].name, 1, strlen(cached[i].name), user_info);
        fwrite(":", 1, 1, user_info);
        fwrite(cached[i].value, 1, strlen(cached[i].value), user_info);
        fwrite("\n", 1, 1, user_info);
    }
    fclose(user_info);
}

struct user_cache* get_user_cache() {
    return cached;
}

char* get_cache_field(const char* name) {
    for(int i = 0; i < cached_len; i++) {
        if(strcmp(cached[i].name, name) == 0) {
            return cached[i].value;
        }
    }
    return NULL;
}

int get_force_thresh() {
    char* value = get_cache_field("force_sense");
    int thresh = 0;
    sscanf(value, "%d", &thresh);
    return thresh;
}

float get_g() {
    char* value = get_cache_field("g");
    float g = 0;
    sscanf(value, "%f", &g);
    return g;
}

uint8_t get_bool(const char* field) {
    char* value = get_cache_field(field);
    int on = 0;
    sscanf(value, "%d", &on);
    return (uint8_t)on;
}

uint8_t get_alarm() {
    return get_bool("alarm");
}

uint8_t get_force_active() {
    return get_bool("force");
}

char* copy_content(FILE* file, int len) {
    char* data = malloc(len);
    int read = 0;

    while(read < len) {
        read += fread(data + read, 1, len - read, file);
    }

    return data;
}