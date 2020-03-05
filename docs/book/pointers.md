# Pointers

Pointers in Delta are almost like in C and C++, except that they can't be null by default.
To create nullable pointers, the pointer type has to be marked as [nullable](nullable-types.html).

To form a pointer, the `&` operator can be used.
To dereference a pointer, the `*` operator can be used.

Comparing pointers always compares to pointed-to objects. To compare the memory addresses,
the reference equality operators `===` and `!==` can be used.

```cs
void main() {
    int i = 6;
    int* p = &i; // Make p point to i
    print(p); // Prints 6

    int* q = p; // Copy p to q, both point to i now
    print(p === q); // Prints true because both pointers point to the same memory address

    int j = 6;
    p = &j; // Change p to point to j
    print(p === q); // Prints false because pointers point to different memory addresses

    *p = 7; // Change the value of j
    print(p * q); // Multiplies the values pointed to by p and q, prints 42
}
```
