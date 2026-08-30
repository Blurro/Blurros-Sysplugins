.section .plugin_coin,"ax",%progbits
.balign 4
.arm

@ --------------- home menu hooks ---------------

@ -------- below must be mirrored exactly on rosalina side asm
.global PLUGIN_coin_dat
PLUGIN_coin_dat:
    .hword 0

.balign 4
.global PLUGIN_coin_bin
PLUGIN_coin_bin:
    .word 0
    .word 0 @ coinRec
    .word 0 @ coinTrue
    .word 0 @ coinsEverSpent
    
.balign 4
.global PLUGIN_coin_change @ a secret counter for coins that were actually earned instead of cheated
PLUGIN_coin_change:
    .hword 0
    .hword 0 @ coinsSpent, for coin setter page
    .hword 0 @ coinEarn pre-calc coins
    .hword 0

.balign 4
.global PLUGIN_coin_stepDiagnostics
PLUGIN_coin_stepDiagnostics:
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
@ -------- end of rosalina mirroring

@ below is this side only
.balign 4
.global PLUGIN_coin_homePtr
PLUGIN_coin_homePtr:
    .word 0                      @ 0x1642C8

PLUGIN_coin_binLast:
    .word 0

.global PLUGIN_coin_homeLoaderPatch
.type   PLUGIN_coin_homeLoaderPatch, %function
PLUGIN_coin_homeLoaderPatch:
    ldrh    r6, [r4, #4]

    adr     r9, PLUGIN_coin_homePtr
    ldr     r9, [r9]              @ ptr to homemenu store

    ldr     r8, [r9, #8]
    ldr     r7, [sp, #0xc]
    str     r7, [r8, #12]
    ldr     r7, [r4, #8]
    str     r7, [r8, #16]
    ldr     r7, [r4, #12]
    str     r7, [r8, #20]
    ldrh    r7, [r4, #6]
    str     r7, [r8, #28]
    mov     r7, #1
    str     r7, [r8, #8]

    @ store post-calc earnings to coinEarn
    ldr     r8, [r9, #8]          @ g_coinEarn addr
    @cmp     r8, #0
    @beq     skipEarnDiff
    add     r8, r8, #4		      @ point to pre-calc holder
    ldrh    r11, [r8]             @ pre-calc coins val
    sub     r7, r6, r11           @ this is coins - (pre-calc coins), coins is always same or higher
    sub     r8, r8, #4            @ point back to coinEarn

    ldrh    r5, [r8]
    add     r7, r5, r7
    strh    r7, [r8]              @ store increased coinEarn (post-calc profits) 

    @ coinsSpent calc
    mov     r5, #0                @ coinsSpent = 0
    ldr     r10, [r9]
    ldrh    r10, [r10]            @ load prev coinDat val
    cmp     r10, r11              @ if coinsDat <= syscoins(pre-calc coins), jump past
    ble     skipSpent
    sub     r5, r10, r11          @ r5 = coinsDat - syscoins
    cmp     r5, #0x12C            @ 300
    movhi   r5, #0x12C            @ clamp to 300
skipSpent:
    ldr     r7, [r9, #4]          @ g_coinBin addr
    add     r7, r7, #4            @ r7 = &coinRec
    ldr     r10, [r7]             @ r6 = coinRec
    cmp     r10, r5
    movlo   r5, r10               @ only legitimate tracked coins count as spent
    sub     r10, r10, r5
    str     r10, [r7]             @ store updated coinRec

    @ increment coinsEverSpent by legitimate coinsSpent
    ldr     r8, [r9, #4]          @ g_coinBin addr
    add     r8, r8, #12           @ point to coinsEverSpent
    ldr     r7, [r8]
    add     r7, r7, r5            @ coinsEverSpent + coinsSpent
    str     r7, [r8]

    @ wipe coinsSpent
    ldr     r8, [r9, #8]          @ g_coinEarn addr
    add     r8, r8, #2 		      @ point to coinsSpent
    mov     r5, #0
    strh    r5, [r8]              @ set coinsSpent to 0
@skipEarnDiff:
    ldr     r5, [r9, #4]          @ g_coinBin addr
    ldr     r7, [r5]              @ coinBin (32-bit)
    adr     r5, PLUGIN_coin_binLast
    str     r7, [r5]

    mov     r8, #0x12C            @ 300

    @ if coinBin < 300, jump past
    cmp     r7, r8
    ble     setBinToDat

    @ coinBin = coinBin - (300 - coinDat)
    sub     r7, r7, r8            @ r7 = coinBin - 300
    add     r7, r7, r6            @ r7 = (coinBin - 300) + coinDat
    b       afterBinLogic

setBinToDat:
    mov     r7, r6

afterBinLogic:
    @ set coinDat to coinBin clamped to 300
    cmp     r7, r8
    movhi   r6, r8

    @ clamp coinBin to 30k
    ldr     r8, =0x7530            @ 30000
    cmp     r7, r8
    movhi   r7, r8

    @ store back
    ldr     r5, [r9]              @ g_coinDat addr
    strh    r6, [r5]              @ store coinDat (16-bit)

    ldr     r5, [r9, #4]          @ g_coinBin addr
    str     r7, [r5]              @ store coinBin (32-bit)

    @ write final coinDat value to struct
    strh    r6, [r4, #4]

    add     sp, sp, #0x7c
    pop     {r4, r5, r6, r7, r8, sb, sl, fp, pc}

.global PLUGIN_coin_homeUIReturn
PLUGIN_coin_homeUIReturn:
    .word 0                       @ 0x1EEF18 on eur 11.5
 
.global PLUGIN_coin_homeLoaderUIHook
.type   PLUGIN_coin_homeLoaderUIHook, %function
PLUGIN_coin_homeLoaderUIHook:
    adr     r0, PLUGIN_coin_homePtr
    ldr     r3, [r0]              @ ptr to homemenu store
    ldr     r0, [r3, #4]          @ g_coinBin addr
    ldr     r3, [r0]
    
    ldr     r0, =0x7530
    cmp     r3, r0
    blt     notMax
    adr     r0, PLUGIN_coin_homePtr     @ if 300000 set text to 'Maxed'
    ldr     r0, [r0]
    add     r2, r0, #12
notMax:
    adr     r0, PLUGIN_coin_homeUIReturn
    ldr     r0, [r0]
    push    {r0}

    mov     r1, #0x10
    add     r0, sp, #0x14         @ add 4 to account for pushed return

    pop     {pc}

.global PLUGIN_coin_preCoinHook
.type   PLUGIN_coin_preCoinHook, %function
PLUGIN_coin_preCoinHook:
    adr     r0, PLUGIN_coin_homePtr
    ldr     r0, [r0]              @ ptr to homemenu store
    @ get and push return addr
    add     r3, r0, #32
    push    {r3}
    @ get pointer to coinEarn
    ldr     r0, [r0, #8]         @ g_coinEarn addr
    @cmp     r0, #0
    @beq     skipEarn
    @ if valid pointer, store pre-calc amount to it (homeLoaderPatch will calc and store earned amt here)
    add      r0, r0, #4		       @ point to pre-calc holder
    ldrh     r3, [r4, #4]          @ load pre-calc amount
    strh     r3, [r0]
@skipEarn:
    ldr     r3, [r4, #8]
    ldr     r0, [sp, #0x10]       @ usually 0xC, add 4 to account for pushed return
    cmp     r3, r0
    
    pop     {pc}

.global PLUGIN_coin_historyDiagHook
.type   PLUGIN_coin_historyDiagHook, %function
PLUGIN_coin_historyDiagHook:
    subs    sl, lr, r0
    mov     r3, #0
    adr     r2, PLUGIN_coin_homePtr
    ldr     r2, [r2]
    ldr     r2, [r2, #8]
    str     lr, [r2, #24]
    adr     r2, PLUGIN_coin_homePtr
    ldr     r2, [r2]
    add     r2, r2, #0x100
    add     r2, r2, #0x34
    mov     pc, r2
    .ltorg
@ --------------- following is for the cost + 3 accumulate code ---------------

.global PLUGIN_coin_costPlus3Hook
.type   PLUGIN_coin_costPlus3Hook, %function
PLUGIN_coin_costPlus3Hook:
    mov     r3, r0                  @ remaining steps
    mov     r2, #0                  @ coins earned this calc
    mov     r1, #100                @ first 10 coins cost 100 steps

    cmp     r12, #10
    blt     costPlus3Loop
    sub     r0, r12, #9
    add     r0, r0, r0, lsl #1
    add     r1, r1, r0              @ coin 11 is 103, then +3 each coin

costPlus3Loop:
    cmp     r3, r1
    blo     costPlus3Done
    sub     r3, r3, r1
    add     r2, r2, #1
    add     r0, r12, r2
    cmp     r0, #10
    addge   r1, r1, #3
    b       costPlus3Loop

costPlus3Done:
    adr     r0, PLUGIN_coin_homePtr
    ldr     r0, [r0]
    add     r0, r0, #0x100
    add     r0, r0, #0x84
    mov     pc, r0
