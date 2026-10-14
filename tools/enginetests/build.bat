@rem Builds and runs one offline engine test, e.g.  build.bat arptest
@rem Extra defines go after the name:  build.bat arptest /DFOO=1
@rem The dsp headers are plain C++, so the tests need no JUCE and no pad.
@call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /std:c++17 /O2 /EHsc /W3 /I ..\..\Source %2 %3 %4 %5 %6 %7 %8 %9 %1.cpp /Fe:%1.exe /Fo:%1.obj && .\%1.exe
