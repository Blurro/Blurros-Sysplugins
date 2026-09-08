# Blurro's Nexus3DS Sysplugins

Source for my [Nexus3DS](https://github.com/2b-zipper/Nexus3DS/tree/dev) Sysplugins!

These use Nexus3DS's `.3nx` system to add new features without needing them built directly into `boot.firm`.

## Installing

Go and grab the latest **[Sysplugin Menu release](https://github.com/Blurro/MENU-Sysplugin-3DS/releases)** and make sure to put `sysplgfetch.txt` configured with:
```
Blurro's Sysplugins
https://blurro.github.io/sysplugins/online_v1/onlinetemporary.3on
```

Onto your SD card in the `/luma/plugins/` folder!

Go to `Rosalina Menu -> Sysplugin Menu -> Open Online Menu -> Blurro's Sysplugins -> Check Sysplugins`,<br>and download what you need!

Make sure to check for **updates** every once in a while.

## Building

Build by overlaying [Stock Nexus3DS](https://github.com/2b-zipper/Nexus3DS/tree/dev) -> [3NX Dev Kit](https://github.com/Blurro/3NX-Plugin-DevKit) -> This repository, then run:

```
./pre_makeplugin.sh
./makeplugin.sh
```

Then place the built `.3nx` files in:

```
/luma/plugins/
```

Make sure **Load external FIRMs and modules** is enabled in the SELECT boot settings.

Plugin IDs, priorities, components, metadata and stacking are configured in `makeplugin.sh`.

---

## Hello World example

`Hello World` is a tiny starter Sysplugin. It has one hook (for Process List), registers one Sysplugin Menu page, displays a counting timer while that page is open, and saves a visit counter through MENU's `LoadData` / `SaveData` API.

**Relevant files:**
- `sysmodules/rosalina/source/hello.c`
- `sysmodules/rosalina/source/hello_hooks.c`

---

## PlayCoinz

A big expansion of the 3DS Play Coin system, making walking and earning coins become, well, even more game-ified than before.

Nintendo normally caps you at 300 Play Coins, with every coin costing the same 100 steps. PlayCoinz raises the maximum to **30,000**, and introduces increasingly-difficult *secrets* to earn through earning, and spending, your Play Coins.

It also keeps track of the coins you've genuinely earned through walking, allowing much more detailed statistics *while still letting you manually change your balance if you want!*

Side note: Do not use Play Coin setters outside of the one provided by this mod! Decreases to Nintendo's gamecoin.dat count as spends, and increases are ignored.

### Features

* Play Coin limits uncapped
* Progressive walking cost (+3 for each coin after the original 10 cap)
* Tracks legitimate walking-backed earnings separately from manually set coins
* Expanded Play Coin setter and statistics menus
* Custom notifications for... *secrets* - with Club Penguin inspired difficulty tiers
* WIP Blackjack casino with gambling lol (walking to the next casino required if you get a backoff)

PlayCoinz patches the Home Menu directly, with the user-facing menus and extra features provided through Rosalina.

Features art by [@AnasAbdin](https://x.com/AnasAbdin) for *\*secret\** top-screen images, huge thanks to him for giving permission to use them!

**Relevant files:**
- `sysmodules/loader/source/playcoin.c`
- `sysmodules/loader/source/playcoin_hooks.c`
- `sysmodules/loader/source/playcoin_loader.s`
- `sysmodules/rosalina/source/playcoin.c`
- `sysmodules/rosalina/source/playcoin_hooks.c`
- `sysmodules/rosalina/source/playcoin_helpers.c`
- `sysmodules/rosalina/source/playcoin_menus.c`
- `sysmodules/rosalina/source/playcoin_secrets.c`
- `sysmodules/rosalina/source/playcoin_rosalina.s`

**Markers in:**
- `sysmodules/loader/source/loader.c`
- `sysmodules/loader/source/patcher.c`

---

## blur

A helper Sysplugin used by multiple of my other plugins, including PlayCoinz. It provides a custom thread and callback support for shared Rosalina-side work.

**Relevant files:**
- `sysmodules/rosalina/source/blurro.c`
- `sysmodules/rosalina/source/blurro_hooks.c`

**Markers in:**
- `sysmodules/rosalina/source/menu.c`

---

## PowerPrevent

A Sysplugin version of [WerWolv's PowerPrevent](https://github.com/WerWolv/PowerPrevent_SysModule), preventing accidental shutdowns by requiring **START + POWER** instead of simply pressing POWER.
This was made as a mini proof-of-concept that the requirement of forking Luma3DS for a single feature has been made obsolete.

**Relevant files:**
- `sysmodules/rosalina/source/powerprevent.c`
- `sysmodules/rosalina/source/powerprevent_hooks.c`

**Markers in:**
- `sysmodules/rosalina/source/menu.c`

---

## IgnoreCfgNor

This is ONLY for systems with a broken NVRAM and won't boot - otherwise IGNORE!<br>Since this breaks normal systems, and broken systems couldn't even boot to get this from the Online Menu, [download it here instead](https://blurro.github.io/sysplugins/online_extra/IgnoreCfgNor.0.3nx)

A Sysplugin reimplementation of the `ignore-cfgnor` patch, originally from [lifehackerhansol/Luma3DS](https://github.com/lifehackerhansol/Luma3DS/tree/ignore-cfgnor), for systems that need `cfg:nor` requests ignored without requiring a separate Luma3DS fork.

**Relevant files:**
- `sysmodules/loader/source/ignore_cfgnor.c`

---

More Sysplugins will probably end up here as I find more wacky things I want my 3DS to do.
