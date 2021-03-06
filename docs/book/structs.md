# Structs

New struct types can be defined with the `struct` keyword:

```
struct Foo {
    // member variables and functions
}
```

## Member Functions

Member functions are just like non-member functions, except that they receive a
`this` parameter (aka the receiver) which is passed using the dot notation, when
calling the function.

To define a member function for a type, write a function inside the type's
definition:

```
struct Foo {
    void bar() {
        // ...
    }
}
```

To call the member function on a `Foo` instance:

```
foo.bar();
```

Inside member functions, the receiver and its member variables must be accessed
via `this`.

### Static Member Functions

Member functions that are scoped to a type, but that are not called on a
receiver, can be declared with the `static` keyword (not implemented yet):

```
struct Foo {
    static void bar() {
        // ...
    }
}
```

They are called as follows:

```
Foo.bar();
```

## Generic Structs

Structs can have generic type parameters:

```
struct Foo<T> { }
```
