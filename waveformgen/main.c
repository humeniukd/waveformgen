/*
 main.c
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <getopt.h>
#include "waveformgen.h"


#define PRINT_VERSION printf("waveformgen v%s - a waveform image generator\n\n",WAVEFORMGEN_VERSION);


// private
void displayHelp();

static int get_filename_ext(char *filename, char *exten)
{
    char *ext;
    ext = strrchr(filename, '.');
    //    if(!ext || ext == filename) return "";
    if (ext)
        if (!strcasecmp(ext, exten))
            return 1;
    return 0;
}

int main (int argc, char *argv[])
{
    char* inFile = NULL;
    char* mFile = NULL;
    
    if(argc < 2)
    {
        displayHelp();
        return EXIT_FAILURE;
    }
    
    width = 1800;
    widthSmall = 800;
    height = 140;
    
    // 	http://www.cs.utah.edu/dept/old/texinfo/glibc-manual-0.02/library_22.html#SEC388
    
    int c;
    
    while((c = getopt(argc, argv, "h:i:w:W:o:")) != -1)
    {
        switch (c)
        {
            case 'i': // input
                inFile = optarg;
                break;
            case 'o': // mp3
                mFile = optarg;
                break;
            case 'W': // width
                width = atoi(optarg);
                break;
            case 'w': // width
                widthSmall = atoi(optarg);
                break;
            case 'h': // height
                height = atoi(optarg);
                break;
            default: // version
                PRINT_VERSION;
                return EXIT_SUCCESS;
                break;
        }
    }
    
    if(argc > optind)
    {
        inFile = argv[optind];
    }
    
    
    if(inFile == NULL)
    {
        fprintf(stderr, "You have to specify an input file!\n");
        return EXIT_FAILURE;
    }
    
    // a too small width would make the audio file buffer quite large.
    if(width < 10)
    {
        fprintf(stderr, "Please specify a width greater than 10!\n");
        return EXIT_FAILURE;
    }
    bool ret;
    ret = wfg_generateImage(inFile, mFile);
    if(ret)
    {
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}

void displayHelp()
{
    PRINT_VERSION;
    
    printf("usage: waveformgen [options] <infile> <outfile>\n\n\
           <infile>:  an audio file\n\
           \n\
           OPTIONS:\n\
           -i file    specify input file\n\n\
           -w dim     specify dimension as [width]. Default: 1800\n\
           -v         display version\n\n"
           );
    
}
