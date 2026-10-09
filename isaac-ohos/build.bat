@echo off
rem isaac-ohos one-click build. Signing: regenerate p7b via DevEco
rem (File > Project Structure > Signing Configs > Automatically generate).
set PATH=C:\Program Files\Huawei\DevEco Studio\tools\node;C:\Program Files\Huawei\DevEco Studio\tools\hvigor\bin;C:\Program Files\Huawei\DevEco Studio\jbr\bin;C:\Program Files\Huawei\DevEco Studio\sdk\default\openharmony\toolchains;%PATH%
set DEVECO_SDK_HOME=C:\Program Files\Huawei\DevEco Studio\sdk
set NODE_HOME=C:\Program Files\Huawei\DevEco Studio\tools\node
set JAVA_HOME=C:\Program Files\Huawei\DevEco Studio\jbr
cd /d %~dp0
call hvigorw.bat --mode module -p product=default -p buildMode=debug assembleHap --no-daemon
echo.
echo HAP: %~dp0entry\build\default\outputs\default\entry-default-signed.hap
