cd fastdoom
wmake clean
wmake fdoom13h.exe EXTERNOPT="/dMODE_CGA16 /dUSE_BACKBUFFER" %1 %2 %3 %4 %5 %6 %7 %8 %9
copy fdoom13h.exe ..\fdoomc16.exe
cd ..
sb -r fdoomc16.exe
ss fdoomc16.exe dos32a.d32