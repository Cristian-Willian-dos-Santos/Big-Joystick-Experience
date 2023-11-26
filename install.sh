#!/bin/bash
gcc -Wall -Wextra -g3 -lm -lX11 -lXtst -ljson-c interpreter.c -o output/interpreter
