.section .plugin_coin,"ax",%progbits
.balign 4096
.arm

@ replayed HOME instructions, opcode checks are in Loader's playcoin_hooks.c

@ this handoff layout must match the Rosalina copy
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
    .word 0 @ progressive tracked coins Today
    .word 0 @ progressive step remainder
    .word 0 @ flat step remainder
    .word 0 @ split progression active
    .word 0 @ calculation validity
    .word 0 @ invalid base pending
    .word 0 @ last history query time low
    .word 0 @ last history query time high
    .word 0 @ preserved Today stays the minimum +3 count
@ end of the shared handoff layout

@ below is this side only
.balign 4
.global PLUGIN_coin_handoffControl
PLUGIN_coin_handoffControl:
    .word 0                      @ calc in flight
    .word 0                      @ ignore RTC catch-up once
    .word 0                      @ RTC day decision: 0 waiting, 1 exact +1, 2 reject
    .word 0                      @ previous progressive step remainder
    .word 0                      @ yyyy | mm << 16 | dd << 24 for this decision
    .word 0                      @ calendar stamp being checked now
    .word 0                      @ previous HOME coins Today
    .word 0                      @ private accounting ready
    .word 0                      @ coins Today
    .word 0                      @ live step boundary
    .word 0                      @ history boundary
    .word 0                      @ year | month << 16 | day << 24
    .word 0                      @ save generation
    .word 0                      @ cap revisited-date history to new lifetime steps
    .word 0                      @ Settings request: 0 idle, 1 waiting, 2 saved, 3 failed
    .word 0                      @ Settings coins Today (normally zero)
    .word 0                      @ Settings PTM total checkpoint
    .word 0                      @ Settings day history checkpoint
    .word 0                      @ Settings calendar date
    .word 0                      @ saved flat mode and boundary offset
    .word 0                      @ raw native Today before private overlay
    .word 0                      @ raw native calendar date
    .word 0                      @ raw native record pending this HOME calculation
    .word 0                      @ saved day bucket ordinal and keep bit
    .word 0                      @ old-day event state
    .word 0                      @ old RTC low
    .word 0                      @ old RTC high
    .word 0                      @ old calendar date
    .word 0                      @ new calendar date
    .word 0                      @ old history boundary
    .word 0                      @ old private Today
    .word 0                      @ old shadow award
    .word 0                      @ old wallet award
    .word 0                      @ old wallet actually credited
    .word 0                      @ generation before old-day save
    .word 0                      @ active native record pointer
    .word 0                      @ wallet before old-day award
    .word 0                      @ earned counter before old-day award
    .word 0                      @ native wallet before old-day award
    .word 0                      @ wallet pointer for rollback
    .word 0                      @ native wallet pointer for rollback
    .word 0                      @ earned counter pointer for rollback

@ Settings already calculated PTM checkpoints, so save ours before native gamecoin.dat
.global PLUGIN_coin_settingsSaveTarget
PLUGIN_coin_settingsSaveTarget: .word 0
.global PLUGIN_coin_settingsSaveReturn
PLUGIN_coin_settingsSaveReturn: .word 0

.global PLUGIN_coin_settingsSaveHook
PLUGIN_coin_settingsSaveHook:
    push    {r4-r8, lr}
    mov     r5, r1              @ calculated 20-byte record on Settings stack
    adr     r7, PLUGIN_coin_handoffControl
    ldr     r6, [r5]
    mov     r8, #0x4f00
    cmp     r6, r8
    bne     settingsSaveInvalid

    ldr     r6, =5000
