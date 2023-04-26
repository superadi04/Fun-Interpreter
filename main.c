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
#include "hashmap.h"
#include "function.h"

#define MAP_SIZE 2 // Initial size for map

// Optional int struct for null return
struct optional_int
{
    bool present;
    uint64_t value;
};

// Optional slice struct for null return
struct optional_slice
{
    bool present;
    struct Slice value;
};

struct Queue * readyQ;

struct Interpreter* global_interpreter; // Interpreter for global scope

bool consumeBracket(const char *str, struct Interpreter *_interpreter);

void statements(bool effects, struct Interpreter *_interpreter);

uint64_t expression(bool effects, struct Interpreter *_interpreter);

int numDigits(uint64_t num);

bool statement(bool effects, struct Interpreter *_interpreter);

uint64_t runFunction(bool effects, struct Interpreter *_interpreter, const char *name);

char *clearUntilClosingParen(struct Interpreter *_interpreter, size_t count);

struct optional_int parseWhileFunction(bool effects, struct Interpreter *_interpreter);

struct optional_int parseIfElseFunction(bool effects, struct Interpreter *_interpreter);

struct optional_int functionStatement(bool effects, struct Interpreter *_interpreter);

bool consumeFunction(const char *str, struct Interpreter *_interpreter);

// Terminate program
noreturn void fail(struct Interpreter *_interpreter)
{
    char const *current = _interpreter->current;
    char const *program = _interpreter->program;

    printf("failed at offset %ld\n", (size_t)(current - program));
    printf("%s\n", current);
    exit(1);
}

void end_or_fail(struct Interpreter *_interpreter)
{
    char const *current = _interpreter->current;

    while (isspace(*current))
    {
        current += 1;
    }

    if (*current != 0)
    {
        fail(_interpreter);
    }
}

// Remove whitespace in String
void skip(struct Interpreter *_interpreter)
{
    char const *current = _interpreter->current;

    while (isspace(*current))
    {
        current += 1;
    }

    // Update value of current
    _interpreter->current = current;
}

// Parse the current line as a String
bool consume(const char *str, struct Interpreter *_interpreter)
{
    skip(_interpreter);

    char const *current = _interpreter->current;

    size_t i = 0;
    while (true)
    {
        char const expected = str[i];
        char const found = current[i];

        if (expected == 0)
        {
            /* survived to the end of the expected string */
            current += i;
            _interpreter->current = current;
            return true;
        }
        if (expected != found)
        {
            return false;
        }
        // assertion: found != 0
        i += 1;
    }
}

// Return the name of the variable (identifier) as a struct
struct optional_slice consume_identifier(struct Interpreter *_interpreter)
{
    skip(_interpreter);

    char const *current = _interpreter->current;

    struct optional_slice v;
    v.present = false;

    if (isalpha(*current)) // Check if current character is alphabetic
    {
        char const *start = current;
        do
        {
            current += 1;
        } while (isalnum(*current)); // Check if current character is alphanumeric

        struct Slice _slice = new_slice1(start, (size_t)(current - start));
        v.present = true;
        v.value = _slice;
        // Update value of current
        _interpreter->current = current;
    }
    return v;
}

// Return the integer value of a variable (literal) as a pointer
struct optional_int consume_literal(struct Interpreter *_interpreter)
{
    skip(_interpreter);

    struct optional_int ans;
    ans.value = 0;
    ans.present = false;

    char const *current = _interpreter->current;

    if (isdigit(*current))
    {
        uint64_t v = 0; // Value of literal
        do
        {
            v = 10 * v + ((*current) - '0');
            current += 1;
        } while (isdigit(*current));

        // Update value of current
        _interpreter->current = current;
        ans.present = true;
        ans.value = v;
    }
    return ans;
}

// Check if 2 strings are equal
bool checkEqualString(char const *a, char *b, size_t len)
{
    while (*b != 0 && *a != 0)
    {
        if (*a != *b)
        {
            return false;
        }
        a++;
        b++;
    }

    return *b == 0 && *a == 0; // Check if both strings are same length
}

void printString(bool effects, struct Interpreter *_interpreter) {
    while (true) {
        if (consume("\"", _interpreter)) {              
            while (*_interpreter->current != '\"') {
                printf("%c", *_interpreter->current);
                _interpreter->current++;
            }
            _interpreter->current++;
        } else {
            char *ans2 = malloc(0);
            size_t maxSize2 = 0;
            size_t i2 = 0;

            if (consume("(", _interpreter)) {
                size_t paren_count = 1;

                while (true) {
                    if (i2 + 1 >= maxSize2) {
                        maxSize2 = maxSize2 * 2 + 1;
                        ans2 = realloc(ans2, sizeof(char) * maxSize2);
                    }

                    if (*_interpreter->current == ')') {
                        paren_count--;
                    } else if (*_interpreter->current == '(') {
                        paren_count++;
                    }

                    if (paren_count == 0) {
                        break;
                    }

                    ans2[i2] = *_interpreter->current;
                    _interpreter->current++;
                    i2++;
                }
                _interpreter->current++;

                struct Interpreter *stringInterpreter = constructor1(ans2);

                printf("%ld", expression(effects, stringInterpreter));
            } else {
                struct optional_slice testid = consume_identifier(_interpreter);

                if (!testid.present) {
                    printf("%ld\n", expression(effects, _interpreter));
                    return;
                }

                if (consume("[", _interpreter)) {
                    uint64_t arrayIndex = expression(effects, _interpreter);
                    consume("]", _interpreter);

                    if (contains(testid.value, _interpreter)) {
                        printf("%ld", get_from_array(testid.value, _interpreter, arrayIndex));
                    } else {
                        printf("%ld", get_from_array(testid.value, global_interpreter, arrayIndex));
                    }
                } else {
                    struct Slice id = testid.value;
                    char *char_id = malloc(id.len + 1);
                    strncpy(char_id, id.start, id.len);
                    char_id[id.len] = '\0';

                    if (contains_function(char_id) && consume("(", _interpreter)) {
                        printf("%ld", runFunction(effects, _interpreter, char_id));
                        consume(")", _interpreter);
                    } else if (contains(testid.value, _interpreter)) {
                        struct data_type returnVal = get_value(testid.value, _interpreter);

                        if (returnVal.curr_data_type == integer) {
                            printf("%ld", returnVal.isInt);
                        } else if (returnVal.curr_data_type == boolean) {
                            printf("%d", returnVal.isBool ? 1 : 0);
                        } else if (returnVal.curr_data_type == string) {
                            printf("%s", returnVal.ifString);
                        } else {
                            fail(_interpreter);
                        }
                    } else {
                        struct data_type returnVal = get_value(testid.value, global_interpreter);

                        if (returnVal.curr_data_type == integer) {
                            printf("%ld", returnVal.isInt);
                        } else if (returnVal.curr_data_type == boolean) {
                            printf("%d", returnVal.isBool ? 1 : 0);
                        } else if (returnVal.curr_data_type == string) {
                            printf("%s", returnVal.ifString);
                        } else {
                            fail(_interpreter);
                        }
                    }
                }
            }
        }
        if (!consume("+", _interpreter)) {
            break;
        }
    }
    printf("\n");
}

