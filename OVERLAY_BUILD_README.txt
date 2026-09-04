Nexus3DS All Overlay
===================

Apply this overlay on top of clean stock Nexus + the NexusPlgDev kit.

Build:
  ./pre_makeplugin.sh
  ./makeplugin.sh

GitHub payloads:
  ./pre_makeplugin.sh
  ./makepluginGIT.sh

Playcoinz top-image assets:
  achvimages/ contains the committed runtime JPEGs used by normal builds.
  pre_makeplugin.sh validates that they are <= 64 KiB, 400x240 metadata is
  preserved, and JPEG sampling is 4:4:4, then packs them into easytop.bin,
  mediumtop.bin, hardtop.bin and extremtop.bin.

  Normal builds require no Pillow.

  achvimages-src/ contains the editable PNG masters. If those images are edited,
  Playcoinz-top-image-author.py can be run in a separate authoring environment
  with Pillow to regenerate the committed JPEG assets. That optional authoring
  step is not part of pre_makeplugin.sh or makeplugin.sh.