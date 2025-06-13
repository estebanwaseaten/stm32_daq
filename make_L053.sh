#!/bin/bash

cmake -DMCU=L053 -S . -B ./buildL053/ --toolchain STM32Toolchain.txt
cmake --build ./buildL053
