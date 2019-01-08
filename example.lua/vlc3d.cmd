@echo off

if "%LRUN_HOME%" == "" SET LRUN_HOME=..\..\..\..\..\..

if "%1" == "" goto play_default
lrun %0\..\vlc3d.lua "%1"
goto exit

:play_default
lrun %0\..\vlc3d.lua "mms://mediatv2.topix.it/RockOne"
goto exit

:exit
