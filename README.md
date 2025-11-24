Experimental branch to add back built in telnet server, only over ssh tunnel.
Work In Progress, may never add back in.
Requres mods to GPL/LOGIN to fudge id to "sdsys" when reading ACCOUNTS file when logging in as root (sudo sd).


SD, the Multivalue String Database

SD is a multivalue database in the Pr1me Information tradition.  It contains open source code
from the Open Source databases openQM and ScarletDME and open source code developed by the
SD developers after the fork from ScarletDME.  While it shares many of the same features,
it was forked to explore some new ideas as to what a modern multivalue database should contain.

SD is 64 bit only and runs only on Linux.  Releases are tested on the most current release
of Debian, Ubuntu, Mint and Fedora.  SD should run on any distribution based on Debian 12,
Ubuntu 24.04 or Fedora 41.  The installer has also installed the database successfully on Ubuntu running
under the Windows Subsystem for Linux, and Ubuntu on the Raspberry Pi 5.

SD should cohabit peacefully with existing openQM and ScarletDME installations as it
creates and uses a System V shared memory segment that will not conflict with OpenQM or ScarletDME.

The current version of the SD repository contains no binary bits.  All features are available
for auditing.  Binary files are only created during the install.

To install on one of the supported systems, just clone (or download the zip file) of the repository to 
the target computer and then run the appropriate install script found in the sdb64 directory.

See the sd64/sdsys/changelog file for changes in each version release.
