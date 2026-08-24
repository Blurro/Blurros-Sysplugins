SECTIONS
{
    .plugin_htps.tls : { *(.text .text.* .gnu.linkonce.t.*) }
    .pluginrodata_htps.tls : { *(.rodata .rodata.* .gnu.linkonce.r.*) }
    .plugindata_htps.tls : { *(.data .data.* .data.rel.ro .data.rel.ro.* .gnu.linkonce.d.*) }
    .pluginbss_htps.tls : { *(.bss .bss.* COMMON .gnu.linkonce.b.*) }
    /DISCARD/ : { *(.comment) *(.note*) *(.debug*) }
}