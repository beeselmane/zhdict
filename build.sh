#!/bin/sh

set -x

CFLAGS="-Iinclude -I/opt/local/include -L/opt/local/lib -lsqlite3 -lxml2 -lzip -Wno-unused-command-line-argument"

mkdir -p build

cc ${CFLAGS} -c -o build/sqlite.o src/sqlite.c
cc ${CFLAGS} -c -o build/xlsx.o src/xlsx.c
cc ${CFLAGS} -c -o build/xml.o src/xml.c

cc ${CFLAGS} -D__XLSX_STANDALONE__ -o build/xlsx src/cmd.c build/{xml,xlsx}.o
cc ${CFLAGS} -D__ZXML_STANDALONE__ -o build/zxml src/cmd.c build/xml.o
cc ${CFLAGS} -D__XML_STANDALONE__ -o build/xml src/cmd.c build/xml.o

cc ${CFLAGS} -o build/xldict src/xldict.c build/{xml,xlsx}.o

cc ${CFLAGS} -o build/conv src/conv.c build/{xml,xlsx,sqlite}.o
