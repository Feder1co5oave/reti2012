reti2012 [![Build Status](https://travis-ci.org/Feder1co5oave/reti2012.svg?branch=master)](https://travis-ci.org/Feder1co5oave/reti2012)
========

Specifications: http://studenti.ing.unipi.it/~s470694/reti-informatiche/Progetto-2012.pdf

Compiling
---------

Compile using make:

	$ make

this will generate client & server binaries.

Running
-------

First, run the server by typing

	$ ./tris_server <host> <listening_port>

then, in another console,

	$ ./tris_client <server_ip> <server_port>

The list of available commands is shown when you type `!help` at the prompt.
