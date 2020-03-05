# Iterators

TODO

## Map and filter

We can use the functional `map` and `filter` operations on lists, using
function pointers or lambda expressions. First, let's see how filter works.

```
var a = List([1, 2, 3]);
var filtered = a.filter((e: int*) -> *e > 2);
printf("%d", *filtered[0]);
// => 3
```

Note that the element `e` in the lambda expression is a pointer, and as such
has to be dereferenced to get the value.

`map` works similarly, except it returns a list of the same size.

```
var a = List([1, 2, 3]);
var b = a.map((e: int*) -> *e * 2);
printf("%d, %d, %d", *b[0], *b[1], *b[2]);
// => 2, 4, 6
```
