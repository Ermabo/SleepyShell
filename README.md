# SleepyShell 💤

SleepyShell is a toy shell written in C – built from scratch to better understand how Unix shells work under the hood.

It's a personal project for learning system programming, not a production shell.  
Work in progress, full of TODOs and questionable decisions – by design.

---

## 🔧 Features so far

- Basic line editing using raw mode
- Basic command parsing
- Built-in commands: `cd`, `pwd`, `echo`, `exit`, `type`
- PATH resolution with `execv`
- Output redirection: `>`, `>>`, `2>`
- Simple quote handling
- Some error handling
- Manual memory management (of course)

---

## 🧠 Why?

I started this project through the [Codecrafters "Build your own Shell"](https://codecrafters.io) challenge, but quickly went off the rails to build and understand more things myself.

This has been a way to explore:

- `fork`, `exec`, `wait`, `dup`, `dup2`, `open`
- Tokenization and quoting rules
- Building clean(ish) C abstractions
- Where things break (and why)

---

### 🚀 Build & run

SleepyShell uses CMake for building.

```bash
cmake -B build
cmake --build build
./build/sleepyshell
```

### 🔬 Run tests
```bash
cd build
ctest
```

Alternatively, if you prefer raw gcc:
```bash
gcc src/*.c -o sleepyshell
./sleepyshell
