/**
 * @file stm32f407vg.ld
 * @brief STM32F407VG 链接脚本
 * @description 定义Flash和RAM布局，预留KV存储区域
 * @author EasyData
 * @date 2026-02-25
 * @version 1.0.0
 */

/* Entry Point */
ENTRY(Reset_Handler)

/* Stack and Heap sizes */
_stack_size = 0x400;  /* 1KB Stack */
_heap_size = 0x200;   /* 512B Heap */

MEMORY
{
    FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 1024K
    RAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 192K
    KV_STORAGE (rx) : ORIGIN = 0x0800F000, LENGTH = 32K
}

SECTIONS
{
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } >FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text*)
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
        _etext = .;
    } >FLASH

    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } >RAM AT>FLASH

    .bss :
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } >RAM

    /* KV存储区域 - 保留在Flash中 */
    .kv_storage :
    {
        . = ALIGN(4);
        _flash_kv_start = .;
        . = ORIGIN(KV_STORAGE);
        *(.kv_storage)
        . = ALIGN(4);
        _flash_kv_end = .;
    } >KV_STORAGE

    /* Heap and Stack */
    ._user_heap_stack :
    {
        . = ALIGN(8);
        PROVIDE ( end = . );
        PROVIDE ( _end = . );
        . = . + _heap_size;
        . = ALIGN(8);
        . = . + _stack_size;
        . = ALIGN(8);
    } >RAM

    /DISCARD/ :
    {
        libc.a ( * )
        libm.a ( * )
        libgcc.a ( * )
    }
}