uint64_t e1(bool effects, struct Interpreter *_interpreter)
{
    // Get the identifier of the variable
    struct optional_slice testid = consume_identifier(_interpreter);

    if (testid.present) // Check if slice is returned
    {
        struct Slice id = testid.value;
        char *char_id = malloc(id.len + 1);
        strncpy(char_id, id.start, id.len);
        char_id[id.len] = '\0';

        if (consume("[", _interpreter)) {
            uint64_t arrayIndex = expression(effects, _interpreter);
            consume("]", _interpreter);

            if (contains(id, _interpreter)) {
                return get_from_array(id, _interpreter, arrayIndex);
            } else {
                return get_from_array(id, global_interpreter, arrayIndex);
            }
        }
	
	    // Check for function
        if (consume("(", _interpreter))
        {
	    // If it is a print function, print the expression and return 0
            if (checkEqualString(char_id, "print", id.len))
            {
                //printf("%lu\n", expression(effects, _interpreter));
                printString(effects, _interpreter);
                consume(")", _interpreter);
		        free(char_id);
                return 0;
            }

	    // If it is a function stored in our map run the function and return its output
            else if (contains_function(char_id))
            {
                uint64_t val = runFunction(effects, _interpreter, char_id);
		        free(char_id);
		        return val;
            }
            else
            {
                fail(_interpreter);
            }
        } else if (operator1("true", testid.value)) {
            return 1;
        } else if (operator1("false", testid.value)) {
            return 0;
        }

        // Get the value of the variable that is already stored in the map
        if (contains(id, _interpreter))
        {
            struct data_type returnVal = get_value(id, _interpreter);

            if (returnVal.curr_data_type == integer) {
                return returnVal.isInt;
            } else if (returnVal.curr_data_type == boolean) {
                return returnVal.isBool ? 1 : 0;
            } else {
                fail(_interpreter);
            }
        }
        else
        {
            struct data_type returnVal = get_value(id, global_interpreter);

            if (returnVal.curr_data_type == integer) {
                return returnVal.isInt;
            } else if (returnVal.curr_data_type == boolean) {
                return returnVal.isBool ? 1 : 0;
            } else {
                fail(_interpreter);
            }
        }
    }

    struct optional_int literal_ = consume_literal(_interpreter);

    // Get the value of the variable that is not already stored in the map
    if (literal_.present)
    {
        return literal_.value;
    }
    else if (consume("(", _interpreter)) // Check for parantheses
    {
        uint64_t v = expression(effects, _interpreter);
        consume(")", _interpreter);
        return v;
    }
    else
    {
        fail(_interpreter);
    }
}

// ++ -- unary+ unary- ... (Right)
uint64_t e2(bool effects, struct Interpreter *_interpreter)
{
    size_t count = 0;
    skip(_interpreter);
    char const *current = _interpreter->current;

    if (*current == '!')
    {
        // Count number of exclamation marks
        do
        {
            current += 1;
            count++;
        } while (*current == '!');

        // Update value of current
        _interpreter->current = current;
    }

    uint64_t v = e1(effects, _interpreter);

    // Check for divisibility of number of exclamation marks

    if (count > 0)
    {
        count %= 2;
        if (v)
        {
            if (count)
            {
                v = 0;
            }
            else
            {
                v = 1;
            }
        }
        else
        {
            if (count)
            {
                v = 1;
            }
            else
            {
                v = 0;
            }
        }
    }

    return v;
}

// * / % (Left)
uint64_t e3(bool effects, struct Interpreter *_interpreter)
{
    uint64_t v = e2(effects, _interpreter);

    while (true)
    {
	// Check for multiplication
        if (consume("*", _interpreter))
        {
            v = v * e2(effects, _interpreter);
        }

	// Check for division
        else if (consume("/", _interpreter))
        {
            uint64_t right = e2(effects, _interpreter);
            v = (right == 0) ? 0 : v / right;
        }

	// Check for modulus
        else if (consume("%", _interpreter))
        {
            uint64_t right = e2(effects, _interpreter);
            v = (right == 0) ? 0 : v % right;
        }
        else
        {
            return v;
        }
    }
}

