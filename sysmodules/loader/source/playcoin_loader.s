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
    .word 0 @ calculation validity
    .word 0 @ invalid base pending
    .word 0 @ last history query time low
    .word 0 @ last history query time high
@ -------- end of rosalina mirroring

@ below is this side only
.balign 4
.global PLUGIN_coin_handoffControl
PLUGIN_coin_handoffControl:
    .word 0                      @ calc in flight
    .word 0                      @ ignore RTC catch-up once
    .word 0                      @ RTC day decision: 0 waiting, 1 exact +1, 2 reject
    .word 0                      @ previous progressive step remainder
    .word 0                      @ YYYY | MM << 16 | DD << 24 for this decision
    .word 0                      @ calendar stamp being checked now

.global PLUGIN_coin_rtcDayGateHook
.type   PLUGIN_coin_rtcDayGateHook, %function
PLUGIN_coin_rtcDayGateHook:
    @ protect the whole RTC decision, not only the later coin hooks
    push    {r0-r2}
    adr     r0, PLUGIN_coin_handoffControl
    mov     r1, #1
    str     r1, [r0]

    @ remember HOME's current date without retagging an older exact +1
    ldrh    r1, [sp, #0x40]
    ldrb    r2, [sp, #0x44]
    orr     r1, r1, r2, lsl #16
    ldrb    r2, [sp, #0x45]
    orr     r1, r1, r2, lsl #24
    str     r1, [r0, #20]
    mcr     p15, 0, r1, c7, c10, 5
    pop     {r0-r2}

    @ redo HOME's 64-bit current day - saved day compare
    subs    r0, r7, r5
    sbcs    r0, r8, r6
    blt     rtcClockBehind

    @ sbcs Z covers only the high-word result, so check both halves
    cmp     r7, r5
    bne     rtcClockAhead
    cmp     r8, r6
    beq     rtcClockSame

rtcClockAhead:
    @ exact +1 is remembered for one previous-day settlement
    @ other forward gaps stay current-day-only
    @ compare the full 64-bit gap against one day
    push    {r1, r2}
    subs    r1, r7, r5
    sbcs    r2, r8, r6
    adr     r0, rtcOneDay
    ldr     r0, [r0, #4]
    cmp     r2, r0
    blo     rtcForwardReject
    bhi     rtcForwardSuppress
    adr     r0, rtcOneDay
    ldr     r0, [r0]
    cmp     r1, r0
    blo     rtcForwardReject
    bhi     rtcForwardSuppress

    @ only an exact Nintendo RTC +1 can settle yesterday
    adr     r0, PLUGIN_coin_handoffControl
    ldr     r1, [r0, #20]
    str     r1, [r0, #16]
    adr     r2, PLUGIN_coin_homePtr
    ldr     r2, [r2]
    ldr     r2, [r2, #8]
    ldr     r2, [r2, #40]        @ old progressive remainder before rebase
    str     r2, [r0, #12]
    mov     r1, #1
    str     r1, [r0, #8]
    b       rtcForwardReady

rtcForwardReject:
    adr     r0, PLUGIN_coin_handoffControl
    ldr     r1, [r0, #20]
    str     r1, [r0, #16]
    mov     r1, #0
    str     r1, [r0, #12]
    mov     r1, #2
    str     r1, [r0, #8]
    b       rtcForwardReady

rtcForwardSuppress:
    @ mark this before reading anything Rosalina can swap
    adr     r0, PLUGIN_coin_handoffControl
    ldr     r1, [r0, #20]
    str     r1, [r0, #16]
    mov     r1, #0
    str     r1, [r0, #12]
    mov     r1, #2
    str     r1, [r0, #8]
    adr     r0, PLUGIN_coin_homePtr
    ldr     r0, [r0]
    ldr     r0, [r0, #0x24]
    mov     r1, #1
    str     r1, [r0]
    str     r1, [r0, #4]
    mcr     p15, 0, r1, c7, c10, 5

rtcForwardReady:
    pop     {r1, r2}
    @ redo the positive flags HOME's +0x34 branch expects
    subs    r0, r7, r5
    sbcs    r0, r8, r6
    b       rtcRebaseCurrentDay

rtcClockBehind:
    @ backwards gap: rebase the cursor and suppress this payout
    push    {r1}
    adr     r0, PLUGIN_coin_handoffControl
    ldr     r1, [r0, #20]
    str     r1, [r0, #16]
    mov     r1, #0
    str     r1, [r0, #12]
    mov     r1, #2
    str     r1, [r0, #8]
    adr     r0, PLUGIN_coin_homePtr
    ldr     r0, [r0]
    ldr     r0, [r0, #0x24]
    mov     r1, #1
    str     r1, [r0]
    str     r1, [r0, #4]
    mcr     p15, 0, r1, c7, c10, 5

    pop     {r1}
    b       rtcRebaseCurrentDay

rtcClockSame:
    @ equality rejects a displayed-date-only rollover
    @ dont overwrite an exact +1 waiting for Rosalina
    push    {r1}
    adr     r0, PLUGIN_coin_handoffControl
    ldr     r1, [r0, #8]
    cmp     r1, #1
    beq     rtcClockSameReady
    ldr     r1, [r0, #20]
    str     r1, [r0, #16]
    mov     r1, #0
    str     r1, [r0, #12]
    mov     r1, #2
    str     r1, [r0, #8]
rtcClockSameReady:
    pop     {r1}
    subs    r0, r7, r5
    sbcs    r0, r8, r6
    b       rtcDayGateContinue

rtcRebaseCurrentDay:
    @ r5:r6 = r7:r8 so HOME queries only the observed day
    mov     r5, r7
    mov     r6, r8
    mov     r0, #0
    strh    r0, [r4, #6]         @ HOME coins today
    str     r0, [r4, #8]         @ force one current-day history query
    str     r0, [r4, #12]        @ no current-day history consumed yet

    @ save today's cursor now in case HOME exits before the pre hook
    ldrh    r0, [sp, #0x34]
    strh    r0, [r4, #16]
    ldrb    r0, [sp, #0x38]
    strb    r0, [r4, #18]
    ldrb    r0, [sp, #0x39]
    strb    r0, [r4, #19]

    @ HOME stores r5/r6 next, then reaches the usual pre hook
rtcDayGateContinue:
    adr     r0, PLUGIN_coin_homePtr
    ldr     r0, [r0]
    sub     r0, r0, #0x10       @ coinCalc + 0x2c
    mov     pc, r0

rtcOneDay:
    .word   0x914F0000
    .word   0x00004E94

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
    ldr     r8, [r9, #0x24]
    mov     r7, #1
    str     r7, [r8]
    mcr     p15, 0, r7, c7, c10, 5 @ publish inFlight before pointer loads
    ldr     r0, [r8, #4]          @ stable RTC rebase marker

    ldr     r8, [r9, #8]
    ldr     r7, [sp, #0xc]
    str     r7, [r8, #12]
    ldr     r7, [r4, #8]
    str     r7, [r8, #16]
    ldr     r7, [r4, #12]
    str     r7, [r8, #20]
    ldrh    r7, [r4, #6]
    str     r7, [r8, #28]
    ldr     r7, [r8, #8]
    orr     r7, r7, #1
    str     r7, [r8, #8]

    @ consume only state produced by this calculation
    ldr     r10, [r8, #52]
    ldr     r11, [r8, #32]
    ldr     r12, [r8, #48]
    mov     r7, #0
    str     r7, [r8, #52]
    str     r7, [r8, #32]

    @ split progression state is valid only when the cost hook ran
    cmp     r12, #0
    beq     afterSplitProgression
    tst     r10, #2
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
    @ dont let restored history remainder pay on the next wake
    @ HOME keeps its rebuilt progressive display while both our streams
    @ rebase at the full current history total
    cmp     r0, #0
    beq     afterRtcBoundaryRebase
    ldr     r5, [r8, #24]
    str     r5, [r4, #12]
    str     r5, [r8, #20]
    mov     r5, #0
    str     r5, [r8, #40]
    str     r5, [r8, #44]
afterRtcBoundaryRebase:

    @ no current pre-hook means no persistent accounting
    tst     r10, #1
    beq     finishHomeCalculation

    ldrh    r5, [r8, #4]          @ current invocation pre-calc coins

    @ recovery can rebuild HOME's display but cant change saved coin state
    cmp     r0, #0
    beq     normalPersistentAccounting
    ldr     r7, [r9]
    strh    r5, [r7]
    strh    r5, [r4, #4]
    b       finishHomeCalculation

normalPersistentAccounting:
    cmp     r6, r5
    subhs   r7, r6, r5
    movlo   r7, #0

    cmp     r12, #0
    beq     currentEarnReady
    tst     r10, #2
    movne   r7, r11               @ tracked progressive earnings
    moveq   r7, #0
currentEarnReady:
    mov     r11, r5               @ keep pre-calc coins for reconciliation
    ldrh    r5, [r8]
    add     r7, r5, r7
    strh    r7, [r8]

    @ coinsSpent calc
    mov     r5, #0
    ldr     r10, [r9]
    ldrh    r10, [r10]            @ previous accepted Home Menu coins
    cmp     r10, r11
    ble     skipSpent
    sub     r5, r10, r11
    cmp     r5, #0x12C
    movhi   r5, #0x12C
skipSpent:
    ldr     r8, [r9, #8]
    ldrh    r12, [r8, #2]
    ldr     r10, [r8, #56]
    cmp     r10, #0
    orrne   r12, r12, #0x8000
    tst     r12, #0x8000
    movne   r5, #0

    ldr     r7, [r9, #4]
    add     r7, r7, #4
    ldr     r10, [r7]
    cmp     r10, r5
    movlo   r5, r10
    sub     r10, r10, r5
    str     r10, [r7]

    ldr     r8, [r9, #4]
    add     r8, r8, #12
    ldr     r7, [r8]
    add     r7, r7, r5
    str     r7, [r8]

    @ a valid post consumes coinsSpent and the invalid-base marker
    ldr     r8, [r9, #8]
    mov     r10, #0
    strh    r10, [r8, #2]
    str     r10, [r8, #56]

    ldr     r5, [r9, #4]
    ldr     r7, [r5]
    adr     r5, PLUGIN_coin_binLast
    str     r7, [r5]

    tst     r12, #0x8000
    beq     validBaseBinLogic
    cmp     r6, r11
    subhs   r10, r6, r11
    movlo   r10, #0
    add     r7, r7, r10
    mov     r6, r7
    b       afterBinLogic

validBaseBinLogic:
    mov     r8, #0x12C
    mov     r10, r7
    cmp     r10, r8
    movhi   r10, r8

    cmp     r10, r11
    subhi   r10, r10, r11
    movls   r10, #0
    sub     r7, r7, r10

    cmp     r6, r11
    subhs   r10, r6, r11
    movlo   r10, #0
    add     r7, r7, r10

afterBinLogic:
    mov     r8, #0x12C
    cmp     r7, r8
    movls   r6, r7
    movhi   r6, r8

    ldr     r8, =0x7530
    cmp     r7, r8
    movhi   r7, r8

    ldr     r5, [r9]
    strh    r6, [r5]

    ldr     r5, [r9, #4]
    str     r7, [r5]

    strh    r6, [r4, #4]

finishHomeCalculation:
    ldr     r8, [r9, #0x24]
    mov     r7, #0
    str     r7, [r8, #4]          @ consume RTC rebase marker once
    mcr     p15, 0, r7, c7, c10, 5 @ publish state before clearing inFlight
    str     r7, [r8]
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
    ldr     r0, [r0]
    ldr     r3, [r0, #0x24]
    mov     r0, #1
    str     r0, [r3]
    mcr     p15, 0, r0, c7, c10, 5 @ publish inFlight before pointer loads
    adr     r0, PLUGIN_coin_homePtr
    ldr     r0, [r0]
    add     r3, r0, #32
    push    {r3}

    ldr     r0, [r0, #8]
    ldrh    r3, [r4, #4]
    strh    r3, [r0, #4]
    mov     r3, #0
    str     r3, [r0, #32]
    mov     r3, #1
    str     r3, [r0, #52]

    ldr     r3, [r4, #8]
    ldr     r0, [sp, #0x10]
    cmp     r3, r0

    pop     {pc}

.global PLUGIN_coin_historyDiagHook
.type   PLUGIN_coin_historyDiagHook, %function
PLUGIN_coin_historyDiagHook:
    subs    sl, lr, r0
    mov     r3, #0
    ldr     r2, PLUGIN_coin_homePtr
    ldr     r3, [r2, #0x24]
    mov     r2, #1
    str     r2, [r3]
    mcr     p15, 0, r2, c7, c10, 5 @ publish inFlight before pointer loads
    mov     r3, #0
    ldr     r2, PLUGIN_coin_homePtr
    ldr     r2, [r2, #8]
    ldr     r3, [r2, #8]
    bic     r3, r3, #2
    str     r3, [r2, #8]
    mcr     p15, 0, r3, c7, c10, 5
    str     lr, [r2, #24]
    str     r5, [r2, #60]
    str     r6, [r2, #64]
    mcr     p15, 0, r3, c7, c10, 5
    orr     r3, r3, #2
    str     r3, [r2, #8]
    mov     r3, #0
    ldr     r2, PLUGIN_coin_homePtr
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
    ldr     r12, [r0, #0x24]
    mov     r0, #1
    str     r0, [r12]
    mcr     p15, 0, r0, c7, c10, 5 @ publish inFlight before pointer loads
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
    ldr     r12, [r0, #52]
    orr     r12, r12, #2
    str     r12, [r0, #52]
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
    ldr     r12, [r0, #52]
    orr     r12, r12, #2
    str     r12, [r0, #52]
    pop     {r0, r12}

costReturn:
    ldr     r0, PLUGIN_coin_homePtr
    add     r0, r0, #0x100
    add     r0, r0, #0x84
    mov     pc, r0
