#!/bin/bash

. "$(dirname "${BASH_SOURCE[0]}")/util.sh"

testcase "Beta I"               "('a.X) X"                      "X"
testcase "Beta II"              "('a.'b.a b) X Y"               "X Y"

testcase "Alpha conv. I"        "('a.'b.a b c) ('c.b)"          "('d.('c.b) d c)"
testcase "Alpha conv. II"       "('x. aa ('aa.'b.aa x)) aa"     "aa ('a.('b.a aa))"
testcase "Alpha conv. III"      "('x. ('aa.'b.aa x) aa) aa"     "('b.aa aa)"
testcase "Alpha conv. IV"       "('x. ('aa.'b.aa x) bb) aa"     "('b.bb aa)"
testcase "Alpha conv. V"        "('x. 'b. ('a.a b) b x) b"      "('c.('a.a c) c b)"
testcase "Variable expansion"   "f = 'b. b\n f X"               "X"

testcase "Eta reduction I"      "'x. f x"                       "('x.f x)"
testcase "Eta reduction II"     "!strong on\n 'x. f x"          "f"

testcase "Strong reduction"     "!strong on\n 'x.('y. f y)x"    "f"
testcase "Weak reduction"       "'x.('y. f y)x"                 "('x.('y.f y) x)"
