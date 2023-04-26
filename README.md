# Fun-Interpreter

"Fun", a simple functional, turing-complete, pythonic, interpreted language with support for conditionals, functions, recursion types, arrays, and co-routines. Below are some examples:

## Conditionals
```python
integer n = 90

if (n % 2 == 0) {
    print("The number is even.")
} else {
    print("The number is odd.")
}
```

## For Loops

```python
for (integer i = 0; i <= 10; i = i + 1) {
    print(i)
}
```

## While Loops
```python
boolean test = true
integer i = 0

while(test) {
    i = i + 1
    
    if (i > 10) {
        test = !test
    }
}
```
