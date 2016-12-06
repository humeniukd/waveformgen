git clone git://source.ffmpeg.org/ffmpeg.git ffmpeg
patch -p0 -i ffmpeg.patch

cd ffmpeg

sh ./ffmpeg_conf #configure ffmpeg

build and install ffmpeg

make && make install

cd ./waveformgen
make

command line:

-i input file

-w dimension Default: 1800


in action - http://loudyo.pp.ua/