// (Left) + -
uint64_t e4(bool effects, struct Interpreter *_interpreter)
{
    uint64_t v = e3(effects, _interpreter);

    while (true)
    {
	// Check for addition
        if (consume("+", _interpreter))
        {
            v = v + e3(effects, _interpreter);
        }
	
	// Check for subtraction
        else if (consume("-", _interpreter))
        {
            v = v - e3(effects, _interpreter);
        }
        else
        {
            return v;
        }
    }
}

// << >>
uint64_t e5(bool effects, struct Interpreter *_interpreter)
{
    return e4(effects, _interpreter);
}

// < <= > >=
uint64_t e6(bool effects, struct Interpreter *_interpreter)
{
    uint64_t v1 = e5(effects, _interpreter);

    while (true)
    {
	// Check if v1 < v2
        if (consume("<", _interpreter))
        {
	    // Check if v1 <= v2
            if (consume("=", _interpreter))
            {
                uint64_t v2 = e5(effects, _interpreter);

                if (v1 <= v2)
                {
                    v1 = 1;
                }
                else
                {
                    v1 = 0;
                }
            }
            else
            {
                uint64_t v2 = e5(effects, _interpreter);

                if (v1 < v2)
                {
                    v1 = 1;
                }
                else
                {
                    v1 = 0;
                }
            }
        }

	// Check if v1 > v2
        else if (consume(">", _interpreter))
        {
	    // Check if v1 >= v2
            if (consume("=", _interpreter))
            {
                uint64_t v2 = e5(effects, _interpreter);

                if (v1 >= v2)
                {
                    v1 = 1;
                }
                else
                {
                    v1 = 0;
                }
            }
            else
            {
                uint64_t v2 = e5(effects, _interpreter);

                if (v1 > v2)
                {
                    v1 = 1;
                }
                else
                {
                    v1 = 0;
                }
            }
        }
        else
        {
            return v1;
        }
    }
}

// == !=
uint64_t e7(bool effects, struct Interpreter *_interpreter)
{
    uint64_t v1 = e6(effects, _interpreter);

    while (true)
    {
	// Check if v1 == v2
        if (consume("==", _interpreter))
        {
            uint64_t v2 = e6(effects, _interpreter);

            if (v1 == v2)
            {
                v1 = 1;
            }
            else
            {
                v1 = 0;
            }
        }

	// Check if v1 != v2
        else if (consume("!=", _interpreter))
        {
            uint64_t v2 = e6(effects, _interpreter);

            if (v1 != v2)
            {
                v1 = 1;
            }
            else
            {
                v1 = 0;
            }
        }
        else
        {
            return v1;
        }
    }
}

// (left) &
uint64_t e8(bool effects, struct Interpreter *_interpreter)
{
    return e7(effects, _interpreter);
}

// ^
uint64_t e9(bool effects, struct Interpreter *_interpreter)
{
    return e8(effects, _interpreter);
}

// |
uint64_t e10(bool effects, struct Interpreter *_interpreter)
{
    return e9(effects, _interpreter);
}

// &&
uint64_t e11(bool effects, struct Interpreter *_interpreter)
{
    uint64_t v1 = e10(effects, _interpreter);

    while (true)
    {
	// Check if v1 && v2
        if (consume("&&", _interpreter))
        {
            uint64_t v2 = e10(effects, _interpreter);

            if (v1 != 0 && v2 != 0)
            {
                v1 = 1;
            }
            else
            {
                v1 = 0;
            }
        }
        else
        {
            return v1;
        }
    }
}

// ||
uint64_t e12(bool effects, struct Interpreter *_interpreter)
{
    uint64_t v1 = e11(effects, _interpreter);

    while (true)
    {
	// Check if v1 || v2
        if (consume("||", _interpreter))
        {
            uint64_t v2 = e11(effects, _interpreter);

            if (v1 != 0 || v2 != 0)
            {
                v1 = 1;
            }
            else
            {
                v1 = 0;
            }
        }
        else
        {
            return v1;
        }
    }
}

// (right with special treatment for middle expression) ?:
uint64_t e13(bool effects, struct Interpreter *_interpreter)
{
    return e12(effects, _interpreter);
}

// = += -= ...
uint64_t e14(bool effects, struct Interpreter *_interpreter)
{
    return e13(effects, _interpreter);
}

// ,
uint64_t e15(bool effects, struct Interpreter *_interpreter)
{
    return e14(effects, _interpreter);
}

// Parse the arithmetic expression recursively
uint64_t expression(bool effects, struct Interpreter *_interpreter)
{
    return e15(effects, _interpreter);
}

