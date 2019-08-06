#!/bin/sh
aclocal
libtoolize --force
aclocal
autoconf
autoheader
automake -a