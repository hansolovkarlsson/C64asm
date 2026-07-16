# c64asm

A two-pass 6502/6510 assembler for the Commodore 64, written in portable C99.

## Layout

```
c64asm/
├── .vscode/            VS Code build tasks, debugger config, IntelliSense config
├── src/
│   └── c64asm.c        assembler source
├── examples/
│   └── hello.asm        sample program, doubles as a build smoke test
├── docs/
│   └── SYNTAX.md        assembly syntax reference
├── bin/                 build output (gitignored, kept via bin/.gitkeep)
├── Makefile
└── .gitignore
```

As the project grows, new modules can go straight into `src/` (e.g.
`lexer.c`, `codegen.c`) and headers into a new `include/` directory — the
Makefile only needs `SRC` widened to a wildcard/list once there's more than
one `.c` file.

## Building

```sh
make            # release build -> bin/c64asm
make debug      # debug build (-g -O0), for lldb / VS Code
make example    # builds, then assembles examples/hello.asm -> bin/hello.prg
make clean      # removes bin/
```

Or from VS Code: **Terminal > Run Task** for "Build (release)", "Build
(debug)", or "Assemble example". Press **F5** to build the debug binary and
launch it under the debugger on `examples/hello.asm` (requires the
[CodeLLDB](https://marketplace.visualstudio.com/items?itemName=vadimcn.vscode-lldb)
extension).

## Usage

```sh
bin/c64asm input.asm -o output.prg [--listing out.lst]
```

See [docs/SYNTAX.md](docs/SYNTAX.md) for the assembly syntax.
