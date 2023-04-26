#pragma once

#include <stdnoreturn.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#include "slice.h"

typedef enum {integer, boolean, string, array, thread, empty} variable_type;

struct data_type
{
    variable_type curr_data_type;
    char* ifString;
    uint64_t isInt;
    bool isBool;
    struct data_type **isArray;
    pthread_t thread_id;
};

// Hashmap entries that store variables as slices w/ their associated value
struct Pair
{
    struct Slice key;
    struct data_type value;
};
