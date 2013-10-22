rat4nuke provides Nuke image reader plugins for Houdini's native .rat and .pic 
formats. Deep RATs are supported, allowing you to do deep compositing in Nuke
with mantra renders.

rat4nuke was originally written by Szymon Kapeniak.

BUILDING
========
Linux:

    $ cd src
    $ make install VAR=value ...

You'll need to have Nuke and Houdini in your PATH, as the Makefile uses
hcustom and the location of the Nuke executable to build paths.

Variables are:

* CXX - name of g++ executable to use (default: g++412)
* NUKE_VERSION - version of Nuke for which you're compiling (default: 7.0)
* INSTALL_DIR - where `make install` puts the .so files (default: ~/.nuke)

Only Linux is supported at the moment. Pull requests are welcome!
