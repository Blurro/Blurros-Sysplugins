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
    .hword 1 @ progressive coin cost enabled

.balign 4
.global PLUGIN_coin_stepDiagnostics
PLUGIN_coin_stepDiagnostics:
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0 @ progressive earned this calc
    .word 0 @ progressive tracked coins today
    .word 0 @ progressive step remainder
    .word 0 @ flat step remainder
    .word 0 @ split progression active
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

    ldr     r10, [r8, #48]
    cmp     r10, #0
    ldrne   r7, [r8, #32]         @ split wallet mode still tracks progressive earnings

    ldrh    r5, [r8]
    add     r7, r5, r7
    strh    r7, [r8]              @ store increased coinEarn (post-calc profits)

    cmp     r10, #0
    beq     afterSplitProgression
    ldr     r7, [r8, #36]
    strh    r7, [r4, #6]
    str     r7, [r8, #28]
    ldr     r5, [r8, #24]
    ldr     r7, [r8, #40]
    cmp     r5, r7
    subhs   r5, r5, r7
    movlo   r5, #0
    str     r5, [r8, #20]
    ldrh    r7, [r8, #6]
    cmp     r7, #0
    beq     afterSplitProgression
    str     r5, [r4, #12]
    mov     r7, #0
    str     r7, [r8, #48]
afterSplitProgression:

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
    @ bad base: bit15 tells the first pass to ignore fake historical spend
    ldr     r8, [r9, #8]          @ g_coinChange addr
    ldrh    r7, [r8, #2]         @ coinsSpent / transient invalid-base marker
    tst     r7, #0x8000
    movne   r5, #0

    ldr     r7, [r9, #4]          @ g_coinBin addr
    add     r7, r7, #4            @ r7 = &coinRec
    ldr     r10, [r7]             @ r10 = coinRec
    cmp     r10, r5
    movlo   r5, r10               @ tracked spend stops when coinsRec reaches zero
    sub     r10, r10, r5
    str     r10, [r7]             @ store updated coinRec

    @ increment coinsEverSpent by the tracked portion only
    ldr     r8, [r9, #4]          @ g_coinBin addr
    add     r8, r8, #12           @ point to coinsEverSpent
    ldr     r7, [r8]
    add     r7, r7, r5            @ coinsEverSpent + tracked coinsSpent
    str     r7, [r8]

    @ wipe coinsSpent, keep only Loader's one-shot bad-base marker
    ldr     r8, [r9, #8]          @ g_coinEarn addr
    add     r8, r8, #2 		      @ point to coinsSpent
    ldrh    r7, [r8]
    tst     r7, #0x8000
    bne     keepInvalidBaseMarker
    mov     r5, #0
    strh    r5, [r8]              @ set coinsSpent to 0
keepInvalidBaseMarker:
@skipEarnDiff:
    ldr     r5, [r9, #4]          @ g_coinBin addr
    ldr     r7, [r5]              @ coinBin (32-bit)
    adr     r5, PLUGIN_coin_binLast
    str     r7, [r5]

    @ bad base: keep the wallet and add only coins genuinely earned this pass
    ldr     r8, [r9, #8]          @ g_coinChange addr
    ldrh    r10, [r8, #2]
    tst     r10, #0x8000
    beq     validBaseBinLogic
    sub     r10, r6, r11          @ this hook's genuine earned amount
    add     r7, r7, r10
    mov     r6, r7                @ invalid recovery rebases system side to wallet
    b       afterBinLogic

validBaseBinLogic:
    @ expectedVanilla = min(starting coinBin, 300)
    @ historicalSpend = max(expectedVanilla - preCalcGamecoin, 0)
    @ genuineEarn     = postCalcGamecoin - preCalcGamecoin
    @ final coinBin   = starting coinBin - historicalSpend + genuineEarn
    @ valid base: raw increases are ignored, raw decreases are spend, this pass can still earn
    mov     r8, #0x12C            @ 300
    mov     r10, r7               @ expectedVanilla = starting coinBin
    cmp     r10, r8
    movhi   r10, r8               @ expectedVanilla = min(coinBin, 300)

    cmp     r10, r11              @ expectedVanilla vs pre-calc gamecoin
    subhi   r10, r10, r11         @ historicalSpend if raw gamecoin decreased
    movls   r10, #0               @ raw increase/equality is ignored
    sub     r7, r7, r10           @ apply historical wallet spend once

    sub     r10, r6, r11          @ genuine Home Menu earnings this calculation
    add     r7, r7, r10           @ only genuine earnings may increase wallet

afterBinLogic:
    mov     r8, #0x12C            @ 300
    @ rebase Home Menu to the accepted wallet so raw increases dont stay live
    cmp     r7, r8
    movls   r6, r7
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
    push    {r0, r12}
    ldr     r0, PLUGIN_coin_homePtr
    ldr     r0, [r0, #8]
    ldr     r2, [r0, #48]
    cmp     r2, #0
    bne     costSplitActive
    ldrh    r1, [r0, #6]
    cmp     r1, #0
    beq     costStartSplit

    ldr     r3, [sp]
    ldr     r12, [sp, #4]
    mov     r2, #0
    mov     r1, #100
    cmp     r12, #10
    blt     costNormalLoop
    sub     r0, r12, #9
    add     r0, r0, r0, lsl #1
    add     r1, r1, r0

costNormalLoop:
    cmp     r3, r1
    blo     costNormalDone
    sub     r3, r3, r1
    add     r2, r2, #1
    add     r0, r12, r2
    cmp     r0, #10
    addge   r1, r1, #3
    b       costNormalLoop

costNormalDone:
    ldr     r0, PLUGIN_coin_homePtr
    ldr     r0, [r0, #8]
    str     r2, [r0, #32]
    add     r1, r12, r2
    str     r1, [r0, #36]
    str     r3, [r0, #40]
    pop     {r0, r12}
    b       costReturn

costStartSplit:
    ldr     r3, [sp]
    ldr     r1, [sp, #4]
    ldr     r12, [r0, #36]
    cmp     r1, r12
    bhs     costStartSplitNoReset
    mov     r12, #0
    str     r12, [r0, #36]
    str     r12, [r0, #40]
    str     r12, [r0, #44]
costStartSplitNoReset:
    mov     r2, #0
    mov     r1, #100
    cmp     r12, #10
    blt     costStartProgressiveLoop
    sub     r0, r12, #9
    add     r0, r0, r0, lsl #1
    add     r1, r1, r0

costStartProgressiveLoop:
    cmp     r3, r1
    blo     costStartProgressiveDone
    sub     r3, r3, r1
    add     r2, r2, #1
    add     r0, r12, r2
    cmp     r0, #10
    addge   r1, r1, #3
    b       costStartProgressiveLoop

costStartProgressiveDone:
    ldr     r0, PLUGIN_coin_homePtr
    ldr     r0, [r0, #8]
    str     r2, [r0, #32]
    add     r1, r12, r2
    str     r1, [r0, #36]
    str     r3, [r0, #40]
    mov     r1, #1
    str     r1, [r0, #48]
    ldr     r3, [sp]
    mov     r2, #0
    mov     r1, #100
    b       costFlatLoop

costSplitActive:
    ldr     r1, [r0, #36]
    ldr     r12, [sp, #4]
    cmp     r12, r1
    bhs     costSplitNoReset
    mov     r1, #0
    str     r1, [r0, #36]
    str     r1, [r0, #40]
    str     r1, [r0, #44]

costSplitNoReset:
    ldr     r12, [r0, #36]
    ldr     r1, [r0, #44]
    ldr     r3, [sp]
    cmp     r3, r1
    subhs   r3, r3, r1
    ldr     r1, [r0, #40]
    add     r3, r3, r1
    mov     r2, #0
    mov     r1, #100
    cmp     r12, #10
    blt     costSplitProgressiveLoop
    sub     r0, r12, #9
    add     r0, r0, r0, lsl #1
    add     r1, r1, r0

costSplitProgressiveLoop:
    cmp     r3, r1
    blo     costSplitProgressiveDone
    sub     r3, r3, r1
    add     r2, r2, #1
    add     r0, r12, r2
    cmp     r0, #10
    addge   r1, r1, #3
    b       costSplitProgressiveLoop

costSplitProgressiveDone:
    ldr     r0, PLUGIN_coin_homePtr
    ldr     r0, [r0, #8]
    str     r2, [r0, #32]
    add     r1, r12, r2
    str     r1, [r0, #36]
    str     r3, [r0, #40]
    ldr     r3, [sp]
    mov     r2, #0
    mov     r1, #100

costFlatLoop:
    cmp     r3, r1
    blo     costFlatDone
    sub     r3, r3, r1
    add     r2, r2, #1
    b       costFlatLoop

costFlatDone:
    ldr     r0, PLUGIN_coin_homePtr
    ldr     r0, [r0, #8]
    str     r3, [r0, #44]
    ldrh    r1, [r0, #6]
    cmp     r1, #0
    ldrne   r2, [r0, #32]
    ldrne   r3, [r0, #40]
    pop     {r0, r12}

costReturn:
    ldr     r0, PLUGIN_coin_homePtr
    add     r0, r0, #0x100
    add     r0, r0, #0x84
    mov     pc, r0
