#!/bin/bash

opts='-O2 -Wall -Wextra -Wno-write-strings -Wno-unused-function -Wno-class-memaccess'
libs='-lGL -lglfw -lGLEW -lcapstone -ldwarf'
g++ $opts -I./ debag.cpp libimgui_static.a -o debag $libs