uint64_t runFunction(bool effects, struct Interpreter *_interpreter, const char *name)
{
    struct Function *func = get_function(name);
    struct optional_int ans;

    // Check if function exists in map
    if (func != NULL)
    {
	// Get all parameters of function
        char *allParameters = clearUntilClosingParen(_interpreter, 1);
        consume(")", _interpreter);

        struct Interpreter *func_interpreter = constructor1(func->code);

        size_t i = 0; // Current position in string
        size_t paramNum = 0; // Number of parameters

        while (allParameters[i])
        {
            size_t parens = 0; // Number of parentheses
            char *currString = ""; // Current parameter

	    // Delimit each parameter by commas
            while (allParameters[i] && (allParameters[i] != ',' || parens > 0))
            {
                char found = allParameters[i];

                if (found == '(')
                {
                    parens++;
                }
                else if (found == ')')
                {
                    parens--;
                }

                if (parens < 0)
                {
                    fail(_interpreter);
                }

		// Append 1 character to string
                size_t sz = strlen(currString);
                char *str = malloc(sz + 2);
                strcpy(str, currString);
                str[sz] = allParameters[i];
                str[sz + 1] = '\0';
                currString = str;

                i++;
            }

	    // Check for comma
            if (allParameters[i] == ',')
            {
                i++;
            }

	    // Check for whitespace
            while (allParameters[i] && allParameters[i] == '\040')
            {
                i++;
            }

            struct Interpreter *param_interpreter = constructor2(currString, _interpreter);
            
	    // Get value of current parameter
	        uint64_t value = expression(true, param_interpreter);
            struct data_type valueToInsert = {integer, "\0", value, false};

            char *param_name = func->params[paramNum];

	    // Add parameter to hashmap of parameters for function
            struct Slice key = new_slice1(param_name, strlen(param_name));
            insert_pair(key, valueToInsert, func_interpreter);

            paramNum++;
        }

	// Sanity check if number of parameters is expected
        if (paramNum != func->numParams)
        {
            fail(_interpreter);
        }

        char *codeCopy = malloc(strlen(func->code) + 1);
        strcpy(codeCopy, func->code);
        func_interpreter->current = codeCopy;
        func_interpreter->program = codeCopy;

	// Run function
        ans = functionStatement(true, func_interpreter);

	free_interpreter(func_interpreter);
    }
    else
    {
        fail(_interpreter);
    }

    // Check if function has return value
    if (ans.present)
    {
        return ans.value;
    }
    else
    {
        return 0;
    }
}

// This method skips through text until a closing bracket is reached, while noting for another parentheses in between
void clearUntilClosingBracket(struct Interpreter *_interpreter)
{
    size_t count = 1;

    char const *current = _interpreter->current;

    while (true)
    {
        if (*current == '{')
        {
            count++;
        }
        else if (*current == '}')
        {
            count--;
        }

        if (*current == '\0' || count < 0)
        {
            fail(_interpreter);
        }

        current += 1;

        if (count == 0)
        {
            break;
        }
    }

    _interpreter->current = current;
}

// Return text until closing bracket is found, account for other opening/closing brackets in between
char *getUntilClosingBracket(struct Interpreter *_interpreter, size_t count)
{
    // Skip through if else
    size_t i = 0;

    char const *current = _interpreter->current;

    char *currString = malloc(0);

    size_t maxSize = 0;

    while (true)
    {
        char const found = current[i];

        if (found == '{')
        {
            count++;
        }
        else if (found == '}')
        {
            count--;
        }

        if (found == '\0' || count < 0)
        {
            fail(_interpreter);
        }
        else if (count == 0) // Have we reached the final closing bracket
        {
            break;
        }

	// If we reach maximum size, reallocate more memory
        if (i + 1 >= maxSize)
        {
            maxSize = maxSize * 2 + 1;
            currString = realloc(currString, sizeof(char) * maxSize);
        }

        currString[i] = found;

        i++;
    }

    if (currString != NULL)
    {
        currString[i] = '\0';
    }

    current += (i + 1);
    _interpreter->current = current;

    return currString;
}

// This method returns text until a closing parenthesis is reached, accounting for other parens in between
char *clearUntilClosingParen(struct Interpreter *_interpreter, size_t count)
{
    skip(_interpreter);
    // Skip through if else
    size_t i = 0;

    char const *current = _interpreter->current;

    char *currString = malloc(0);

    size_t maxSize = 0;

    while (true)
    {
        char const found = current[i];

        if (found == '\0')
        {
            fail(_interpreter);
        }
        else if (found == '(')
        {
            count++;
        }
        else if (found == ')')
        {
            count--;
        }

	// Have we reached the final closing paren?
        if (count == 0)
        {
            break;
        }

	// If we reach max size, reallocate more memory for the string
        if (i + 1 >= maxSize)
        {
            maxSize = maxSize * 2 + 1;
            currString = realloc(currString, sizeof(char) * maxSize);
        }

        currString[i] = found;

        i++;
    }

    if (currString != NULL)
    {
        currString[i] = '\0';
    }

    current += i;
    _interpreter->current = current;

    return currString;
}

void parseFunction(bool effects, struct Interpreter *_interpreter)
{
    skip(_interpreter);

    // Get name of function
    struct optional_slice test_func_name = consume_identifier(_interpreter);

    if (!test_func_name.present) {
        fail(_interpreter);
    }

    struct Slice id = test_func_name.value;

    char *function_name = (char *)malloc(id.len + 1);
    strncpy(function_name, id.start, id.len);
    function_name[id.len] = '\0';

    consume("(", _interpreter);

    // Get parameters of function
    char *allParameters = clearUntilClosingParen(_interpreter, 1);
    consume(")", _interpreter);
    consume("{", _interpreter);

    char *buffer = strtok(allParameters, ",");
    char **parameters = malloc(0); // Store all parameters in char of pointers of pointeres
    size_t i = 0;

    // Delimit parameters with comma
    while (buffer)
    {
        parameters = realloc(parameters, (i + 1) * sizeof(char *));
        parameters[i] = malloc(strlen(buffer));
        strcpy(parameters[i], buffer);

        buffer = strtok(NULL, ",");
        while (buffer && *buffer == '\040')
        {
            buffer++;
        }
        i++;
    }

    // Code within function
    char *code = getUntilClosingBracket(_interpreter, 1);

    // Create function struct
    struct Function *func = (struct Function *)malloc(sizeof(struct Function));
    func->code = code;
    func->params = parameters;
    func->numParams = i;

    // Add function to function hashmap
    insert_function(function_name, func);
}

