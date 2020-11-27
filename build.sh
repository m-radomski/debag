#!/bin/bash

opts='-g -fsanitize=address -Wall -Wextra -pedantic -Wno-write-strings -Wno-unused-function'
libs='-lGL -lglfw -lGLEW -lcapstone -ldwarf'
g++ $opts -I./ debag.cpp libimgui_static.a -o debag $libs