SD Extension - branch of SD

The purpose of this branch is to define a generic function for extending SD / BASIC to interface with C code.

Why do this?

Currently SD has 16 unassigned op codes for use by GPL developers, 
as development advances, it is highly probable that the project will run out of "free" available op codes.

    Function SDME.EXT:

    Function SDME.EXT(Arg, IsArgMV, Function_id)
    where
      Function_Id = the integer value used to identify what c code / function is execute.
      IsArgMV     = If passing a multiple arguments in Arg, set to true, otherwise false.  
      Arg         = string value passed to the c code / function. For functions requiring multiple arguments, set IsArgMV to true and pass arguments in a FIELD_MARK separated string, with a maximum of ?10? fields??

    Note: Use the BASIC function STATUS() to return function status:
	
	STATUS() = 0 successful call, or  STATUS() = 1 unsuccessful call
 
    Function SDME.EXT is patterned off the code for OSPATH / op_ospath found in gplsrc/op_dio2.c

Step by step process to follow for adding new op codes to SD found in SD_ScarletDME-notes/AddingOpcodes.rst

Also see:

Call the python interpreter from SD using the python API: SD_ScarletDME-notes/PythonInSD.rst

Encryption via Libsodium: SD_ScarletDME-notes/Encryption.rst


this is a work in progress.......     
  
------- 



SD, the Multivalue String Database

SD is a multivalue database in the Pr1me Information tradition.  It contains open source code
from the Open Source databases openQM and ScarletDME and open source code developed by the
SD developers after the fork from ScarletDME.  While it shares many of the same features,
it was forked to explore some new ideas as to what a modern multivalue database should contain.

SD is 64 bit only and runs only on Linux.  Releases are tested on the most current release
of Debian & Fedora Workstation and Server.  SD should run on any distribution based on Debian 12 
or Fedora 40

SD should cohabit peacefully with existing openQM and ScarletDME installations as
it is installed to a different location and uses memory offsets not used by OpenQM or ScarletDME.

The current version of the SD repository contains no binary bits.  Al features are available
for auditing.  Binary files are only created during the install.

To install on your Debian or Fedora based system, just clone the repository to the target computer
and then run either the Debian or Fedora install script found in the sdb64 directory.

See the sd64/sdsys/changelog file for changes in each version release.
