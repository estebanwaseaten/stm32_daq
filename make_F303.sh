#!/bin/bash

cmake -DMCU=F303 -S . -B ./buildF303/ --toolchain STM32Toolchain.txt
cmake --build ./buildF303
