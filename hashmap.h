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

#include "pair.h"
// #include "slice.h"

#define MAP_SIZE 2

struct Interpreter
{
    char const * program;
    char const * current;
    struct Pair **variables; // Hashmap of variables within scope
    size_t HASHMAP_CURR_SIZE; // Current size of hashmap
    struct Interpreter *next;
};

// Helper method to free interpreter and all its contents from memory
void free_interpreter(struct Interpreter *_interpreter) {
    for (int i = 0; i < _interpreter->HASHMAP_CURR_SIZE; i++) {
	    free(_interpreter->variables[i]);
    }
    free(_interpreter->variables);
    free(_interpreter);
}

// Initialize all values in hash_table to null
void init_table(struct Interpreter *_interpreter) {

    _interpreter->HASHMAP_CURR_SIZE = MAP_SIZE;
    _interpreter->variables = malloc(sizeof(struct Pair) * MAP_SIZE);

    for (int i = 0; i < MAP_SIZE; i++) {
        _interpreter->variables[i] = NULL;
    }
}

// Constructor method for Interpreter struct
struct Interpreter *constructor1(char const *prog)
{
    struct Interpreter *_interpreter = (struct Interpreter *) malloc(sizeof(struct Interpreter));
    _interpreter->program = prog;
    _interpreter->current = prog;

    init_table(_interpreter); // Initialize hashmap

    return _interpreter;
}

struct Interpreter *constructor2(char const *prog, struct Interpreter *prev)
{
    struct Interpreter *_interpreter = (struct Interpreter *) malloc(sizeof(struct Interpreter));

    _interpreter->program = prog;
    _interpreter->current = prog;
    _interpreter->HASHMAP_CURR_SIZE = prev->HASHMAP_CURR_SIZE;
    _interpreter->variables = prev->variables;

    return _interpreter;
}

// Method used to resize hashmap if full
struct Pair **resize_variable_map(struct Interpreter *_interpreter) {
    size_t HASHMAP_CURR_SIZE = _interpreter->HASHMAP_CURR_SIZE * 2;
    struct Pair **hash_table = _interpreter->variables;

    // Double size of hashmap
    struct Pair **resizePair = malloc(sizeof(struct Pair) * HASHMAP_CURR_SIZE);

    for (int i = 0; i < HASHMAP_CURR_SIZE; i++) {
        resizePair[i] = NULL;
    }

    for (int i = 0; i < HASHMAP_CURR_SIZE / 2; i++) {
        struct Pair *entry = hash_table[i];
        size_t index = hash_function(entry->key) % (HASHMAP_CURR_SIZE);

	    // Rehash the entries in the previous hashtable and set them to new positions
        for (int j = index; j < index + HASHMAP_CURR_SIZE; j++)
        {
            // Find first value that is null, or w/ same key
            if (resizePair[j % HASHMAP_CURR_SIZE] == NULL)
            {
                resizePair[j % HASHMAP_CURR_SIZE] = entry;
                break;
            }
        }
    }

    _interpreter->HASHMAP_CURR_SIZE = HASHMAP_CURR_SIZE;

    free(_interpreter->variables);
    return resizePair;
}

uint64_t get_from_array(struct Slice key, struct Interpreter *_interpreter, size_t arrayIndex) {
    size_t HASHMAP_CURR_SIZE = _interpreter->HASHMAP_CURR_SIZE;
    
    // Get index to place pair w/ modulus
    size_t index = hash_function(key) % HASHMAP_CURR_SIZE;

    for (int i = index; i < index + HASHMAP_CURR_SIZE; i++)
    {
        // Find first value that is not null at index and is equal to key
        if (_interpreter->variables[i % HASHMAP_CURR_SIZE] != NULL && operator2(key, (_interpreter->variables[i % HASHMAP_CURR_SIZE]->key)))
        {
            return _interpreter->variables[i % HASHMAP_CURR_SIZE]->value.isArray[arrayIndex]->isInt;
        }
    }

    return 0;
}

void insert_into_array(struct Slice key, struct data_type value, struct Interpreter *_interpreter, size_t arrayIndex) {
    size_t HASHMAP_CURR_SIZE = _interpreter->HASHMAP_CURR_SIZE;
    
    // Get index to place pair w/ modulus
    size_t index = hash_function(key) % HASHMAP_CURR_SIZE;

    for (int i = index; i < index + HASHMAP_CURR_SIZE; i++)
    {
        // Find first value that is not null at index and is equal to key
        if (_interpreter->variables[i % HASHMAP_CURR_SIZE] != NULL && operator2(key, (_interpreter->variables[i % HASHMAP_CURR_SIZE]->key)))
        {
            _interpreter->variables[i % HASHMAP_CURR_SIZE]->value.isArray[arrayIndex]->isInt = value.isInt;
            return;
        }
    }
}

void insert_pair(struct Slice key, struct data_type value, struct Interpreter *_interpreter)
{
    size_t HASHMAP_CURR_SIZE = _interpreter->HASHMAP_CURR_SIZE;

    // Get index to place pair w/ modulus
    int index = hash_function(key) % HASHMAP_CURR_SIZE;

    for (int i = index; i < index + HASHMAP_CURR_SIZE; i++)
    {
        // Find first value that is null, or w/ same key
        if (_interpreter->variables[i % HASHMAP_CURR_SIZE] == NULL || operator2(key, (_interpreter->variables[i % HASHMAP_CURR_SIZE]->key)))
        {
            _interpreter->variables[i % HASHMAP_CURR_SIZE] = malloc(sizeof(struct Pair));
            _interpreter->variables[i % HASHMAP_CURR_SIZE]->key = key;
            _interpreter->variables[i % HASHMAP_CURR_SIZE]->value = value;

            return;
        }
    }

    _interpreter->variables = resize_variable_map(_interpreter);
    insert_pair(key, value, _interpreter);
}

struct data_type get_value(struct Slice key, struct Interpreter *_interpreter)
{
    size_t HASHMAP_CURR_SIZE = _interpreter->HASHMAP_CURR_SIZE;
    
    // Get index to place pair w/ modulus
    size_t index = hash_function(key) % HASHMAP_CURR_SIZE;
    for (int i = index; i < index + HASHMAP_CURR_SIZE; i++)
    {
        // Find first value that is not null at index and is equal to key
        if (_interpreter->variables[i % HASHMAP_CURR_SIZE] != NULL && operator2(key, (_interpreter->variables[i % HASHMAP_CURR_SIZE]->key)))
        {
            return _interpreter->variables[i % HASHMAP_CURR_SIZE]->value;
        }
    }

    // Default return value
    struct data_type default_return = {empty, '\0', 0, false};
    return default_return;
}

bool contains(struct Slice key, struct Interpreter *_interpreter)
{
    size_t HASHMAP_CURR_SIZE = _interpreter->HASHMAP_CURR_SIZE;
    struct Pair **hash_table = _interpreter->variables;

    // Get index to place pair w/ modulus
    size_t index = hash_function(key) % HASHMAP_CURR_SIZE;
    for (int i = index; i < index + HASHMAP_CURR_SIZE; i++)
    {
        // Find first value that is not null at index and is equal to key
        if (hash_table[i % HASHMAP_CURR_SIZE] != NULL && operator2(key, (hash_table[i % HASHMAP_CURR_SIZE]->key)))
        {
            return true;
        }
    }

    // Default return value
    return false;
}
