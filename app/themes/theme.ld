/*
 * The .theme file format (app/source/theme_api.h: ThemeHeader), made by the linker: a theme
 * add-on is linked with this script (as a shared object, so the linker lists the words that
 * hold addresses), then objcopy -O binary turns it into the file (Makefile: without the
 * linker's symbol tables, which the app doesn't need). Addresses below are offsets in the
 * theme's memory; the file holds the code part and then, right after it, the data part:
 *
 *   0          ThemeHeader (the words below) and ThemeInfo (the theme's THEME_MODULE_DEFINE)
 *              code, read-only data, the relocations
 *   codeEnd    (the file's data part starts here)
 *   codeSize   (page-aligned) data: written to as the theme runs; in the file at codeEnd
 *              onwards (its file offsets = these addresses - (codeSize - codeEnd))
 *              the end marker: the file ends after it (at fileSize)
 *   memorySize zeroed data (.bss) before it
 *
 * The header's words must stay in ThemeHeader's order; THEME_FILE_MAGIC, THEME_FILE_FORMAT
 * and THEME_FILE_END there must be the numbers here.
 */
OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(ThemeModule_Entry)

PHDRS
{
    code    PT_LOAD FLAGS(5);           /* read, execute */
    data    PT_LOAD FLAGS(6);           /* read, write */
    linker  PT_LOAD FLAGS(4);           /* the linker's tables: not in the .theme file */
    dynamic PT_DYNAMIC FLAGS(4);
}

SECTIONS
{
    . = 0;
    .theme_header :
    {
        LONG(0x48543341)                /* magic "A3TH" (THEME_FILE_MAGIC) */
        LONG(1)                         /* THEME_FILE_FORMAT */
        LONG(ThemeModule_Entry)         /* entry */
        LONG(__theme_code_end)          /* codeEnd */
        LONG(__theme_code_size)         /* codeSize */
        LONG(__theme_rel_start)         /* relStart */
        LONG(__theme_rel_end)           /* relEnd */
        LONG(__theme_file_size)         /* fileSize */
        LONG(__theme_memory_size)       /* memorySize */
        KEEP(*(.theme_info))            /* ThemeInfo */
    } :code
    .text       : { *(.text .text.*) } :code
    .rodata     : { *(.rodata .rodata.*) } :code
    .ARM.exidx  : { *(.ARM.exidx*) } :code
    .rel.dyn    : { __theme_rel_start = .; *(.rel.dyn) *(.rel.*) __theme_rel_end = .; } :code
    __theme_code_end = .;

    /* The data part: in memory on the next page, in the file right after the code part. */
    . = ALIGN(0x1000);
    __theme_code_size = .;
    .data.rel.ro : AT(__theme_code_end) { *(.data.rel.ro .data.rel.ro.*) } :data
    .got        : { *(.got.plt) *(.got) } :data
    .data       : { *(.data .data.*) } :data
    .theme_end  : { LONG(0x444E4554) } :data       /* "TEND" (THEME_FILE_END) */
    __theme_file_size = LOADADDR(.theme_end) + SIZEOF(.theme_end);
    .bss        : { *(.bss .bss.*) *(COMMON) } :data
    __theme_memory_size = ALIGN(4);

    /* What only a system loader would use: past the theme's memory, left out of the file. */
    . = ALIGN(0x1000);
    .dynamic    : { *(.dynamic) } :linker :dynamic
    .hash       : { *(.hash) } :linker
    .dynsym     : { *(.dynsym) } :linker
    .dynstr     : { *(.dynstr) } :linker

    /DISCARD/ : { *(.comment) *(.note*) *(.interp) *(.ARM.attributes) }
}
