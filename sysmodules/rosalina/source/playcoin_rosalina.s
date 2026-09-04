.section .plugindata_coin,"aw",%progbits
.balign 4
.arm

@ --------------- home menu stuff ---------------

.balign 4
.global g_coinDat
g_coinDat:
    .hword 0

.balign 4
.global g_coinData
g_coinData:
    .word 0
    .word 0 @ coinRec
    .word 0 @ coinTrue
    .word 0 @ coinsEverSpent

.global g_coinChange @ a secret counter for coins that were actually earned instead of cheated
g_coinChange:
    .hword 0
    .hword 0 @ coinsSpent, for coin setter page
    .hword 0 @ coinEarn pre-calc coins

.balign 4
.global PLUGIN_coin_stepDiagnostics
PLUGIN_coin_stepDiagnostics:
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
