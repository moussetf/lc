# lc

A minimal lambda expression REPL based on GNU readline supporting various reduction strategies.

Compile with `make`.

## Usage

Enter a lambda expression on the prompt to see how it reduces, as follows:

```
+ ('a.'b.a b) (a b)
(λc.a b c)
```

Note that an apostrophe is read as the letter λ.

Diverging evaluations can be interrupted with Ctrl-C:

```
+ ('x.x x) ('x.x x)
^C Interrupted
```

It is also possible to define names in the top-level scope:

```
+ Y = 'f.('x.f (x x)) ('x.f (x x))
(λf.(λx.f (x x)) (λx.f (x x)))
+ Y ('x.1)
1
```

Symbols with top-level bindings are expanded only when necessary, i.e., when
they appear as the first term in an application. This enables self-referential
definitions. For example, `Y` above could equally have been defined by `Y = 'f.f (Y f)`.
The binding can be removed by entering `!unset Y`.

Finally, the program can be passed file names as a command line argument, causing
it to first process all lines in the given file before launching the REPL. This allows
experimenting with the use of the lamdba calculus as a programming language (a pretty
limited one obviously, as there is no way to interact with the system). For example,
the included file `church.lc` contains a definition of the Fibonacci numbers using
Church-encoded numerals:
```
$ ./lc church.lc
+ show (fibonacci one)
0 1
+ show (fibonacci two)
0 1 1
+ show (fibonacci three)
0 1 1 1
+ show (fibonacci four)
0 1 1 1 1 1
+ show (fibonacci five)
0 1 1 1 1 1 1 1 1
```

## Switches

The default behaviour can be modified using the following commands:

- `!step on`: enable single-step mode, which pauses at each intermediate step of
  the reduction
- `!inner on`: enable reducing the leftmost innermost redex first (as opposed
  to the leftmost outermost)
- `!strong on` enable reductions inside abstractions (including η-reductions)

There are also corresponding `off` commands that revert the corresponding
setting to its default behaviour.

Several common reduction strategies for the lambda calculus can be achieved by
combining these switches appropriately:

- call-by-name: `!inner off` and `!strong off`
- call-by-value: `!inner on` and `!strong off`
- normal-order: `!inner off` and `!strong on`
- applicative-order: `!inner on` and `!strong on`
