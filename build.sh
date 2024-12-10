#!/bin/sh

set -x

CFLAGS="-I/opt/local/include -L/opt/local/lib -lxml2 -lzip"

cc ${CFLAGS} -D__XLSX_STANDALONE__ -o xlsx xlsx.c xml.c
cc ${CFLAGS} -D__XML_STANDALONE__ -o xml xml.c
cc ${CFLAGS} -o zxml zxml.c xml.c
cc ${CFLAGS} -o dict dict.c xlsx.c xml.c