settingsSavePriorWait:
    ldr     r8, [r7, #56]
    cmp     r8, #0
    beq     settingsSavePublish
    cmp     r8, #2
    beq     settingsSaveClearPrior
    cmp     r8, #3
    beq     settingsSaveAbort
    ldr     r0, =1000000
    mov     r1, #0
    svc     #0x0a
    subs    r6, r6, #1
    bne     settingsSavePriorWait
    b       settingsSaveAbort

settingsSaveClearPrior:
    mov     r8, #0
    str     r8, [r7, #56]
    mcr     p15, 0, r8, c7, c10, 5
settingsSavePublish:

    ldrh    r6, [r5, #6]
    str     r6, [r7, #60]
    ldr     r6, [r5, #8]
    str     r6, [r7, #64]
    ldr     r6, [r5, #12]
    str     r6, [r7, #68]
    ldr     r6, [r5, #16]
    str     r6, [r7, #72]
    mcr     p15, 0, r6, c7, c10, 5
    mov     r6, #1
    str     r6, [r7, #56]
    mcr     p15, 0, r6, c7, c10, 5

    ldr     r6, =5000
settingsSaveWait:
    ldr     r8, [r7, #56]
    cmp     r8, #2
    beq     settingsSaveReady
    cmp     r8, #3
    beq     settingsSaveAbort
    ldr     r0, =1000000        @ 1 ms, bounded to five seconds
    mov     r1, #0
    svc     #0x0a              @ svcSleepThread
    subs    r6, r6, #1
    bne     settingsSaveWait

settingsSaveAbort:
    @ keep the request for retry, and don't save native before coins.bin catches up
    mov     r6, #1
    str     r6, [r7, #56]
    mcr     p15, 0, r6, c7, c10, 5
    mvn     r0, #0
    b       settingsSaveReturn

settingsSaveInvalid:
    @ dont write an invalid Settings record past our protected cursor
    mvn     r0, #0
    b       settingsSaveReturn

settingsSaveReady:
    mov     r6, #0
    str     r6, [r7, #56]
    mcr     p15, 0, r6, c7, c10, 5
settingsSaveOriginal:
    ldrh    r8, [r5, #6]        @ request still has Settings' real Today
    ldr     r6, [r7, #28]
    cmp     r6, #1
    moveq   r6, #11
    streqh  r6, [r5, #6]        @ leave the native file locked after Settings
    adr     r6, PLUGIN_coin_settingsSaveTarget
    ldr     r6, [r6]
    sub     r0, r5, #28         @ Settings' original file object
    mov     r1, r5
    blx     r6
    strh    r8, [r5, #6]        @ do not let sentinel enter HOME's snapshot
settingsSaveReturn:
    pop     {r4-r8, lr}
    cmp     r4, #0              @ Settings instruction displaced at +0x6c
    ldr     pc, PLUGIN_coin_settingsSaveReturn
    .ltorg

@ hook both shared 20-byte HOME record helpers at entry, so every caller uses them
.global PLUGIN_coin_accountingLoadTarget
PLUGIN_coin_accountingLoadTarget: .word 0   @ original shared load helper + 8
.global PLUGIN_coin_accountingSaveTarget
PLUGIN_coin_accountingSaveTarget: .word 0   @ original shared save helper + 8

@ replay the original load-helper prologue and resume at the third word Loader checks both words before patching
PLUGIN_coin_accountingLoadOriginal:
    push    {r3-r9, lr}
.global PLUGIN_coin_accountingLoadPrologue2
PLUGIN_coin_accountingLoadPrologue2:
    .word 0
    ldr     pc, PLUGIN_coin_accountingLoadTarget

.global PLUGIN_coin_accountingLoadHook
PLUGIN_coin_accountingLoadHook:
    push    {r4-r8, lr}
    mov     r4, r1
    adr     r6, PLUGIN_coin_handoffControl
    ldr     r8, [r6]            @ keep an outer load busy through nested saves
    cmp     r8, #0
    moveq   r7, #0
    streq   r7, [r6, #88]       @ never replay a previous native observation
    mov     r7, #1
    str     r7, [r6]
    mcr     p15, 0, r7, c7, c10, 5
    bl      PLUGIN_coin_accountingLoadOriginal
    mov     r5, r0
    cmp     r5, #0
    blt     accountingLoadDone
    ldr     r6, [r4]
    mov     r7, #0x4f00
    cmp     r6, r7
    bne     accountingLoadDone
    adr     r6, PLUGIN_coin_handoffControl
    ldr     r7, [r6, #28]
    cmp     r7, #1
    bne     accountingLoadDone
    cmp     r8, #0
    bne     accountingLoadOverlay
    ldrh    r7, [r4, #6]
    cmp     r7, #10
    bhi     accountingLoadClampWallet
    str     r7, [r6, #80]
    ldr     r0, [r4, #16]
    str     r0, [r6, #84]
    @ correct the native wallet before HOME sees private day/step fields or runs the coin calculator
    ldrh    r0, [r4, #4]
    cmp     r0, r7
    movlo   r0, #0             @ game may have spent some new coins already
    subhs   r0, r0, r7         @ never wrap the native u16 wallet
    strh    r0, [r4, #4]
    mov     r0, #1
    str     r0, [r6, #88]
accountingLoadClampWallet:
    @ higher native balances cannot raise the trusted HOME wallet
    ldrh    r0, [r4, #4]
    adrl    r1, PLUGIN_coin_homePtr
    ldr     r1, [r1]
    ldr     r1, [r1, #4]
    ldr     r1, [r1]
    cmp     r1, #300
    movhi   r1, #300
    cmp     r0, r1
    movhi   r0, r1
    strh    r0, [r4, #4]
accountingLoadOverlay:
    ldr     r7, [r6, #32]
    strh    r7, [r4, #6]
    ldr     r7, [r6, #36]
    str     r7, [r4, #8]
    ldr     r7, [r6, #40]
    str     r7, [r4, #12]
    ldr     r7, [r6, #44]
    str     r7, [r4, #16]
accountingLoadDone:
    @ don't replay yesterday's private cursor while Settings is still saving
    adr     r6, PLUGIN_coin_handoffControl
    ldr     r7, [r6, #56]
    cmp     r7, #1
    bne     accountingLoadFinish
    ldr     r7, [r6, #60]
    strh    r7, [r4, #6]
    ldr     r7, [r6, #64]
    str     r7, [r4, #8]
    ldr     r7, [r6, #68]
    str     r7, [r4, #12]
    ldr     r7, [r6, #72]
    str     r7, [r4, #16]
accountingLoadFinish:
    @ HOME may skip the calculation/save. restore only our own busy state
    adr     r6, PLUGIN_coin_handoffControl
    str     r8, [r6]
    mcr     p15, 0, r8, c7, c10, 5
    mov     r0, r5
    pop     {r4-r8, pc}        @ return directly to whatever caller loaded

@ replay the two verified helper-prologue words before entering +8. the helper returns through our BL to the hook
PLUGIN_coin_accountingSaveOriginal:
    push    {r4-r7, lr}
    sub     sp, sp, #0xc
    ldr     pc, PLUGIN_coin_accountingSaveTarget

.global PLUGIN_coin_accountingSaveHook
PLUGIN_coin_accountingSaveHook:
    push    {r4-r8, lr}
    mov     r4, r1
    adr     r5, PLUGIN_coin_handoffControl
    ldr     r7, [r5]            @ a default save can run inside a load
    mov     r6, #1
    str     r6, [r5]
    mcr     p15, 0, r6, c7, c10, 5
    ldrh    r6, [r4, #6]        @ keep HOME's real counter
    ldr     r8, [r5, #28]
    cmp     r8, #1
    bne     accountingSaveNativeReady
    ldr     r8, [r4]
    mov     r5, #0x4f00
    cmp     r8, r5
    mov     r8, #11
    streqh  r8, [r4, #6]        @ original writer sees the native lock
accountingSaveNativeReady:
    bl      PLUGIN_coin_accountingSaveOriginal
    mov     r8, r0              @ keep the helper's return value
    strh    r6, [r4, #6]        @ HOME and the private snapshot stay real
    cmp     r8, #0
    blt     accountingSaveDone  @ failed native save cannot advance snapshot
    @ a load can make a default record, so keep the earlier private snapshot
    cmp     r7, #0
    bne     accountingSaveDone
    @ only trust a valid gamecoin record as the next snapshot
    ldr     r5, [r4]
    mov     r6, #0x4f00
    cmp     r5, r6
    bne     accountingSaveDone
    adr     r5, PLUGIN_coin_handoffControl
    ldrh    r6, [r4, #6]        @ original HOME Today was restored after native save
    str     r6, [r5, #32]      @ publish real Today, never the 0x4f00 magic
    ldr     r6, [r4, #8]
    str     r6, [r5, #36]
    ldr     r6, [r4, #12]
    str     r6, [r5, #40]
    ldr     r6, [r4, #16]
    str     r6, [r5, #44]
    ldr     r6, [r5, #48]
    add     r6, r6, #1
    str     r6, [r5, #48]
    ldr     r6, [r5, #76]
    and     r6, r6, #0x40000000
    str     r6, [r5, #76]        @ this snapshot needs a fresh flat checkpoint
    mov     r6, #0
    str     r6, [r5, #92]        @ the saved day bucket is stale too
    mcr     p15, 0, r6, c7, c10, 5
accountingSaveDone:
    mov     r0, r4
    mov     r1, r8
    bl      PLUGIN_coin_FinishHomeCatchupSave
    adr     r5, PLUGIN_coin_handoffControl
    str     r7, [r5]
    mcr     p15, 0, r7, c7, c10, 5
    mov     r0, r8
    pop     {r4-r8, pc}        @ original caller owns its post-save instructions

.global PLUGIN_coin_rtcDayGateHook
.type   PLUGIN_coin_rtcDayGateHook, %function
PLUGIN_coin_rtcDayGateHook:
    @ protect the whole RTC decision, not only the later coin hooks
    push    {r0-r2}
    adrl    r0, PLUGIN_coin_handoffControl
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

    @ Nintendo HOME replay: current day - saved day
    subs    r0, r7, r5
    sbcs    r0, r8, r6
    blt     rtcClockBehind

    @ sbcs z covers only the high-word result, so check both halves
    cmp     r7, r5
    bne     rtcClockAhead
    cmp     r8, r6
    beq     rtcClockSame

rtcClockAhead:
    @ exact +1 can settle yesterday, but other forward gaps only use today's steps
    push    {r1, r2}
    subs    r1, r7, r5
    sbcs    r2, r8, r6

    @ after an RTC rollback, restart the +3 counter on the first real new day
    adr     r0, PLUGIN_coin_homePtr
    ldr     r0, [r0]
    ldr     r0, [r0, #8]
    ldr     r0, [r0, #68]       @ was the backwards-rebase floor active?
    cmp     r0, #0
    beq     rtcForwardFloorReady

    @ a positive RTC jump isn't always a new day, so check the date before dropping the old floor
    push    {r1, r2}
    adrl    r0, PLUGIN_coin_handoffControl
    ldr     r0, [r0, #20]
    ldrh    r1, [r4, #16]
    ldrb    r2, [r4, #18]
    orr     r1, r1, r2, lsl #16
    ldrb    r2, [r4, #19]
    orr     r1, r1, r2, lsl #24
    cmp     r0, r1
    pop     {r1, r2}
    beq     rtcForwardFloorReady

    adr     r0, rtcThreeHundredDays
    ldr     r0, [r0, #4]
    cmp     r2, r0
    bhi     rtcForwardFloorReady
    blo     rtcForwardClearFloor
    adr     r0, rtcThreeHundredDays
    ldr     r0, [r0]
    cmp     r1, r0
    bhi     rtcForwardFloorReady
rtcForwardClearFloor:
    @ when revisiting a date, only count steps gained since our checkpoint
    adrl    r0, PLUGIN_coin_handoffControl
    mov     r1, #1
    str     r1, [r0, #52]
    mcr     p15, 0, r1, c7, c10, 5
    adr     r0, PLUGIN_coin_homePtr
    ldr     r0, [r0]
    ldr     r0, [r0, #8]
    mov     r1, #0
    str     r1, [r0, #68]       @ stop old-day floor before new-day calc
    str     r1, [r0, #36]       @ new day: progressive Today starts at zero
    str     r1, [r0, #40]       @ previous day's +3 remainder
    str     r1, [r0, #44]       @ previous day's flat remainder
    str     r1, [r0, #48]       @ previous split-calculation marker
    mcr     p15, 0, r1, c7, c10, 5

    subs    r1, r7, r5
    sbcs    r2, r8, r6
rtcForwardFloorReady:
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
    adrl    r0, PLUGIN_coin_handoffControl
    ldr     r1, [r0, #20]
    str     r1, [r0, #16]
    ldrh    r1, [r4, #6]
    str     r1, [r0, #24]
    adr     r2, PLUGIN_coin_homePtr
    ldr     r2, [r2]
    ldr     r2, [r2, #8]
    ldr     r2, [r2, #40]        @ old progressive remainder before rebase
    str     r2, [r0, #12]
    mov     r1, #1
    str     r1, [r0, #8]
    push    {r0, r3, r12, lr}
    mov     r0, r4
    mov     r1, r5
    mov     r2, r6
    bl      PLUGIN_coin_PrepareHomeCatchup
    pop     {r0, r3, r12, lr}
    b       rtcForwardReady

rtcForwardReject:
    adrl    r0, PLUGIN_coin_handoffControl
    ldr     r1, [r0, #20]
    str     r1, [r0, #16]
    mov     r1, #0
    str     r1, [r0, #12]
    str     r1, [r0, #24]
    mov     r1, #2
    str     r1, [r0, #8]
    b       rtcForwardReady

rtcForwardSuppress:
    @ mark this before reading anything Rosalina can swap
    adrl    r0, PLUGIN_coin_handoffControl
    ldr     r1, [r0, #20]
    str     r1, [r0, #16]
    mov     r1, #0
    str     r1, [r0, #12]
    str     r1, [r0, #24]
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
    @ Nintendo HOME replay: flags for its next branch
    subs    r0, r7, r5
    sbcs    r0, r8, r6
    b       rtcRebaseCurrentDay

rtcClockBehind:
    @ backwards gap: rebase the cursor and suppress this payout
    push    {r1}
    adrl    r0, PLUGIN_coin_handoffControl
    mov     r1, #0
    str     r1, [r0, #52]
    ldr     r1, [r0, #20]
    str     r1, [r0, #16]
    mov     r1, #0
    str     r1, [r0, #12]
    str     r1, [r0, #24]
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
    @ equality rejects a displayed-date-only rollover dont overwrite an exact +1 waiting for Rosalina
    push    {r1}
    adrl    r0, PLUGIN_coin_handoffControl
    ldr     r1, [r0, #8]
    cmp     r1, #1
    beq     rtcClockSameReady
    ldr     r1, [r0, #20]
    str     r1, [r0, #16]
    mov     r1, #0
    str     r1, [r0, #12]
    str     r1, [r0, #24]
    mov     r1, #2
    str     r1, [r0, #8]
rtcClockSameReady:
    push    {r0-r4, r12, lr}
    bl      PLUGIN_coin_RetryHomeCatchup
    pop     {r0-r4, r12, lr}
    pop     {r1}
    subs    r0, r7, r5
    sbcs    r0, r8, r6
    b       rtcDayGateContinue

rtcRebaseCurrentDay:
    @ r5:r6 = r7:r8 so HOME queries only the observed day
    mov     r5, r7
    mov     r6, r8
    mov     r0, #0
    strh    r0, [r4, #6]         @ HOME coins Today
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
    .word   0x05265C00
    .word   0x00000000
rtcThreeHundredDays:
    .word   0x08F3D000          @ 300 * 86400000 milliseconds (low)
    .word   0x00000006          @ upper 32 bits

.balign 4
.global PLUGIN_coin_homePtr
PLUGIN_coin_homePtr:
    .word 0                      @ 0x1642c8

.global PLUGIN_coin_homePtmHistoryFunction
PLUGIN_coin_homePtmHistoryFunction:
    .word 0

.global PLUGIN_coin_HomeHandoff
.type   PLUGIN_coin_HomeHandoff, %function
PLUGIN_coin_HomeHandoff:
    adrl    r0, PLUGIN_coin_handoffControl
    bx      lr

.global PLUGIN_coin_HomePtmHistoryFunction
.type   PLUGIN_coin_HomePtmHistoryFunction, %function
PLUGIN_coin_HomePtmHistoryFunction:
    adr     r0, PLUGIN_coin_homePtmHistoryFunction
    ldr     r0, [r0]
    bx      lr

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
    @ rebase both step streams to the current total so old remainder can't pay twice
    cmp     r0, #0
    beq     afterRtcBoundaryRebase
    ldr     r5, [r8, #24]
    str     r5, [r4, #12]
    str     r5, [r8, #20]
    mov     r5, #0
    str     r5, [r8, #40]
    str     r5, [r8, #44]
afterRtcBoundaryRebase:
    @ dont let a clock correction restart the +3 curve
    ldr     r7, [r8, #68]
    cmp     r7, #0
    beq     afterPreservedTodayFloor
    ldr     r7, [r8, #36]
    ldrh    r5, [r4, #6]
    cmp     r5, r7
    strloh  r7, [r4, #6]
    strlo   r7, [r8, #28]
afterPreservedTodayFloor:
    @ clock rollback skips pre hook, record balance still applies
    tst     r10, #1
    bne     currentPreBalanceReady
    cmp     r0, #0
    beq     finishHomeCalculation
    mov     r5, r6
    mov     r12, #0
    b       normalPersistentAccounting

currentPreBalanceReady:
    ldrh    r5, [r8, #4]          @ current invocation pre-calc coins

    @ RTC rebase can't pay steps, but a lower game balance still spent coins
    cmp     r0, #0
    beq     normalPersistentAccounting
    mov     r6, r5
    mov     r12, #0

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
    ldrh    r10, [r10]            @ previous accepted HOME Menu coins
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
    push    {r0-r3, r12, lr}
    mov     r0, r4
    ldr     r1, [r9, #4]
    ldr     r2, [r9]
    ldr     r3, [r9, #8]
    bl      PLUGIN_coin_ApplyHomeCatchup
    pop     {r0-r3, r12, lr}

finishHomeCalculation:
    ldr     r8, [r9, #0x24]
    mov     r7, #0
    str     r7, [r8, #4]          @ consume RTC rebase marker once
    mcr     p15, 0, r7, c7, c10, 5 @ publish state before clearing inFlight
    str     r7, [r8]
    @ Nintendo HOME replay: original coin-calc return
    add     sp, sp, #0x7c
    pop     {r4, r5, r6, r7, r8, sb, sl, fp, pc}

.global PLUGIN_coin_homeUIReturn
PLUGIN_coin_homeUIReturn:
    .word 0                       @ 0x1eef18 on EUR 11.5
 
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
    @ after rollback, cap reopened-date history to newly gained lifetime steps
    adrl    r2, PLUGIN_coin_handoffControl
    ldr     r1, [r2, #52]
    mov     r3, #0
    str     r3, [r2, #52]
    cmp     r1, #0
    beq     historyNewStepsReady
    ldr     r1, [sp, #0xc]
    ldr     r2, [r4, #8]
    subs    r1, r1, r2
    movlo   r1, #0
    subs    r2, lr, r0
    movlo   r2, #0
    cmp     r2, r1
    subhi   r0, lr, r1
historyNewStepsReady:
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
    ldr     r2, [r0, #68]
    cmp     r2, #0
    beq     costNormalFloorReady
    ldr     r2, [r0, #36]
    cmp     r12, r2
    movlo   r12, r2
    strloh  r2, [r4, #6]
costNormalFloorReady:
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

@ original terms for the custom svc stubs in Loader's playcoin_hooks.c
@   This paricular file is licensed under the following terms:

@   This software is provided 'as-is', without any express or implied warranty. In no event will the authors be held liable
@   for any damages arising from the use of this software.
@
@   Permission is granted to anyone to use this software for any purpose, including commercial applications, and to alter it
@   and redistribute it freely, subject to the following restrictions:
@
@    The origin of this software must not be misrepresented; you must not claim that you wrote the original software.
@    If you use this software in a product, an acknowledgment in the product documentation would be appreciated but is not required.
@
@    Altered source versions must be plainly marked as such, and must not be misrepresented as being the original software.
@    This notice may not be removed or altered from any source distribution.
