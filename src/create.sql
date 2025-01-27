create table radicals (
    編號            integer primary key,
    字              text,
    筆畫數          integer
) strict;

create table dict (
    字詞名          text,
    字數            integer,
    編號            integer primary key,
    部手            integer references radicals (id),
    筆畫數          integer,
    部首外筆畫數    integer,
    注音            text,
    漢拼            text,
    釋義資料        text,
    多音資料        text,
    多音排序        integer
) strict;

create index ientries on dict (entry);
