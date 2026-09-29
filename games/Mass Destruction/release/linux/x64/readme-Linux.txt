Mass Destruction for Linux (x64)
Development version

Original DOS Mass Destruction CD version is required for playing:
the installed game and an image of its CD, or a copy of the CD's files,
which runs without installing (see "Without installing").

This is a static recompilation of the game's MASSD.EXE; it replaces MASSD.EXE
and DOS4GW.EXE, which are not needed to run it.

Libraries
---------

The game requires following 64-bit libraries: SDL2, SDL2_mixer
On debian based distributions these libraries are in following packages: libsdl2-2.0-0 libsdl2-mixer-2.0-0

The binary needs glibc 2.38 or newer.


Installation
------------

Put files from this archive into the installed game's directory
(the directory with MASSD.EXE and TANK.INI).

Simple instructions:
1) install Mass Destruction on your PC (or copy installed game from another computer)
2) copy the files from this archive into the game's directory
3) put an image of the CD into the directory CD in the game's directory
   (see "The CD")
4) run the game using MassDestruction.sh

Example (Detailed instructions):
1) install Mass Destruction on your PC
    - create directory ~/Games
    - install Mass Destruction using DOSBox
      - in dosbox: mount ~/Games as C:
      - in dosbox: install Mass Destruction into C:\MASSDEST

2) copy the files from this archive into the game's directory
    - copy the content of this archive into ~/Games/MASSDEST

3) put an image of the CD into the directory CD in the game's directory
    - create directory ~/Games/MASSDEST/CD
    - copy an image of the CD (e.g. MD.cue and MD.img) into it

4) run the game using MassDestruction.sh
    - run MassDestruction.sh in the game's directory: ~/Games/MASSDEST/MassDestruction.sh


Without installing
------------------

The game also runs from a copy of the CD's files, without the DOS installer:
1) copy the CD's files (FLIC, TANK.RES, MASSD.EXE, ...) into a new directory,
   e.g. ~/Games/MASSDEST
2) for the music, put the CD's audio tracks, as files of their own (ogg,
   flac, wav or mp3), and a cue sheet listing them into the directory AUDIO
   in it (see "Music")
3) create TANK.INI in it, as the installer would (see "TANK.INI")
4) copy the files from this archive into that directory
5) run the game using MassDestruction.sh


TANK.INI
--------

The installer writes TANK.INI into the game's directory: where TANK.RES and
the movies are, the language and the keys. Without it the game stops with
"Can't open resource .". For a copy of the CD's files it is, as the installer
writes it for a full install:

RESFILE_PATH: TANK.RES
FLIC_PATH: D:\FLIC\
LANGUAGE: 0
PARTIAL_INSTALL: 0

K_UP: 72
K_DOWN: 80
K_LEFT: 75
K_RIGHT: 77
K_T_LEFT: 44
K_T_RIGHT: 46
K_FIRE: 45
K_WT_LEFT: 29
K_WT_RIGHT: 56

LANGUAGE: 0 English, 1 French, 2 German, 3 Spanish, 4 American English.
A partial install leaves TANK.RES on the CD: RESFILE_PATH: D:\TANK.RES and
PARTIAL_INSTALL: 1. The game writes TANK.INI again when the keys are changed
in its options (see "Saved games").


The CD
------

The game reads its CD as drive D: for the CD check, the movies and the music.
The CD is one of:
1) an image of the CD: a cue sheet and its image file, as DOSBox's IMGMOUNT
   takes it, in the directory CD in the game's directory; the environment
   variable MD_CD_IMAGE (the cue sheet) takes it from elsewhere
2) with no image, the game's directory itself, when it is a copy of the CD's
   files (it has the CD's directory FLIC, which the installer does not copy;
   see "Without installing")


Music
-----

The game's music is the CD's audio tracks. They are played from the image,
or, for a copy of the CD's files, from the directory AUDIO: a cue sheet
(e.g. MD.cue) and the files it lists, one per track, e.g.

FILE "track02.ogg" OGG
  TRACK 02 AUDIO
    INDEX 01 00:00:00
FILE "track03.ogg" OGG
  TRACK 03 AUDIO
    INDEX 01 00:00:00
...

The tracks are 2 to 12. Without AUDIO there is no music.


Saved games
-----------

The game writes its files (TANK.SAV, TANK.INI, TANK.SCR, ERROR.LOG and
others) into the game's directory, as the DOS version does, so the two
share their saved games. The environment variable MD_WRITE_ROOT (an
absolute path) puts them elsewhere; the game then reads its own files
from there first.


Configuration
-------------

The port has no configuration file of its own (the game's is TANK.INI).
The environment variables are:
MD_CD_IMAGE    the CD's cue sheet (see "The CD")
MD_DATA_DIR    the game's directory, when the game is not started from it
MD_WRITE_ROOT  where the files the game writes go (see "Saved games")
MD_NOSOUND=1   no digital sound
MD_NOMUSIC=1   no music


Controls
--------

The keyboard, as in the DOS game (TANK.INI sets the keys):
arrow keys: drive
Z / C: turn the turret
X: fire

The window can be resized; the picture keeps its 4:3 shape.


Misc
----

Network multiplayer (IPX) does not work.

The game is recompiled with SR, the static recompiler: https://github.com/M-HT/SR
