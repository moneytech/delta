# List

`List` is a resizable array that grows automatically when elements are inserted.
It works like `std::vector` in C++ or `List` in C#.

Like builtin arrays, `List` elements can be accessed using the subscript operator `[]`,
and iterated over with a `for` loop.

Unlike in C++, the subscript operator does bounds checks in debug builds.

```cs
void main() {
    // Create an empty list of integers.
    var list = List<int>();

    // Add some numbers to the list.
    list.push(1);
    list.push(2);
    list.push(3);

    print(list[0]); // prints the first element, 1
    print(list.first()); // also prints the first element
    print(list.last()); // prints the last element, 3
    // print(list[3]); // invalid index, this would cause an assertion error

    // Print each element in the list.
    for (var element in list) {
        print(element);
    }
}
```

## Array reference

The normal way to pass an array or `List` into a function is by an array reference,
written `T[]` where `T` is the element type.
Under the hood, an array reference is a pointer to the array and a size.
It is known in other languages by the name slice, span, or array view.

```cs
// Function to calculate the sum of an array's elements.
int calculateSum(int[] array) {
    var sum = 0;

    for (var element in array) {
        sum += element;
    }

    return sum;
}

void main() {
    print(calculateSum([1, 2, 3]));

    // Also works with a List:
    var list = List([1, 2, 3]);
    print(calculateSum(list));
}
```