void parseWhile(bool effects, struct Interpreter *_interpreter)
{
    char const *current = _interpreter->current;

    while (true)
    {
        skip(_interpreter);

        uint64_t trueOrFalse = expression(effects, _interpreter); // Get the boolean condition

        consume(")", _interpreter);
        consume("{", _interpreter);

        if (trueOrFalse != 0) // If condition is true, run the code inside
        {
            statements(effects, _interpreter);
        }
        else // Condition is not true anymore
        {
            clearUntilClosingBracket(_interpreter);
            break;
        }

        _interpreter->current = current;
    }
}

void parseFor(bool effects, struct Interpreter *_interpreter)
{
    char const *current = _interpreter->current;

    while (true)
    {
        skip(_interpreter);

        if (!consume("integer", _interpreter)) {
            fail(_interpreter);
        }

        struct optional_slice variableName = consume_identifier(_interpreter);

        if (!variableName.present || !consume("=", _interpreter)) {
            fail(_interpreter);
        }

        uint64_t variableData = 0;

        if (contains(variableName.value, _interpreter)) {
            variableData = get_value(variableName.value, _interpreter).isInt;

            while (*_interpreter->current != ';') {
                _interpreter->current++;
            }
        } else {
            variableData = expression(effects, _interpreter);
        }

        struct data_type variableDataType = {integer, "\0", variableData, false};

        insert_pair(variableName.value, variableDataType, _interpreter);

        if (!consume(";", _interpreter)) {
            fail(_interpreter);
        }

        uint64_t trueOrFalse = expression(effects, _interpreter); // Get the boolean condition

        if (!consume(";", _interpreter)) {
            fail(_interpreter);
        }

        struct optional_slice variableName2 = consume_identifier(_interpreter);

        if (!variableName2.present || !consume("=", _interpreter)) {
            fail(_interpreter);
        }

        uint64_t variableData2 = expression(effects, _interpreter);
        struct data_type variableDataType2 = {integer, "\0", variableData2, false};

        consume(")", _interpreter);
        consume("{", _interpreter);

        if (trueOrFalse != 0) // If condition is true, run the code inside
        {
            statements(effects, _interpreter);
        }
        else // Condition is not true anymore
        {
            clearUntilClosingBracket(_interpreter);
            // Need to remove variable from map
            break;
        }

        insert_pair(variableName2.value, variableDataType2, _interpreter);

        _interpreter->current = current;
    }
}

void parseIfElse(bool effects, struct Interpreter *_interpreter)
{
    skip(_interpreter);

    uint64_t trueOrFalse = expression(effects, _interpreter); // Get boolean condition

    consume(")", _interpreter);
    consume("{", _interpreter);

    if (trueOrFalse != 0) // If condition is true, run code inside if
    {
        statements(effects, _interpreter);
    }
    else // Otherwise, skip through the code
    {
        clearUntilClosingBracket(_interpreter);
    }

    // If condition is false and there is else statement, run code inside else
    if (consumeBracket("else", _interpreter))
    {
        if (trueOrFalse == 0)
        {
            statements(effects, _interpreter);
        }
        else
        {
            clearUntilClosingBracket(_interpreter);
        }
    }
}

bool checkType(struct optional_slice potential_variable) {
    return operator1("integer", potential_variable.value) || operator1("boolean", potential_variable.value) || operator1("string", potential_variable.value) || operator1("array", potential_variable.value);
}

int numDigits(uint64_t num) {
    int ans = 0;

    while (num > 0) {
        num /= 10;
        ans++;
    }

    return ans;
}

struct data_type parseDataType(struct Interpreter *_interpreter, struct optional_slice type, bool effects, variable_type currType) {
    if (operator1("integer", type.value) || currType == integer) {
        uint64_t v = expression(effects, _interpreter);
        struct data_type toReturn = {integer, '\0', v, false};
        return toReturn;
    } else if (operator1("boolean", type.value) || currType == boolean) {
        uint64_t v = expression(effects, _interpreter);
        struct data_type toReturn = {boolean, '\0', 0, (v == 1) ? true : false};
        return toReturn;
    } else if (operator1("string", type.value) || currType == string) {
        char *ans = malloc(0);
        size_t maxSize = 0;
        size_t i = 0;

        while (true) {
            if (consume("\"", _interpreter)) {;              
                while (*_interpreter->current != '\"') {
                    if (i + 1 >= maxSize) {
                        maxSize = maxSize * 2 + 1;
                        ans = realloc(ans, sizeof(char) * maxSize);
                    }
                    ans[i] = *_interpreter->current;
                    _interpreter->current++;
                    i++;
                }
                _interpreter->current++;
            } else {
                char *ans2 = malloc(0);
                size_t maxSize2 = 0;
                size_t i2 = 0;

                if (consume("(", _interpreter)) {
                    size_t paren_count = 1;

                    while (true) {
                        if (i2 + 1 >= maxSize2) {
                            maxSize2 = maxSize2 * 2 + 1;
                            ans2 = realloc(ans2, sizeof(char) * maxSize2);
                        }

                        if (*_interpreter->current == ')') {
                            paren_count--;
                        } else if (*_interpreter->current == '(') {
                            paren_count++;
                        }

                        if (paren_count == 0) {
                            break;
                        }

                        ans2[i2] = *_interpreter->current;
                        _interpreter->current++;
                        i2++;
                    }
                    _interpreter->current++;
                } else {
                    fail(_interpreter);
                }

                struct Interpreter *stringInterpreter = constructor1(ans2);

                uint64_t v = expression(effects, stringInterpreter);
                int ndigits = numDigits(v);

                if (i + ndigits >= maxSize) {
                    maxSize = (ndigits >= i) ? maxSize + ndigits + 1 : maxSize * 2 + 1;
                    ans = realloc(ans, sizeof(char) * maxSize);
                }

                char num_str[ndigits];

                // Convert integer to string using sprintf()
                sprintf(num_str, "%ld", v);

                // Concatenate integer string to original string using strcat()
                strcat(ans, num_str);

                i += ndigits;
            }

            if (!consume("+", _interpreter)) {
                break;
            }
        }

        struct data_type toReturn = {string, ans, 0, false};
        return toReturn;
    } 

