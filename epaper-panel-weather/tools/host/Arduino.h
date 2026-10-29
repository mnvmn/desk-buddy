// Host-only stub shadowing the Arduino core header for the pixel-verification
// harness. The firmware drawing path only needs the C standard library, so this
// just pulls it in. (-I tools/host places this ahead of any real Arduino.h.)
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
