cd fastdoom
wmake fdoom13h.exe EXTERNOPT="/dMODE_CGA /dUSE_BACKBUFFER" %1 %2 %3 %4 %5 %6 %7 %8 %9
copy fdoom13h.exe ..\fdoomcga.exe
cd ..
sb -r fdoomcga.exe
ss fdoomcga.exe dos32a.d32