    fail(_interpreter);
}

// Method for running expressions in a global scope
bool statement(bool effects, struct Interpreter *_interpreter)
{
    // Check for print statements
    if (consumeFunction("print", _interpreter))
    {
        // print ...
        if (effects)
        {
            printString(effects, _interpreter);
            //printf("%lu\n", expression(effects, _interpreter));
            consume(")", _interpreter);
        }
        return true;
    }
    else if (consumeFunction("if", _interpreter))
    {
        // if (...       )

        if (effects)
        {
            parseIfElse(effects, _interpreter);
        }
        return true;
    }
    else if (consumeFunction("while", _interpreter))
    {
	// while (...    )
	
        if (effects)
        {
            parseWhile(effects, _interpreter);
        }
        return true;
    }
    else if (consumeFunction("for", _interpreter))
    {
        if (effects)
        {
            parseFor(effects, _interpreter);
        }
        return true;
    }
    else if (consume("fun ", _interpreter))
    {
	// fun <FUNCTION_NAME>(..., , )
        if (effects)
        {
            parseFunction(effects, _interpreter);
        }
        return true;
    }
    else if (consume("}", _interpreter))
    {
	// Check for closing bracket
        return false;
    }

    struct optional_slice testid = consume_identifier(_interpreter);

    if (testid.present)
    {
        struct Slice id = testid.value;

        if (consume("(", _interpreter))
        {
            char *char_id = (char *)malloc(id.len + 1);
            strncpy(char_id, id.start, id.len);
            char_id[id.len] = '\0';

            runFunction(effects, _interpreter, char_id);
            return true;
        }

        /*
        if (!checkType(testid)) {
            fail(_interpreter);
        }
        */

        struct optional_slice name = consume_identifier(_interpreter);
        int arrayIndex = -1;

        if (!name.present) {
            if (consume("[", _interpreter)) {
                uint64_t arrayIndexInt = expression(effects, _interpreter);
                arrayIndex = arrayIndexInt;
                consume("]", _interpreter);
            }
        } else {
            id = name.value;
        }

        // x = ...
        if (consume("=", _interpreter)) // Check if we are setting variable equal to an expression
        {
            if (effects)
            {
                if (arrayIndex != -1) {
                    
                    /*
                    uint64_t val = expression(effects, _interpreter);
                    struct data_type value = {integer, "\0", val, false};
                    */

                    // Determine global vs local scope
                    
                    if (contains(id, _interpreter))
                    {
                        struct data_type value = parseDataType(_interpreter, testid, effects, get_value(id, _interpreter).isArray[0]->curr_data_type);
                        insert_into_array(id, value, _interpreter, arrayIndex);
                    }
                    else if (contains(id, global_interpreter))
                    {
                        struct data_type value = parseDataType(_interpreter, testid, effects, get_value(id, global_interpreter).isArray[0]->curr_data_type);
                        insert_into_array(id, value, global_interpreter, arrayIndex);
                    }
                    else
                    {
                        fail(_interpreter);
                    }
                } else {

                    /*
                    uint64_t val = expression(effects, _interpreter);
                    struct data_type value = {integer, "\0", val, false};
                    */

                    // Determine global vs local scope
                    if (contains(id, _interpreter))
                    {
                        struct data_type value = parseDataType(_interpreter, testid, effects, get_value(id, _interpreter).curr_data_type);
                        insert_pair(id, value, _interpreter);
                    }
                    else if (contains(id, global_interpreter))
                    {
                        struct data_type value = parseDataType(_interpreter, testid, effects, get_value(id, global_interpreter).curr_data_type);
                        insert_pair(id, value, global_interpreter);
                    }
                    else
                    {
                        struct data_type value = parseDataType(_interpreter, testid, effects, empty);
                        insert_pair(id, value, _interpreter);
                    }
                }
            }
            return true;
        }

        // We have found an array
        else if (consume("[", _interpreter)) 
        {
            if (effects) 
            {
                uint64_t arraySize = expression(effects, _interpreter);

                if (!consume("]", _interpreter)) {
                    fail(_interpreter);
                }

                struct data_type toReturn;
                toReturn.curr_data_type = array;
                toReturn.isArray = malloc(sizeof(struct data_type) * arraySize);

                for (int i = 0; i < arraySize; i++) {
                    struct data_type *currElement = (struct data_type*) malloc(sizeof(struct data_type));
                    currElement->curr_data_type = integer;
                    currElement->isInt = 0;
                    toReturn.isArray[i] = currElement;
                }

                if (contains(id, _interpreter)) {
                    fail(_interpreter);
                }

                insert_pair(id, toReturn, _interpreter);
            }   

            return true;
        }

	    // We have found a function instead and not a variable assignment
        else
        {
            fail(_interpreter);
        }
    }
    return false;
}

void statements(bool effects, struct Interpreter *_interpreter)
{
    // Run program line by line
    while (statement(effects, _interpreter))
        ;
}

// Run program
void run(struct Interpreter *_interpreter)
{
    statements(true, _interpreter);
    end_or_fail(_interpreter);
}

struct optional_int parseForFunction(bool effects, struct Interpreter *_interpreter)
{
    char const *current = _interpreter->current;

