reti2012
========

Specifications: [http://www2.ing.unipi.it/~a008149/corsi/reti/lucidi/Progetto-2012.pdf](http://www2.ing.unipi.it/~a008149/corsi/reti/lucidi/Progetto-2012.pdf)

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
