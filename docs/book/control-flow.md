# Control flow

## for

TODO: Use break and continue in examples

The for-loop iterates over each element in the range expression.

```cs
void main() {
    print("the range can be any collection");
    for (var element in [1, 2, 3]) {
        print(element);
    }

    print("or an inclusive numeric range");
    for (var element in 4...6) {
        print(element);
    }

    print("or an exclusive numeric range");
    for (var element in 7..9) {
        print(element);
    }
}
```

## while

TODO

## if

```cs
void main() {
    var answer = 42;

    if (answer == 0) {
        print("answer is zero");
    } else if (answer < 0) {
        print("answer is negative");
    } else {
        print("answer is ", answer);
    }
}
```

TODO: if expression

## switch

Unlike in most C-based languages, the case bodies don't fall through to the next
by default. This behavior can be enabled for individual cases using the
`fallthrough` keyword (not implemented yet).

```
switch (value) {
    case 0:
        // code to execute if value equals 0
    case 1:
        // code to execute if value equals 1
    default:
        // code to execute if value equals something else
}
```

## defer

`defer` defers the execution of a statement to the exits of the current scope.
This is useful for example when we need to perform some cleanup before returning.
This avoids the mistake of forgetting to add necessary cleanup calls when we add a new return statement.

```cs
int main() {
    var p = malloc(sizeof(int)); // allocate some resource
    defer free(p); // defer deallocation of the resource

    if (p == null) {
        return 1; // free(p) will be called immediately before this return
    }

    return 0; // free(p) will be called also before this return
}
```