    struct optional_int v;
    v.present = false;
    v.value = 0;

    while (true)
    {
        skip(_interpreter);

        if (!consume("integer", _interpreter)) {
            fail(_interpreter);
        }

        struct optional_slice variableName = consume_identifier(_interpreter);

        if (!variableName.present || !consume("=", _interpreter)) {
            fail(_interpreter);
        }

        uint64_t variableData = 0;

        if (contains(variableName.value, _interpreter)) {
            variableData = get_value(variableName.value, _interpreter).isInt;

            while (*_interpreter->current != ';') {
                _interpreter->current++;
            }
        } else {
            variableData = expression(effects, _interpreter);
        }

        struct data_type variableDataType = {integer, "\0", variableData, false};

        insert_pair(variableName.value, variableDataType, _interpreter);

        if (!consume(";", _interpreter)) {
            fail(_interpreter);
        }

        uint64_t trueOrFalse = expression(effects, _interpreter); // Get the boolean condition

        if (!consume(";", _interpreter)) {
            fail(_interpreter);
        }

        struct optional_slice variableName2 = consume_identifier(_interpreter);

        if (!variableName2.present || !consume("=", _interpreter)) {
            fail(_interpreter);
        }

        uint64_t variableData2 = expression(effects, _interpreter);
        struct data_type variableDataType2 = {integer, "\0", variableData2, false};

        consume(")", _interpreter);
        consume("{", _interpreter);

        if (trueOrFalse != 0) // If condition is true, run the code inside
        {
            v = functionStatement(effects, _interpreter);
            if (v.present)
            {
                break;
            }
        }
        else // Condition is not true anymore
        {
            clearUntilClosingBracket(_interpreter);
            // Need to remove variable from map
            break;
        }

        insert_pair(variableName2.value, variableDataType2, _interpreter);

        _interpreter->current = current;
    }

    return v;
}

// This method is meant to run code within functions, it returns the return value of the function
struct optional_int functionStatement(bool effects, struct Interpreter *_interpreter)
{
    while (true)
    {
        // Check for print statements
        if (consumeFunction("print", _interpreter))
        {
            // print ...
            if (effects)
            {
                printString(effects, _interpreter);
                //printf("%lu\n", expression(effects, _interpreter));
            }

            consume(")", _interpreter);
            continue;
        }
        else if (consumeFunction("if", _interpreter))
        {
	    // if (...     )
            struct optional_int v = parseIfElseFunction(effects, _interpreter);
            if (v.present)
            {
                return v;
            }
            continue;
        }
        else if (consumeFunction("while", _interpreter))
        {
	    // while (...  )
            struct optional_int v = parseWhileFunction(effects, _interpreter);
            if (v.present)
            {
                return v;
            }
            continue;
        }
        else if (consumeFunction("for", _interpreter))
        {
	    // for (...  )
            struct optional_int v = parseForFunction(effects, _interpreter);
            if (v.present)
            {
                return v;
            }
            continue;
        }
        else if (consume("return ", _interpreter))
        {
	    // return <EXPRESSION>
            struct optional_int v;
            v.present = true;
            v.value = expression(effects, _interpreter);
            return v;
        }
        else if (consume("}", _interpreter))
        {
	    // Check for closing bracket
            struct optional_int v;
            v.present = false;
            v.value = 0;
            return v;
        }

        struct optional_slice testid = consume_identifier(_interpreter);

        if (testid.present)
        {
            struct Slice id = testid.value;
            // x = ...

            if (consume("(", _interpreter))
            {
		// We have found another function
                char *char_id = (char *)malloc(id.len + 1);
                strncpy(char_id, id.start, id.len);
                char_id[id.len] = '\0';

                skip(_interpreter);

		// Run the interior function call
                runFunction(effects, _interpreter, char_id);
                continue;
            }

            struct optional_slice name = consume_identifier(_interpreter);
            int arrayIndex = -1;

            if (!name.present) {
                if (consume("[", _interpreter)) {
                    uint64_t arrayIndexInt = expression(effects, _interpreter);
                    arrayIndex = arrayIndexInt;
                    consume("]", _interpreter);
                }
            } else {
                id = name.value;
            }

            if (consume("=", _interpreter)) // Check if we are setting variable equal to an expression
        {
            if (effects)
            {
                if (arrayIndex != -1) {
                    
                    /*
                    uint64_t val = expression(effects, _interpreter);
                    struct data_type value = {integer, "\0", val, false};
                    */

                    // Determine global vs local scope
                    if (contains(id, _interpreter))
                    {
                        struct data_type value = parseDataType(_interpreter, testid, effects, get_value(id, _interpreter).isArray[0]->curr_data_type);
                        insert_into_array(id, value, _interpreter, arrayIndex);
                    }
                    else if (contains(id, global_interpreter))
                    {
                        struct data_type value = parseDataType(_interpreter, testid, effects, get_value(id, global_interpreter).isArray[0]->curr_data_type);
                        insert_into_array(id, value, global_interpreter, arrayIndex);
                    }
                    else
                    {
                        fail(_interpreter);
                    }
                } else {

                    /*
                    uint64_t val = expression(effects, _interpreter);
                    struct data_type value = {integer, "\0", val, false};
                    */

                    // Determine global vs local scope
                    if (contains(id, _interpreter))
                    {
                        struct data_type value = parseDataType(_interpreter, testid, effects, get_value(id, _interpreter).curr_data_type);
                        insert_pair(id, value, _interpreter);
                    }
                    else if (contains(id, global_interpreter))
                    {
                        struct data_type value = parseDataType(_interpreter, testid, effects, get_value(id, global_interpreter).curr_data_type);
                        insert_pair(id, value, global_interpreter);
                    }
                    else
                    {
                        struct data_type value = parseDataType(_interpreter, testid, effects, empty);
                        insert_pair(id, value, _interpreter);
                    }
                }
            }
            continue;
        }

        // We have found an array
        else if (consume("[", _interpreter)) 
        {
            if (effects) 
            {
                uint64_t arraySize = expression(effects, _interpreter);

                if (!consume("]", _interpreter)) {
                    fail(_interpreter);
                }

                struct data_type toReturn;
                toReturn.curr_data_type = array;
                toReturn.isArray = malloc(sizeof(struct data_type) * arraySize);

                for (int i = 0; i < arraySize; i++) {
                    struct data_type *currElement = (struct data_type*) malloc(sizeof(struct data_type));
                    currElement->curr_data_type = integer;
                    currElement->isInt = 0;
                    toReturn.isArray[i] = currElement;
                }

                if (contains(id, _interpreter)) {
                    fail(_interpreter);
                }

                insert_pair(id, toReturn, _interpreter);
            }   

            continue;
        }
            
            else
            {
                fail(_interpreter);
            }
        }

        break;
    }

