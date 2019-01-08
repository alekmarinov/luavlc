@echo off

if "%LRUN_HOME%" == "" SET LRUN_HOME=..\..\..\..\..\..

if "%1" == "" goto play_default
lrun %0\..\vlcgl.lua %1
goto exit

:play_default
lrun %0\..\vlcgl.lua "mms://mediatv2.topix.it/RockOne"
goto exit

:exit
