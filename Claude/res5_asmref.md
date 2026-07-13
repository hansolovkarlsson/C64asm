res5_asmref.md

# Prompt

I'd like a reference manual for the assembler

# Response

This should be a proper reference document rather than something you read once in chat — I'll build it as a standalone Markdown file you can keep alongside the source.Let me verify a few claims in the manual against the actual code before shipping it, to make sure nothing drifted:Confirmed accurate — `01 02` (numeric), `41 42` (PETSCII "AB"), `00`. Now let's package it up:Covers:

1. Command-line usage
2. Source line structure (labels, comments, `=`/`.equ`)
3. Number/literal formats
4. Expression grammar (operators, precedence, `<`/`>`, forward references)
5. Addressing-mode syntax table, including the zero-page/absolute auto-selection rule
6. PETSCII text handling — including the uppercase-only limitation we just tracked down together
7. Full directive reference with an annotated `.basic` example
8. The complete 56-mnemonic opcode table, every supported addressing mode and its opcode byte
9. `.prg` output format
10. Listing file format
11. Every error message the assembler can produce and what triggers it
12. **Known limitations** — including the forward-reference zero-page sizing edge case, no macros/includes, no illegal opcodes, single-error-at-a-time, uppercase-only text
13. A worked example, using the corrected `hello.asm` from our debugging session

Everything in it is checked against the actual assembler behavior rather than written from memory — I spot-verified the addressing-mode table, the mixed `.byte` numeric/string syntax, and the `.basic`/listing behavior against real assembler runs before including them.