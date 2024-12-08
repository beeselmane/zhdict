#!/bin/sh

set -x

CFLAGS="-I/opt/local/include -L/opt/local/lib -lxml2 -lzip"

cc ${CFLAGS} -D__XML_STANDALONE__ -o xml xml.c
cc ${CFLAGS} -o xlsx xlsx.c xml.c
