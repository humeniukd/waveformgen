git clone git://source.ffmpeg.org/ffmpeg.git ffmpeg

cd ffmpeg

patch -p0 -i ffmpeg.patch

sh ./ffmpeg_conf #configure ffmpeg

make && make install #build and install ffmpeg

cd ./waveformgen

make

command line:

-i input file

-w dimension Default: 1800

wf.py - example for using with AWS S3 and SQS
