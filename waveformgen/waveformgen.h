/*
 waveformgen.h

 This file is part of waveformgen.
 
 waveformgen is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 
 waveformgen is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with waveformgen. If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdbool.h>

#ifndef WAVEFORMGEN_H
#define WAVEFORMGEN_H

#define WAVEFORMGEN_VERSION "0.11"

int width, widthSmall, height;
int wfg_generateImage(char *infile, char *outfile);
char* wfg_lastErrorMessage();
int wfg_Seconds();

#define WFG_PACK_RGB(_POINTER,_R,_G,_B) _POINTER[0] = _R;_POINTER[1] = _G; _POINTER[2] = _B
#define WFG_UNPACK_RGB(_POINTER) _POINTER[0], _POINTER[1], _POINTER[2]

#endif
