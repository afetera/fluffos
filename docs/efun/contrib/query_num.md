---
layout: doc
title: contrib / query_num.pre
---
# query_num

### NAME

    query_num

### SYNOPSIS

    string query_num(int, int default:0);

### DESCRIPTION

    english only!!!
    converts 1st into a string representation
    2nd int is limit above which 1st int is "many"
    1st int > 99,999 is always "many", the same for <0 (<- huh???)

    query_num(99999) ;
    "ninety-nine thousand, nine hundred and ninety-nine"

    query_num(99999, 50) ;
    "many"

    query_num(-99999) ;
    "many" ????

### SEE ALSO

    pluralize(3)
