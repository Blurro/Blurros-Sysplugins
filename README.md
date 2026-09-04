# Blurro's Nexus3DS Sysplugins

Source for my [Nexus3DS](https://github.com/2b-zipper/Nexus3DS/tree/dev) Sysplugins!

These use Nexus3DS's `.3nx` system to add new features without needing them built directly into `boot.firm`.

## Usage

Build using the current Nexus3DS Sysplugin Dev Kit, then place the resulting `.3nx` files in:

```text
/luma/plugins/
```

Make sure **Load external FIRMs and modules** is enabled in the SELECT boot settings.

Plugin IDs, priorities, components, metadata and stacking are configured in `makeplugin.sh`.

---

## PlayCoinz

A big expansion of the 3DS Play Coin system, making walking and earning coins become, well, even more game-ified than before.

Nintendo normally caps you at 300 Play Coins, with every coin costing the same 100 steps. PlayCoinz raises the balance to **30,000**, while introducing a progressive system where coins gradually take more steps to earn.

It also keeps track of the coins you've genuinely earned through walking, allowing much more detailed statistics *while still letting you manually change your balance if you want!*

Side note: Do not use Play Coin setters outside of the one provided by this mod! Decreases to Nintendo's gamecoin.dat count as spends, and increases are ignored.

### Features

* Play Coin limits uncapped
* Progressive walking cost (+3 for each coin after the original 10 cap)
* Tracks genuine walking-backed earnings separately from manually set coins
* Expanded Play Coin setter and statistics menus
* Custom notifications for... *secrets* - with Club Penguin inspired difficulty tiers
* Persistent save data designed to remain compatible as PlayCoinz expands
* WIP Blackjack casino with gambling lol (walking to the next casino required if you get a backoff)

PlayCoinz patches the Home Menu directly, with the user-facing menus and extra features provided through Rosalina.

Features art by [@AnasAbdin](https://x.com/AnasAbdin) for notification top-screen images, huge thanks to him for giving permission to use them!

---

## PowerPrevent

A Sysplugin version of [WerWolv's PowerPrevent](https://github.com/WerWolv/PowerPrevent_SysModule), preventing accidental shutdowns by requiring **START + POWER** instead of simply pressing POWER.
This was made as a mini proof-of-concept that the requirement of forking Luma3DS for a single feature has been made obsolete.

---

## IgnoreCfgNor

A Sysplugin reimplementation of the `ignore-cfgnor` patch, originally from [lifehackerhansol/Luma3DS](https://github.com/lifehackerhansol/Luma3DS/tree/ignore-cfgnor), for systems that need `cfg:nor` requests ignored without requiring a separate Luma3DS fork.

---

More Sysplugins will probably end up here as I find more wacky things I want my 3DS to do.
