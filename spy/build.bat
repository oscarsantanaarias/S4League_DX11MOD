@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
cl /nologo /O2 /MT /LD /EHsc d3d9spy.cpp /link /OUT:sneoz.dll d3d9.lib user32.lib kernel32.lib
