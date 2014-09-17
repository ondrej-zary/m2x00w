m2x00w
======

Driver for Konica Minolta magicolor 2300W, 2400W and 2500W printers

This is a fork of m2300w driver from http://sourceforge.net/projects/m2300w/
The code is cleaned up and support for magicolor 2500W is added.

It prints with 2500W but it's far from perfect:
 - margins are off
 - artifacts are present around black objects in color mode
 - it still uses the foomatic crap

A rewrite for CUPS is available: m2x00w-cups - https://github.com/ondrej-zary/m2x00w-cups
