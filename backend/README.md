# Backend

Generates x86-32 assembly from LLVM IR using linear-scan register allocation.

## Build

```
make
```

## Usage (Standalone)

```
./backend <input.ll> <output.s>
```

Then link and run:

```
clang -m32 output.s assembly_gen_tests/main.c -o program
./program
```

## Testing

```
make generate-ll   # generate .ll files from assembly_gen_tests/*.c
make test          # compile all .ll files to assembly
make test-run      # compile, link, and run all tests
```

## Clean

```
make clean
```
