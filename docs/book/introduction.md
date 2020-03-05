# Hello World

The following program outputs "Hello world" and exits:

```c
void main()
{
    print("Hello world");
}
```

Like C-based languages, the program starts execution in the `main` function.
In this program we don't need to return anything from main, so the return type is declared as `void`.
If we wanted to specify an exit status, we could change the return type to `int` and add a return statement.

To compile and run the code from the command line, we can put the code in a file called for example `hello.delta`, and then do:

```sh
delta hello.delta
./hello # or hello.exe on Windows
```

Or we can let the Delta compiler do both steps using the `run` command:

```sh
delta run hello.delta
```

If we use the `run` command, the executable file `hello` will not be created.
