cd fastdoom
wmake fdoom13h.exe EXTERNOPT="/dMODE_HERC /dUSE_BACKBUFFER" %1 %2 %3 %4 %5 %6 %7 %8 %9
copy fdoom13h.exe ..\fdoomhgc.exe
cd ..
sb -r fdoomhgc.exe
ss fdoomhgc.exe dos32a.d32