    struct optional_int ans;
    ans.value = 0;
    ans.present = false;

    return ans;
}

// This method runs while method that are present within functions, and returns any return value within the while method
struct optional_int parseWhileFunction(bool effects, struct Interpreter *_interpreter)
{
    char const *current = _interpreter->current;

    struct optional_int v;
    v.present = false;
    v.value = 0;

    while (true)
    {
        skip(_interpreter);

	// Boolean condition
        uint64_t trueOrFalse = expression(effects, _interpreter);

        consume(")", _interpreter);
        consume("{", _interpreter);

	// Check if condition is satisfed
        if (trueOrFalse != 0)
        {
            v = functionStatement(effects, _interpreter);

	    // Check for return value
            if (v.present)
            {
                break;
            }
        }
        else
        {
	    // Exit the while loop
            clearUntilClosingBracket(_interpreter);
            break;
        }

        _interpreter->current = current;
    }

    return v;
}

// This method runs if/else statements found within functions and returns the associated return value if present
struct optional_int parseIfElseFunction(bool effects, struct Interpreter *_interpreter)
{
    skip(_interpreter);

    // Boolean condition
    uint64_t trueOrFalse = expression(effects, _interpreter);

    consume(")", _interpreter);
    consume("{", _interpreter);

    struct optional_int v;
    v.present = false;
    v.value = 0;

    // Check if condition is satisfied
    if (trueOrFalse != 0)
    {
        v = functionStatement(effects, _interpreter);
    }
    else // Skip through code in if segment
    {
        clearUntilClosingBracket(_interpreter);
    }

    // Check if else exists, if so and if condition isn't satisfied, run else
    if (consumeBracket("else", _interpreter))
    {
        consume("{", _interpreter);
        if (trueOrFalse == 0)
        {
            v = functionStatement(effects, _interpreter);
        }
        else
        {
            clearUntilClosingBracket(_interpreter);
        }
    }

    return v;
}

// Function used for edge cases w/ calling functions, checks if paren is placed after "fun"
bool consumeFunction(const char *str, struct Interpreter *_interpreter)
{
    skip(_interpreter);

    char const *current = _interpreter->current;

    size_t i = 0;
    while (true)
    {
        char const expected = str[i];
        char const found = current[i];

        if (expected == 0)
        {
            /* survived to the end of the expected string */
            current += i;
            _interpreter->current = current;

	    // If we have found the paren, exit
            if (consume("(", _interpreter))
            {
                return true;
            }
            else
            {
                current -= i;
                _interpreter->current = current;
                return false;
            }
        }
        if (expected != found)
        {
            return false;
        }
        // assertion: found != 0
        i += 1;
    }
}

// Function used for edge cases w/ if/else and while statements, checks if bracket is placed after if/else and while calls
bool consumeBracket(const char *str, struct Interpreter *_interpreter)
{
    skip(_interpreter);

    char const *current = _interpreter->current;

    size_t i = 0;
    while (true)
    {
        char const expected = str[i];
        char const found = current[i];

        if (expected == 0)
        {
            /* survived to the end of the expected string */
            current += i;
            _interpreter->current = current;

	    // If we have reached the bracket, return
            if (consume("{", _interpreter))
            {
                return true;
            }
            else
            {
                current -= i;
                _interpreter->current = current;
                return false;
            }
        }
        if (expected != found)
        {
            return false;
        }
        // assertion: found != 0
        i += 1;
    }
}

int main(int argc, const char *const *const argv)
{

    /*
    if (argc != 2) {
        fprintf(stderr,"usage: %s <file name>\n",argv[0]);
        exit(1);
    }
    */

    // open the file
    int fd = open("adityac.fun", O_RDONLY);
    if (fd < 0)
    {
        perror("open");
        exit(1);
    }

    // determine its size (std::filesystem::get_size?)
    struct stat file_stats;
    int rc = fstat(fd, &file_stats);
    if (rc != 0)
    {
        perror("fstat");
        exit(1);
    }

    // map the file in my address space
    char const *prog = (char const *)mmap(
        0,
        file_stats.st_size,
        PROT_READ,
        MAP_PRIVATE,
        fd,
        0);
    if (prog == MAP_FAILED)
    {
        perror("mmap");
        exit(1);
    }

    // Initialize function hashmap
    init_function_table();

    // Initialize interpreter for global scope
    struct Interpreter *x = constructor1(prog); // Get Interpreter struct
    global_interpreter = x;

    run(x);
    
    free_interpreter(global_interpreter);

    return 0;
}
